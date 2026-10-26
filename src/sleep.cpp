#include "sleep.h"

#include <WiFi.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>

#include "app.h"

bool canWakeFrom(int pin) { return pin >= 0 && esp_sleep_is_valid_wakeup_gpio((gpio_num_t)pin); }

const char *wakePins() {
#if CONFIG_IDF_TARGET_ESP32C3
    return "GPIO 0-5";
#elif CONFIG_IDF_TARGET_ESP32C6
    return "GPIO 0-7";
#elif CONFIG_IDF_TARGET_ESP32S3
    return "GPIO 0-21";
#else
    return "GPIO 0, 2, 4, 12-15, 25-27 or 32-39";
#endif
}

void sleepBegin() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) log_i("woken from deep sleep (cause %d)", (int)cause);
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
    // Release any pin hold left by an earlier build, which held every pin
    // through the sleep (the flash's too).
    if (app.config.buttonPin >= 0) gpio_hold_dis((gpio_num_t)app.config.buttonPin);
#if ESP_IDF_VERSION_MAJOR < 5
    gpio_deep_sleep_hold_dis();
#endif
#endif
}

void sleepLoop() {
    int pin = app.config.buttonPin;
    if (app.config.sleepMinutes <= 0 || !canWakeFrom(pin)) return;
    // Not with the setup access point up (the Wi-Fi settings need fixing),
    // a scan waiting for its slot, or the button held (it would wake the
    // board at once).
    if (app.apMode || app.last.waiting || digitalRead(pin) == LOW) return;
    if (millis() - app.usedAt < (uint32_t)app.config.sleepMinutes * 60000) return;

    log_i("unused for %d min: deep sleep, the button on GPIO %d wakes", app.config.sleepMinutes, pin);
    app.feedback.stop();
    app.display.off();
    app.reader.powerDown();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.flush();
    gpio_num_t gpio = (gpio_num_t)pin;
#if !SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
    esp_sleep_enable_ext0_wakeup(gpio, 0);
    rtc_gpio_pullup_en(gpio);
    rtc_gpio_pulldown_dis(gpio);
#else
    // The pad keeps its pull-up through the sleep (as in Espressif's
    // deep_sleep example).
    gpio_pullup_en(gpio);
    gpio_pulldown_dis(gpio);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << pin, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
    esp_deep_sleep_start();
}
