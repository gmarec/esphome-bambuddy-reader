#include "spoolbuddy.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "esphome/components/network/util.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <cmath>
#include <cstdlib>

namespace esphome {
namespace spoolbuddy {

static const char *const TAG = "spoolbuddy";

static const uint32_t TASK_STACK = 10240;  // TLS handshake needs a large stack
static const uint32_t TASK_TICK_MS = 200;
static const uint32_t RETRY_MS = 5000;
static const int HTTP_TIMEOUT_MS = 8000;
static const size_t MAX_RESPONSE = 2048;
// Same rules as the SpoolBuddy daemon: report at most once per second, only on a change of 2 g or more;
// stable when the readings of the last second stay within 2 g
static const uint32_t SCALE_REPORT_MS = 1000;
static const float SCALE_REPORT_DELTA_G = 2.0f;
static const float SCALE_STABLE_SPREAD_G = 2.0f;
static const uint32_t SCALE_STALE_MS = 2000;

// --- Small JSON helpers (flat fields only) ---

static bool json_number(const std::string &body, const char *key, double &out) {
  std::string k = std::string("\"") + key + "\":";
  size_t p = body.find(k);
  if (p == std::string::npos)
    return false;
  p += k.size();
  if (body.compare(p, 4, "null") == 0)
    return false;
  out = strtod(body.c_str() + p, nullptr);
  return true;
}

static std::string json_string(const std::string &body, const char *key) {
  std::string k = std::string("\"") + key + "\":\"";
  size_t p = body.find(k);
  if (p == std::string::npos)
    return "";
  p += k.size();
  size_t e = body.find('"', p);
  return e == std::string::npos ? "" : body.substr(p, e - p);
}

static esp_err_t http_event(esp_http_client_event_t *evt) {
  auto *buf = static_cast<std::string *>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf != nullptr && buf->size() < MAX_RESPONSE)
    buf->append(static_cast<const char *>(evt->data), evt->data_len);
  return ESP_OK;
}

// =====================================================================
// Lifecycle
// =====================================================================

void SpoolBuddy::setup() {
  if (this->scale_ != nullptr)
    this->scale_->add_on_raw_state_callback([this](float raw) { this->on_raw_(raw); });

  if (this->url_.rfind("http", 0) != 0) {
    ESP_LOGI(TAG, "No BambuBuddy URL configured, SpoolBuddy registration disabled");
    return;
  }
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
  ESP_LOGCONFIG(TAG, "  Scale: %s", YESNO(this->scale_ != nullptr));
}

// =====================================================================
// Scale (main loop side)
// =====================================================================

void SpoolBuddy::on_raw_(float raw) {
  if (std::isnan(raw))
    return;
  uint32_t now = millis();
  this->samples_[this->sample_idx_] = (int32_t) raw;
  this->sample_idx_ = (this->sample_idx_ + 1) % AVG_SAMPLES;
  if (this->sample_count_ < AVG_SAMPLES)
    this->sample_count_++;
  int64_t sum = 0;
  for (uint8_t i = 0; i < this->sample_count_; i++)
    sum += this->samples_[i];
  int32_t avg = (int32_t) (sum / this->sample_count_);
  float grams = (avg - this->tare_offset_.load()) * this->factor_.load();

  // Stability over the last second
  this->history_g_[this->history_idx_] = grams;
  this->history_ms_[this->history_idx_] = now;
  this->history_idx_ = (this->history_idx_ + 1) % 8;
  float lo = grams, hi = grams;
  uint8_t recent = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (this->history_ms_[i] == 0 || now - this->history_ms_[i] > 1000)
      continue;
    lo = std::min(lo, this->history_g_[i]);
    hi = std::max(hi, this->history_g_[i]);
    recent++;
  }

  this->raw_avg_ = avg;
  this->weight_ = grams;
  this->stable_ = recent >= 3 && (hi - lo) < SCALE_STABLE_SPREAD_G;
  this->last_raw_ms_ = now;
}

