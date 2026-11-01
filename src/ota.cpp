// Firmware updates from GitHub (see ota.h). Runs as its own task, so the web
// page can follow the download while it goes.
#include "ota.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/md.h>
#include <memory>

#include "app.h"
#include "version.h"

namespace {

const char RELEASES[] = "https://github.com/DIEHARDave/LaneTag/releases/download/";
constexpr uint32_t TIMEOUT = 15000;
constexpr size_t CHUNK = 2048;

volatile bool running = false;
volatile bool done = false;
volatile int progress = 0;
String url, sha256, failure;

bool fail(const String &why) {
    failure = why;
    log_w("update: %s", why.c_str());
    return false;
}

bool download() {
    WiFiClientSecure tls;
    // The release's SHA-256, checked below, vouches for the file; the
    // reader keeps no CA certificates.
    tls.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(TIMEOUT);
    http.setTimeout(TIMEOUT);
    // GitHub hands release files out from another host.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(tls, url)) return fail("bad address " + url);
    int code = http.GET();
    if (code != 200) {
        http.end();
        return fail(code <= 0 ? "GitHub not reachable: " + HTTPClient::errorToString(code)
                              : "GitHub answered HTTP " + String(code));
    }
    int total = http.getSize();
    if (total <= 0) {
        http.end();
        return fail("GitHub did not say how large the file is");
    }
    if (!Update.begin(total)) {
        http.end();
        return fail(String("no room for the update: ") + Update.errorString());
    }

    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[CHUNK]);
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    bool ok = buf && !mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) && !mbedtls_md_starts(&md);
    if (!ok) fail("out of memory");
    WiFiClient *stream = http.getStreamPtr();
    int got = 0;
    uint32_t lastData = millis();
    while (ok && got < total) {
        size_t avail = stream->available();
        if (!avail) {
            if (!stream->connected() || millis() - lastData > TIMEOUT) {
                ok = fail("the download stopped at " + String((int)progress) + "%");
                break;
            }
            delay(5);
            continue;
        }
        int n = stream->readBytes(buf.get(), min(avail, CHUNK));
        if (n <= 0) continue;
        lastData = millis();
        // Downloading keeps the reader awake.
        app.usedAt = lastData;
        mbedtls_md_update(&md, buf.get(), n);
        if (Update.write(buf.get(), n) != (size_t)n) {
            ok = fail(String("writing the update failed: ") + Update.errorString());
            break;
        }
        got += n;
        progress = (int)((int64_t)got * 99 / total);
    }
    http.end();

    uint8_t digest[32];
    if (ok && !mbedtls_md_finish(&md, digest) && sha256.length()) {
        char hex[65];
        for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
        if (!sha256.equalsIgnoreCase(hex)) ok = fail("the downloaded file is damaged (SHA-256 does not match)");
    }
    mbedtls_md_free(&md);
    if (!ok) {
        Update.abort();
        return false;
    }
    if (!Update.end()) return fail(String("the update was not accepted: ") + Update.errorString());
    return true;
}

void run(void *) {
    log_i("update from %s", url.c_str());
    if (download()) {
        progress = 100;
        done = true;
        running = false;
        // Time for the page to see it.
        delay(2000);
        ESP.restart();
    }
    running = false;
    vTaskDelete(nullptr);
}

}  // namespace

String otaStart(const String &fileUrl, const String &digest) {
    if (running || done) return "an update is already running";
    if (Update.isRunning()) return "a firmware upload is already running";
    if (WiFi.status() != WL_CONNECTED || WiFi.getMode() == WIFI_AP) return "the reader is not on a Wi-Fi network yet";
    if (!fileUrl.startsWith(RELEASES)) return "not a LaneTag release file";
    if (!fileUrl.endsWith("-" FIRMWARE_BOARD ".bin")) return "not the file for this board (" FIRMWARE_BOARD ")";
    if (digest.length() && digest.length() != 64) return "bad SHA-256";
    url = fileUrl;
    sha256 = digest;
    failure = "";
    progress = 0;
    running = true;
    // TLS needs a deeper stack than plain HTTP.
    if (xTaskCreate(run, "update", 10240, nullptr, 1, nullptr) != pdPASS) {
        running = false;
        failure = "out of memory";
        return failure;
    }
    return "";
}

void otaToJson(JsonObject out) {
    out["running"] = (bool)running;
    out["progress"] = (int)progress;
    out["done"] = (bool)done;
    if (failure.length()) out["error"] = failure;
}

bool otaRunning() { return running || done; }
