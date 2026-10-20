#include "pn532.h"

namespace {

constexpr uint8_t I2C_ADDRESS = 0x24;
constexpr size_t I2C_MAX_READ = 128;     // the Arduino Wire buffer
constexpr uint8_t I2C_BUS = 0;           // the bus behind Wire

constexpr uint8_t CMD_GET_FIRMWARE_VERSION = 0x02;
constexpr uint8_t CMD_SAM_CONFIGURATION = 0x14;
constexpr uint8_t CMD_RF_CONFIGURATION = 0x32;
constexpr uint8_t CMD_IN_DATA_EXCHANGE = 0x40;
constexpr uint8_t CMD_IN_LIST_PASSIVE_TARGET = 0x4A;
constexpr uint8_t CMD_IN_RELEASE = 0x52;

// After power-up an HSU PN532 is in LowVbat mode and only answers
// SAMConfiguration sent right behind a 0x55 wakeup run (as libnfc does).
const uint8_t WAKEUP[] = {0x55, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

}  // namespace

void Pn532::attachI2C(TwoWire &wire, int sda, int scl) {
    bus_ = Pn532Bus::I2C;
    wire_ = &wire;
    wire_->begin(sda, scl, 100000);
    wire_->setTimeOut(50);
}

void Pn532::attachUart(HardwareSerial &serial, int rx, int tx, uint32_t baud) {
    bus_ = Pn532Bus::UART;
    serial_ = &serial;
    serial_->setRxBufferSize(512);
    serial_->begin(baud, SERIAL_8N1, rx, tx);
}

void Pn532::end() {
    ok_ = false;
    if (bus_ == Pn532Bus::UART && serial_) serial_->end();
}

bool Pn532::init() {
    ok_ = false;
    failures_ = 0;
    uint8_t resp[8];
    const uint8_t sam[] = {0x01, 0x14, 0x01};       // normal mode, 1 s timeout, use IRQ
    for (int attempt = 0; attempt < 3; attempt++) {
        if (command(CMD_SAM_CONFIGURATION, sam, sizeof(sam), resp, sizeof(resp), 500,
                    bus_ == Pn532Bus::UART) >= 0)
            break;
        delay(50);
    }
    int n = command(CMD_GET_FIRMWARE_VERSION, nullptr, 0, resp, sizeof(resp), 500);
    if (n < 4 || resp[0] != 0x32) return false;
    char text[16];
    snprintf(text, sizeof(text), "%d.%d", resp[1], resp[2]);
    firmware_ = text;
    // Few passive activation retries, so a poll without a card returns in
    // tens of milliseconds instead of blocking.
    const uint8_t retries[] = {0x05, 0xFF, 0x01, 0x02};
    command(CMD_RF_CONFIGURATION, retries, sizeof(retries), resp, sizeof(resp), 500);
    ok_ = true;
    return true;
}

bool Pn532::writeFrame(const uint8_t *data, size_t len, bool wake) {
    uint8_t frame[280];
    size_t pos = 0;
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;
    frame[pos++] = 0xFF;
    frame[pos++] = len;
    frame[pos++] = (uint8_t)(-(int)len);
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        frame[pos++] = data[i];
        sum += data[i];
    }
    frame[pos++] = (uint8_t)(-(int)sum);
    frame[pos++] = 0x00;
    rxLen_ = 0;
    if (bus_ == Pn532Bus::UART) {
        while (serial_->available()) serial_->read();
        if (wake) serial_->write(WAKEUP, sizeof(WAKEUP));
        return serial_->write(frame, pos) == pos;
    }
    wire_->beginTransmission(I2C_ADDRESS);
    wire_->write(frame, pos);
    return wire_->endTransmission() == 0;
}

// I2C: wait until the PN532's status byte says ready, then read a frame in
// one transfer (the first byte is the status byte again). A busy PN532
// NAKs its address; that only means "not yet", so the reads go through the
// HAL, which (unlike Wire::requestFrom) does not log every NAK as an error.
bool Pn532::i2cFill(size_t len, uint32_t deadline) {
    len = min(len + 1, I2C_MAX_READ);
    uint8_t buf[I2C_MAX_READ];
    while ((int32_t)(millis() - deadline) < 0) {
        size_t got = 0;
        if (i2cRead(I2C_BUS, I2C_ADDRESS, buf, 1, 20, &got) == ESP_OK && got == 1 && (buf[0] & 0x01) &&
            i2cRead(I2C_BUS, I2C_ADDRESS, buf, len, 50, &got) == ESP_OK && got >= 2 && (buf[0] & 0x01)) {
            rxLen_ = min(got - 1, sizeof(rx_));
            memcpy(rx_, buf + 1, rxLen_);
            return true;
        }
        delay(1);
    }
    return false;
}

void Pn532::uartFill(uint32_t deadline) {
    while (!serial_->available() && (int32_t)(millis() - deadline) < 0) delay(1);
    while (serial_->available() && rxLen_ < sizeof(rx_)) rx_[rxLen_++] = serial_->read();
}

