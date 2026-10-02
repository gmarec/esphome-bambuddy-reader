#include "bambu_rc522.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <mbedtls/md.h>

#include <cmath>
#include <cstring>
#include <ctime>

namespace esphome {
namespace bambu_rc522 {

static const char *const TAG = "bambu_rc522";

// ---- Registres / commandes MFRC522 ----
static const uint8_t REG_COMMAND = 0x01;
static const uint8_t REG_COM_IRQ = 0x04;
static const uint8_t REG_DIV_IRQ = 0x05;
static const uint8_t REG_ERROR = 0x06;
static const uint8_t REG_STATUS2 = 0x08;
static const uint8_t REG_FIFO_DATA = 0x09;
static const uint8_t REG_FIFO_LEVEL = 0x0A;
static const uint8_t REG_CONTROL = 0x0C;
static const uint8_t REG_BIT_FRAMING = 0x0D;
static const uint8_t REG_COLL = 0x0E;
static const uint8_t REG_MODE = 0x11;
static const uint8_t REG_TX_MODE = 0x12;
static const uint8_t REG_RX_MODE = 0x13;
static const uint8_t REG_TX_CONTROL = 0x14;
static const uint8_t REG_TX_ASK = 0x15;
static const uint8_t REG_CRC_H = 0x21;
static const uint8_t REG_CRC_L = 0x22;
static const uint8_t REG_MOD_WIDTH = 0x24;
static const uint8_t REG_T_MODE = 0x2A;
static const uint8_t REG_T_PRESCALER = 0x2B;
static const uint8_t REG_T_RELOAD_H = 0x2C;
static const uint8_t REG_T_RELOAD_L = 0x2D;
static const uint8_t REG_VERSION = 0x37;

static const uint8_t CMD_IDLE = 0x00;
static const uint8_t CMD_CALC_CRC = 0x03;
static const uint8_t CMD_TRANSCEIVE = 0x0C;
static const uint8_t CMD_MF_AUTHENT = 0x0E;
static const uint8_t CMD_SOFT_RESET = 0x0F;

static const uint8_t PICC_WUPA = 0x52;
static const uint8_t PICC_SEL_CL1 = 0x93;
static const uint8_t PICC_AUTH_KEY_A = 0x60;
static const uint8_t PICC_READ = 0x30;
static const uint8_t PICC_HLTA = 0x50;

static const uint32_t IO_TIMEOUT_MS = 40;

// ---- Cle Bambu ----
static const uint8_t BAMBU_MASTER_KEY[] = {0x9a, 0x75, 0x9c, 0xf2, 0xc4, 0xf7, 0xca, 0xff,
                                           0x22, 0x2c, 0xb9, 0x76, 0x9b, 0x41, 0xbc, 0x96};
static const uint8_t BAMBU_HKDF_INFO[] = {'R', 'F', 'I', 'D', '-', 'A', '\0'};

// =====================================================================
// Driver MFRC522
// =====================================================================

void BambuRc522::write_reg_(uint8_t reg, uint8_t val) { this->write_byte(reg, val); }

uint8_t BambuRc522::read_reg_(uint8_t reg) {
  uint8_t v = 0;
  this->read_byte(reg, &v);
  return v;
}

bool BambuRc522::calc_crc_(const uint8_t *data, uint8_t len, uint8_t *out) {
  this->write_reg_(REG_COMMAND, CMD_IDLE);
  this->write_reg_(REG_DIV_IRQ, 0x04);
  this->write_reg_(REG_FIFO_LEVEL, 0x80);
  this->write_bytes(REG_FIFO_DATA, data, len);
  this->write_reg_(REG_COMMAND, CMD_CALC_CRC);
  uint32_t start = millis();
  while (millis() - start < IO_TIMEOUT_MS) {
    if (this->read_reg_(REG_DIV_IRQ) & 0x04) {
      this->write_reg_(REG_COMMAND, CMD_IDLE);
      out[0] = this->read_reg_(REG_CRC_L);
      out[1] = this->read_reg_(REG_CRC_H);
      return true;
    }
  }
  return false;
}

BambuRc522::Status BambuRc522::communicate_(uint8_t cmd, uint8_t wait_irq, const uint8_t *send, uint8_t send_len,
                                            uint8_t *back, uint8_t *back_len, uint8_t *valid_bits,
                                            uint8_t tx_last_bits, bool check_crc) {
  this->write_reg_(REG_COMMAND, CMD_IDLE);
  this->write_reg_(REG_COM_IRQ, 0x7F);
  this->write_reg_(REG_FIFO_LEVEL, 0x80);
  this->write_bytes(REG_FIFO_DATA, send, send_len);
  this->write_reg_(REG_BIT_FRAMING, tx_last_bits & 0x07);
  this->write_reg_(REG_COMMAND, cmd);
  if (cmd == CMD_TRANSCEIVE)
    this->set_bits_(REG_BIT_FRAMING, 0x80);  // StartSend

  uint32_t start = millis();
  while (true) {
    uint8_t irq = this->read_reg_(REG_COM_IRQ);
    if (irq & wait_irq)
      break;
    if (irq & 0x01)  // TimerIRq : pas de reponse de la carte
      return RC_TIMEOUT;
    if (millis() - start > IO_TIMEOUT_MS)
      return RC_TIMEOUT;
  }

  uint8_t err = this->read_reg_(REG_ERROR);
  if (err & 0x13)  // BufferOvfl | ParityErr | ProtocolErr
    return RC_ERROR;

  uint8_t vb = 0;
  if (back != nullptr && back_len != nullptr) {
    uint8_t n = this->read_reg_(REG_FIFO_LEVEL);
    if (n > *back_len)
      return RC_NO_ROOM;
    *back_len = n;
    if (n > 0)
      this->read_bytes(REG_FIFO_DATA, back, n);
    vb = this->read_reg_(REG_CONTROL) & 0x07;
    if (valid_bits != nullptr)
      *valid_bits = vb;
  }

  if (err & 0x08)
    return RC_COLLISION;

  if (check_crc && back != nullptr && back_len != nullptr) {
    if (*back_len == 1 && vb == 4)  // NAK MIFARE
      return RC_ERROR;
    if (*back_len < 2 || vb != 0)
      return RC_CRC;
    uint8_t crc[2];
    if (!this->calc_crc_(back, *back_len - 2, crc))
      return RC_TIMEOUT;
    if (back[*back_len - 2] != crc[0] || back[*back_len - 1] != crc[1])
      return RC_CRC;
  }
  return RC_OK;
}

bool BambuRc522::wakeup_() {
  this->clear_bits_(REG_COLL, 0x80);
  uint8_t cmd = PICC_WUPA;
  uint8_t atqa[2];
  uint8_t len = sizeof(atqa), vb = 0;
  auto st = this->communicate_(CMD_TRANSCEIVE, 0x30, &cmd, 1, atqa, &len, &vb, 7, false);
  return st == RC_OK && len == 2 && vb == 0;
}

bool BambuRc522::select_(std::vector<uint8_t> &uid) {
  uint8_t buf[9];
  uint8_t back[5];
  uint8_t len = sizeof(back), vb = 0;

  // Anticollision niveau 1
  this->clear_bits_(REG_COLL, 0x80);
  buf[0] = PICC_SEL_CL1;
  buf[1] = 0x20;
  if (this->communicate_(CMD_TRANSCEIVE, 0x30, buf, 2, back, &len, &vb, 0, false) != RC_OK || len != 5)
    return false;
  if ((back[0] ^ back[1] ^ back[2] ^ back[3]) != back[4])
    return false;

  // SELECT
  buf[1] = 0x70;
  memcpy(buf + 2, back, 5);
  if (!this->calc_crc_(buf, 7, buf + 7))
    return false;
  uint8_t sak[3];
  len = sizeof(sak);
  if (this->communicate_(CMD_TRANSCEIVE, 0x30, buf, 9, sak, &len, &vb, 0, true) != RC_OK || len != 3)
    return false;

  // back[0] == 0x88 : tag de cascade (UID 7 octets) -> pas un tag Bambu, on garde l'UID partiel
  uid.assign(back, back + 4);
  return true;
}

bool BambuRc522::authenticate_(uint8_t block, const uint8_t *key, const std::vector<uint8_t> &uid) {
  uint8_t buf[12];
  buf[0] = PICC_AUTH_KEY_A;
  buf[1] = block;
  memcpy(buf + 2, key, 6);
  memcpy(buf + 8, uid.data(), 4);
  if (this->communicate_(CMD_MF_AUTHENT, 0x10, buf, sizeof(buf), nullptr, nullptr, nullptr, 0, false) != RC_OK)
    return false;
  return (this->read_reg_(REG_STATUS2) & 0x08) != 0;  // MFCrypto1On
}

bool BambuRc522::read_block_(uint8_t block, std::vector<uint8_t> &out) {
  uint8_t buf[4] = {PICC_READ, block, 0, 0};
  if (!this->calc_crc_(buf, 2, buf + 2))
    return false;
  uint8_t back[18];
  uint8_t len = sizeof(back), vb = 0;
  if (this->communicate_(CMD_TRANSCEIVE, 0x30, buf, 4, back, &len, &vb, 0, true) != RC_OK || len != 18)
    return false;
  out.assign(back, back + 16);
  return true;
}

void BambuRc522::halt_() {
  uint8_t buf[4] = {PICC_HLTA, 0, 0, 0};
  if (!this->calc_crc_(buf, 2, buf + 2))
    return;
  // Une carte qui s'arrete ne repond pas : le timeout est le cas nominal
  this->communicate_(CMD_TRANSCEIVE, 0x30, buf, 4, nullptr, nullptr, nullptr, 0, false);
}

void BambuRc522::stop_crypto_() { this->clear_bits_(REG_STATUS2, 0x08); }

// =====================================================================
// Cycle de vie ESPHome
// =====================================================================

void BambuRc522::setup() {
  if (!this->write_byte(REG_COMMAND, CMD_SOFT_RESET)) {
    ESP_LOGE(TAG, "Pas de reponse I2C du lecteur");
    this->mark_failed();
    return;
  }
  delay(50);
  for (int i = 0; i < 10 && (this->read_reg_(REG_COMMAND) & 0x10); i++)
    delay(10);

  this->write_reg_(REG_TX_MODE, 0x00);
  this->write_reg_(REG_RX_MODE, 0x00);
  this->write_reg_(REG_MOD_WIDTH, 0x26);
  // Timer ~25 ms : 13.56 MHz / (2*169+1) ~ 40 kHz, reload 1000
  this->write_reg_(REG_T_MODE, 0x80);
  this->write_reg_(REG_T_PRESCALER, 0xA9);
  this->write_reg_(REG_T_RELOAD_H, 0x03);
  this->write_reg_(REG_T_RELOAD_L, 0xE8);
  this->write_reg_(REG_TX_ASK, 0x40);  // 100% ASK
  this->write_reg_(REG_MODE, 0x3D);    // CRC preset 0x6363

  uint8_t tx = this->read_reg_(REG_TX_CONTROL);
  if ((tx & 0x03) != 0x03)
    this->write_reg_(REG_TX_CONTROL, tx | 0x03);  // antenne ON

  this->version_ = this->read_reg_(REG_VERSION);
  if (this->version_ == 0x00 || this->version_ == 0xFF) {
    ESP_LOGE(TAG, "Version lecteur invalide (0x%02X)", this->version_);
    this->mark_failed();
  }
}

void BambuRc522::dump_config() {
  ESP_LOGCONFIG(TAG, "Bambu RC522 (I2C):");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Version puce: 0x%02X", this->version_);
  LOG_UPDATE_INTERVAL(this);
  if (this->is_failed())
    ESP_LOGE(TAG, "  Initialisation echouee");
}

void BambuRc522::update() {
  std::vector<uint8_t> uid;
  if (!this->wakeup_() || !this->select_(uid)) {
    // Deux polls sans tag avant de declarer le retrait (evite les faux retraits)
    if (!this->current_uid_.empty() && ++this->miss_count_ >= 2) {
      this->current_uid_.clear();
      this->miss_count_ = 0;
      for (auto *t : this->removed_triggers_)
        t->trigger();
    }
    return;
  }
  this->miss_count_ = 0;

  if (uid == this->current_uid_) {
    this->halt_();
    return;
  }
  this->current_uid_ = uid;

  if (this->card_uid_sensor_ != nullptr) {
    char hex[3];
    std::string s;
    for (auto b : uid) {
      snprintf(hex, sizeof(hex), "%02X", b);
      s += hex;
    }
    this->card_uid_sensor_->publish_state(s);
  }

  ReadResult res = this->read_bambu_data_(uid);
  this->halt_();
  this->stop_crypto_();

  if (res == READ_OK) {
    for (auto *t : this->success_triggers_)
      t->trigger();
    return;
  }
  // Echec de lecture transitoire : on oublie l'UID pour retenter au prochain poll.
  // Tag non Bambu : on garde l'UID pour ne pas reboucler sur l'erreur tant qu'il reste pose.
  if (res == READ_FAILED)
    this->current_uid_.clear();
  for (auto *t : this->error_triggers_)
    t->trigger();
}

// =====================================================================
// HKDF + lecture Bambu
// =====================================================================

static bool hkdf_sha256(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
                        const uint8_t *info, size_t info_len, uint8_t *okm, size_t okm_len) {
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (md == nullptr)
    return false;
  uint8_t prk[32];
  if (mbedtls_md_hmac(md, salt, salt_len, ikm, ikm_len, prk) != 0)
    return false;

  uint8_t t[32];
  size_t t_len = 0, offset = 0;
  uint8_t counter = 1;
  while (offset < okm_len) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, md, 1) != 0) {
      mbedtls_md_free(&ctx);
      return false;
    }
    mbedtls_md_hmac_starts(&ctx, prk, sizeof(prk));
    if (t_len > 0)
      mbedtls_md_hmac_update(&ctx, t, t_len);
    mbedtls_md_hmac_update(&ctx, info, info_len);
    mbedtls_md_hmac_update(&ctx, &counter, 1);
    mbedtls_md_hmac_finish(&ctx, t);
    mbedtls_md_free(&ctx);
    t_len = 32;
    size_t n = (okm_len - offset < 32) ? okm_len - offset : 32;
    memcpy(okm + offset, t, n);
    offset += n;
    counter++;
  }
  return true;
}

