#include "mqtt.h"

#include <WiFi.h>

namespace {

constexpr uint32_t TIMEOUT = 8000;

// Reads at most a packet's payload from the TLS stream, for ArduinoJson.
class PayloadStream : public Stream {
public:
    PayloadStream(Client &client, size_t length) : client_(client), left_(length) { setTimeout(3000); }
    int available() override { return left_ ? client_.available() : 0; }
    int read() override {
        if (!left_) return -1;
        int c = client_.read();
        if (c >= 0) left_--;
        return c;
    }
    int peek() override { return left_ ? client_.peek() : -1; }
    size_t write(uint8_t) override { return 0; }
    size_t left() const { return left_; }

private:
    Client &client_;
    size_t left_;
};

}  // namespace

bool Mqtt::open(const String &host, uint16_t port, const String &user, const String &password) {
    refused = 0;
    if (WiFi.status() != WL_CONNECTED) return fail("Wi-Fi is not connected");
    tls_.setInsecure();
    tls_.setHandshakeTimeout(10);
    if (!tls_.connect(host.c_str(), port, TIMEOUT)) return fail("no TLS connection to " + host + ":" + String(port));
    std::vector<uint8_t> body;
    addString(body, "MQTT");
    body.push_back(4);          // MQTT 3.1.1
    body.push_back(0xC2);       // user name, password, clean session
    body.push_back(0);
    body.push_back(60);         // keep alive, s
    addString(body, "lanetag-" + String((uint32_t)esp_random(), HEX));
    addString(body, user);
    addString(body, password);
    if (!send(0x10, body)) return false;
    uint8_t type;
    size_t length;
    uint8_t ack[2];
    if (!readHeader(type, length, millis() + TIMEOUT) || (type >> 4) != 2 || length != 2 || !readBytes(ack, 2))
        return fail("the printer did not answer the MQTT login");
    refused = ack[1];
    if (refused) return fail("the printer refused the MQTT login (" + String(refused) + ")");
    return true;
}

bool Mqtt::subscribe(const String &topic) {
    uint16_t id = packetId_++;
    std::vector<uint8_t> body{(uint8_t)(id >> 8), (uint8_t)(id & 0xFF)};
    addString(body, topic);
    body.push_back(0);          // QoS 0
    if (!send(0x82, body)) return false;
    // Messages may come before the SUBACK; they are skipped.
    uint32_t deadline = millis() + TIMEOUT;
    uint8_t type;
    size_t length;
    while (readHeader(type, length, deadline)) {
        if ((type >> 4) != 9) {
            if (!skip(length)) break;
            continue;
        }
        uint8_t sub[3];
        if (length != 3 || !readBytes(sub, 3)) break;
        if (sub[2] & 0x80) return fail("the printer refused the subscription to " + topic);
        return true;
    }
    return fail("no reply to the subscription to " + topic);
}

bool Mqtt::publish(const String &topic, const JsonDocument &doc) {
    String payload;
    serializeJson(doc, payload);
    std::vector<uint8_t> body;
    addString(body, topic);
    body.insert(body.end(), payload.c_str(), payload.c_str() + payload.length());
    return send(0x30, body);
}

bool Mqtt::receive(JsonDocument &doc, const JsonDocument &filter, uint32_t deadline, String *topic) {
    uint8_t type;
    size_t length;
    while (readHeader(type, length, deadline)) {
        if ((type >> 4) != 3) {
            if (!skip(length)) return false;
            continue;
        }
        uint8_t len[2];
        if (length < 2 || !readBytes(len, 2)) return false;
        size_t topicLength = (len[0] << 8) | len[1];
        size_t header = 2 + topicLength + (((type >> 1) & 3) ? 2 : 0);
        if (length < header) return false;
        String name;
        uint32_t limit = millis() + TIMEOUT;
        for (size_t i = 0; i < topicLength; i++) {
            uint8_t b;
            if (!readByte(b, limit)) return false;
            if (topic) name += (char)b;
        }
        if (header > 2 + topicLength && !skip(2)) return false;     // packet id (QoS > 0)
        PayloadStream payload(tls_, length - header);
        DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
        if (!skip(payload.left())) return false;
        if (!err) {
            if (topic) *topic = name;
            return true;
        }
    }
    return false;
}

void Mqtt::close() {
    if (tls_.connected()) {
        uint8_t disconnect[2] = {0xE0, 0};
        tls_.write(disconnect, 2);
    }
    tls_.stop();
}

bool Mqtt::fail(const String &message) {
    error = message;
    return false;
}

void Mqtt::addString(std::vector<uint8_t> &out, const String &text) {
    out.push_back(text.length() >> 8);
    out.push_back(text.length() & 0xFF);
    out.insert(out.end(), text.c_str(), text.c_str() + text.length());
}

bool Mqtt::send(uint8_t type, const std::vector<uint8_t> &body) {
    std::vector<uint8_t> packet{type};
    size_t length = body.size();
    do {
        uint8_t b = length & 0x7F;
        length >>= 7;
        packet.push_back(length ? b | 0x80 : b);
    } while (length);
    packet.insert(packet.end(), body.begin(), body.end());
    if (tls_.write(packet.data(), packet.size()) != packet.size()) return fail("lost the printer's connection");
    return true;
}

bool Mqtt::readByte(uint8_t &b, uint32_t deadline) {
    while (!tls_.available()) {
        if (!tls_.connected() || (int32_t)(millis() - deadline) >= 0) return false;
        delay(5);
    }
    b = tls_.read();
    return true;
}

bool Mqtt::readBytes(uint8_t *out, size_t length) {
    uint32_t deadline = millis() + TIMEOUT;
    for (size_t n = 0; n < length; n++)
        if (!readByte(out[n], deadline)) return false;
    return true;
}

bool Mqtt::skip(size_t length) {
    uint32_t deadline = millis() + TIMEOUT;
    uint8_t b;
    while (length--)
        if (!readByte(b, deadline)) return false;
    return true;
}

bool Mqtt::readHeader(uint8_t &type, size_t &length, uint32_t deadline) {
    if (!readByte(type, deadline)) return false;
    length = 0;
    for (int shift = 0; shift < 28; shift += 7) {
        uint8_t b;
        if (!readByte(b, millis() + TIMEOUT)) return false;
        length |= (size_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
    }
    return false;
}
