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

// ---- MFRC522 registers / commands ----
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
static const uint8_t REG_RF_CFG = 0x26;
static const uint8_t REG_CW_GSP = 0x28;
static const uint8_t REG_MOD_GSP = 0x29;
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
static const uint8_t PICC_SEL_CL2 = 0x95;
static const uint8_t PICC_AUTH_KEY_A = 0x60;
static const uint8_t PICC_READ = 0x30;
static const uint8_t PICC_UL_WRITE = 0xA2;  // NTAG / Ultralight: write one 4-byte page
static const uint8_t PICC_HLTA = 0x50;

static const uint32_t IO_TIMEOUT_MS = 40;

// ---- Bambu key ----
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
    if (irq & 0x01)  // TimerIRq: no answer from the card
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
  if (st == RC_OK && len == 2 && vb == 0)
    return true;
  if (st != RC_TIMEOUT) {
    this->stat_wupa_bad_++;
    this->last_err_ = this->read_reg_(REG_ERROR);
  }
  return false;
}

bool BambuRc522::select_(std::vector<uint8_t> &uid, uint8_t &sak) {
  // Cascade level 1 (4-byte UID, Bambu), then level 2 if the SAK reports an incomplete UID (NTAG: 7 bytes)
  static const uint8_t LEVELS[] = {PICC_SEL_CL1, PICC_SEL_CL2};
  uid.clear();
  for (uint8_t level : LEVELS) {
    uint8_t buf[9];
    uint8_t back[5];
    uint8_t len = sizeof(back), vb = 0;

    // Anticollision
    this->clear_bits_(REG_COLL, 0x80);
    buf[0] = level;
    buf[1] = 0x20;
    if (this->communicate_(CMD_TRANSCEIVE, 0x30, buf, 2, back, &len, &vb, 0, false) != RC_OK || len != 5) {
      this->stat_anticoll_fail_++;
      this->last_err_ = this->read_reg_(REG_ERROR);
      return false;
    }
    if ((back[0] ^ back[1] ^ back[2] ^ back[3]) != back[4]) {
      this->stat_anticoll_fail_++;
      return false;
    }

    // SELECT
    buf[1] = 0x70;
    memcpy(buf + 2, back, 5);
    if (!this->calc_crc_(buf, 7, buf + 7))
      return false;
    uint8_t sak_buf[3];
    len = sizeof(sak_buf);
    if (this->communicate_(CMD_TRANSCEIVE, 0x30, buf, 9, sak_buf, &len, &vb, 0, true) != RC_OK || len != 3) {
      this->stat_select_fail_++;
      this->last_err_ = this->read_reg_(REG_ERROR);
      return false;
    }

    if (sak_buf[0] & 0x04) {
      // Incomplete UID: back[0] is the 0x88 cascade tag, keep the next 3 bytes
      uid.insert(uid.end(), back + 1, back + 4);
      continue;
    }
    uid.insert(uid.end(), back, back + 4);
    sak = sak_buf[0];
    return true;
  }
  return false;
}

bool BambuRc522::authenticate_(uint8_t block, const uint8_t *key, const std::vector<uint8_t> &uid) {
  uint8_t buf[12];
  buf[0] = PICC_AUTH_KEY_A;
  buf[1] = block;
  memcpy(buf + 2, key, 6);
  memcpy(buf + 8, uid.data(), 4);
  auto st = this->communicate_(CMD_MF_AUTHENT, 0x10, buf, sizeof(buf), nullptr, nullptr, nullptr, 0, false);
  uint8_t s2 = this->read_reg_(REG_STATUS2);
  if (st != RC_OK || !(s2 & 0x08)) {
    ESP_LOGW(TAG, "Auth block %u failed (status %u, Status2 0x%02X, Error 0x%02X)", block, st, s2,
             this->read_reg_(REG_ERROR));
    return false;
  }
  return true;
}

bool BambuRc522::read_block_(uint8_t block, std::vector<uint8_t> &out) {
  uint8_t buf[4] = {PICC_READ, block, 0, 0};
  if (!this->calc_crc_(buf, 2, buf + 2))
    return false;
  uint8_t back[18];
  uint8_t len = sizeof(back), vb = 0;
  auto st = this->communicate_(CMD_TRANSCEIVE, 0x30, buf, 4, back, &len, &vb, 0, true);
  if (st != RC_OK || len != 18) {
    ESP_LOGW(TAG, "Read block %u failed (status %u, len %u, bits %u, Error 0x%02X)", block, st, len, vb,
             this->read_reg_(REG_ERROR));
    return false;
  }
  out.assign(back, back + 16);
  return true;
}

