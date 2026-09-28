#include "state.h"
#include <Preferences.h>

PetSave P;
Settings SET;

const Food FOODS[FOOD_COUNT] = {
  {"Apple", 22, 4, 4, 1},   {"Cookie", 12, 10, -2, 3}, {"Pizza", 35, 8, -3, 3},     {"Sushi", 25, 8, 3, 1},
  {"Carrot", 18, 2, 5, 1},  {"Cake", 25, 14, -5, 4},   {"Burger", 38, 8, -4, 3},    {"Ice cream", 15, 14, -3, 1},
  {"Broccoli", 18, -2, 7, 1}, {"Candy", 6, 12, -4, 1}, {"Taco", 30, 9, -1, 4},      {"Milk", 14, 4, 4, 0},
};

namespace state {

static Preferences prefs;
static bool petDirty = false, setDirty = false, frozen = false;
static uint32_t petDirtyAt = 0, setDirtyAt = 0;

static DiaryEntry diary[DIARY_LEN];
static uint8_t diaryHead = 0, diaryN = 0;

void defaultSettings() {
  memset(&SET, 0, sizeof SET);
  SET.magic = SET_MAGIC;
  SET.contrast = 200;
  SET.autoRotate = 1;
  SET.bubbles = 1;
  SET.sens = 2;
  SET.sleepDim = 1;
  SET.bedtime = 22 * 60;
  SET.waketime = 7 * 60;
  strlcpy(SET.appUrl, APP_URL_DEFAULT, sizeof SET.appUrl);
}

bool load() {
  prefs.begin("peekabyte", false);
  defaultSettings();
  if (prefs.getBytesLength("set") == sizeof(Settings)) {
    Settings s;
    prefs.getBytes("set", &s, sizeof s);
    if (s.magic == SET_MAGIC) SET = s;
  }
  if (prefs.getBytesLength("diary") == sizeof diary) {
    prefs.getBytes("diary", diary, sizeof diary);
    diaryHead = prefs.getUChar("dhead", 0) % DIARY_LEN;
    diaryN = min<uint8_t>(prefs.getUChar("dn", 0), DIARY_LEN);
  }
  if (prefs.getBytesLength("pet") != sizeof(PetSave)) return false;
  prefs.getBytes("pet", &P, sizeof P);
  return P.magic == PET_MAGIC && P.ver == PET_VER;
}

void markPet() {
  if (!petDirty) petDirtyAt = millis();
  petDirty = true;
}

void markSettings() {
  setDirty = true;
  setDirtyAt = millis();
}

void savePetNow() {
  petDirty = false;
  if (frozen) return;
  prefs.putBytes("pet", &P, sizeof P);
}

void saveSettingsNow() {
  setDirty = false;
  if (frozen) return;
  prefs.putBytes("set", &SET, sizeof SET);
}

void loop() {
  uint32_t now = millis();
  // Care actions batch up for a few seconds before hitting flash.
  if (petDirty && now - petDirtyAt > 4000) savePetNow();
  if (setDirty && now - setDirtyAt > 1500) saveSettingsNow();
}

void diaryAdd(uint8_t type, uint8_t arg) {
  diary[diaryHead] = DiaryEntry{P.ageSec, type, arg};
  diaryHead = (diaryHead + 1) % DIARY_LEN;
  if (diaryN < DIARY_LEN) diaryN++;
  if (frozen) return;
  prefs.putBytes("diary", diary, sizeof diary);
  prefs.putUChar("dhead", diaryHead);
  prefs.putUChar("dn", diaryN);
}

int diaryCount() { return diaryN; }

const DiaryEntry &diaryAt(int i) {
  int start = (diaryHead - diaryN + DIARY_LEN) % DIARY_LEN;
  return diary[(start + i) % DIARY_LEN];
}

void diaryClear() {
  diaryN = 0;
  diaryHead = 0;
  if (frozen) return;
  prefs.putUChar("dhead", 0);
  prefs.putUChar("dn", 0);
}

void factoryReset() {
  prefs.clear();
  frozen = true;
}

}  // namespace state
