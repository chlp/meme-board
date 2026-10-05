#pragma once

#include <stdint.h>

// M5 Unit Dual Button on the Grove port (Port.A, G2/G1).
// The unit has its own pull-ups; we enable the internal pull-downs, so an idle
// connected unit reads HIGH on both pins and an empty port reads LOW.  That is
// how presence is detected (both HIGH ≥ 100 ms → connected; both LOW ≥ 2 s →
// gone).  A pressed button pulls its pin LOW.
//
// Events are valid for one loop() iteration: dualButtonUpdate() clears the
// previous ones, so call it once per loop before any consumer.

enum DualBtnId { DBTN_BLUE = 0, DBTN_RED = 1 };

void dualButtonInit();
void dualButtonUpdate();
bool dualButtonConnected();

bool dualButtonTakePress(int id); // fresh press (debounced)
bool dualButtonTakeShort(int id); // released before DBTN_LONG_MS
bool dualButtonTakeLong(int id);  // held for DBTN_LONG_MS (fires while held)

const char *dualButtonName(int id);
