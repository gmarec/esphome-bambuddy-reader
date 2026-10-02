#pragma once

// Lecteur de tags Bambu Lab pour lecteurs MFRC522 / WS1850S en I2C (M5Stack Dial).
// Driver MFRC522 minimal et autonome (pas de dependance au composant rc522 d'ESPHome,
// qui ne gere pas l'authentification MIFARE Classic).
// Decodage et capteurs repris de bambu_nfc (bemble / piitaya).

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"

#include "bambu_colors.h"
#include "bambu_densities.h"

#include <vector>

namespace esphome {
namespace bambu_rc522 {

class BambuRc522;

class BambuRc522ResetButton : public button::Button, public Parented<BambuRc522> {
 protected:
  void press_action() override;
};

class BambuSuccessTrigger : public Trigger<> {};
class BambuErrorTrigger : public Trigger<> {};
class BambuTagRemovedTrigger : public Trigger<> {};

class BambuRc522 : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void clear_sensors();

  void set_filament_type_sensor(text_sensor::TextSensor *s) { filament_type_sensor_ = s; }
  void set_filament_color_sensor(text_sensor::TextSensor *s) { filament_color_sensor_ = s; }
  void set_filament_color_name_sensor(text_sensor::TextSensor *s) { filament_color_name_sensor_ = s; }
  void set_filament_subtype_sensor(text_sensor::TextSensor *s) { filament_subtype_sensor_ = s; }
  void set_tray_uid_sensor(text_sensor::TextSensor *s) { tray_uid_sensor_ = s; }
  void set_tray_info_idx_sensor(text_sensor::TextSensor *s) { tray_info_idx_sensor_ = s; }
  void set_variant_id_sensor(text_sensor::TextSensor *s) { variant_id_sensor_ = s; }
  void set_card_uid_sensor(text_sensor::TextSensor *s) { card_uid_sensor_ = s; }
  void set_production_date_sensor(text_sensor::TextSensor *s) { production_date_sensor_ = s; }
  void set_last_scan_date_sensor(text_sensor::TextSensor *s) { last_scan_date_sensor_ = s; }

  void set_min_temp_sensor(sensor::Sensor *s) { min_temp_sensor_ = s; }
  void set_max_temp_sensor(sensor::Sensor *s) { max_temp_sensor_ = s; }
  void set_bed_temp_sensor(sensor::Sensor *s) { bed_temp_sensor_ = s; }
  void set_spool_weight_sensor(sensor::Sensor *s) { spool_weight_sensor_ = s; }
  void set_filament_diameter_sensor(sensor::Sensor *s) { filament_diameter_sensor_ = s; }
  void set_drying_temp_sensor(sensor::Sensor *s) { drying_temp_sensor_ = s; }
  void set_drying_time_sensor(sensor::Sensor *s) { drying_time_sensor_ = s; }
  void set_nozzle_diameter_sensor(sensor::Sensor *s) { nozzle_diameter_sensor_ = s; }
  void set_spool_width_sensor(sensor::Sensor *s) { spool_width_sensor_ = s; }
  void set_filament_length_sensor(sensor::Sensor *s) { filament_length_sensor_ = s; }
  void set_material_density_sensor(sensor::Sensor *s) { material_density_sensor_ = s; }

  void register_bambu_success_trigger(BambuSuccessTrigger *t) { success_triggers_.push_back(t); }
  void register_bambu_error_trigger(BambuErrorTrigger *t) { error_triggers_.push_back(t); }
  void register_tag_removed_trigger(BambuTagRemovedTrigger *t) { removed_triggers_.push_back(t); }

 protected:
  enum Status : uint8_t { RC_OK, RC_TIMEOUT, RC_ERROR, RC_COLLISION, RC_CRC, RC_NO_ROOM };
  enum ReadResult : uint8_t { READ_OK, READ_NOT_BAMBU, READ_FAILED };

  // --- Driver MFRC522 bas niveau ---
  void write_reg_(uint8_t reg, uint8_t val);
  uint8_t read_reg_(uint8_t reg);
  void set_bits_(uint8_t reg, uint8_t mask) { this->write_reg_(reg, this->read_reg_(reg) | mask); }
  void clear_bits_(uint8_t reg, uint8_t mask) { this->write_reg_(reg, this->read_reg_(reg) & ~mask); }
  bool calc_crc_(const uint8_t *data, uint8_t len, uint8_t *out);
  Status communicate_(uint8_t cmd, uint8_t wait_irq, const uint8_t *send, uint8_t send_len, uint8_t *back,
                      uint8_t *back_len, uint8_t *valid_bits, uint8_t tx_last_bits, bool check_crc);

  // --- Couche ISO14443A / MIFARE Classic ---
  bool wakeup_();
  bool select_(std::vector<uint8_t> &uid);
  bool authenticate_(uint8_t block, const uint8_t *key, const std::vector<uint8_t> &uid);
  bool read_block_(uint8_t block, std::vector<uint8_t> &out);
  void halt_();
  void stop_crypto_();

  // --- Bambu ---
  ReadResult read_bambu_data_(const std::vector<uint8_t> &uid);
  void publish_bambu_data_(const std::vector<uint8_t> &uid, const std::vector<uint8_t> &b1,
                           const std::vector<uint8_t> &b2, const std::vector<uint8_t> &b4,
                           const std::vector<uint8_t> &b5, const std::vector<uint8_t> &b6,
                           const std::vector<uint8_t> &b8, const std::vector<uint8_t> &b9,
                           const std::vector<uint8_t> &b10, const std::vector<uint8_t> &b12,
                           const std::vector<uint8_t> &b14);

  std::vector<uint8_t> current_uid_;
  uint8_t miss_count_{0};
  uint8_t version_{0};

  text_sensor::TextSensor *filament_type_sensor_{nullptr};
  text_sensor::TextSensor *filament_color_sensor_{nullptr};
  text_sensor::TextSensor *filament_color_name_sensor_{nullptr};
  text_sensor::TextSensor *filament_subtype_sensor_{nullptr};
  text_sensor::TextSensor *tray_uid_sensor_{nullptr};
  text_sensor::TextSensor *tray_info_idx_sensor_{nullptr};
  text_sensor::TextSensor *variant_id_sensor_{nullptr};
  text_sensor::TextSensor *card_uid_sensor_{nullptr};
  text_sensor::TextSensor *production_date_sensor_{nullptr};
  text_sensor::TextSensor *last_scan_date_sensor_{nullptr};

  sensor::Sensor *min_temp_sensor_{nullptr};
  sensor::Sensor *max_temp_sensor_{nullptr};
  sensor::Sensor *bed_temp_sensor_{nullptr};
  sensor::Sensor *spool_weight_sensor_{nullptr};
  sensor::Sensor *filament_diameter_sensor_{nullptr};
  sensor::Sensor *drying_temp_sensor_{nullptr};
  sensor::Sensor *drying_time_sensor_{nullptr};
  sensor::Sensor *nozzle_diameter_sensor_{nullptr};
  sensor::Sensor *spool_width_sensor_{nullptr};
  sensor::Sensor *filament_length_sensor_{nullptr};
  sensor::Sensor *material_density_sensor_{nullptr};

  std::vector<BambuSuccessTrigger *> success_triggers_;
  std::vector<BambuErrorTrigger *> error_triggers_;
  std::vector<BambuTagRemovedTrigger *> removed_triggers_;
};

}  // namespace bambu_rc522
}  // namespace esphome