bool SpoolBuddy::is_scale_ok() const {
  uint32_t last = this->last_raw_ms_;
  return last != 0 && millis() - last < SCALE_STALE_MS;
}

void SpoolBuddy::tare() {
  if (!this->is_scale_ok()) {
    ESP_LOGW(TAG, "Tare ignored: no scale reading");
    return;
  }
  this->tare_offset_ = this->raw_avg_.load();
  this->tare_generation_++;
  this->tare_pending_ = true;
  ESP_LOGI(TAG, "Tared at raw=%d", (int) this->tare_offset_.load());
}

bool SpoolBuddy::take_weight_used(float &weight_used) {
  if (!this->weight_result_ready_.exchange(false))
    return false;
  weight_used = this->weight_used_result_;
  return true;
}

void SpoolBuddy::apply_calibration_(int32_t tare, float factor) {
  if (tare != this->tare_offset_ || factor != this->factor_)
    ESP_LOGI(TAG, "Scale calibration from BambuBuddy: tare=%d factor=%.6f", (int) tare, factor);
  this->tare_offset_ = tare;
  this->factor_ = factor;
  // BambuBuddy's default factor is 1.0 (raw counts): not calibrated with a known weight yet
  this->calibrated_ = std::fabs(factor - 1.0f) > 1e-6f && factor != 0.0f;
}

// =====================================================================
// Background task
// =====================================================================

void SpoolBuddy::task_trampoline_(void *arg) { static_cast<SpoolBuddy *>(arg)->task_loop_(); }

void SpoolBuddy::task_loop_() {
  uint32_t next_heartbeat = 0;
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
      next_heartbeat = 0;
    }

    if (this->tare_pending_.exchange(false))
      this->send_tare_();
    int spool_id = this->weight_update_spool_.exchange(0);
    if (spool_id > 0)
      this->send_spool_weight_(spool_id);
    if (this->scale_ != nullptr)
      this->report_scale_();

    uint32_t now = millis();
    if ((int32_t) (now - next_heartbeat) >= 0) {
      bool ok = this->send_heartbeat_();
      this->state_ = ok ? LinkState::ONLINE : LinkState::ERROR;
      next_heartbeat = now + (ok ? this->heartbeat_interval_ms_ : RETRY_MS);
    }
    vTaskDelay(pdMS_TO_TICKS(TASK_TICK_MS));
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
           "\"has_nfc\":true,\"has_scale\":%s,\"has_backlight\":false,"
           "\"nfc_reader_type\":\"%s\",\"nfc_connection\":\"%s\",\"backend_url\":\"%s\"}",
           this->device_id_.c_str(), this->hostname_.c_str(), this->ip_address_().c_str(), ESPHOME_VERSION,
           this->scale_ != nullptr ? "true" : "false", this->nfc_reader_type_.c_str(),
           this->nfc_connection_.c_str(), this->url_.c_str());
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
           "{\"nfc_ok\":%s,\"scale_ok\":%s,\"uptime_s\":%u,\"firmware_version\":\"%s\",\"ip_address\":\"%s\"}",
           nfc_ok ? "true" : "false", this->is_scale_ok() ? "true" : "false", (unsigned) (millis() / 1000),
           ESPHOME_VERSION, this->ip_address_().c_str());
  uint32_t tare_gen = this->tare_generation_;
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

  std::string cmd = json_string(resp, "pending_command");
  if (cmd != this->last_command_ && !cmd.empty() && cmd != "tare")
    ESP_LOGI(TAG, "Pending command: %s (not supported yet)", cmd.c_str());
  this->last_command_ = cmd;

  if (this->scale_ != nullptr) {
    if (cmd == "tare") {
      // Tare requested from BambuBuddy: this response predates it, skip its calibration
      this->tare();
      this->tare_pending_ = false;
      this->send_tare_();
      return true;
    }
    // Ignore the calibration if a local tare happened during the request (the response would undo it)
    double tare, factor;
    if (tare_gen == this->tare_generation_ && !this->tare_pending_ && json_number(resp, "tare_offset", tare) &&
        json_number(resp, "calibration_factor", factor))
      this->apply_calibration_((int32_t) tare, (float) factor);
  }
  return true;
}