BambuRc522::ReadResult BambuRc522::read_bambu_data_(const std::vector<uint8_t> &uid) {
  if (uid.size() != 4 || uid[0] == 0x88) {
    ESP_LOGW(TAG, "UID non 4 octets : pas un tag Bambu");
    return READ_NOT_BAMBU;
  }

  uint8_t keys[96];
  if (!hkdf_sha256(BAMBU_MASTER_KEY, sizeof(BAMBU_MASTER_KEY), uid.data(), uid.size(), BAMBU_HKDF_INFO,
                   sizeof(BAMBU_HKDF_INFO), keys, sizeof(keys))) {
    ESP_LOGE(TAG, "Derivation HKDF echouee");
    return READ_FAILED;
  }

  std::vector<uint8_t> b1, b2, b4, b5, b6, b8, b9, b10, b12, b14;

  if (!this->authenticate_(3, &keys[0], uid)) {
    ESP_LOGW(TAG, "Auth secteur 0 refusee : pas un tag Bambu ?");
    return READ_NOT_BAMBU;
  }
  if (!this->read_block_(1, b1) || !this->read_block_(2, b2))
    return READ_FAILED;

  if (!this->authenticate_(7, &keys[6], uid) || !this->read_block_(4, b4) || !this->read_block_(5, b5) ||
      !this->read_block_(6, b6))
    return READ_FAILED;

  if (!this->authenticate_(11, &keys[12], uid) || !this->read_block_(8, b8) || !this->read_block_(9, b9) ||
      !this->read_block_(10, b10))
    return READ_FAILED;

  if (!this->authenticate_(15, &keys[18], uid) || !this->read_block_(12, b12) || !this->read_block_(14, b14))
    return READ_FAILED;

  this->publish_bambu_data_(uid, b1, b2, b4, b5, b6, b8, b9, b10, b12, b14);
  return READ_OK;
}

