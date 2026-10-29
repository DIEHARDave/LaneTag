// Printer discovery (see discovery.h). Runs as its own task, so the web page
// and the reader keep working while it sweeps the network.
#include "discovery.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <fcntl.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>
#include <vector>

#include "config.h"

namespace {

// Flashforge's discovery (as OrcaSlicer and FlashForge's own software send
// it): the printer answers to the sender's port with its name, product ID
// and serial number. Firmware with the native screen off (Zmod) does not.
constexpr uint16_t FF_DISCOVERY_PORT = 48899;
constexpr uint16_t FF_MULTICAST_PORT = 19000;
constexpr uint16_t FF_LISTEN_PORT = 18007;
const uint8_t FF_REQUEST[20] = {0x77, 0x77, 0x77, 0x2e, 0x75, 0x73, 0x72, 0x22, 0x65, 0x36, 0xc0};
// Bambu printers announce themselves every few seconds (SSDP NOTIFY) to
// port 2021 on the LAN.
constexpr uint16_t BAMBU_SSDP_PORT = 2021;

constexpr int PARALLEL = 8;
// Printers on Wi-Fi sometimes miss the first try; the hosts that did not
// answer get a second, slower one.
constexpr uint32_t CONNECT_TIMEOUT = 400;
constexpr uint32_t RETRY_TIMEOUT = 700;
constexpr uint32_t HTTP_TIMEOUT = 3000;
// UDP answers are collected at least this long after the requests.
constexpr uint32_t LISTEN_MIN = 8000;

enum class Seen : uint8_t { Absent, Closed, Open };

struct Found {
    String host;
    String name;
    Model model = Model::Klipper;
    Method method = Method::LaneData;
    String serial;
    int unit = 1;
    bool supported = true;
    String note;
    int rank = 0;  // which find wins for one address: 1 Flashforge UDP, 2
                   // Moonraker, 3 the vendor's own port
};

SemaphoreHandle_t lock = nullptr;
std::vector<Found> found;
volatile bool running = false;
volatile int progress = 0;
String subnet, failure;

void add(Found f) {
    xSemaphoreTake(lock, portMAX_DELAY);
    for (Found &old : found) {
        // A Spoolman server is listed beside the printer on the same host.
        if (old.host != f.host || (old.model == Model::Spoolman) != (f.model == Model::Spoolman)) continue;
        if (f.rank > old.rank) {
            if (!f.serial.length()) f.serial = old.serial;
            old = f;
        }
        xSemaphoreGive(lock);
        return;
    }
    found.push_back(f);
    xSemaphoreGive(lock);
}

String ipText(uint32_t ip) { return IPAddress(ip).toString(); }

// --- UDP: Flashforge and Bambu ---------------------------------------------------

int udpSocket(uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

void sendTo(int fd, const char *ip, uint16_t port, const void *data, size_t len) {
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    sendto(fd, data, len, 0, (sockaddr *)&addr, sizeof addr);
}

String fixedText(const uint8_t *data, size_t len) {
    String text;
    for (size_t i = 0; i < len && data[i]; i++) text += (char)data[i];
    text.trim();
    return text;
}

void flashforgeReply(const uint8_t *data, size_t len, const String &host) {
    if (len < 0x92) return;
    Found f;
    f.host = host;
    f.rank = 1;
    f.name = fixedText(data, min(len, (size_t)128));
    f.serial = len > 0x92 ? fixedText(data + 0x92, min(len - 0x92, (size_t)128)) : "";
    uint16_t product = data[0x88] << 8 | data[0x89];
    f.model = Model::AD5X;
    f.method = Method::Stock;
    switch (product) {
    case 0x26: break;
    case 0x28:
    case 0x29: f.model = Model::Creator5; break;
    case 0x23:
    case 0x24:
        f.model = Model::Adventurer5M;
        f.method = Method::LaneData;
        f.supported = false;
        f.note = "stock AD5M firmware has no filament slots to set; it needs Zmod or Forge-X";
        break;
    default:
        f.model = Model::Klipper;
        f.supported = false;
        f.note = "this Flashforge printer (product 0x" + String(product, HEX) + ") is not supported";
        break;
    }
    if (f.supported) f.note = "stock firmware: enter the printer's check code";
    if (!f.name.length()) f.name = "Flashforge";
    add(f);
}

// "Key: value" from an SSDP message.
String header(const String &msg, const char *key) {
    String lower = msg;
    lower.toLowerCase();
    String k = String(key) + ":";
    k.toLowerCase();
    int at = lower.indexOf(k);
    if (at < 0) return "";
    int end = msg.indexOf('\n', at);
    String value = msg.substring(at + k.length(), end < 0 ? msg.length() : end);
    value.trim();
    return value;
}

void bambuReply(const uint8_t *data, size_t len, const String &from) {
    String msg((const char *)data, len);
    if (msg.indexOf("bambulab") < 0) return;
    Found f;
    f.host = header(msg, "Location");
    if (!f.host.length()) f.host = from;
    f.rank = 3;
    f.model = Model::Bambu;
    f.method = Method::Bambu;
    f.serial = header(msg, "USN");
    f.name = header(msg, "DevName.bambu.com");
    if (!f.name.length()) f.name = "Bambu " + header(msg, "DevModel.bambu.com");
    f.note = header(msg, "DevConnect.bambu.com") == "cloud"
                 ? "the printer is in cloud mode: turn on LAN Only mode, then enter its access code"
                 : "enter the printer's LAN access code";
    add(f);
}

struct Listeners {
    int flashforge = -1, bambu = -1;

    void open(uint32_t broadcast) {
        flashforge = udpSocket(FF_LISTEN_PORT);
        bambu = udpSocket(BAMBU_SSDP_PORT);
        if (bambu >= 0) {
            ip_mreq group = {};
            group.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
            group.imr_interface.s_addr = htonl(INADDR_ANY);
            setsockopt(bambu, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof group);
        }
        request(broadcast);
    }
    void request(uint32_t broadcast) {
        if (flashforge < 0) return;
        String direct = ipText(broadcast);
        for (const char *ip : {"255.255.255.255", direct.c_str()})
            sendTo(flashforge, ip, FF_DISCOVERY_PORT, FF_REQUEST, sizeof FF_REQUEST);
        sendTo(flashforge, "225.0.0.9", FF_MULTICAST_PORT, FF_REQUEST, sizeof FF_REQUEST);
    }
    void poll() {
        uint8_t buf[600];
        for (int fd : {flashforge, bambu}) {
            if (fd < 0) continue;
            for (;;) {
                sockaddr_in from = {};
                socklen_t fromLen = sizeof from;
                int n = recvfrom(fd, buf, sizeof buf - 1, 0, (sockaddr *)&from, &fromLen);
                if (n <= 0) break;
                String host = ipText(from.sin_addr.s_addr);
                if (fd == flashforge) flashforgeReply(buf, n, host);
                else bambuReply(buf, n, host);
            }
        }
    }
    void close() {
        if (flashforge >= 0) ::close(flashforge);
        if (bambu >= 0) ::close(bambu);
        flashforge = bambu = -1;
    }
};

// --- TCP sweep ---------------------------------------------------------------

void closeNow(int fd) {
    linger off = {1, 0};  // no TIME_WAIT: lwIP has few TCP slots
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &off, sizeof off);
    close(fd);
}

// lwIP reports a refused connection as a reset.
Seen afterError(int err) { return err == ECONNREFUSED || err == ECONNRESET ? Seen::Closed : Seen::Absent; }

// Tries port on every host, PARALLEL at a time, collecting UDP answers in
// between. Absent: no answer (no such host, or a firewall); Closed: the
// host refused.
void sweep(const std::vector<uint32_t> &hosts, uint16_t port, std::vector<Seen> &seen, uint32_t timeout,
           int progressFrom, int progressTo, Listeners &udp) {
    struct Pending {
        int fd;
        size_t index;
        uint32_t until;
    };
    std::vector<Pending> pending;
    seen.assign(hosts.size(), Seen::Absent);
    size_t next = 0;
    while (next < hosts.size() || !pending.empty()) {
        while ((int)pending.size() < PARALLEL && next < hosts.size()) {
            int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (fd < 0) break;  // out of sockets: wait for one to finish
            fcntl(fd, F_SETFL, O_NONBLOCK);
            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = hosts[next];
            if (connect(fd, (sockaddr *)&addr, sizeof addr) == 0) {
                seen[next++] = Seen::Open;
                closeNow(fd);
            } else if (errno == EINPROGRESS) {
                pending.push_back({fd, next++, millis() + timeout});
            } else {
                seen[next++] = afterError(errno);
                closeNow(fd);
            }
        }
        if (pending.empty()) {
            if (next < hosts.size()) delay(50);
            continue;
        }
        fd_set writable;
        FD_ZERO(&writable);
        int top = 0;
        for (const Pending &p : pending) {
            FD_SET(p.fd, &writable);
            top = max(top, p.fd);
        }
        timeval wait = {0, 50000};
        select(top + 1, nullptr, &writable, nullptr, &wait);
        uint32_t now = millis();
        for (size_t i = 0; i < pending.size();) {
            Pending &p = pending[i];
            bool done = true;
            if (FD_ISSET(p.fd, &writable)) {
                int err = 0;
                socklen_t len = sizeof err;
                getsockopt(p.fd, SOL_SOCKET, SO_ERROR, &err, &len);
                seen[p.index] = err == 0 ? Seen::Open : afterError(err);
            } else if ((int32_t)(now - p.until) < 0) {
                done = false;
            }
            if (done) {
                closeNow(p.fd);
                pending.erase(pending.begin() + i);
            } else {
                i++;
            }
        }
        progress = progressFrom + (progressTo - progressFrom) * (int)(next - pending.size()) / (int)hosts.size();
        udp.poll();
    }
}

// --- Identifying what answered -------------------------------------------------

bool getJson(const String &url, JsonDocument &doc, JsonDocument *filter = nullptr) {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT);
    http.setTimeout(HTTP_TIMEOUT);
    if (!http.begin(client, url)) return false;
    bool ok = http.GET() == 200 &&
              !(filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                       : deserializeJson(doc, http.getStream()));
    http.end();
    return ok;
}

