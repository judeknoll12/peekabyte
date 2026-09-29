#pragma once
// Persistence of the pet, the settings and the diary in NVS flash.
#include <Arduino.h>
#include "petdata.h"

extern PetSave P;
extern Settings SET;

namespace state {

bool load();                 // false if there was no saved pet (fresh egg needed)
void defaultSettings();
void markPet();              // save the pet soon (debounced)
void markSettings();
void savePetNow();
void saveSettingsNow();
void loop();

void diaryAdd(uint8_t type, uint8_t arg);
int diaryCount();
const DiaryEntry &diaryAt(int i);   // 0 = oldest
void diaryClear();

void factoryReset();         // wipe everything; nothing is written afterwards

// How the last few runs ended: the reset reason seen at the next start, and how long the run
// lasted. Short runs ending in power cuts point at a power bank switching itself off;
// brownouts at a battery that can't keep up.
struct Run {
  uint32_t lasted;   // seconds (to within a minute or so)
  uint8_t why;       // esp_reset_reason() of the start that followed
};
constexpr int RUNS = 8;
void logBoot(uint8_t why);   // at start-up, after load()
int runCount();
const Run &runAt(int i);     // 0 = the run before this one

}  // namespace state
