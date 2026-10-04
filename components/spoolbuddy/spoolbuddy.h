#pragma once

// Registers the device with BambuBuddy as a SpoolBuddy device and keeps it online with periodic heartbeats.
// HTTP runs in a dedicated FreeRTOS task so TLS handshakes and slow responses never block the ESPHome loop
// (display, encoder, NFC polling).

#include "esphome/core/component.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <string>

namespace esphome {
namespace spoolbuddy {

enum class LinkState : uint8_t {
  CONNECTING,  // not registered yet, or network down
  ONLINE,      // last heartbeat accepted
  ERROR,       // last request failed (HTTP error, timeout, TLS)
};

class SpoolBuddy : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_url(const std::string &url) { url_ = url; }
  void set_api_key(const std::string &key) { api_key_ = key; }
  void set_device_id(const std::string &id) { device_id_ = id; }
  void set_hostname(const std::string &h) { hostname_ = h; }
  void set_heartbeat_interval(uint32_t ms) { heartbeat_interval_ms_ = ms; }
  void set_nfc_reader(Component *reader) { nfc_reader_ = reader; }
  void set_nfc_reader_type(const std::string &t) { nfc_reader_type_ = t; }
  void set_nfc_connection(const std::string &c) { nfc_connection_ = c; }

  LinkState get_state() const { return state_; }
  bool is_online() const { return state_ == LinkState::ONLINE; }
  const std::string &get_device_id() const { return device_id_; }

  // Last pending command returned by a heartbeat ("" if none). Thread-safe copy.
  std::string get_pending_command();

 protected:
  static void task_trampoline_(void *arg);
  void task_loop_();
  bool register_device_();
  bool send_heartbeat_();
  // POST json_body to path (relative to url_). Returns the HTTP status (or -1) and fills response.
  int post_(const std::string &path, const std::string &json_body, std::string &response);
  std::string ip_address_();

  std::string url_;
  std::string api_key_;
  std::string device_id_;
  std::string hostname_;
  std::string nfc_reader_type_;
  std::string nfc_connection_;
  uint32_t heartbeat_interval_ms_{15000};
  Component *nfc_reader_{nullptr};

  volatile LinkState state_{LinkState::CONNECTING};
  bool registered_{false};
  int last_status_{0};

  SemaphoreHandle_t lock_{nullptr};
  std::string pending_command_;
  TaskHandle_t task_{nullptr};
};

}  // namespace spoolbuddy
}  // namespace esphome
