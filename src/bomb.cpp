#include "bomb.h"
#include "config.h"
#include "dualbutton.h"
#include "log.h"
#include "settings.h"
#include "ui_utils.h"
#include <vector>
#include <math.h>
#include "esp_random.h"

int bombBest = 0;

enum BombState { B_SPLASH, B_PLAY, B_FLASH, B_BOOM };

// Targets: 'a'-'z', '0'-'9', or one of these two for the dual buttons.
static const char TGT_BLUE = 'B';
static const char TGT_RED  = 'R';

static const uint16_t COL_DEFUSED = 0x3E4D; // soft green  (60,200,110)
static const uint16_t COL_BAR_BG  = 0x2104;
static const uint16_t COL_BLUE    = 0x1C9F;
static const uint16_t COL_RED     = 0xF8A2;

static const int BAR_H    = 8;
static const int HUD_Y    = 12;
static const int TARGET_Y = 34;

// Explosion sound: synthesised once per visit, unsigned 8-bit mono.
static const uint32_t BOOM_RATE = 11025;
static const size_t   BOOM_LEN  = BOOM_RATE * 3 / 2;
static const int      CH_BOOM    = NOTE_CH_BASE;
static const int      CH_TICK    = NOTE_CH_BASE + 1;
static const int      CH_SUCCESS = NOTE_CH_BASE + 2;

static BombState         s_state = B_SPLASH;
static char              s_target = 0;
static int               s_score = 0;
static bool              s_newRecord = false;
static uint32_t          s_limitMs = 0;
static uint32_t          s_deadline = 0;
static uint32_t          s_flashUntil = 0;
static uint32_t          s_boomAt = 0;
static int               s_lastBarW = -1;
static int               s_lastTenths = -1;
static uint32_t          s_nextTick = 0;
static uint8_t          *s_boom = nullptr;
static std::vector<char> s_prevKeys;

// ── Sound ─────────────────────────────────────────────────────────────────────

static void buildBoom() {
    if (s_boom) return;
    s_boom = (uint8_t *)malloc(BOOM_LEN);
    if (!s_boom) { logLine("BOMB", "boom buffer alloc failed"); return; }
    uint32_t x = 0x9E3779B9u;
    float lp = 0;
    for (size_t i = 0; i < BOOM_LEN; i++) {
        float t = (float)i / BOOM_RATE;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        float n = (int32_t)x / 2147483648.0f;
        // Low-pass that starts bright (crack) and closes into a rumble.
        float a = 0.03f + 0.7f * expf(-t * 8.0f);
        lp += a * (n - lp);
        // Compensate the filter's loss of level so the envelope alone shapes it.
        float norm = sqrtf(a / (2.0f - a));
        float env  = (t < 0.004f) ? t / 0.004f : expf(-t * 2.8f);
        float v    = lp / norm * env * 0.55f;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        s_boom[i] = (uint8_t)(128 + (int)(v * 127));
    }
}

static void playBoom() {
    if (s_boom) M5.Speaker.playRaw(s_boom, BOOM_LEN, BOOM_RATE, false, 1, CH_BOOM, true);
}

// ── Game logic ────────────────────────────────────────────────────────────────

static char pickTarget(char prev) {
    for (;;) {
        char c;
        if (dualButtonConnected() && (int)(esp_random() % 100) < BOMB_BTN_CHANCE) {
            c = (esp_random() & 1) ? TGT_BLUE : TGT_RED;
        } else {
            int r = esp_random() % 36;
            c = (r < 26) ? (char)('a' + r) : (char)('0' + r - 26);
        }
        if (c != prev) return c;
    }
}

static uint32_t limitForScore(int score) {
    int ms = BOMB_START_MS - score * BOMB_STEP_MS;
    return ms < BOMB_MIN_MS ? BOMB_MIN_MS : (uint32_t)ms;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

static void drawTimer(uint32_t now) {
    auto &d = M5Cardputer.Display;
    uint32_t left = (now < s_deadline) ? s_deadline - now : 0;
    int w = (int)((uint64_t)SCREEN_W * left / s_limitMs);
    if (w != s_lastBarW) {
        uint16_t col = (left * 3 > s_limitMs * 2) ? TFT_GREEN
                     : (left * 3 > s_limitMs)     ? TFT_YELLOW : TFT_RED;
        d.fillRect(0, 0, w, BAR_H, col);
        d.fillRect(w, 0, SCREEN_W - w, BAR_H, COL_BAR_BG);
        s_lastBarW = w;
    }
    int tenths = (int)((left + 99) / 100);
    if (tenths != s_lastTenths) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%2d.%ds ", tenths / 10, tenths % 10);
        d.setTextSize(2);
        d.setTextColor(left < 3000 ? TFT_RED : TFT_WHITE, TFT_BLACK);
        d.drawString(buf, 4, HUD_Y);
        s_lastTenths = tenths;
    }
}