// Find one frame in rx_ and consume it.
int Pn532::parse(uint8_t *body, size_t max) {
    for (size_t start = 0; start + 5 <= rxLen_; start++) {
        if (rx_[start] != 0x00 || rx_[start + 1] != 0x00 || rx_[start + 2] != 0xFF) continue;
        uint8_t length = rx_[start + 3], lcs = rx_[start + 4];
        if (length == 0 && lcs == 0xFF) {           // ACK
            size_t end = start + 6;
            memmove(rx_, rx_ + min(end, rxLen_), rxLen_ - min(end, rxLen_));
            rxLen_ -= min(end, rxLen_);
            return 0;
        }
        if ((uint8_t)(length + lcs) != 0) continue;  // not a frame start
        size_t end = start + 5 + length + 2;
        if (end > rxLen_) return -1;                 // incomplete; read more
        uint8_t sum = 0;
        for (size_t i = 0; i <= length; i++) sum += rx_[start + 5 + i];
        int result = -2;
        if (sum == 0 && length <= max && !(length == 1 && rx_[start + 5] == 0x7F)) {
            memcpy(body, rx_ + start + 5, length);
            result = length;
        }
        memmove(rx_, rx_ + end, rxLen_ - end);
        rxLen_ -= end;
        return result;
    }
    return -1;
}

int Pn532::readFrame(uint8_t *body, size_t max, uint32_t timeoutMs, size_t i2cLen) {
    uint32_t deadline = millis() + timeoutMs;
    if (bus_ == Pn532Bus::I2C) {
        // Every I2C transfer holds at most one frame; leftovers are padding.
        if (!i2cFill(i2cLen, deadline)) return -1;
        int result = parse(body, max);
        rxLen_ = 0;
        return result;
    }
    while (true) {
        int result = parse(body, max);
        if (result != -1) return result;
        if ((int32_t)(millis() - deadline) >= 0) return -1;
        uartFill(deadline);
    }
}

int Pn532::command(uint8_t cmd, const uint8_t *params, size_t len, uint8_t *out, size_t expect,
                   uint32_t timeoutMs, bool wake) {
    uint8_t data[264];
    if (len > sizeof(data) - 2) return -1;
    data[0] = 0xD4;
    data[1] = cmd;
    if (len) memcpy(data + 2, params, len);
    if (!writeFrame(data, len + 2, wake)) {
        failures_++;
        return -1;
    }
    uint8_t body[264];
    if (readFrame(body, sizeof(body), min<uint32_t>(timeoutMs, 500), 6) != 0) {
        failures_++;
        return -1;
    }
    // Frame overhead: preamble, LEN, LCS, TFI, response code, DCS, postamble.
    int n = readFrame(body, sizeof(body), timeoutMs, expect + 9);
    if (n < 2 || body[0] != 0xD5 || body[1] != cmd + 1) {
        failures_++;
        return -1;
    }
    failures_ = 0;
    size_t count = min((size_t)(n - 2), expect);
    if (count) memcpy(out, body + 2, count);
    return count;
}

bool Pn532::listTarget(NfcTarget &target) {
    const uint8_t params[] = {0x01, 0x00};          // one target, 106 kbps type A
    uint8_t resp[32];
    int n = command(CMD_IN_LIST_PASSIVE_TARGET, params, sizeof(params), resp, sizeof(resp), 300);
    if (n < 6 || resp[0] == 0) {
        // No card answered; that is not a reader failure.
        if (n >= 1) failures_ = 0;
        return false;
    }
    uint8_t uidLen = resp[5];
    if (uidLen > sizeof(target.uid) || 6 + uidLen > n) return false;
    target.atqa[0] = resp[2];
    target.atqa[1] = resp[3];
    target.sak = resp[4];
    target.uidLen = uidLen;
    memcpy(target.uid, resp + 6, uidLen);
    return true;
}

int Pn532::exchange(const uint8_t *data, size_t len, uint8_t *out, size_t expect, uint32_t timeoutMs) {
    uint8_t params[260];
    if (len + 1 > sizeof(params)) return -1;
    params[0] = 0x01;                               // target 1
    memcpy(params + 1, data, len);
    uint8_t resp[264];
    int n = command(CMD_IN_DATA_EXCHANGE, params, len + 1, resp, min(expect + 1, sizeof(resp)), timeoutMs);
    // An authentication refused or a read error is the card's answer, not a
    // failing reader.
    if (n >= 1) failures_ = 0;
    if (n < 1 || (resp[0] & 0x3F)) return -1;
    memcpy(out, resp + 1, n - 1);
    return n - 1;
}

void Pn532::release() {
    const uint8_t params[] = {0x00};
    uint8_t resp[4];
    command(CMD_IN_RELEASE, params, sizeof(params), resp, sizeof(resp), 300);
}