// The name the user gave the printer in Mainsail or Fluidd, if any.
String moonrakerName(const String &base) {
    JsonDocument doc;
    if (getJson(base + "/server/database/item?namespace=mainsail&key=general.printername", doc) &&
        doc["result"]["value"].is<const char *>())
        return doc["result"]["value"].as<String>();
    doc.clear();
    if (getJson(base + "/server/database/item?namespace=fluidd&key=uiSettings.general.instanceName", doc) &&
        doc["result"]["value"].is<const char *>())
        return doc["result"]["value"].as<String>();
    return "";
}

void identifyMoonraker(const String &host) {
    String base = "http://" + host + ":" + String(MOONRAKER_PORT);
    JsonDocument objects, info;
    if (!getJson(base + "/printer/objects/list", objects) || !getJson(base + "/server/info", info)) {
        Found f;
        f.host = host;
        f.rank = 2;
        f.name = "Moonraker";
        f.supported = false;
        f.note = "Moonraker answers but would not list Klipper's objects (Klipper not ready, or the "
                 "reader is not a trusted client)";
        add(f);
        return;
    }
    bool zmod = false, ffNfc = false, kiln = false, ifs = false, u1 = false, mmu = false, afc = false,
         qidi = false;
    int extruders = 0;
    for (JsonVariantConst o : objects["result"]["objects"].as<JsonArrayConst>()) {
        String name = o.as<String>();
        // FF_NFC was zmod_nfc on Zmod before.
        if (name == "ff_nfc" || name == "zmod_nfc") ffNfc = true;
        if (name == "zmod" || name == "zmod_color" || name == "zmod_ifs" || name == "zmod_nfc") zmod = true;
        if (name == "ifs_materials") kiln = true;
        if (name == "zmod_ifs") ifs = true;
        if (name == "print_task_config") u1 = true;
        if (name == "mmu") mmu = true;
        if (name == "AFC") afc = true;
        if (name.startsWith("box_stepper slot")) qidi = true;
        if (name == "extruder" || (name.startsWith("extruder") && isDigit(name[8]))) extruders++;
    }
    bool spoolLink = false;
    for (JsonVariantConst c : info["result"]["components"].as<JsonArrayConst>())
        if (c.as<String>() == "spoollink") spoolLink = true;

    Found f;
    f.host = host;
    f.rank = 2;
    String label;
    if (zmod) {
        // zmod_afclite adds AFC objects to Zmod printers; Zmod comes first.
        f.model = extruders >= 4 ? Model::Creator5 : ifs ? Model::AD5X : Model::Adventurer5M;
        f.method = f.model == Model::Adventurer5M ? Method::LaneData
                   : ffNfc                         ? Method::FfNfc
                                                   : Method::ZmodBasic;
        label = f.model == Model::Creator5 ? "Creator 5" : f.model == Model::AD5X ? "AD5X" : "AD5M";
        if (!ffNfc && f.model != Model::Adventurer5M)
            f.note = "Zmod without the FF_NFC plugin: color and material only";
    } else if (kiln && ffNfc) {
        // Kiln OS on an AD5X (its IFS is ifs/ifs_materials) with FF_NFC.
        f.model = Model::AD5X;
        f.method = Method::FfNfc;
        label = "AD5X";
    } else if (u1) {
        f.model = Model::SnapmakerU1;
        f.method = spoolLink ? Method::Snapmaker : Method::SnapmakerStock;
        label = "U1";
    } else if (qidi) {
        f.model = Model::Qidi;
        f.method = Method::QidiBox;
        label = "Qidi";
    } else {
        f.model = Model::Klipper;
        f.method = mmu ? Method::HappyHare : afc ? Method::Afc : Method::LaneData;
        label = "Klipper";
    }
    f.name = moonrakerName(base);
    if (!f.name.length()) {
        JsonDocument printer;
        String hostname;
        if (getJson(base + "/printer/info", printer)) hostname = printer["result"]["hostname"] | "";
        // Firmware that names every printer the same is no help.
        f.name = f.model == Model::Klipper && hostname.length() ? hostname : label;
    }
    add(f);
}