static void drawHud() {
    auto &d = M5Cardputer.Display;
    char buf[16];
    snprintf(buf, sizeof(buf), "SCORE %d", s_score);
    d.setTextSize(2);
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.drawRightString(buf, SCREEN_W - 4, HUD_Y);
    d.setTextSize(1);
    d.setTextColor(0x7BEF, TFT_BLACK);
    snprintf(buf, sizeof(buf), "BEST %d", bombBest);
    d.drawString(buf, 4, SCREEN_H - 10);
    d.drawRightString(dualButtonConnected() ? "DUAL BTN: ON" : "DUAL BTN: OFF", SCREEN_W - 4, SCREEN_H - 10);
}

static void drawTarget() {
    auto &d = M5Cardputer.Display;
    d.fillRect(0, TARGET_Y, SCREEN_W, STATUS_Y - TARGET_Y + 8, TFT_BLACK);
    if (s_target == TGT_BLUE || s_target == TGT_RED) {
        bool blue = s_target == TGT_BLUE;
        uint16_t col = blue ? COL_BLUE : COL_RED;
        d.fillCircle(SCREEN_W / 2, TARGET_Y + 36, 34, col);
        d.drawCircle(SCREEN_W / 2, TARGET_Y + 36, 36, TFT_WHITE);
        d.setTextSize(2);
        d.setTextColor(TFT_WHITE, col);
        d.drawCenterString(blue ? "BLUE" : "RED", SCREEN_W / 2, TARGET_Y + 29);
    } else {
        char lbl[2] = { (char)toupper(s_target), 0 };
        d.setTextSize(8);
        d.setTextColor(TFT_WHITE, TFT_BLACK);
        d.drawCenterString(lbl, SCREEN_W / 2, TARGET_Y + 6);
    }
    d.setTextSize(1);
}

static void drawPlay() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    s_lastBarW = s_lastTenths = -1;
    drawHud();
    drawTarget();
    drawTimer(millis());
}

static void drawSplash() {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextSize(2);
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.drawCenterString("BOMB DEFUSE", SCREEN_W / 2, 22);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.drawCenterString("Press the shown key before", SCREEN_W / 2, 52);
    d.drawCenterString("the timer runs out", SCREEN_W / 2, 64);
    char buf[24];
    snprintf(buf, sizeof(buf), "BEST %d", bombBest);
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.drawCenterString(buf, SCREEN_W / 2, 82);
    d.setTextColor(0x7BEF, TFT_BLACK);
    d.drawCenterString(dualButtonConnected() ? "dual button: ON" : "dual button: OFF", SCREEN_W / 2, 96);
    d.drawCenterString("any key = start   TAB = next", SCREEN_W / 2, 116);
}

static void drawBoom() {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextSize(4);
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.drawCenterString("BOOM!", SCREEN_W / 2, 14);
    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE %d", s_score);
    d.setTextSize(2);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.drawCenterString(buf, SCREEN_W / 2, 56);
    d.setTextColor(s_newRecord ? TFT_YELLOW : TFT_CYAN, TFT_BLACK);
    if (s_newRecord) snprintf(buf, sizeof(buf), "NEW RECORD!");
    else             snprintf(buf, sizeof(buf), "BEST %d", bombBest);
    d.drawCenterString(buf, SCREEN_W / 2, 78);
    d.setTextSize(1);
    d.setTextColor(0x7BEF, TFT_BLACK);
    d.drawCenterString("ENTER = again   ` = menu", SCREEN_W / 2, 116);
}

// ── State transitions ─────────────────────────────────────────────────────────

static void nextTarget() {
    s_target   = pickTarget(s_target);
    s_limitMs  = limitForScore(s_score);
    s_deadline = millis() + s_limitMs;
    s_nextTick = millis() + 1000;
    s_state    = B_PLAY;
    drawPlay();
}

static void startGame() {
    s_score  = 0;
    s_target = 0;
    s_newRecord = false;
    logLine("BOMB", "start dual=%d", (int)dualButtonConnected());
    nextTarget();
}

