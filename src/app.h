// Shared state of the device: settings, the reader and the last scan.
#pragma once

#include "config.h"
#include "display.h"
#include "feedback.h"
#include "printers.h"
#include "reader.h"

#define FIRMWARE_VERSION "0.1.0"

struct ScanRecord {
    uint32_t id = 0;            // counts scans; 0: none yet
    uint32_t at = 0;            // millis() of the read
    Tag tag;
    Filament filament;          // decoded here
    int printer = -1;           // printer it went to
    bool waiting = false;       // a slot still has to be picked
    bool ok = false;
    String status;              // what happened, for the page
    String printerTitle;        // zmod_nfc: the printer's name for the spool
    int printerSpool = 0;       // zmod_nfc: the Spoolman spool the printer found
};

struct App {
    Config config;
    TagReader reader;
    Feedback feedback;
    Display display;
    ScanRecord last;
    bool apMode = false;
    String apName;
    String wifiError;           // why the home network could not be joined
    uint32_t quietUntil = 0;    // millis() until which the reader is not polled

    String readerName() const;
    // A new tag was read: decode it and send it to the active printer.
    void handleTag(const Tag &tag);
    // Put the last scan into a slot of printer index.
    PrinterReply assignLast(int printer, int slot);
    // zmod_nfc: send the last scan to printer index again (new prompt).
    PrinterReply resendLast(int printer);

private:
    void record(const PrinterReply &reply);
};

extern App app;

// Set the Wi-Fi transmit power from the settings (after any WiFi.mode()).
void applyWifiPower();

void webBegin();
void webLoop();
