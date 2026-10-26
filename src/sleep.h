// Deep sleep after a set time without use (Settings > Power), for a reader
// on a battery: the reader chip, screen, LED and Wi-Fi are switched off and
// the slot button wakes the board, which then starts as after a reset.
#pragma once

#include <Arduino.h>

// Whether a button (to ground) on this pin can wake the board.
bool canWakeFrom(int pin);
// The pins that can, for an error message.
const char *wakePins();

// At start, before the button's pinMode.
void sleepBegin();
// Every loop: sleeps once nothing has used the reader for the set time.
void sleepLoop();