static void defused() {
    s_score++;
    M5.Speaker.tone(1760, 70, CH_SUCCESS, true);
    M5Cardputer.Display.fillScreen(COL_DEFUSED);
    s_flashUntil = millis() + BOMB_FLASH_MS;
    s_state = B_FLASH;
}

static void explode() {
    auto &d = M5Cardputer.Display;
    M5.Speaker.stop(CH_TICK);
    playBoom();
    s_newRecord = s_score > bombBest;
    if (s_newRecord) { bombBest = s_score; saveSettings(); }
    logLine("BOMB", "boom score=%d best=%d", s_score, bombBest);
    d.fillScreen(TFT_WHITE);  delay(40);
    d.fillScreen(0xFD20);     delay(70);
    d.fillScreen(TFT_RED);    delay(70);
    s_boomAt = millis();
    s_state  = B_BOOM;
    drawBoom();
}

// ── Public API ────────────────────────────────────────────────────────────────

void bombEnter() {
    buildBoom();
    s_prevKeys.clear();
    s_state = B_SPLASH;
    drawSplash();
}

void bombLeave() {
    M5.Speaker.stop(CH_BOOM);
    M5.Speaker.stop(CH_TICK);
    M5.Speaker.stop(CH_SUCCESS);
    free(s_boom);
    s_boom = nullptr;
    s_state = B_SPLASH;
}

void bombRedraw() {
    switch (s_state) {
        case B_SPLASH: drawSplash(); break;
        case B_PLAY:   drawPlay();   break;
        case B_FLASH:  M5Cardputer.Display.fillScreen(COL_DEFUSED); break;
        case B_BOOM:   drawBoom();   break;
    }
}

void bombLoop() {
    uint32_t now = millis();

    bool btnPress[2] = { dualButtonTakePress(DBTN_BLUE), dualButtonTakePress(DBTN_RED) };

    switch (s_state) {
        case B_SPLASH:
            if (btnPress[0] || btnPress[1]) startGame();
            break;

        case B_PLAY: {
            if ((s_target == TGT_BLUE && btnPress[DBTN_BLUE]) ||
                (s_target == TGT_RED  && btnPress[DBTN_RED])) {
                defused();
                break;
            }
            // Unit unplugged mid-round while it was the target → pick another.
            if ((s_target == TGT_BLUE || s_target == TGT_RED) && !dualButtonConnected()) {
                s_target = pickTarget(s_target);
                drawTarget();
            }
            if ((int32_t)(now - s_deadline) >= 0) { explode(); break; }
            uint32_t left = s_deadline - now;
            if ((int32_t)(now - s_nextTick) >= 0) {
                M5.Speaker.tone(left < 5000 ? 2000 : 1200, 25, CH_TICK, true);
                s_nextTick = now + (left < 5000 ? 500 : 1000);
            }
            drawTimer(now);
            break;
        }

        case B_FLASH:
            if ((int32_t)(now - s_flashUntil) >= 0) nextTarget();
            break;

        case B_BOOM:
            if (now - s_boomAt >= 1200 && (btnPress[0] || btnPress[1])) startGame();
            break;
    }
}

void bombHandleKeyChange(const Keyboard_Class::KeysState &st) {
    std::vector<char> curr;
    for (char c : st.word) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) curr.push_back(c);
    }
    std::vector<char> fresh;
    for (char c : curr) {
        bool wasHeld = false;
        for (char pc : s_prevKeys) if (pc == c) { wasHeld = true; break; }
        if (!wasHeld) fresh.push_back(c);
    }
    s_prevKeys = curr;

    bool pressed = M5Cardputer.Keyboard.isPressed();
    bool esc     = isEscKey(st);
    bool startKey = !fresh.empty() || (pressed && (st.enter || st.space));

    switch (s_state) {
        case B_SPLASH:
            if (startKey) startGame();
            break;
        case B_PLAY:
            if (esc) { M5.Speaker.stop(CH_TICK); s_state = B_SPLASH; drawSplash(); break; }
            for (char c : fresh) {
                if (c == s_target) { defused(); break; }
            }
            break;
        case B_FLASH:
            break;
        case B_BOOM:
            if (esc) { M5.Speaker.stop(CH_BOOM); s_state = B_SPLASH; drawSplash(); break; }
            if (startKey && millis() - s_boomAt >= 1200) startGame();
            break;
    }
}
