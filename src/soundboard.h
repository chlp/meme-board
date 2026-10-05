#pragma once

#include <M5Cardputer.h>
#include <vector>
#include <Arduino.h>

extern std::vector<String> boardPaths;
extern int                 soundboardBoardIdx;
extern String              soundboardDir;
extern bool                boardSplashActive;
extern bool                sdSoundActive;
extern char                sbCurKey;
extern bool                sbPrevComma;
extern bool                sbPrevSlash;
// Dual-button memory slots (index = DualBtnId): board dir + key, '\0' = empty.
extern String              sbSlotDir[2];
extern char                sbSlotKey[2];

void scanSoundboardDirs();
void sbInitBrowseSelection();
void sbResetInputState(); // clears prevSbKeys, sbPrevComma, sbPrevSlash

void soundboardRefresh();
void soundboardHandleKeyChange(const Keyboard_Class::KeysState &st);
void sbDrawBrowseBadge(char key);
void soundboardHandleButtons(); // dual button: long = remember meme, short = play it

void drawBoardSplash();
void drawPiano();

bool resolveMemeMp3ForKey(char key, char *path, size_t pathCap);
bool useSoundboardBrowseUI();
