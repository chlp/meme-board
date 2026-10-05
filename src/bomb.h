#pragma once

#include <M5Cardputer.h>

// Bomb defuse: press the shown key (a-z, 0-9, or a dual-button colour) before
// the timer runs out.  30 s for the first target, −1 s per success, ≥ 1 s.

extern int bombBest; // persisted in /settings.cfg

void bombEnter();  // show splash; caller has already stopped audio
void bombLeave();  // release the explosion buffer
void bombLoop();   // call every loop() while mode == BOMB
void bombHandleKeyChange(const Keyboard_Class::KeysState &st);
void bombRedraw(); // full redraw of the current screen
