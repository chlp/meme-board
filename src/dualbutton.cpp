#include "dualbutton.h"
#include "config.h"
#include "log.h"
#include <Arduino.h>

struct DualBtn {
    uint8_t  pin;
    bool     raw;       // last raw "pressed" sample
    bool     down;      // debounced state
    uint32_t rawSince;
    uint32_t downSince;
    bool     longFired;
    bool     evPress, evShort, evLong;
};

static DualBtn  s_btn[2] = { { DBTN_PIN_BLUE }, { DBTN_PIN_RED } };
static bool     s_connected     = false;
static uint32_t s_bothHighSince = 0;
static uint32_t s_bothLowSince  = 0;

static void resetButtons() {
    for (auto &b : s_btn) {
        b.raw = b.down = b.longFired = false;
        b.evPress = b.evShort = b.evLong = false;
    }
}

void dualButtonInit() {
    for (auto &b : s_btn) pinMode(b.pin, INPUT_PULLDOWN);
    delay(2);
    logLine("DBTN", "init blue=G%d lvl=%d red=G%d lvl=%d",
            DBTN_PIN_BLUE, digitalRead(DBTN_PIN_BLUE),
            DBTN_PIN_RED,  digitalRead(DBTN_PIN_RED));
}

void dualButtonUpdate() {
    uint32_t now = millis();
    for (auto &b : s_btn) b.evPress = b.evShort = b.evLong = false;

    bool hiBlue = digitalRead(DBTN_PIN_BLUE);
    bool hiRed  = digitalRead(DBTN_PIN_RED);

    if (hiBlue && hiRed) {
        s_bothLowSince = 0;
        if (!s_connected) {
            if (!s_bothHighSince) s_bothHighSince = now;
            else if (now - s_bothHighSince >= DBTN_DETECT_MS) {
                s_connected = true;
                resetButtons();
                logLine("DBTN", "connected");
            }
        }
    } else {
        s_bothHighSince = 0;
        if (!hiBlue && !hiRed) {
            if (!s_bothLowSince) s_bothLowSince = now;
            else if (s_connected && now - s_bothLowSince >= DBTN_GONE_MS) {
                s_connected = false;
                resetButtons();
                logLine("DBTN", "disconnected");
            }
        } else {
            s_bothLowSince = 0;
        }
    }
    if (!s_connected) return;

    for (int i = 0; i < 2; i++) {
        DualBtn &b = s_btn[i];
        bool pressed = !(i == DBTN_BLUE ? hiBlue : hiRed);
        if (pressed != b.raw) {
            b.raw = pressed;
            b.rawSince = now;
        } else if (pressed != b.down && now - b.rawSince >= DBTN_DEBOUNCE_MS) {
            b.down = pressed;
            if (pressed) {
                b.downSince = now;
                b.longFired = false;
                b.evPress   = true;
                logLine("DBTN", "%s down", dualButtonName(i));
            } else if (!b.longFired) {
                b.evShort = true;
            }
        }
        if (b.down && !b.longFired && now - b.downSince >= DBTN_LONG_MS) {
            b.longFired = true;
            b.evLong    = true;
        }
    }
}

bool dualButtonConnected() { return s_connected; }

static bool take(bool &ev) { bool v = ev; ev = false; return v; }
bool dualButtonTakePress(int id) { return take(s_btn[id].evPress); }
bool dualButtonTakeShort(int id) { return take(s_btn[id].evShort); }
bool dualButtonTakeLong(int id)  { return take(s_btn[id].evLong); }

const char *dualButtonName(int id) { return id == DBTN_BLUE ? "BLUE" : "RED"; }
