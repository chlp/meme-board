#pragma once

// SD card SPI pins for M5 Cardputer ADV
#define SD_SCK  40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS   12

#define BOARDS_DIR  "/boards"
#define BOARDS_MEME "/boards/meme"
#define MP3_DIR     "/mp3"

#define BOARD_TITLE_MAX 10

#define SETTINGS_PATH  "/settings.cfg"
#define VOL_STEP       16
#define VOL_DISPLAY_MS 1500

#define SCREEN_W 240
#define SCREEN_H 135
#define IMG_H    110
#define STATUS_Y 110

#define NOTE_CH_BASE 1
#define NOTE_CH_CNT  7

// M5 Unit Dual Button on the Grove port (Port.A): yellow wire = G2, white = G1
#define DBTN_PIN_BLUE    2
#define DBTN_PIN_RED     1
#define DBTN_DEBOUNCE_MS 25
#define DBTN_LONG_MS     600
#define DBTN_DETECT_MS   100
#define DBTN_GONE_MS     2000

// Bomb defuse game
#define BOMB_START_MS   30000
#define BOMB_STEP_MS    1000
#define BOMB_MIN_MS     1000
#define BOMB_FLASH_MS   150
#define BOMB_BTN_CHANCE 30   // % of targets that are a dual button (if connected)

enum AppMode { SOUNDBOARD, BOMB, MP3_PLAYER };
