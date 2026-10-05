#pragma once

#include <M5Cardputer.h>
#include "config.h"
#include <stddef.h>
#include <stdint.h>

bool     isEscKey(const Keyboard_Class::KeysState &st);
void     clearStatusBar();
// Text in the status bar; restored by loop() after VOL_DISPLAY_MS (like the volume overlay).
void     showStatusOverlay(const char *text, uint16_t color);
uint8_t *readFileToBuffer(const char *path, size_t &outLen);