bool BambuRc522::write_page_(uint8_t page, const uint8_t *data) {
  uint8_t buf[8] = {PICC_UL_WRITE, page, data[0], data[1], data[2], data[3], 0, 0};
  if (!this->calc_crc_(buf, 6, buf + 6))
    return false;
  uint8_t ack[1];
  uint8_t len = sizeof(ack), vb = 0;
  // Expected answer: 4-bit ACK (0xA)
  if (this->communicate_(CMD_TRANSCEIVE, 0x30, buf, 8, ack, &len, &vb, 0, false) != RC_OK || len != 1 || vb != 4 ||
      (ack[0] & 0x0F) != 0x0A) {
    ESP_LOGW(TAG, "Write page %u failed", page);
    return false;
  }
  return true;
}

void BambuRc522::halt_() {
  uint8_t buf[4] = {PICC_HLTA, 0, 0, 0};
  if (!this->calc_crc_(buf, 2, buf + 2))
    return;
  // A halting card does not answer: the timeout is the normal case
  this->communicate_(CMD_TRANSCEIVE, 0x30, buf, 4, nullptr, nullptr, nullptr, 0, false);
}

void BambuRc522::stop_crypto_() { this->clear_bits_(REG_STATUS2, 0x08); }

// =====================================================================
// PN532 driver (I2C, external module on the Grove port)
// =====================================================================
// Frame: 00 00 FF LEN LCS D4 CMD... DCS 00. Over I2C every read starts with a status byte
// (0x01 = ready). Reference: PN532 User Manual (UM0701-02) and ESPHome's pn532 component.

static const uint8_t PN_CMD_GET_FIRMWARE = 0x02;
static const uint8_t PN_CMD_SAM_CONFIG = 0x14;
static const uint8_t PN_CMD_RF_CONFIG = 0x32;
static const uint8_t PN_CMD_IN_DATA_EXCHANGE = 0x40;
static const uint8_t PN_CMD_IN_LIST_PASSIVE = 0x4A;

bool BambuRc522::pn_wait_ready_(uint32_t timeout_ms) {
  uint32_t start = millis();
  uint8_t status = 0;
  while (millis() - start < timeout_ms) {
    if (this->read(&status, 1) == i2c::ERROR_OK && status == 0x01)
      return true;
    delay(1);
  }
  return false;
}

