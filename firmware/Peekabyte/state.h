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

}  // namespace state
