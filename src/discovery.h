// Finding printers on the local network, for the Printers tab: Flashforge's
// UDP discovery (stock firmware), Bambu's SSDP announcements, and a sweep of
// the reader's /24 for Moonraker (7125), Creality's WebSocket (9999) and
// Anycubic's LAN handshake (18910).
#pragma once

#include <ArduinoJson.h>

// Starts a search in the background (about 45 s; printers show up as they
// are found). Returns an error message, or "" when it started (or one is
// already running).
String discoveryStart();
// {running, progress (0-100), subnet, error, printers: [{host, name, model,
// method, serial, unit, supported, note}]}; model and method are the keys
// the settings use.
void discoveryToJson(JsonObject out);