void SpoolBuddy::send_tare_() {
  char body[64];
  snprintf(body, sizeof(body), "{\"tare_offset\":%d}", (int) this->tare_offset_.load());
  std::string resp;
  int status = this->post_("/api/v1/spoolbuddy/devices/" + this->device_id_ + "/calibration/set-tare", body, resp);
  if (status != 200)
    ESP_LOGW(TAG, "set-tare failed (HTTP %d)", status);
}

void SpoolBuddy::report_scale_() {
  if (!this->is_scale_ok())
    return;
  uint32_t now = millis();
  if (now - this->last_report_ms_ < SCALE_REPORT_MS)
    return;
  float g = this->weight_;
  bool stable = this->stable_;
  if (!std::isnan(this->last_reported_g_) && std::fabs(g - this->last_reported_g_) < SCALE_REPORT_DELTA_G &&
      stable == this->last_reported_stable_)
    return;
  char body[160];
  snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"weight_grams\":%.1f,\"stable\":%s,\"raw_adc\":%d}",
           this->device_id_.c_str(), g, stable ? "true" : "false", (int) this->raw_avg_.load());
  std::string resp;
  if (this->post_("/api/v1/spoolbuddy/scale/reading", body, resp) == 200) {
    this->last_reported_g_ = g;
    this->last_reported_stable_ = stable;
  }
  this->last_report_ms_ = now;
}

void SpoolBuddy::send_spool_weight_(int spool_id) {
  float g = std::max(0.0f, (float) this->weight_);
  char body[96];
  snprintf(body, sizeof(body), "{\"spool_id\":%d,\"weight_grams\":%.1f}", spool_id, g);
  std::string resp;
  int status = this->post_("/api/v1/spoolbuddy/scale/update-spool-weight", body, resp);
  double used;
  if (status != 200 || !json_number(resp, "weight_used", used)) {
    ESP_LOGW(TAG, "Spool %d weight update failed (HTTP %d)", spool_id, status);
    return;
  }
  ESP_LOGI(TAG, "Spool %d: %.1f g on the scale, %.1f g used", spool_id, g, used);
  this->weight_used_result_ = (float) used;
  this->weight_result_ready_ = true;
}

int SpoolBuddy::post_(const std::string &path, const std::string &json_body, std::string &response) {
  std::string url = this->url_ + path;
  if (this->client_ == nullptr) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.keep_alive_enable = true;  // reuse the TLS session between requests
    config.event_handler = http_event;
    if (url.rfind("https", 0) == 0)
      config.crt_bundle_attach = esp_crt_bundle_attach;
    this->client_ = esp_http_client_init(&config);
    if (this->client_ == nullptr)
      return -1;
    esp_http_client_set_header(this->client_, "Content-Type", "application/json");
    esp_http_client_set_header(this->client_, "X-API-Key", this->api_key_.c_str());
  }
  response.clear();
  esp_http_client_set_url(this->client_, url.c_str());
  esp_http_client_set_method(this->client_, HTTP_METHOD_POST);
  esp_http_client_set_user_data(this->client_, &response);
  esp_http_client_set_post_field(this->client_, json_body.data(), json_body.size());
  esp_err_t err = esp_http_client_perform(this->client_);
  if (err != ESP_OK) {
    // Drop the connection, a fresh one is opened on the next request
    esp_http_client_cleanup(this->client_);
    this->client_ = nullptr;
    return -1;
  }
  return esp_http_client_get_status_code(this->client_);
}

}  // namespace spoolbuddy
}  // namespace esphome