// Creality's WebSocket answers an upgrade and then pushes status JSON, which
// names the model.
void identifyCreality(const String &host) {
    WiFiClient tcp;
    if (!tcp.connect(host.c_str(), CREALITY_PORT, HTTP_TIMEOUT)) return;
    tcp.print("GET / HTTP/1.1\r\nHost: " + host + ":9999\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    String seen;
    uint32_t until = millis() + 2500;
    while (millis() < until && seen.length() < 4096) {
        while (tcp.available() && seen.length() < 4096) {
            char c = tcp.read();
            if (c) seen += c;
        }
        if (seen.indexOf("\"model\"") >= 0 && seen.indexOf('"', seen.indexOf("\"model\"") + 9) > 0) break;
        delay(20);
    }
    tcp.stop();
    if (!seen.startsWith("HTTP/1.1 101")) return;
    Found f;
    f.host = host;
    f.rank = 3;
    f.model = Model::Creality;
    f.method = Method::CrealityCfs;
    f.name = "Creality";
    int at = seen.indexOf("\"model\"");
    if (at >= 0) {
        int start = seen.indexOf('"', seen.indexOf(':', at) + 1) + 1, end = seen.indexOf('"', start);
        if (start > 0 && end > start) f.name = seen.substring(start, end);
    }
    add(f);
}

void identifyAnycubic(const String &host) {
    JsonDocument info;
    if (!getJson("http://" + host + ":" + String(ANYCUBIC_PORT) + "/info", info) ||
        !(info["modelId"].is<const char *>() || info["token"].is<const char *>()))
        return;
    Found f;
    f.host = host;
    f.rank = 3;
    f.model = Model::Anycubic;
    f.method = Method::AnycubicAce;
    f.name = info["printerName"] | (info["modelName"] | "Anycubic");
    if (info["ctrlType"] == "cloud") f.note = "the printer is in cloud mode: turn on LAN mode on its screen";
    add(f);
}

void identifySpoolman(const String &host) {
    JsonDocument info;
    if (!getJson("http://" + host + ":" + String(SPOOLMAN_PORT) + "/api/v1/info", info) ||
        !info["version"].is<const char *>())
        return;
    Found f;
    f.host = host;
    f.rank = 3;
    f.model = Model::Spoolman;
    f.method = Method::Spoolman;
    f.name = "Spoolman";
    f.note = "Spoolman " + String(info["version"].as<const char *>()) + ": scans are looked up there, no printer";
    add(f);
}

// --- The search --------------------------------------------------------------

void search(void *) {
    uint32_t self = (uint32_t)WiFi.localIP(), mask = (uint32_t)WiFi.subnetMask();
    // Larger networks: only the reader's own /24 (a /16 would take an hour).
    if ((ntohl(mask) & 0xffffff00) != 0xffffff00) mask = htonl(0xffffff00);
    uint32_t net = self & mask, broadcast = net | ~mask;
    subnet = ipText(net) + "/" + String(__builtin_popcount(mask));
    std::vector<uint32_t> hosts;
    for (uint32_t h = ntohl(net) + 1; h < ntohl(broadcast); h++)
        if (htonl(h) != self) hosts.push_back(htonl(h));

    Listeners udp;
    udp.open(broadcast);
    uint32_t started = millis();

    // Moonraker printers found by the first, quick pass are listed (and can
    // be added) while the slower second pass runs.
    std::vector<Seen> moonraker;
    sweep(hosts, MOONRAKER_PORT, moonraker, CONNECT_TIMEOUT, 0, 30, udp);
    for (size_t i = 0; i < hosts.size(); i++)
        if (moonraker[i] == Seen::Open) {
            identifyMoonraker(ipText(hosts[i]));
            udp.poll();
        }
    std::vector<uint32_t> silent;
    std::vector<size_t> silentAt;
    for (size_t i = 0; i < hosts.size(); i++)
        if (moonraker[i] == Seen::Absent) {
            silent.push_back(hosts[i]);
            silentAt.push_back(i);
        }
    std::vector<Seen> retry;
    sweep(silent, MOONRAKER_PORT, retry, RETRY_TIMEOUT, 30, 75, udp);
    udp.request(broadcast);  // once more, for printers that missed the first
    for (size_t i = 0; i < silent.size(); i++) {
        moonraker[silentAt[i]] = retry[i];
        if (retry[i] == Seen::Open) {
            identifyMoonraker(ipText(silent[i]));
            udp.poll();
        }
    }

    // Hosts that answered at all may also run the vendors' own services.
    std::vector<uint32_t> alive;
    for (size_t i = 0; i < hosts.size(); i++)
        if (moonraker[i] != Seen::Absent) alive.push_back(hosts[i]);
    std::vector<Seen> creality, anycubic, spoolman;
    sweep(alive, CREALITY_PORT, creality, RETRY_TIMEOUT, 75, 82, udp);
    sweep(alive, ANYCUBIC_PORT, anycubic, RETRY_TIMEOUT, 82, 89, udp);
    sweep(alive, SPOOLMAN_PORT, spoolman, RETRY_TIMEOUT, 89, 95, udp);
    for (size_t i = 0; i < alive.size(); i++) {
        if (creality[i] == Seen::Open) identifyCreality(ipText(alive[i]));
        if (anycubic[i] == Seen::Open) identifyAnycubic(ipText(alive[i]));
        if (spoolman[i] == Seen::Open) identifySpoolman(ipText(alive[i]));
        udp.poll();
    }
    while (millis() - started < LISTEN_MIN) {
        udp.poll();
        delay(100);
    }
    udp.poll();
    udp.close();
    progress = 100;
    running = false;
    vTaskDelete(nullptr);
}

}  // namespace