// =====================================================================
// Publication (reprise de bambu_nfc)
// =====================================================================

static std::string trim_string(const std::vector<uint8_t> &data, size_t from = 0, size_t to = 16) {
  if (to > data.size())
    to = data.size();
  std::string s(data.begin() + from, data.begin() + to);
  size_t end = s.find_last_not_of(std::string("\0 ", 2));
  if (end == std::string::npos)
    return "";
  s = s.substr(0, end + 1);
  size_t nul = s.find('\0');
  return nul == std::string::npos ? s : s.substr(0, nul);
}

static std::string format_hex(const std::vector<uint8_t> &data) {
  std::string s;
  char hex[3];
  for (auto b : data) {
    snprintf(hex, sizeof(hex), "%02X", b);
    s += hex;
  }
  return s;
}

static float read_float_le(const std::vector<uint8_t> &d, size_t off) {
  float v;
  memcpy(&v, &d[off], 4);
  return v;
}
static uint16_t read_u16_le(const std::vector<uint8_t> &d, size_t off) { return d[off] | (d[off + 1] << 8); }

static void pub(text_sensor::TextSensor *s, const std::string &v) {
  if (s != nullptr)
    s->publish_state(v);
}
static void pub(sensor::Sensor *s, float v) {
  if (s != nullptr)
    s->publish_state(v);
}

