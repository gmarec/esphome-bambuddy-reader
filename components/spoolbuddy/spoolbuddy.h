#pragma once

// Registers the device with BambuBuddy as a SpoolBuddy device, keeps it online with periodic heartbeats and,
// when a scale is configured, implements the SpoolBuddy scale protocol:
//   weight = (raw - tare_offset) * calibration_factor, both values stored by BambuBuddy and returned by heartbeats;
//   readings reported to /scale/reading, tare via the heartbeat "tare" command or tare(), spool weight updates via
//   /scale/update-spool-weight.
// HTTP runs in a dedicated FreeRTOS task with a kept-alive connection, so TLS handshakes and slow responses never
// block the ESPHome loop (display, encoder, NFC polling).

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <string>

struct esp_http_client;

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
  void set_scale(sensor::Sensor *raw) { scale_ = raw; }

  LinkState get_state() const { return state_; }
  bool is_online() const { return state_ == LinkState::ONLINE; }
  const std::string &get_device_id() const { return device_id_; }

  // --- Scale (main loop side) ---
  bool has_scale() const { return scale_ != nullptr; }
  // A reading arrived in the last 2 s
  bool is_scale_ok() const;
  // Calibration received from BambuBuddy and different from the factory default (factor 1.0)
  bool is_scale_calibrated() const { return calibrated_; }
  float get_weight() const { return weight_; }
  bool is_weight_stable() const { return stable_; }
  // Zero the scale on the current reading and send the new offset to BambuBuddy
  void tare();
  // Send the current weight to BambuBuddy for this spool; the result is read with take_weight_used()
  void request_spool_weight_update(int spool_id) { weight_update_spool_ = spool_id; }
  // True once per completed update, with the spool's weight_used returned by BambuBuddy
  bool take_weight_used(float &weight_used);

 protected:
  static void task_trampoline_(void *arg);
  void task_loop_();
  bool register_device_();
  bool send_heartbeat_();
  void report_scale_();
  void send_tare_();
  void send_spool_weight_(int spool_id);
  // POST json_body to path (relative to url_). Returns the HTTP status (or -1) and fills response.
  int post_(const std::string &path, const std::string &json_body, std::string &response);
  std::string ip_address_();
  void on_raw_(float raw);
  void apply_calibration_(int32_t tare, float factor);

  std::string url_;
  std::string api_key_;
  std::string device_id_;
  std::string hostname_;
  std::string nfc_reader_type_;
  std::string nfc_connection_;
  uint32_t heartbeat_interval_ms_{15000};
  Component *nfc_reader_{nullptr};
  sensor::Sensor *scale_{nullptr};

  std::atomic<LinkState> state_{LinkState::CONNECTING};
  bool registered_{false};
  int last_status_{0};
  std::string last_command_;

  // HTTP (task only)
  esp_http_client *client_{nullptr};

  // Scale: written by the main loop, read by the task (and vice versa for the calibration)
  static const uint8_t AVG_SAMPLES = 5;
  int32_t samples_[AVG_SAMPLES]{};
  uint8_t sample_count_{0}, sample_idx_{0};
  float history_g_[8]{};
  uint32_t history_ms_[8]{};
  uint8_t history_idx_{0};
  std::atomic<int32_t> raw_avg_{0};
  std::atomic<float> weight_{0};
  std::atomic<bool> stable_{false};
  std::atomic<uint32_t> last_raw_ms_{0};
  std::atomic<int32_t> tare_offset_{0};
  std::atomic<float> factor_{1.0f};
  std::atomic<bool> calibrated_{false};
  std::atomic<bool> tare_pending_{false};
  std::atomic<uint32_t> tare_generation_{0};
  std::atomic<int> weight_update_spool_{0};
  std::atomic<bool> weight_result_ready_{false};
  std::atomic<float> weight_used_result_{0};
  float last_reported_g_{NAN};
  bool last_reported_stable_{false};
  uint32_t last_report_ms_{0};

  TaskHandle_t task_{nullptr};
};

}  // namespace spoolbuddy
}  // namespace esphome