String discoveryStart() {
    if (running) return "";
    if (WiFi.status() != WL_CONNECTED || WiFi.getMode() == WIFI_AP)
        return "the reader is not on a Wi-Fi network yet";
    if (!lock) lock = xSemaphoreCreateMutex();
    xSemaphoreTake(lock, portMAX_DELAY);
    found.clear();
    xSemaphoreGive(lock);
    failure = "";
    progress = 0;
    running = true;
    if (xTaskCreate(search, "discovery", 8192, nullptr, 1, nullptr) != pdPASS) {
        running = false;
        failure = "out of memory";
        return failure;
    }
    return "";
}

void discoveryToJson(JsonObject out) {
    out["running"] = (bool)running;
    out["progress"] = (int)progress;
    out["subnet"] = subnet;
    if (failure.length()) out["error"] = failure;
    JsonArray list = out["printers"].to<JsonArray>();
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (const Found &f : found) {
        JsonObject o = list.add<JsonObject>();
        o["host"] = f.host;
        o["name"] = f.name;
        o["model"] = modelName(f.model);
        o["method"] = methodName(f.method);
        o["serial"] = f.serial;
        o["unit"] = f.unit;
        o["supported"] = f.supported;
        if (f.note.length()) o["note"] = f.note;
    }
    xSemaphoreGive(lock);
}
