// The firmware's version (git describe) and board (PlatformIO env), from the
// lanetag_version.h that tools/version.py writes at build time.
#pragma once

#if __has_include("lanetag_version.h")
#include "lanetag_version.h"
#endif
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef FIRMWARE_BOARD
#define FIRMWARE_BOARD "unknown"
#endif
