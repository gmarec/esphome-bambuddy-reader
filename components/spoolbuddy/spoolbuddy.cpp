#include "spoolbuddy.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "esphome/components/network/util.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>

namespace esphome {
namespace spoolbuddy {

static const char *const TAG = "spoolbuddy";

static const uint32_t TASK_STACK = 10240;  // TLS handshake needs a large stack
static const uint32_t RETRY_MS = 5000;
static const int HTTP_TIMEOUT_MS = 8000;
static const size_t MAX_RESPONSE = 2048;

void SpoolBuddy::setup() {
  if (this->url_.rfind("http", 0) != 0) {
    ESP_LOGI(TAG, "No BambuBuddy URL configured, SpoolBuddy registration disabled");
    return;
  }
  this->lock_ = xSemaphoreCreateMutex();
  // Core 0 runs Wi-Fi; the ESPHome loop runs on core 1, so HTTP never stalls the UI
  if (xTaskCreatePinnedToCore(task_trampoline_, "spoolbuddy", TASK_STACK, this, 1, &this->task_, 0) != pdPASS) {
    ESP_LOGE(TAG, "Could not start the SpoolBuddy task");
    this->mark_failed();
  }
}

void SpoolBuddy::dump_config() {
  ESP_LOGCONFIG(TAG, "SpoolBuddy device:");
  ESP_LOGCONFIG(TAG, "  URL: %s", this->url_.c_str());
  ESP_LOGCONFIG(TAG, "  Device ID: %s", this->device_id_.c_str());
  ESP_LOGCONFIG(TAG, "  Hostname: %s", this->hostname_.c_str());
  ESP_LOGCONFIG(TAG, "  Heartbeat: %u ms", (unsigned) this->heartbeat_interval_ms_);
  ESP_LOGCONFIG(TAG, "  NFC: %s / %s", this->nfc_reader_type_.c_str(), this->nfc_connection_.c_str());
}

std::string SpoolBuddy::get_pending_command() {
  std::string cmd;
  if (this->lock_ != nullptr && xSemaphoreTake(this->lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
    cmd = this->pending_command_;
    xSemaphoreGive(this->lock_);
  }
  return cmd;
}

void SpoolBuddy::task_trampoline_(void *arg) { static_cast<SpoolBuddy *>(arg)->task_loop_(); }

void SpoolBuddy::task_loop_() {
  while (true) {
    if (!network::is_connected()) {
      this->state_ = LinkState::CONNECTING;
      this->registered_ = false;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!this->registered_) {
      this->registered_ = this->register_device_();
      if (!this->registered_) {
        this->state_ = LinkState::ERROR;
        vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
        continue;
      }
    }
    bool ok = this->send_heartbeat_();
    this->state_ = ok ? LinkState::ONLINE : LinkState::ERROR;
    vTaskDelay(pdMS_TO_TICKS(ok ? this->heartbeat_interval_ms_ : RETRY_MS));
  }
}

std::string SpoolBuddy::ip_address_() {
  for (auto &ip : network::get_ip_addresses()) {
    if (ip.is_ip4())
      return ip.str();
  }
  return "0.0.0.0";
}

bool SpoolBuddy::register_device_() {
  char body[512];
  snprintf(body, sizeof(body),
           "{\"device_id\":\"%s\",\"hostname\":\"%s\",\"ip_address\":\"%s\",\"firmware_version\":\"%s\","
           "\"has_nfc\":true,\"has_scale\":false,\"has_backlight\":false,"
           "\"nfc_reader_type\":\"%s\",\"nfc_connection\":\"%s\",\"backend_url\":\"%s\"}",
           this->device_id_.c_str(), this->hostname_.c_str(), this->ip_address_().c_str(), ESPHOME_VERSION,
           this->nfc_reader_type_.c_str(), this->nfc_connection_.c_str(), this->url_.c_str());
  std::string resp;
  int status = this->post_("/api/v1/spoolbuddy/devices/register", body, resp);
  if (status != 200) {
    ESP_LOGW(TAG, "Registration failed (HTTP %d)", status);
    return false;
  }
  ESP_LOGI(TAG, "Registered with BambuBuddy as '%s'", this->device_id_.c_str());
  return true;
}

bool SpoolBuddy::send_heartbeat_() {
  bool nfc_ok = this->nfc_reader_ != nullptr && !this->nfc_reader_->is_failed();
  char body[256];
  snprintf(body, sizeof(body),
           "{\"nfc_ok\":%s,\"scale_ok\":false,\"uptime_s\":%u,\"firmware_version\":\"%s\",\"ip_address\":\"%s\"}",
           nfc_ok ? "true" : "false", (unsigned) (millis() / 1000), ESPHOME_VERSION, this->ip_address_().c_str());
  std::string resp;
  int status = this->post_("/api/v1/spoolbuddy/devices/" + this->device_id_ + "/heartbeat", body, resp);
  if (status == 404) {
    // Device deleted on the BambuBuddy side: register again on the next round
    ESP_LOGW(TAG, "Unknown to BambuBuddy, registering again");
    this->registered_ = false;
    return false;
  }
  if (status != 200) {
    if (status != this->last_status_)
      ESP_LOGW(TAG, "Heartbeat failed (HTTP %d)", status);
    this->last_status_ = status;
    return false;
  }
  if (this->last_status_ != 200)
    ESP_LOGI(TAG, "Online");
  this->last_status_ = 200;

  // "pending_command":"write_tag" or null
  std::string cmd;
  size_t p = resp.find("\"pending_command\":\"");
  if (p != std::string::npos) {
    p += 19;
    size_t e = resp.find('"', p);
    if (e != std::string::npos)
      cmd = resp.substr(p, e - p);
  }
  if (xSemaphoreTake(this->lock_, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (cmd != this->pending_command_ && !cmd.empty())
      ESP_LOGI(TAG, "Pending command: %s (not supported yet)", cmd.c_str());
    this->pending_command_ = cmd;
    xSemaphoreGive(this->lock_);
  }
  return true;
}

int SpoolBuddy::post_(const std::string &path, const std::string &json_body, std::string &response) {
  std::string url = this->url_ + path;
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.disable_auto_redirect = false;
  if (url.rfind("https", 0) == 0)
    config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
    return -1;
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "X-API-Key", this->api_key_.c_str());

  int status = -1;
  if (esp_http_client_open(client, json_body.size()) == ESP_OK) {
    if (esp_http_client_write(client, json_body.data(), json_body.size()) == (int) json_body.size()) {
      esp_http_client_fetch_headers(client);
      status = esp_http_client_get_status_code(client);
      char buf[256];
      int n;
      response.clear();
      while ((n = esp_http_client_read(client, buf, sizeof(buf))) > 0 && response.size() < MAX_RESPONSE)
        response.append(buf, n);
    }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return status;
}

}  // namespace spoolbuddy
}  // namespace esphome