void BambuRc522::publish_bambu_data_(const std::vector<uint8_t> &uid, const std::vector<uint8_t> &b1,
                                     const std::vector<uint8_t> &b2, const std::vector<uint8_t> &b4,
                                     const std::vector<uint8_t> &b5, const std::vector<uint8_t> &b6,
                                     const std::vector<uint8_t> &b8, const std::vector<uint8_t> &b9,
                                     const std::vector<uint8_t> &b10, const std::vector<uint8_t> &b12,
                                     const std::vector<uint8_t> &b14) {
  ESP_LOGI(TAG, "uid=%s b1=%s b2=%s b4=%s b5=%s b6=%s", format_hex(uid).c_str(), format_hex(b1).c_str(),
           format_hex(b2).c_str(), format_hex(b4).c_str(), format_hex(b5).c_str(), format_hex(b6).c_str());

  // Bloc 1 : variante (8) + ID materiau (8)
  pub(this->variant_id_sensor_, trim_string(b1, 0, 8));
  pub(this->tray_info_idx_sensor_, trim_string(b1, 8, 16));

  // Blocs 2 et 4 : type de base / type detaille
  std::string base_type = trim_string(b2);
  std::string detailed = trim_string(b4);
  pub(this->filament_type_sensor_, base_type);
  std::string subtype;
  if (detailed.size() > base_type.size() + 1 && detailed.compare(0, base_type.size(), base_type) == 0 &&
      detailed[base_type.size()] == ' ')
    subtype = detailed.substr(base_type.size() + 1);
  pub(this->filament_subtype_sensor_, subtype);
  pub(this->material_density_sensor_, find_bambu_density(detailed.c_str()));

  // Bloc 5 : RGBA + poids + diametre
  char color_hex[8];
  snprintf(color_hex, sizeof(color_hex), "#%02X%02X%02X", b5[0], b5[1], b5[2]);
  pub(this->filament_color_sensor_, std::string(color_hex));
  const char *cname = find_bambu_color_name(detailed.c_str(), color_hex + 1);
  if (cname != nullptr) {
    std::string cn(cname);
    size_t sp = detailed.find(' ');
    if (sp != std::string::npos) {
      std::string sub = detailed.substr(sp + 1);
      if (cn.size() > sub.size() && cn.compare(0, sub.size() + 1, sub + " ") == 0)
        cn = cn.substr(sub.size() + 1);
      else if (cn.size() > sub.size() && cn.compare(cn.size() - sub.size() - 1, sub.size() + 1, " " + sub) == 0)
        cn = cn.substr(0, cn.size() - sub.size() - 1);
    }
    pub(this->filament_color_name_sensor_, cn);
  } else {
    pub(this->filament_color_name_sensor_, std::string(color_hex));
  }
  pub(this->spool_weight_sensor_, read_u16_le(b5, 4));
  pub(this->filament_diameter_sensor_, read_float_le(b5, 8));

  // Bloc 6 : sechage + plateau + buse
  pub(this->drying_temp_sensor_, read_u16_le(b6, 0));
  pub(this->drying_time_sensor_, read_u16_le(b6, 2));
  pub(this->bed_temp_sensor_, read_u16_le(b6, 6));
  pub(this->max_temp_sensor_, read_u16_le(b6, 8));
  pub(this->min_temp_sensor_, read_u16_le(b6, 10));

  pub(this->nozzle_diameter_sensor_, read_float_le(b8, 12));   // Bloc 8
  pub(this->tray_uid_sensor_, format_hex(b9));                 // Bloc 9
  pub(this->spool_width_sensor_, read_u16_le(b10, 4) / 100.0f);  // Bloc 10

  // Bloc 12 : "YYYY_MM_DD_HH_MM" -> "YYYY-MM-DD"
  std::string d = trim_string(b12);
  if (d.size() >= 10 && d[4] == '_' && d[7] == '_')
    d = d.substr(0, 4) + "-" + d.substr(5, 2) + "-" + d.substr(8, 2);
  pub(this->production_date_sensor_, d);

  pub(this->filament_length_sensor_, read_u16_le(b14, 4));  // Bloc 14

  if (this->last_scan_date_sensor_ != nullptr) {
    time_t now = ::time(nullptr);
    struct tm ti;
    gmtime_r(&now, &ti);
    if (ti.tm_year > 100) {
      char buf[32];
      strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+00:00", &ti);
      this->last_scan_date_sensor_->publish_state(buf);
    }
  }
}

// =====================================================================
// Reset
// =====================================================================

void BambuRc522ResetButton::press_action() { this->parent_->clear_sensors(); }

void BambuRc522::clear_sensors() {
  text_sensor::TextSensor *ts[] = {filament_type_sensor_,  filament_color_sensor_, filament_color_name_sensor_,
                                   filament_subtype_sensor_, tray_uid_sensor_,     tray_info_idx_sensor_,
                                   variant_id_sensor_,       production_date_sensor_, last_scan_date_sensor_};
  for (auto *s : ts)
    if (s != nullptr)
      s->publish_state("unknown");
  sensor::Sensor *ns[] = {min_temp_sensor_,    max_temp_sensor_,       bed_temp_sensor_,        spool_weight_sensor_,
                          filament_diameter_sensor_, drying_temp_sensor_, drying_time_sensor_, nozzle_diameter_sensor_,
                          spool_width_sensor_, filament_length_sensor_, material_density_sensor_};
  for (auto *s : ns)
    if (s != nullptr)
      s->publish_state(NAN);
  this->current_uid_.clear();
}

}  // namespace bambu_rc522
}  // namespace esphome
