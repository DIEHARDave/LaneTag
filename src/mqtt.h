// A small MQTT 3.1.1 client over TLS for printers' own brokers (Bambu Lab,
// Anycubic). Status reports can be 10-30 KB, more than an MQTT library
// buffers comfortably on a C3, so they are parsed straight from the TLS
// stream through an ArduinoJson filter. QoS 0 only; one short session per
// scan.
#pragma once

#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <vector>

class Mqtt {
public:
    String error;
    // The broker's CONNACK refusal (4: bad user name or password, 5: not
    // authorized), or 0.
    int refused = 0;

    // TLS (the printer's certificate is not checked) and the login.
    bool open(const String &host, uint16_t port, const String &user, const String &password);
    bool subscribe(const String &topic);
    bool publish(const String &topic, const JsonDocument &doc);
    // The next message, through filter, and its topic. False on timeout or a
    // lost connection.
    bool receive(JsonDocument &doc, const JsonDocument &filter, uint32_t deadline, String *topic = nullptr);
    void close();
    ~Mqtt() { close(); }

private:
    WiFiClientSecure tls_;
    uint16_t packetId_ = 1;

    bool fail(const String &message);
    static void addString(std::vector<uint8_t> &out, const String &text);
    bool send(uint8_t type, const std::vector<uint8_t> &body);
    bool readByte(uint8_t &b, uint32_t deadline);
    bool readBytes(uint8_t *out, size_t length);
    bool skip(size_t length);
    bool readHeader(uint8_t &type, size_t &length, uint32_t deadline);
};
