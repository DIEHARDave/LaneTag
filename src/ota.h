// Firmware updates straight from a LaneTag release on GitHub: the reader
// downloads the file itself (GitHub's downloads can't be handed over by the
// browser), checks its SHA-256 and restarts into it. Needs internet access.
#pragma once

#include <ArduinoJson.h>

// Starts the download in the background. url must be this board's
// lanetag-<version>-<board>.bin of a LaneTag release; sha256 (hex, may be
// empty) is the file's digest from the release. Returns an error message,
// or "" when it started.
String otaStart(const String &url, const String &sha256);
// {running, progress (0-100), done, error}; done: installed, restarting.
void otaToJson(JsonObject out);
bool otaRunning();