bool BambuRc522::pn_command_(const std::vector<uint8_t> &cmd, std::vector<uint8_t> &resp, uint32_t timeout_ms) {
  std::vector<uint8_t> frame = {0x00, 0x00, 0xFF};
  uint8_t len = cmd.size() + 1;
  frame.push_back(len);
  frame.push_back(~len + 1);
  frame.push_back(0xD4);
  uint8_t sum = 0xD4;
  for (auto b : cmd) {
    frame.push_back(b);
    sum += b;
  }
  frame.push_back(~sum + 1);
  frame.push_back(0x00);
  if (this->write(frame.data(), frame.size()) != i2c::ERROR_OK)
    return false;

  // ACK
  static const uint8_t ACK[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  uint8_t ack[7];
  if (!this->pn_wait_ready_(30) || this->read(ack, sizeof(ack)) != i2c::ERROR_OK || memcmp(ack + 1, ACK, 6) != 0)
    return false;

  // Response
  if (!this->pn_wait_ready_(timeout_ms)) {
    this->write(ACK, sizeof(ACK));  // abort the running command
    return false;
  }
  uint8_t buf[48];
  if (this->read(buf, sizeof(buf)) != i2c::ERROR_OK)
    return false;
  size_t i = 1;  // buf[0]: status byte
  while (i + 1 < sizeof(buf) && !(buf[i] == 0x00 && buf[i + 1] == 0xFF))
    i++;
  i += 2;
  if (i + 2 > sizeof(buf))
    return false;
  uint8_t flen = buf[i];
  if (static_cast<uint8_t>(flen + buf[i + 1]) != 0 || flen < 2 || i + 2 + flen + 1 > sizeof(buf))
    return false;
  const uint8_t *d = &buf[i + 2];
  if (d[0] != 0xD5 || d[1] != cmd[0] + 1)
    return false;
  uint8_t dsum = 0;
  for (uint8_t k = 0; k <= flen; k++)
    dsum += d[k];
  if (dsum != 0)
    return false;
  resp.assign(d + 2, d + flen);
  return true;
}

bool BambuRc522::pn_setup_() {
  std::vector<uint8_t> r;
  // The PN532 may ignore the first frame after power-up
  bool ok = false;
  for (int i = 0; i < 3 && !ok; i++) {
    ok = this->pn_command_({PN_CMD_GET_FIRMWARE}, r, 100) && r.size() >= 4;
    if (!ok)
      delay(50);
  }
  if (!ok)
    return false;
  this->version_ = r[0];  // 0x32 for a PN532
  this->pn_fw_ = (r[1] << 8) | r[2];
  // Normal mode, no IRQ
  if (!this->pn_command_({PN_CMD_SAM_CONFIG, 0x01, 0x14, 0x00}, r, 100))
    return false;
  // MaxRetries: a single passive activation attempt, otherwise InListPassiveTarget blocks with no tag
  return this->pn_command_({PN_CMD_RF_CONFIG, 0x05, 0x00, 0x01, 0x01}, r, 100);
}

bool BambuRc522::pn_detect_(std::vector<uint8_t> &uid, uint8_t &sak) {
  std::vector<uint8_t> r;
  // 1 target, 106 kbps type A. Response: NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID...
  if (!this->pn_command_({PN_CMD_IN_LIST_PASSIVE, 0x01, 0x00}, r, 100) || r.size() < 6 || r[0] == 0)
    return false;
  uint8_t n = r[5];
  if (r.size() < 6u + n)
    return false;
  sak = r[4];
  uid.assign(r.begin() + 6, r.begin() + 6 + n);
  return true;
}

bool BambuRc522::pn_exchange_(const std::vector<uint8_t> &picc_cmd, std::vector<uint8_t> &out) {
  std::vector<uint8_t> cmd = {PN_CMD_IN_DATA_EXCHANGE, 0x01};
  cmd.insert(cmd.end(), picc_cmd.begin(), picc_cmd.end());
  std::vector<uint8_t> r;
  if (!this->pn_command_(cmd, r, 100) || r.empty() || (r[0] & 0x3F) != 0)
    return false;
  out.assign(r.begin() + 1, r.end());
  return true;
}

// =====================================================================
// Primitives shared by both readers
// =====================================================================

bool BambuRc522::detect_(std::vector<uint8_t> &uid, uint8_t &sak) {
  if (this->reader_ == READER_PN532)
    return this->pn_detect_(uid, sak);
  bool woke = this->wakeup_();
  if (woke)
    this->stat_wupa_ok_++;
  return woke && this->select_(uid, sak);
}

bool BambuRc522::auth_(uint8_t block, const uint8_t *key, const std::vector<uint8_t> &uid) {
  if (this->reader_ != READER_PN532)
    return this->authenticate_(block, key, uid);
  std::vector<uint8_t> cmd = {PICC_AUTH_KEY_A, block};
  cmd.insert(cmd.end(), key, key + 6);
  cmd.insert(cmd.end(), uid.begin(), uid.begin() + 4);
  std::vector<uint8_t> out;
  if (!this->pn_exchange_(cmd, out)) {
    ESP_LOGW(TAG, "Auth block %u failed (PN532)", block);
    return false;
  }
  return true;
}

bool BambuRc522::read16_(uint8_t block, std::vector<uint8_t> &out) {
  if (this->reader_ != READER_PN532)
    return this->read_block_(block, out);
  std::vector<uint8_t> r;
  if (!this->pn_exchange_({PICC_READ, block}, r) || r.size() < 16) {
    ESP_LOGW(TAG, "Read block %u failed (PN532)", block);
    return false;
  }
  out.assign(r.begin(), r.begin() + 16);
  return true;
}

bool BambuRc522::write4_(uint8_t page, const uint8_t *data) {
  if (this->reader_ != READER_PN532)
    return this->write_page_(page, data);
  std::vector<uint8_t> r;
  if (!this->pn_exchange_({PICC_UL_WRITE, page, data[0], data[1], data[2], data[3]}, r)) {
    ESP_LOGW(TAG, "Write page %u failed (PN532)", page);
    return false;
  }
  return true;
}

void BambuRc522::end_session_() {
  // PN532: InListPassiveTarget reselects the tag on the next poll, nothing to do
  if (this->reader_ == READER_PN532)
    return;
  this->halt_();
  this->stop_crypto_();
}

// =====================================================================
// ESPHome lifecycle
// =====================================================================

void BambuRc522::setup() {
  if (this->reader_ == READER_PN532) {
    if (!this->pn_setup_()) {
      ESP_LOGE(TAG, "PN532: no response (wiring, address 0x24, switches set to I2C?)");
      this->mark_failed();
    }
    return;
  }
  if (!this->write_byte(REG_COMMAND, CMD_SOFT_RESET)) {
    ESP_LOGE(TAG, "No I2C response from the reader");
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
  this->write_reg_(REG_RF_CFG, 0x70);  // max receiver gain (48 dB): better range
  // Max transmit power (TX1/TX2 output conductance, default 0x20 out of 0x3F):
  // the emitted field powers the passive tag, so it drives the range
  this->write_reg_(REG_CW_GSP, 0x3F);
  this->write_reg_(REG_MOD_GSP, 0x3F);

  uint8_t tx = this->read_reg_(REG_TX_CONTROL);
  if ((tx & 0x03) != 0x03)
    this->write_reg_(REG_TX_CONTROL, tx | 0x03);  // antenna ON

  this->version_ = this->read_reg_(REG_VERSION);
  if (this->version_ == 0x00 || this->version_ == 0xFF) {
    ESP_LOGE(TAG, "Invalid reader version (0x%02X)", this->version_);
    this->mark_failed();
  }
}

void BambuRc522::dump_config() {
  ESP_LOGCONFIG(TAG, "Bambu RC522 (I2C):");
  LOG_I2C_DEVICE(this);
  if (this->reader_ == READER_PN532) {
    ESP_LOGCONFIG(TAG, "  Reader: PN532 (chip 0x%02X, firmware %u.%u)", this->version_, this->pn_fw_ >> 8,
                  this->pn_fw_ & 0xFF);
  } else {
    ESP_LOGCONFIG(TAG, "  Reader: internal WS1850S (version 0x%02X)", this->version_);
    ESP_LOGCONFIG(TAG, "  RX gain: 0x%02X, TX power: 0x%02X", this->read_reg_(REG_RF_CFG),
                  this->read_reg_(REG_CW_GSP));
  }
  LOG_UPDATE_INTERVAL(this);
  if (this->is_failed())
    ESP_LOGE(TAG, "  Setup failed");
}

void BambuRc522::update() {
  if (this->diag_mode_) {
    this->measure_signal_();
    return;
  }
  this->log_stats_();
  this->stat_polls_++;
  std::vector<uint8_t> uid;
  uint8_t sak = 0;
  if (!this->detect_(uid, sak)) {
    // Two polls without a tag before reporting removal (avoids false removals)
    if (!this->current_uid_.empty() && ++this->miss_count_ >= 2) {
      this->current_uid_.clear();
      this->miss_count_ = 0;
      for (auto *t : this->removed_triggers_)
        t->trigger();
    }
    return;
  }
  this->miss_count_ = 0;
  this->stat_select_ok_++;

  if (uid == this->current_uid_) {
    this->end_session_();
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

  // NTAG / Ultralight (SAK 0x00, 7-byte UID): no Bambu data, the tag is identified by its UID
  if (sak == 0x00 && uid.size() == 7) {
    this->end_session_();
    ESP_LOGI(TAG, "NTAG tag detected: %s", this->card_uid_sensor_ != nullptr
                                                ? this->card_uid_sensor_->state.c_str() : "");
    this->clear_bambu_sensors_();
    for (auto *t : this->ntag_triggers_)
      t->trigger();
    return;
  }

  ReadResult res = this->read_bambu_data_(uid, sak);
  this->end_session_();

  if (res == READ_OK) {
    for (auto *t : this->success_triggers_)
      t->trigger();
    return;
  }
  // Transient read failure: forget the UID to retry on the next poll.
  // Non-Bambu tag: keep the UID so the error does not loop while the tag stays in place.
  if (res == READ_FAILED)
    this->current_uid_.clear();
  for (auto *t : this->error_triggers_)
    t->trigger();
}

// =====================================================================
// Diagnostic mode: tag <-> antenna coupling measurement
// =====================================================================
// The chip has no RSSI, so we measure:
//  - reliability: % of successful wake-up + select cycles at max gain
//  - margin: number of receiver gain steps (18 -> 48 dB) at which the tag still answers.
//    The lower the gain it answers at, the stronger the coupling.

void BambuRc522::set_diag_mode(bool on) {
  this->diag_mode_ = on;
  this->diag_score_ = 0;
  this->diag_pct_ = 0;
  if (!on) {
    if (this->reader_ != READER_PN532)
      this->write_reg_(REG_RF_CFG, 0x70);
    this->current_uid_.clear();  // re-read the tag in place when leaving diagnostic mode
  }
}

bool BambuRc522::probe_() {
  std::vector<uint8_t> uid;
  uint8_t sak;
  if (!this->detect_(uid, sak))
    return false;
  this->end_session_();
  return true;
}

void BambuRc522::measure_signal_() {
  // Distinct RxGain steps: 18, 23, 33, 38, 43, 48 dB
  static const uint8_t GAINS[] = {0x00, 0x10, 0x40, 0x50, 0x60, 0x70};
  static const uint8_t N_GAINS = sizeof(GAINS);
  static const uint8_t TRIES = 6;

  if (this->reader_ == READER_PN532) {
    // No access to RF registers: reliability only, scaled to 6
    uint8_t ok = 0;
    for (uint8_t i = 0; i < TRIES; i++)
      ok += this->probe_();
    uint8_t pct = ok * 100 / TRIES;
    if (pct != this->diag_pct_)
      ESP_LOGD(TAG, "Signal (PN532): reliability %u%%", pct);
    this->diag_pct_ = pct;
    this->diag_score_ = ok * N_GAINS / TRIES;
    return;
  }

  // Short timer (~2 ms): a tag answers in < 1 ms, no need to wait 25 ms on every failure
  this->write_reg_(REG_T_RELOAD_H, 0x00);
  this->write_reg_(REG_T_RELOAD_L, 0x50);

  this->write_reg_(REG_RF_CFG, 0x70);
  uint8_t ok = 0;
  for (uint8_t i = 0; i < TRIES; i++)
    ok += this->probe_();

  // Margin: from the lowest gain upwards, stop at the first step that answers 2 out of 2
  uint8_t score = 0;
  if (ok > 0) {
    for (uint8_t g = 0; g < N_GAINS; g++) {
      this->write_reg_(REG_RF_CFG, GAINS[g]);
      if (this->probe_() && this->probe_()) {
        score = N_GAINS - g;
        break;
      }
    }
  }

  this->write_reg_(REG_RF_CFG, 0x70);
  this->write_reg_(REG_T_RELOAD_H, 0x03);
  this->write_reg_(REG_T_RELOAD_L, 0xE8);

  uint8_t pct = ok * 100 / TRIES;
  if (score != this->diag_score_ || pct != this->diag_pct_)
    ESP_LOGD(TAG, "Signal: margin %u/%u, reliability %u%%", score, N_GAINS, pct);
  this->diag_score_ = score;
  this->diag_pct_ = pct;

  this->stat_polls_ = this->stat_wupa_ok_ = this->stat_wupa_bad_ = 0;
  this->stat_anticoll_fail_ = this->stat_select_fail_ = this->stat_select_ok_ = 0;
}

// Range diagnostics: summary every 2 s, only if a tag answered (even partially)
void BambuRc522::log_stats_() {
  uint32_t now = millis();
  if (now - this->stat_since_ < 2000)
    return;
  if (this->stat_wupa_ok_ || this->stat_wupa_bad_ || this->stat_anticoll_fail_ || this->stat_select_fail_) {
    ESP_LOGD(TAG, "Diag %us: polls=%u wakeup_ok=%u wakeup_partial=%u anticoll_fail=%u select_fail=%u select_ok=%u err=0x%02X",
             (unsigned) ((now - this->stat_since_) / 1000), this->stat_polls_, this->stat_wupa_ok_,
             this->stat_wupa_bad_, this->stat_anticoll_fail_, this->stat_select_fail_, this->stat_select_ok_,
             this->last_err_);
  }
  this->stat_since_ = now;
  this->stat_polls_ = this->stat_wupa_ok_ = this->stat_wupa_bad_ = 0;
  this->stat_anticoll_fail_ = this->stat_select_fail_ = this->stat_select_ok_ = 0;
  this->last_err_ = 0;
}

// =====================================================================
// HKDF + Bambu read
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

BambuRc522::ReadResult BambuRc522::read_bambu_data_(const std::vector<uint8_t> &uid, uint8_t sak) {
  if (uid.size() != 4 || (sak != 0x08 && sak != 0x18)) {
    ESP_LOGW(TAG, "Not a MIFARE Classic tag (SAK 0x%02X, %u-byte UID): not a Bambu tag", sak, (unsigned) uid.size());
    return READ_NOT_BAMBU;
  }

  uint8_t keys[96];
  if (!hkdf_sha256(BAMBU_MASTER_KEY, sizeof(BAMBU_MASTER_KEY), uid.data(), uid.size(), BAMBU_HKDF_INFO,
                   sizeof(BAMBU_HKDF_INFO), keys, sizeof(keys))) {
    ESP_LOGE(TAG, "HKDF derivation failed");
    return READ_FAILED;
  }

  std::vector<uint8_t> b1, b2, b4, b5, b6, b8, b9, b10, b12, b14;

  if (!this->auth_(3, &keys[0], uid)) {
    ESP_LOGW(TAG, "Sector 0 auth refused: not a Bambu tag?");
    return READ_NOT_BAMBU;
  }
  if (!this->read16_(1, b1) || !this->read16_(2, b2))
    return READ_FAILED;

  if (!this->auth_(7, &keys[6], uid) || !this->read16_(4, b4) || !this->read16_(5, b5) ||
      !this->read16_(6, b6))
    return READ_FAILED;

  if (!this->auth_(11, &keys[12], uid) || !this->read16_(8, b8) || !this->read16_(9, b9) ||
      !this->read16_(10, b10))
    return READ_FAILED;

  if (!this->auth_(15, &keys[18], uid) || !this->read16_(12, b12) || !this->read16_(14, b14))
    return READ_FAILED;

  this->publish_bambu_data_(uid, b1, b2, b4, b5, b6, b8, b9, b10, b12, b14);
  return READ_OK;
}

// =====================================================================
// Publishing (taken from bambu_nfc)
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

  // Block 1: variant (8) + material ID (8)
  pub(this->variant_id_sensor_, trim_string(b1, 0, 8));
  pub(this->tray_info_idx_sensor_, trim_string(b1, 8, 16));

  // Blocks 2 and 4: base type / detailed type
  std::string base_type = trim_string(b2);
  std::string detailed = trim_string(b4);
  pub(this->filament_type_sensor_, base_type);
  std::string subtype;
  if (detailed.size() > base_type.size() + 1 && detailed.compare(0, base_type.size(), base_type) == 0 &&
      detailed[base_type.size()] == ' ')
    subtype = detailed.substr(base_type.size() + 1);
  pub(this->filament_subtype_sensor_, subtype);
  pub(this->material_density_sensor_, find_bambu_density(detailed.c_str()));

  // Block 5: RGBA + weight + diameter
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

  // Block 6: drying + bed + nozzle
  pub(this->drying_temp_sensor_, read_u16_le(b6, 0));
  pub(this->drying_time_sensor_, read_u16_le(b6, 2));
  pub(this->bed_temp_sensor_, read_u16_le(b6, 6));
  pub(this->max_temp_sensor_, read_u16_le(b6, 8));
  pub(this->min_temp_sensor_, read_u16_le(b6, 10));

  pub(this->nozzle_diameter_sensor_, read_float_le(b8, 12));   // Block 8
  pub(this->tray_uid_sensor_, format_hex(b9));                 // Block 9
  pub(this->spool_width_sensor_, read_u16_le(b10, 4) / 100.0f);  // Block 10

  // Block 12: "YYYY_MM_DD_HH_MM" -> "YYYY-MM-DD"
  std::string d = trim_string(b12);
  if (d.size() >= 10 && d[4] == '_' && d[7] == '_')
    d = d.substr(0, 4) + "-" + d.substr(5, 2) + "-" + d.substr(8, 2);
  pub(this->production_date_sensor_, d);

  pub(this->filament_length_sensor_, read_u16_le(b14, 4));  // Block 14

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
  this->clear_bambu_sensors_();
  this->current_uid_.clear();
}

void BambuRc522::clear_bambu_sensors_() {
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
}

}  // namespace bambu_rc522
}  // namespace esphome
