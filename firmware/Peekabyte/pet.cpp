#include "pet.h"
#include <sys/time.h>
#include "act.h"
#include "config.h"
#include "face.h"
#include "fx.h"
#include "games.h"
#include "gfx.h"
#include "imu.h"
#include "comms.h"
#include "protocol.h"
#include "screens.h"
#include "sprites.h"
#include "state.h"

namespace pet {

using namespace face;

enum Mode : uint8_t { M_SPLASH, M_EGG, M_HATCH, M_LIFE };
static Mode mode = M_SPLASH;
static float modeT = 0, clk = 0;

// ---- Wall clock (from the phone) ------------------------------------------------------
// The phone sets the chip's clock whenever it connects. The clock keeps running through
// restarts (a crash, a watchdog, "Restart"), and the time zone waits for it in memory that
// survives them; only a power cut loses both, until the phone is back.
RTC_NOINIT_ATTR static uint32_t rtcClockMagic;
RTC_NOINIT_ATTR static int16_t rtcTz;
constexpr uint32_t CLOCK_MAGIC = 0x7EC10C4Bu;
static bool timeKnown = false;
static int16_t tzMin = 0;

static uint32_t epochNow() { return timeKnown ? (uint32_t)time(nullptr) : 0; }

static void setClock(uint32_t epoch, int16_t tz) {
  struct timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
  tzMin = tz;
  timeKnown = true;
  rtcTz = tz;
  rtcClockMagic = CLOCK_MAGIC;
}

static void restoreClock() {
  if (rtcClockMagic == CLOCK_MAGIC && time(nullptr) > 1700000000) {
    tzMin = rtcTz;
    timeKnown = true;
  } else {
    rtcClockMagic = 0;
  }
}
static int minuteOfDay() {
  if (!timeKnown) return -1;
  int32_t local = (int32_t)(epochNow() % 86400) + tzMin * 60;
  local = ((local % 86400) + 86400) % 86400;
  return local / 60;
}
static bool inWindow(int m, int from, int to) { return from <= to ? (m >= from && m < to) : (m >= from || m < to); }

// The sleep schedule: asleep from bedtime to wake-up time, unless the owner runs bedtime by hand.
static bool scheduled() { return !SET.manualSleep; }
static bool bedtimeNow() {
  int m = minuteOfDay();
  return scheduled() && m >= 0 && inWindow(m, SET.bedtime, SET.waketime);
}
static int minutesToBedtime() {   // -1 without a clock or a schedule
  int m = minuteOfDay();
  if (!scheduled() || m < 0) return -1;
  return ((SET.bedtime - m) % 1440 + 1440) % 1440;
}
static bool nightish() {   // for picking words: "goodnight" rather than "nap"
  int m = minuteOfDay();
  return bedtimeNow() || (m >= 0 && (m >= 19 * 60 || m < 5 * 60));
}

// ---- Moment-to-moment state -------------------------------------------------------------
static float timeScale = 1;
static float grumpyT = 0, dizzyT = 0, scaredT = 0, joyT = 0, surpriseT = 0, loveT = 0, yuckT = 0;
static uint8_t emote = EMO_NONE;
static float emoteT = 0;
static bool petting = false;
static float petT = 0, petX = 64, petY = 32, heartT = 0, strokeSpeed = 0;
static uint32_t petMoveAt = 0;
static bool phoneLook = false;
static float phoneLX = 0, phoneLY = 0;
static float talk = 0;
static uint32_t talkAt = 0;
static float gazeX = 0, gazeY = 0, gazeTX = 0, gazeTY = 0, gazeHold = 1;
static float fidgetT = 8;
static float zzzT = 0, dreamT = 20;
static bool hiding = false;
static float hideT = 0;
static bool upside = false;
static float lightsDarkT = 0;
static uint8_t bigShakes = 0;
static uint32_t lastBigShake = 0, lastPickupSay = 0, lastBoopSay = 0;
static float rockT = 0;
static int8_t awaitTrick = -1;
static bool awaitOk = false;
static float awaitT = 0;
static uint8_t want = 0;
static float wantT = 0;
static uint32_t lastSay = 0;
static bool card = false;
static float cardT = 0;
static uint32_t rebootAt = 0;
static float contrastNow = 200;
static bool previewing = false;
static Avatar savedAv;
static uint32_t previewAt = 0;
static uint32_t lastSave = 0, lastPush = 0;
static bool stateDirty = true;
static uint8_t playing = 0;   // game being played
// The phone link: a short blip (screen lock, app switch) shouldn't make the pet wave
// goodbye and greet you all over again.
constexpr uint32_t BLIP_MS = 30000;
static uint32_t phoneLeftAt = 0;   // when the Bluetooth link last dropped (0 = not since boot)
static uint32_t byeAt = 0;         // wave goodbye at this time unless the phone is back
// Sleep
static bool nightSleep = false;    // this is the night's sleep (ends at wake-up time), not a nap
static uint32_t sleptAt = 0;
static uint32_t stayUp = 0;        // woken during the scheduled night: back to sleep 10 min after the last fuss
static bool windDownSaid = false;  // "almost bedtime!" said already
// The owner is talking to the pet through the phone's microphone.
static float listen = 0;           // their voice level 0..1
static uint32_t listenAt = 0;

// eating / medicine
static int8_t eatFood = -1;   // -1 none, 0..11 food, 100 pill, 101 treat
static float eatT = 0;
static bool eatDone = false;

enum Want : uint8_t { W_NONE, W_FOOD, W_SLEEP, W_LOVE, W_PLAY, W_MEDICINE, W_CLEAN };
static const char *const WANT_NAMES[] = {"", "food", "sleep", "love", "play", "medicine", "clean"};

static float rnd() { return (esp_random() & 0xFFFFFF) / 16777216.0f; }
static float clamp100(float v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
static bool hasTrait(uint8_t t) { return P.trait[0] == t || P.trait[1] == t; }

static void markDirty() {
  stateDirty = true;
  state::markPet();
}

static float moodScore() {
  return P.hunger * 0.28f + P.energy * 0.18f + P.fun * 0.27f + P.love * 0.27f - (P.sick ? 20 : 0) - (grumpyT > 0 ? 15 : 0);
}

static const char *moodName() {
  if (mode == M_EGG) return "egg";
  if (P.asleep) return "asleep";
  if (P.sick) return "sick";
  if (dizzyT > 0) return "dizzy";
  if (grumpyT > 0) return "grumpy";
  if (P.energy < 15) return "exhausted";
  if (P.hunger < 15) return "starving";
  float m = moodScore();
  if (m > 80) return "ecstatic";
  if (m > 64) return "happy";
  if (m > 45) return "okay";
  if (m > 30) return P.love < P.fun ? "lonely" : "bored";
  return "sad";
}

// ---- Talking to the phone -------------------------------------------------------------------
static void sendEvent(const String &body) {
  if (!comms::anyone()) return;
  comms::send("{\"t\":\"ev\"," + body + "}");
}

static void jsonStr(String &j, const char *s) {
  j += '"';
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c == '"' || c == '\\') { j += '\\'; j += (char)c; }
    else if (c < 0x20) j += ' ';
    else j += (char)c;
  }
  j += '"';
}

static void sfx(const char *name) {
  String b = "\"e\":\"sfx\",\"s\":\"";
  b += name;
  b += '"';
  sendEvent(b);
}

// Short lines the pet shows by itself when no phone is around.
static const char *soloLine(const char *intent) {
  static const char *const MAP[][2] = {
    {"dizzy", "Whoa~"}, {"scared", "Aaah!"}, {"whee", "Wheee!"}, {"peekaboo", "Peekaboo!"}, {"greet", "Hi!"},
    {"grumpy", "Hmph!"}, {"yum", "Yum!"}, {"fav_food", "YUM!!"}, {"yuck", "Yuck!"}, {"full", "I'm full!"},
    {"stop", "Stop it!"}, {"goodnight", "Night night"}, {"morning", "Morning!"}, {"boop", "Boop!"},
    {"burp", "*burp*"}, {"trick_fail", "Oops!"}, {"learned", "I did it!"}, {"stage_up", "I grew!"},
    {"cured", "All better!"}, {"clean_thanks", "Sparkly!"}, {"hatched", "Hello world!"}, {"picked_up", "Hi!"},
    {"rocked", "So cozy..."}, {"medicine", "Blegh!"}, {"not_sleepy", "Not tired!"}, {"tickle", "Hehe!"},
    {"new_name", "Love it!"}, {"new_look", "Lookin' good"}, {"record", "New record!"}, {"bedtime", "Bedtime soon"},
  };
  for (auto &m : MAP)
    if (!strcmp(m[0], intent)) return m[1];
  return nullptr;
}

// A reason for the pet to speak. The phone turns it into words (AI or phrase book)
// and says it out loud; without a phone the pet shows a short line itself.
static void say(const char *intent, const char *ctx = nullptr, bool force = false) {
  uint32_t now = millis();
  if (!force && now - lastSay < 2200) return;
  lastSay = now;
  if (comms::anyone()) {
    String b = "\"e\":\"say\",\"i\":\"";
    b += intent;
    b += "\",\"m\":\"";
    b += moodName();
    b += '"';
    if (ctx) {
      b += ",\"x\":";
      jsonStr(b, ctx);
    }
    sendEvent(b);
  } else if (SET.bubbles) {
    const char *l = soloLine(intent);
    if (l) fx::say(l, 1.8f);
  }
}

// ---- Needs ------------------------------------------------------------------------------------
static void addNeeds(float h, float e, float f, float l) {
  P.hunger = clamp100(P.hunger + h);
  P.energy = clamp100(P.energy + e);
  P.fun = clamp100(P.fun + f);
  P.love = clamp100(P.love + l);
  markDirty();
}

static bool listening() { return listenAt && millis() - listenAt < 2500 && !P.asleep; }

// Anything the owner does. Awake in the scheduled night, the pet stays up while there's fuss.
static void poke() {
  if (stayUp) stayUp = millis() | 1;
}

static void fallAsleep() {
  if (P.asleep) return;
  act::stop();
  P.asleep = 1;
  zzzT = 0;
  dreamT = 25 + rnd() * 40;
  int toBed = minutesToBedtime();
  nightSleep = bedtimeNow() || (toBed >= 0 && toBed <= 60) || (!scheduled() && P.lightsOff);
  sleptAt = millis();
  stayUp = 0;
  listenAt = 0;
  fx::hush();
  say(nightish() ? "goodnight" : "nap", nullptr, true);
  sfx("yawn");
  markDirty();
}

static void wakeUp(bool gently) {
  if (!P.asleep) return;
  P.asleep = 0;
  lightsDarkT = 0;
  if (bedtimeNow()) stayUp = millis() | 1;   // up in the night: it goes back to sleep once things calm down
  if (!gently && P.energy < 60) {
    grumpyT = 45;
    P.fun = clamp100(P.fun - 5);
    say("grumpy", "woken up", true);
  } else {
    int m = minuteOfDay();
    say(m >= 4 * 60 && m < 12 * 60 && !bedtimeNow() ? "morning" : "wake", nullptr, true);
    joyT = 1.0f;
  }
  surpriseT = 0.4f;
  markDirty();
}

// A new schedule shouldn't knock the pet out mid-play: it gets the usual 10 minutes.
static void scheduleChanged() {
  if (!P.asleep && bedtimeNow()) stayUp = millis() | 1;
  windDownSaid = false;
}

// ---- New pets ------------------------------------------------------------------------------------
static const char *const NAMES[] = {
  "Mochi", "Pixel", "Biscuit", "Nugget", "Tofu", "Pip", "Wiggles", "Bean", "Noodle", "Sprout", "Blip", "Zuzu",
  "Kiki", "Peanut", "Dumpling", "Button", "Pickle", "Bubbles", "Waffles", "Momo", "Boba", "Taco", "Ziggy", "Bloop",
  "Squish", "Pebble", "Mango", "Cosmo", "Juno", "Luna", "Nimbus", "Orbit", "Poppy", "Pudding", "Tater", "Toffee",
  "Widget", "Yoyo", "Zappy", "Gizmo", "Sprocket", "Beep"};
static const char *const SYL1[] = {"Mo", "Pi", "Bo", "Zu", "Ki", "Lu", "Nu", "Po", "Ti", "Bi", "Fi", "Mi",
                                   "Ru", "Ya", "Chi", "Do", "Ko", "Pe", "Wu", "Ze", "Gi", "Ba", "Sho", "Mu"};
static const char *const SYL2[] = {"mo", "ppy", "xel", "bo", "ki", "nu", "ko", "zzy", "lo", "bit", "chi", "to",
                                   "ba", "po", "ri", "mi", "bu", "ffin", "dle", "nk", "zu", "pi", "la", "noo"};

static void randomName(char *out, size_t cap) {
  if (rnd() < 0.55f) strlcpy(out, NAMES[esp_random() % (sizeof NAMES / sizeof NAMES[0])], cap);
  else snprintf(out, cap, "%s%s", SYL1[esp_random() % 24], SYL2[esp_random() % 24]);
}

static uint8_t pick(uint8_t n) { return esp_random() % n; }

static void randomLook(Avatar &a) {
  a.eyes = pick(AV_COUNTS[0]);
  a.size = 1 + pick(3);
  a.gap = 1 + pick(3);
  a.pupil = pick(10) < 7 ? pick(3) : pick(AV_COUNTS[3]);
  if (a.eyes == EYE_CAT && pick(2)) a.pupil = PUP_SLIT;
  a.lashes = pick(10) < 2 ? 1 + pick(2) : 0;
  a.brows = pick(10) < 3 ? 1 + pick(4) : 0;
  int extras = 0;
  a.hat = pick(100) < 45 ? 1 + pick(AV_COUNTS[6] - 1) : 0;
  extras += a.hat != 0;
  a.glasses = pick(100) < 22 ? 1 + pick(AV_COUNTS[7] - 1) : 0;
  extras += a.glasses != 0;
  a.face = extras < 2 && pick(100) < 15 ? 1 + pick(AV_COUNTS[8] - 1) : 0;
  extras += a.face != 0;
  a.mouth = extras < 2 && pick(100) < 30 ? 1 + pick(AV_COUNTS[9] - 1) : 0;
  a.cheeks = pick(100) < 40 ? 1 + pick(AV_COUNTS[10] - 1) : 0;
  a.neck = extras < 2 && pick(100) < 18 ? 1 + pick(AV_COUNTS[11] - 1) : 0;
}

static void newEgg() {
  memset(&P, 0, sizeof P);
  P.magic = PET_MAGIC;
  P.ver = PET_VER;
  P.stage = ST_EGG;
  randomName(P.name, sizeof P.name);
  P.nameRandom = 1;
  randomLook(P.av);
  P.trait[0] = pick(TRAIT_COUNT);
  do P.trait[1] = pick(TRAIT_COUNT); while (P.trait[1] == P.trait[0]);
  P.favFood = pick(FOOD_COUNT);
  do P.hateFood = pick(FOOD_COUNT); while (P.hateFood == P.favFood);
  P.hunger = 80;
  P.energy = 85;
  P.fun = 70;
  P.love = 60;
  P.health = 100;
  state::diaryClear();
  state::savePetNow();
  Serial.printf("[pet] new egg: %s\n", P.name);
}

static void bleNameUpdate() {
  char n[32];
  snprintf(n, sizeof n, "Peeka %s", P.name);
  comms::setName(n);
}

// ---- Food and medicine -----------------------------------------------------------------------------
static void startEating(int8_t food) {
  eatFood = food;
  eatT = 0;
  eatDone = false;
  act::stop();
  sfx(food == 100 ? "pill" : "drop");
}

static void feed(uint8_t food) {
  if (food >= FOOD_COUNT) return;
  if (P.asleep) { fx::spawn(fx::FX_ZZZ, face::cx() + 22, face::eyeY() - 10, 0, 0, 1.5f); return; }
  if (eatFood >= 0) return;
  if (P.hunger > 92) {
    static const uint8_t NO[] = {act::MV_NO};
    act::play(NO, 1, act::TAG_REACT);
    say("full", FOODS[food].name, true);
    return;
  }
  startEating(food);
}

static void finishEating() {
  eatDone = true;
  if (eatFood == 100) {   // medicine
    yuckT = 1.4f;
    static const uint8_t SHIVER[] = {act::MV_WOBBLE, act::MV_WOBBLE};
    act::play(SHIVER, 2, act::TAG_REACT);
    if (P.sick) {
      P.sick = 0;
      P.health = max(P.health, 72.0f);
      state::diaryAdd(DI_CURED, 0);
      say("cured", nullptr, true);
      fx::burst(fx::FX_SPARKLE, face::cx(), face::eyeY(), 5, 20);
    } else {
      say("medicine", "wasn't sick", true);
    }
    markDirty();
    return;
  }
  if (eatFood == 101) {   // training treat
    joyT = 0.8f;
    addNeeds(4, 0, 3, 1);
    return;
  }
  const Food &f = FOODS[eatFood];
  float joy = hasTrait(TR_FOODIE) ? 1.3f : 1.0f;
  addNeeds(f.fill * (P.sick ? 0.7f : 1.0f), 0, f.fun * joy, 1);
  P.health = clamp100(P.health + f.health);
  P.fed++;
  if (eatFood == P.favFood) {
    loveT = 2.0f;
    addNeeds(0, 0, 10, 4);
    for (int i = 0; i < 4; i++) fx::spawn(fx::FX_HEART, face::cx() + random(-26, 27), face::eyeY() - 6, random(-6, 7), -14);
    if (!P.knownFav) {
      P.knownFav = 1;
      state::diaryAdd(DI_FAVFOOD, eatFood);
    }
    say("fav_food", f.name, true);
    sfx("yum");
  } else if (eatFood == P.hateFood) {
    yuckT = 1.6f;
    addNeeds(0, 0, -8, -2);
    say("yuck", f.name, true);
    sfx("yuck");
  } else {
    joyT = 1.0f;
    say("yum", f.name, true);
    sfx("yum");
  }
  markDirty();
}

// Food falls in, gets eaten in three bites, then the pet reacts.
static void updateEating(float dt, Pose &p) {
  if (eatFood < 0) return;
  eatT += dt;
  float mx = face::cx(), my = face::mouthY() - 2;
  float land = my - 8;
  if (eatT < 0.55f) {
    p.lookY = 0.3f + eatT;
    p.expr = EX_SURPRISED;
  } else if (eatT < 1.9f) {
    p.expr = EX_CHOMP;
    p.lookY = 0.8f;
    int bite = (int)((eatT - 0.55f) / 0.45f);
    static int lastBite = -1;
    if (bite != lastBite && bite < 3) {
      lastBite = bite;
      sfx(eatFood == 100 ? "gulp" : "chomp");
      if (eatFood < FOOD_COUNT && FOODS[eatFood].crumbs) {
        fx::addCrumbs(mx + random(-6, 7), land + 6, FOODS[eatFood].crumbs);
        if (bite == 2) P.crumbs = min(40, P.crumbs + (int)FOODS[eatFood].crumbs);   // one meal's worth of mess
      }
    }
    if (bite >= 3) lastBite = -1;
    p.dy += fabsf(sinf(eatT * 14)) * 1.2f;
  } else if (!eatDone) {
    finishEating();
  } else if (eatT > 2.9f) {
    if (eatFood < FOOD_COUNT && rnd() < 0.12f && FOODS[eatFood].fill > 25) {
      say("burp", nullptr, true);
      sfx("burp");
      fx::spawn(fx::FX_POOF, mx + 6, my, 0, -8, 0.8f);
    }
    eatFood = -1;
  }
}

static void drawEating() {
  if (eatFood < 0 || eatT > 1.9f) return;
  const char *const *s = eatFood == 100 ? spr::PILL : (eatFood == 101 ? spr::COOKIE : spr::FOOD[eatFood]);
  int h = eatFood == 100 ? spr::PILL_H : spr::FOOD_H;
  float mx = face::cx(), my = face::mouthY() - 2;
  float land = my - 8;
  float y;
  if (eatT < 0.55f) {
    float k = eatT / 0.55f;
    y = -14 + (land + 14) * k * k;
  } else {
    y = land;
  }
  int x = lroundf(mx) - 6;
  int bites = eatT < 0.55f ? 0 : min(3, 1 + (int)((eatT - 0.55f) / 0.45f));
  int keep = 12 - bites * 4;   // bitten from the right
  gfx::fillRect(x - 1, lroundf(y) - 1, keep + 2, h + 2, 0);
  gfx::setClip(x, 0, x + keep, SCREEN_H);
  gfx::sprite(x, lroundf(y), s, h);
  gfx::resetClip();
}

// ---- Tricks -----------------------------------------------------------------------------------------
static bool trickMoves(uint8_t id, const uint8_t *&mv, uint8_t &n) {
  if (id < BUILTIN_TRICKS) {
    mv = act::builtinTrick(id, n);
    return mv != nullptr;
  }
  if (id >= TRICK_SLOTS) return false;
  const CustomTrick &c = P.custom[id - BUILTIN_TRICKS];
  if (!c.n) return false;
  mv = c.moves;
  n = c.n;
  return true;
}

static void commandTrick(uint8_t id) {
  const uint8_t *mv;
  uint8_t n;
  if (!trickMoves(id, mv, n)) return;
  if (P.asleep) { say("asleep", "trick", true); return; }
  if (eatFood >= 0 || games::active()) return;
  if (P.sick || P.energy < 8) {
    static const uint8_t NO[] = {act::MV_NO};
    act::play(NO, 1, act::TAG_REACT);
    say(P.sick ? "too_sick" : "too_tired", nullptr, true);
    return;
  }
  float skill = P.skill[id] / 100.0f;
  float p = 0.12f + skill * 0.85f + (moodScore() / 100.0f - 0.5f) * 0.2f;
  if (grumpyT > 0) p -= 0.25f;
  if (P.energy < 25) p -= 0.15f;
  if (hasTrait(TR_PLAYFUL)) p += 0.05f;
  bool ok = rnd() < constrain(p, 0.05f, 0.97f);
  if (ok) act::play(mv, n, act::TAG_TRICK, id);
  else act::fumble(mv[0]);
  awaitTrick = id;
  awaitOk = ok;
  awaitT = 12;
  addNeeds(0, -1.5f, ok ? 2 : 0.5f, 0);
  if (ok) {
    P.tricksDone++;
    P.skill[id] = min(100, P.skill[id] + 1);
  }
  String b = "\"e\":\"trick\",\"id\":";
  b += id;
  b += ",\"ok\":";
  b += ok ? 1 : 0;
  sendEvent(b);
  if (!ok) say("trick_fail", nullptr, true);
  markDirty();
}

static void reward(uint8_t how) {
  if (P.asleep) return;
  if (awaitTrick < 0) {   // just being nice
    if (how == TM_TREAT && eatFood < 0) startEating(101);
    else {
      loveT = 1.2f;
      addNeeds(0, 0, 1, 3);
      say("praised", nullptr);
    }
    return;
  }
  int id = awaitTrick;
  awaitTrick = -1;
  int gain = awaitOk ? (how == TM_TREAT ? random(8, 15) : random(5, 10)) : (how == TM_TREAT ? 2 : 1);
  if (hasTrait(TR_PLAYFUL)) gain = gain * 13 / 10;
  if (hasTrait(TR_CURIOUS)) gain += 1;
  int before = P.skill[id];
  P.skill[id] = min(100, before + gain);
  bool learned = before < 80 && P.skill[id] >= 80;
  if (how == TM_TREAT && eatFood < 0) startEating(101);
  else {
    loveT = 1.0f;
    addNeeds(0, 0, 1, 3);
  }
  String b = "\"e\":\"skill\",\"id\":";
  b += id;
  b += ",\"v\":";
  b += P.skill[id];
  b += ",\"learned\":";
  b += learned ? 1 : 0;
  sendEvent(b);
  if (learned) {
    state::diaryAdd(DI_LEARNED, id);
    static const uint8_t YAY[] = {act::MV_STARS, act::MV_HOP, act::MV_SPARKLE};
    act::play(YAY, 3, act::TAG_REACT);
    char name[20];
    snprintf(name, sizeof name, "%d", id);
    say("learned", name, true);
  } else {
    say(awaitOk ? "praised" : "try_again", nullptr);
  }
  markDirty();
}

// ---- Motion reactions ---------------------------------------------------------------------------------
static void motion(uint8_t e) {
  if (mode == M_EGG) {
    if (e == imu::EV_TAP || e == imu::EV_SHAKE) {
      P.eggSec += 15;
      sfx("wobble");
    }
    return;
  }
  if (mode != M_LIFE || games::active()) return;
  poke();
  bool brave = hasTrait(TR_BRAVE), shy = hasTrait(TR_SHY);
  switch (e) {
    case imu::EV_TAP:
      if (P.asleep) {
        if (rnd() < 0.35f) wakeUp(false);
        else fx::spawn(fx::FX_ZZZ, face::cx() + 20, face::eyeY() - 8, 0, 0, 1.5f);
        break;
      }
      surpriseT = shy ? 1.0f : 0.45f;
      if (shy) {
        static const uint8_t JUMP[] = {act::MV_HOP};
        act::play(JUMP, 1, act::TAG_REACT);
      }
      fx::spawn(fx::FX_EXCLAIM, face::cx() + 24, face::eyeY() - 12, 0, 0, 0.8f);
      sfx("boop");
      if (millis() - lastBoopSay > 20000) {
        lastBoopSay = millis();
        say("boop");
      }
      break;
    case imu::EV_DOUBLE_TAP:
      if (P.asleep) break;
      loveT = 1.2f;
      addNeeds(0, 0, 1, 2);
      say("tickle");
      sfx("giggle");
      break;
    case imu::EV_SHAKE:
      if (P.asleep) wakeUp(false);
      surpriseT = 0.6f;
      break;
    case imu::EV_BIG_SHAKE: {
      act::stop();
      P.shaken++;
      uint32_t now = millis();
      bigShakes = now - lastBigShake < 120000 ? bigShakes + 1 : 1;
      lastBigShake = now;
      dizzyT = 3.0f;
      if (brave) {
        addNeeds(0, 0, 3, 0);
        say("whee", "shaken", true);
      } else if (bigShakes >= 3) {
        grumpyT = 60;
        addNeeds(0, 0, -4, -3);
        fx::spawn(fx::FX_ANGER, face::cx() + 24, face::eyeY() - 14, 0, 0, 2.5f);
        say("stop", "shaken", true);
      } else {
        addNeeds(0, 0, -2, -1);
        say("dizzy", "shaken", true);
      }
      sfx("dizzy");
      markDirty();
      break;
    }
    case imu::EV_FREEFALL:
      if (P.asleep) wakeUp(false);
      scaredT = 1.2f;
      say(brave ? "whee" : "scared", "falling", true);
      sfx(brave ? "whee" : "scream");
      break;
    case imu::EV_LAND:
      if (scaredT > 0) {
        dizzyT = brave ? 0 : 1.6f;
        if (brave) joyT = 1.2f;
        fx::burst(fx::FX_STAR, face::cx(), face::eyeY() - 10, 4, 18);
        sfx("bonk");
      }
      break;
    case imu::EV_FACE_DOWN:
      hiding = true;
      hideT = 0;
      break;
    case imu::EV_FACE_UP:
      if (hiding && !P.asleep && hideT < 10) {
        joyT = 1.4f;
        addNeeds(0, 0, 3, 3);
        fx::spawn(fx::FX_HEART, face::cx() - 20, face::eyeY() - 8, -4, -12);
        fx::spawn(fx::FX_HEART, face::cx() + 20, face::eyeY() - 8, 4, -12);
        say("peekaboo", nullptr, true);
        sfx("giggle");
      }
      hiding = false;
      break;
    case imu::EV_UPSIDE_DOWN:
      upside = true;
      if (SET.autoRotate) gfx::setFlip(!SET.flip);
      if (!P.asleep) {
        surpriseT = 0.8f;
        say("upside_down");
      }
      break;
    case imu::EV_UPRIGHT:
      upside = false;
      gfx::setFlip(SET.flip);
      break;
    case imu::EV_PICKUP:
      if (P.asleep) {
        wakeUp(P.energy > 70);
        break;
      }
      surpriseT = 0.5f;
      gazeX = gazeTX = 0;
      gazeY = gazeTY = -0.1f;
      gazeHold = 2.5f;
      if (millis() - lastPickupSay > 120000) {
        lastPickupSay = millis();
        say("picked_up");
      }
      break;
    case imu::EV_ROCK_START:
      rockT = 0;
      if (!P.asleep) say("rocked");
      break;
    case imu::EV_SPIN:
      if (P.asleep) break;
      joyT = 0.8f;
      dizzyT = 2.2f;
      say("whee", "spun", true);
      sfx("whee");
      break;
    default:
      break;
  }
}

// ---- Petting ----------------------------------------------------------------------------------------
static void onPet(uint8_t phase, uint8_t x, uint8_t y) {
  uint32_t now = millis();
  if (phase == 0) {
    petting = true;
    petT = 0;
    strokeSpeed = 0;
    P.petted++;
    markDirty();
    if (P.asleep) return;
    if (grumpyT > 0) grumpyT = max(0.0f, grumpyT - 20);
    sfx("purr");
  } else if (phase == 1) {
    float d = fabsf(x - petX) + fabsf(y - petY);
    float dt = max<uint32_t>(1, now - petMoveAt) / 1000.0f;
    strokeSpeed += ((d / dt) - strokeSpeed) * 0.3f;
  } else {
    if (petting && petT > 1.5f && !P.asleep) say(strokeSpeed > 160 ? "tickle" : "purr");
    petting = false;
    sfx("purr_end");
  }
  petX = x;
  petY = y;
  petMoveAt = now;
}

// ---- Needs over time ---------------------------------------------------------------------------------
static void wants() {
  uint8_t w = W_NONE;
  if (P.sick) w = W_MEDICINE;
  else if (P.hunger < 30) w = W_FOOD;
  else if (!P.asleep && (P.energy < 20 || (bedtimeNow() && P.energy < 60))) w = W_SLEEP;
  else if (P.love < 30) w = W_LOVE;
  else if (P.fun < 30) w = W_PLAY;
  else if (P.crumbs > 8) w = W_CLEAN;
  if (P.asleep && w != W_MEDICINE && w != W_FOOD) w = W_NONE;
  bool changed = w != want;
  want = w;
  if (w == W_NONE) return;
  wantT -= 1;
  if ((changed || wantT <= 0) && !P.asleep) {
    wantT = 420 + rnd() * 240;
    static const uint8_t ASK[] = {act::MV_HOP, act::MV_LOOK_D};
    if (!act::busy() && eatFood < 0) act::play(ASK, 1, act::TAG_REACT);
    static const uint8_t ICONS[] = {fx::IC_NONE, fx::IC_FOOD, fx::IC_SLEEP, fx::IC_HEART, fx::IC_BALL, fx::IC_PILL, fx::IC_BROOM};
    if (!comms::anyone() || SET.bubbles) fx::icon(ICONS[w], 4);
    static const char *const INTENTS[] = {"", "hungry", "sleepy", "lonely", "bored", "sick", "messy"};
    say(INTENTS[w], nullptr, true);
    stateDirty = true;
  }
}

static void grow() {
  static const uint32_t AT[] = {0, 6 * 3600UL, 30 * 3600UL, 80 * 3600UL};
  if (P.stage >= ST_ADULT || P.stage == ST_EGG) return;
  if (P.ageSec >= AT[P.stage]) {
    P.stage++;
    state::diaryAdd(DI_STAGE, P.stage);
    static const uint8_t YAY[] = {act::MV_SQUISH, act::MV_GROW, act::MV_STARS, act::MV_JOY};
    act::play(YAY, 4, act::TAG_REACT);
    static const char *const ST[] = {"egg", "baby", "kid", "teen", "adult"};
    say("stage_up", ST[P.stage], true);
    sfx("levelup");
    markDirty();
  }
}

void tick() {
  if (mode == M_EGG) {
    P.eggSec++;
    if (P.eggSec >= 300) {   // nobody came: hatch on our own
      mode = M_HATCH;
      modeT = 0;
      sfx("hatch");
    }
    return;
  }
  if (mode != M_LIFE) return;
  float s = timeScale;              // simulated seconds this tick
  float h = s / 3600.0f;            // ... in hours
  P.ageSec += (uint32_t)s;
  bool asleep = P.asleep;
  float hr = asleep ? -3.0f : -9.0f, er = asleep ? (P.lightsOff ? 24.0f : 16.0f) : -7.0f;
  float fr = asleep ? 0 : -8.0f, lr = asleep ? -1.0f : -6.0f;
  if (!asleep && scheduled() && timeKnown) {
    // Paced so a pet on a schedule makes it from wake-up time to bedtime.
    int day = ((SET.bedtime - SET.waketime) % 1440 + 1440) % 1440;
    if (day >= 240) er = -min(7.0f, 88.0f * 60 / day);
  }
  if (hasTrait(TR_FOODIE)) hr *= 1.35f;
  if (hasTrait(TR_SLEEPY) && !asleep) er *= 1.35f;
  if (hasTrait(TR_PLAYFUL)) fr *= 1.3f;
  if (hasTrait(TR_CUDDLY)) lr *= 1.35f;
  if (hasTrait(TR_CURIOUS) && imu::stillSeconds() > 600) fr *= 1.3f;
  if (games::active()) er -= 20;
  if (petting && !asleep) lr = 60;
  if (imu::rocking()) lr = max(lr, 20.0f);
  P.hunger = clamp100(P.hunger + hr * h);
  P.energy = clamp100(P.energy + er * h);
  P.fun = clamp100(P.fun + fr * h);
  P.love = clamp100(P.love + lr * h);

  int bad = (P.hunger < 15) + (P.energy < 8) + (P.crumbs > 10) + (P.fun < 10 && P.love < 10);
  if (bad) P.health = max(5.0f, P.health - 5.0f * bad * h);
  else if (!P.sick) P.health = min(100.0f, P.health + 3.0f * h);
  else P.health = max(5.0f, P.health - 1.5f * h);
  if (!P.sick && P.health < 40 && rnd() < (40 - P.health) / 40.0f * 0.002f * s) {
    P.sick = 1;
    P.sickCount++;
    state::diaryAdd(DI_SICK, 0);
    say("sick", nullptr, true);
    markDirty();
  }
  if (P.sick && P.health > 80 && P.hunger > 50 && P.energy > 50) {   // got better on its own
    P.sick = 0;
    state::diaryAdd(DI_CURED, 1);
    say("cured", nullptr, true);
  }

  // Sleep. On the schedule the pet sleeps from bedtime to wake-up time; run by hand it sleeps
  // when put to bed (or worn out) and stays asleep until woken.
  bool night = bedtimeNow();
  if (P.asleep) {
    bool wake;
    if (scheduled() && timeKnown) {
      if (night) nightSleep = true;                 // a nap that ran into bedtime
      wake = nightSleep ? !night : P.energy >= 90;  // morning, or rested after a nap
    } else if (P.lightsOff && !scheduled()) {       // put to bed by hand: only hunger or a very long sleep wake it
      wake = P.hunger < 12 || (millis() - sleptAt > (uint32_t)(14 * 3600000.0f / timeScale) && P.energy > 95);
    } else {
      // Worn out, or no clock to follow (a power cut, and no phone since): up once rested.
      wake = P.energy >= 99.5f || (millis() - sleptAt > (uint32_t)(10 * 3600000.0f / timeScale) && P.energy > 80);
    }
    if (wake) {
      if (nightSleep && timeKnown && !night && millis() - sleptAt > (uint32_t)(4 * 3600000.0f / timeScale)) {
        P.nights++;
        if (P.nights == 1) state::diaryAdd(DI_NIGHT, 0);
      }
      P.lightsOff = 0;
      wakeUp(true);
    }
  } else if (night && !games::active() && eatFood < 0 && (!stayUp || millis() - stayUp > 10 * 60000UL)) {
    P.lightsOff = 1;   // bedtime: lights out
    fallAsleep();
  } else if (P.lightsOff) {
    lightsDarkT += 1;
    if (lightsDarkT > (P.energy < 80 || night ? 4 : 60)) fallAsleep();
  } else if (P.energy < 10) {
    fallAsleep();
  }
  if (!night) stayUp = 0;
  int toBed = minutesToBedtime();
  if (toBed > 0 && toBed <= 10 && !P.asleep && !windDownSaid) {   // almost bedtime
    windDownSaid = true;
    say("bedtime", nullptr, true);
    static const uint8_t YAWN[] = {act::MV_STRETCH};
    if (!act::busy() && eatFood < 0 && !games::active()) act::play(YAWN, 1, act::TAG_REACT);
    sfx("yawn");
  } else if (toBed < 0 || toBed > 10) {
    windDownSaid = false;
  }

  if (grumpyT > 0) grumpyT = max(0.0f, grumpyT - 1);
  if (awaitT > 0 && (awaitT -= 1) <= 0) awaitTrick = -1;
  grow();
  wants();

  // Show off a mastered trick now and then when in a great mood.
  if (!P.asleep && !act::busy() && eatFood < 0 && moodScore() > 70 && rnd() < 1.0f / 400) {
    uint8_t best = 255;
    for (uint8_t i = 0; i < TRICK_SLOTS; i++) {
      const uint8_t *mv;
      uint8_t n;
      if (P.skill[i] >= 80 && trickMoves(i, mv, n) && (best == 255 || rnd() < 0.4f)) best = i;
    }
    if (best != 255) {
      const uint8_t *mv;
      uint8_t n;
      trickMoves(best, mv, n);
      act::play(mv, n, act::TAG_SHOWOFF, best);
      char b[6];
      snprintf(b, sizeof b, "%u", best);
      say("show_off", b);
    }
  }
  // Idle chatter for the phone's AI.
  static float chatT = 600;
  if (comms::anyone() && !P.asleep && (chatT -= 1) <= 0) {
    chatT = 480 + rnd() * 600;
    say("muse");
  }

  if (millis() - lastSave > SAVE_EVERY_S * 1000UL) {
    lastSave = millis();
    state::savePetNow();
  }
}

// ---- Drawing a life frame ---------------------------------------------------------------------------
static uint8_t emoteExpr() {
  switch (emote) {
    case EMO_HAPPY: return EX_HAPPY;
    case EMO_JOY: return EX_JOY;
    case EMO_SAD: return EX_SAD;
    case EMO_ANGRY: return EX_ANGRY;
    case EMO_SURPRISED: return EX_SURPRISED;
    case EMO_SCARED: return EX_SCARED;
    case EMO_SLEEPY: return EX_SLEEPY;
    case EMO_LOVE: return EX_LOVE;
    case EMO_STARS: return EX_STARS;
    case EMO_THINK: return EX_THINK;
    case EMO_SMUG: return EX_SMUG;
    case EMO_SILLY: return EX_HAPPY;
    case EMO_CRY: return EX_CRY;
    case EMO_WINK: return EX_WINK;
    default: return EX_NEUTRAL;
  }
}

static uint8_t baseExpr() {
  if (P.asleep) return EX_ASLEEP;
  if (dizzyT > 0) return EX_DIZZY;
  if (scaredT > 0) return EX_SCARED;
  if (yuckT > 0) return EX_YUCK;
  if (loveT > 0) return EX_LOVE;
  if (joyT > 0) return EX_JOY;
  if (surpriseT > 0) return EX_SURPRISED;
  if (emoteT > 0) return emoteExpr();
  if (petting) return petT > 2.5f ? EX_JOY : EX_HAPPY;
  if (grumpyT > 0) return EX_ANGRY;
  if (P.sick) return EX_SICK;
  if (imu::rocking()) return EX_CONTENT;
  if (P.lightsOff) return EX_SLEEPY;
  if (P.energy < 18 || (bedtimeNow() && P.energy < 45)) return EX_SLEEPY;
  if (P.hunger < 18) return EX_SAD;
  float m = moodScore();
  if (m > 80) return EX_HAPPY;
  if (m > 62) return EX_CONTENT;
  if (m > 42) return EX_NEUTRAL;
  if (m > 28) return EX_BORED;
  return EX_SAD;
}

static void idle(float dt, Pose &p) {
  // wandering gaze
  gazeHold -= dt;
  if (gazeHold <= 0) {
    if (rnd() < 0.35f) { gazeTX = 0; gazeTY = 0; }
    else { gazeTX = rnd() * 1.6f - 0.8f; gazeTY = rnd() * 1.0f - 0.5f; }
    if (p.expr == EX_SLEEPY || p.expr == EX_BORED) gazeTY = max(gazeTY, 0.3f);
    gazeHold = 0.8f + rnd() * 3.2f;
  }
  float k = min(1.0f, dt * 9);
  gazeX += (gazeTX - gazeX) * k;
  gazeY += (gazeTY - gazeY) * k;
  p.lookX = gazeX;
  p.lookY = gazeY;
  if (phoneLook) { p.lookX = phoneLX; p.lookY = phoneLY; }
  if (petting) { p.lookX = (petX - SCREEN_W / 2) / 50.0f; p.lookY = (petY - 30) / 40.0f; }
  if (listening()) {   // all ears: looks up at you and perks up as you speak
    p.lookX = gazeX * 0.15f;
    p.lookY = -0.3f;
    p.sy *= 1.04f + listen * 0.1f;
    p.dy -= 1 + listen * 2;
  }
  if (talk > 0.02f) { p.lookX *= 0.3f; p.lookY *= 0.3f; }
  // breathing
  p.dy += sinf(clk * 2.1f) * 0.7f;
  // fidgets
  if (act::busy() || eatFood >= 0 || P.asleep || petting || listening()) return;
  fidgetT -= dt;
  if (fidgetT > 0) return;
  fidgetT = 6 + rnd() * 10;
  static uint8_t seq[3];
  float m = moodScore();
  bool sleepy = P.energy < 30 || P.lightsOff;
  float r = rnd();
  if (sleepy && r < 0.5f) {
    seq[0] = act::MV_STRETCH; seq[1] = act::MV_BLINK;
    act::play(seq, 2, act::TAG_REACT);
    sfx("yawn");
  } else if (m > 70 && r < 0.3f) {
    seq[0] = hasTrait(TR_PLAYFUL) ? act::MV_BOUNCE : act::MV_HOP;
    act::play(seq, 1, act::TAG_REACT);
  } else if (m > 60 && r < 0.45f) {
    seq[0] = act::MV_NOTE;
    act::play(seq, 1, act::TAG_REACT);
  } else if (r < 0.6f) {
    seq[0] = act::MV_LOOK_L; seq[1] = act::MV_LOOK_R;
    act::play(seq, 2, act::TAG_REACT);
  } else if (r < 0.75f && hasTrait(TR_CURIOUS)) {
    seq[0] = act::MV_LOOK_U; seq[1] = act::MV_WIGGLE;
    act::play(seq, 2, act::TAG_REACT);
  } else if (m < 40 && r < 0.9f) {
    seq[0] = act::MV_LOOK_D;
    act::play(seq, 1, act::TAG_REACT);
  } else {
    seq[0] = act::MV_BLINK; seq[1] = act::MV_WOBBLE;
    act::play(seq, 2, act::TAG_REACT);
  }
}

static void timers(float dt) {
  auto dec = [dt](float &v) { if (v > 0) v = max(0.0f, v - dt); };
  dec(dizzyT); dec(scaredT); dec(joyT); dec(surpriseT); dec(loveT); dec(yuckT); dec(emoteT);
  if (petting) {
    petT += dt;
    heartT += dt;
    if (heartT > 0.9f) {
      heartT = 0;
      fx::spawn(fx::FX_HEART, petX + random(-6, 7), petY - 6, random(-4, 5), -14);
    }
    if (millis() - petMoveAt > 2500) petting = false;   // the phone went quiet
  }
  if (millis() - talkAt > 450) talk = max(0.0f, talk - dt * 4);
  if (hiding) hideT += dt;
  if (hiding && hideT > 6 && !P.asleep && (P.energy < 60 || bedtimeNow())) {   // tucked in face-down
    P.lightsOff = 1;
    fallAsleep();
  }
  if (imu::rocking()) {
    rockT += dt;
    if (!P.asleep && rockT > 8 && (P.energy < 55 || bedtimeNow())) fallAsleep();
  }
  if (previewing && millis() - previewAt > 90000) {   // abandoned preview: back to the saved look
    P.av = savedAv;
    previewing = false;
  }
}

static void drawLife(float dt) {
  Pose p;
  p.stage = P.stage;
  p.expr = baseExpr();
  p.talk = talk;
  p.dy -= talk * 1.5f;
  p.sy *= 1 + talk * 0.03f;
  p.blush = petting ? 0.8f : (loveT > 0 ? 0.6f : 0);
  p.dark = P.lightsOff && !P.asleep;
  p.hidden = hiding;
  if (imu::ok()) {
    p.swayX = constrain(-imu::swayX() * 1.5f + imu::tilt() * 0.12f, -0.6f, 0.6f);
    p.swayY = constrain(-imu::swayY() * 1.5f, -0.5f, 0.5f);
  }
  if (P.asleep) {
    p.dy += sinf(clk * 1.2f) * 1.0f;
    zzzT += dt;
    if (zzzT > 1.8f) {
      zzzT = 0;
      fx::spawn(fx::FX_ZZZ, face::cx() + 18, face::eyeY() - 6, 0, 0, 2.2f);
    }
    dreamT -= dt;
    if (dreamT < 0) {
      dreamT = 40 + rnd() * 60;
      static const uint8_t DREAMS[] = {fx::IC_FOOD, fx::IC_HEART, fx::IC_BALL};
      fx::icon(DREAMS[esp_random() % 3], 3);
    }
  } else {
    idle(dt, p);
  }
  updateEating(dt, p);
  act::update(dt, p);
  if (dizzyT > 0 && !act::busy()) p.dx += sinf(clk * 9) * 1.5f;
  face::update(dt, p);
  gfx::clear();
  fx::update(dt, imu::ok() ? imu::gx() : 0, imu::ok() ? imu::gy() : 1, imu::ok() ? imu::gz() : 0);
  fx::drawBack();
  face::draw(P.av, p);
  drawEating();
  fx::drawFront();

  int lostCrumbs = fx::takeLostCrumbs();
  if (lostCrumbs) {
    P.crumbs = max(0, (int)P.crumbs - lostCrumbs);
    if (P.crumbs == 0) {
      say("clean_thanks", "tilted", true);
      joyT = 0.8f;
    }
    markDirty();
  }
  if (P.crumbs > fx::crumbs() + 2 && fx::crumbs() < 24) fx::addCrumbs(rnd() * 100 + 14, 50, 1);   // show saved mess
}

// ---- Frame -------------------------------------------------------------------------------------------
void frame(float dt) {
  clk += dt;
  modeT += dt;
  for (uint8_t e; (e = imu::nextEvent()) != imu::EV_NONE;) motion(e);
  timers(dt);

  if (card) {
    cardT += dt;
    screens::connectCard(cardT, P.name, comms::bleConnected());
  } else {
    switch (mode) {
      case M_SPLASH:
        screens::splash(modeT);
        if (screens::splashDone(modeT)) {
          mode = P.stage == ST_EGG ? M_EGG : M_LIFE;
          modeT = 0;
          if (mode == M_LIFE) fx::setCrumbs(min<int>(P.crumbs, 24));
        }
        break;
      case M_EGG:
        screens::egg(modeT, min(1.0f, P.eggSec / 300.0f), !comms::anyone());
        break;
      case M_HATCH: {
        Pose p;
        p.stage = ST_BABY;
        face::update(dt, p);
        screens::hatch(modeT, P.av, p);
        if (screens::hatchDone(modeT)) {
          mode = M_LIFE;
          modeT = 0;
          P.stage = ST_BABY;
          P.ageSec = 0;
          P.bornEpoch = epochNow();
          P.hunger = 75;
          P.energy = 80;
          P.fun = 80;
          P.love = 70;
          state::diaryAdd(DI_HATCHED, 0);
          state::savePetNow();
          joyT = 1.5f;
          say("hatched", nullptr, true);
          sfx("tada");
          stateDirty = true;
        }
        break;
      }
      case M_LIFE:
        if (games::active()) {
          Pose p;
          p.stage = P.stage;
          face::update(dt, p);
          gfx::clear();
          games::update(dt);
          fx::update(dt, 0, 1, 0);
          games::draw(P.av, p);
          fx::drawFront();
          stateDirty = true;
        } else {
          drawLife(dt);
        }
        if (games::finished()) {
          games::collect();
          int sc = games::score();
          bool record = playing == GAME_CATCH && sc > (int)P.bestCatch;
          if (record) {
            P.bestCatch = sc;
            state::diaryAdd(DI_RECORD, min(sc, 255));
          }
          P.games++;
          addNeeds(-3, -6, games::won() ? 22 : 12, 3);
          String b = "\"e\":\"game\",\"g\":";
          b += playing;
          b += ",\"score\":";
          b += sc;
          b += ",\"won\":";
          b += games::won() ? 1 : 0;
          b += ",\"best\":";
          b += record ? 1 : 0;
          sendEvent(b);
          char s[8];
          snprintf(s, sizeof s, "%d", sc);
          say(record ? "record" : (games::won() ? "game_win" : "game_lose"), s, true);
          playing = 0;
        }
        break;
    }
  }

  // Dim the panel while sleeping.
  float target = (P.asleep && SET.sleepDim && !card && mode == M_LIFE) ? 6 : SET.contrast;
  if (fabsf(target - contrastNow) > 0.5f) {
    contrastNow += (target - contrastNow) * min(1.0f, dt * 2);
    gfx::setContrast((uint8_t)contrastNow);
  }
  screens::overlay(dt);
}

// ---- Messages from the phone -------------------------------------------------------------------------
static bool validAvatar(const uint8_t *d) {
  for (int i = 0; i < AVATAR_FIELDS; i++)
    if (d[i] >= AV_COUNTS[i]) return false;
  return true;
}

static void setName(const uint8_t *d, size_t n) {
  bool random = n >= 1 && d[0];
  if (random) randomName(P.name, sizeof P.name);
  else {
    size_t len = min(n - 1, (size_t)NAME_MAX);
    while (len > 0 && len < n - 1 && (d[1 + len] & 0xC0) == 0x80) len--;   // don't split a character
    size_t o = 0;
    for (size_t i = 0; i < len; i++)
      if (d[1 + i] >= 0x20) P.name[o++] = (char)d[1 + i];
    P.name[o] = 0;
    if (!o) randomName(P.name, sizeof P.name);
  }
  P.nameRandom = random;
  bleNameUpdate();
  state::diaryAdd(DI_RENAMED, 0);
  if (mode == M_LIFE) {
    joyT = 1.0f;
    say("new_name", P.name, true);
  }
  state::savePetNow();
  stateDirty = true;
}

static void setAvatar(const uint8_t *d, size_t n) {
  if (n < AVATAR_FIELDS + 1 || !validAvatar(d)) return;
  uint8_t how = d[AVATAR_FIELDS];   // 0 random look, 1 custom look, 2 preview, 3 cancel preview
  if (how == 3) {
    if (previewing) P.av = savedAv;
    previewing = false;
    stateDirty = true;
    return;
  }
  if (how == 2) {
    if (!previewing) savedAv = P.av;
    previewing = true;
    previewAt = millis();
    memcpy(&P.av, d, AVATAR_FIELDS);
    return;
  }
  memcpy(&P.av, d, AVATAR_FIELDS);
  P.avatarCustom = how == 1;
  previewing = false;
  state::diaryAdd(DI_NEWLOOK, 0);
  state::savePetNow();
  if (mode == M_LIFE) {
    static const uint8_t SHOW[] = {act::MV_SPARKLE, act::MV_TIPHAT};
    act::play(SHOW, 2, act::TAG_REACT);
    say("new_look", nullptr, true);
  }
  stateDirty = true;
}

static void setTrickDef(const uint8_t *d, size_t n) {
  if (n < 3) return;
  uint8_t slot = d[0];
  if (slot >= CUSTOM_TRICKS) return;
  uint8_t nl = d[1];
  if (2 + nl + 1 > n) return;
  CustomTrick &c = P.custom[slot];
  uint8_t cnt = d[2 + nl];
  if (cnt == 0) {   // delete
    memset(&c, 0, sizeof c);
    P.skill[BUILTIN_TRICKS + slot] = 0;
  } else {
    if (3 + nl + cnt > n || cnt > TRICK_MOVES_MAX) return;
    bool isNew = c.n == 0;
    size_t len = min<size_t>(nl, sizeof c.name - 1);
    while (len > 0 && len < nl && (d[2 + len] & 0xC0) == 0x80) len--;
    memcpy(c.name, d + 2, len);
    c.name[len] = 0;
    c.n = 0;
    for (uint8_t i = 0; i < cnt; i++)
      if (d[3 + nl + i] < act::MV_COUNT) c.moves[c.n++] = d[3 + nl + i];
    if (isNew) {
      P.skill[BUILTIN_TRICKS + slot] = 0;
      state::diaryAdd(DI_NEWTRICK, BUILTIN_TRICKS + slot);
    }
  }
  state::savePetNow();
  stateDirty = true;
}

static void applySetting(uint8_t key, uint16_t v) {
  switch (key) {
    case SET_CONTRAST: SET.contrast = min<uint16_t>(v, 255); contrastNow = SET.contrast; gfx::setContrast(SET.contrast); break;
    case SET_FLIP: SET.flip = v != 0; gfx::setFlip(SET.flip ^ (upside && SET.autoRotate)); break;
    case SET_AUTOROTATE: SET.autoRotate = v != 0; gfx::setFlip(SET.flip ^ (upside && SET.autoRotate)); break;
    case SET_BEDTIME: SET.bedtime = v % 1440; scheduleChanged(); break;
    case SET_WAKETIME: SET.waketime = v % 1440; scheduleChanged(); break;
    case SET_MANUALSLEEP: SET.manualSleep = v != 0; scheduleChanged(); break;
    case SET_BUBBLES: SET.bubbles = v != 0; break;
    case SET_SENS: SET.sens = min<uint16_t>(v, 4); imu::setSensitivity(SET.sens); break;
    case SET_SLEEPDIM: SET.sleepDim = v != 0; break;
    case SET_TIMESCALE: timeScale = constrain(v, 1, 3600); stateDirty = true; return;
    case SET_DRIVER:
      if (v <= 1 && v != SET.driver) {
        SET.driver = v;
        state::saveSettingsNow();
        state::savePetNow();
        screens::toast("Restarting...");
        rebootAt = millis() + 700;
      }
      return;
    default: return;
  }
  state::markSettings();
  stateDirty = true;
}

static void system(uint8_t a) {
  switch (a) {
    case SYS_REBOOT:
      state::savePetNow();
      state::saveSettingsNow();
      screens::toast("Restarting...");
      rebootAt = millis() + 600;
      break;
    case SYS_NEW_EGG:
      act::stop();
      games::quit();
      fx::clear();
      fx::setCrumbs(0);
      newEgg();
      bleNameUpdate();
      mode = M_EGG;
      modeT = 0;
      stateDirty = true;
      break;
    case SYS_FACTORY:
      state::factoryReset();
      screens::toast("Factory reset");
      rebootAt = millis() + 800;
      break;
    case SYS_HATCH:
      if (mode == M_EGG) {
        mode = M_HATCH;
        modeT = 0;
        sfx("hatch");
      }
      break;
    case SYS_CONNECT_CARD:
      toggleCard();
      break;
  }
}

static void calibrate(uint8_t step) {
  bool ok = false, done = false;
  if (step == CAL_UPRIGHT) { done = imu::captureUpright(); ok = true; }
  else if (step == CAL_FLAT) { done = imu::captureFlat(); ok = true; }
  else if (step == CAL_RESET) {
    imu::resetCalibration();
    SET.calibrated = 0;
    ok = true;
  }
  if (done) {
    imu::getMatrix(SET.calib);
    SET.calibrated = 1;
  }
  state::markSettings();
  String b = "\"e\":\"calib\",\"step\":";
  b += step;
  b += ",\"ok\":";
  b += ok ? 1 : 0;
  b += ",\"done\":";
  b += done ? 1 : 0;
  sendEvent(b);
  stateDirty = true;
}

static void emoteMsg(uint8_t e, uint8_t tenths) {
  if (e >= EMO_COUNT || P.asleep || mode != M_LIFE) return;
  emote = e;
  emoteT = constrain(tenths, 5, 60) / 10.0f;
  float x = face::cx(), y = face::eyeY();
  switch (e) {
    case EMO_LOVE: fx::spawn(fx::FX_HEART, x + 22, y - 10, 4, -12); break;
    case EMO_STARS: fx::burst(fx::FX_SPARKLE, x, y - 6, 4, 16); break;
    case EMO_ANGRY: fx::spawn(fx::FX_ANGER, x + 24, y - 14, 0, 0, emoteT); break;
    case EMO_SURPRISED: fx::spawn(fx::FX_EXCLAIM, x + 24, y - 12, 0, 0, 1); break;
    case EMO_THINK: fx::spawn(fx::FX_DOTS, x + 22, y - 18, 0, 0, emoteT); break;
    case EMO_CRY:
      fx::spawn(fx::FX_TEAR, face::eyeX(0), y + 8, -3, 10, 1.2f);
      fx::spawn(fx::FX_TEAR, face::eyeX(1), y + 8, 3, 10, 1.2f);
      break;
    case EMO_SILLY: {
      static const uint8_t S[] = {act::MV_CROSS};
      act::play(S, 1, act::TAG_REACT);
      break;
    }
    default: break;
  }
}

static void sendDiary(uint32_t cid) {
  String j = "{\"t\":\"diary\",\"items\":[";
  for (int i = 0; i < state::diaryCount(); i++) {
    const DiaryEntry &e = state::diaryAt(i);
    if (i) j += ',';
    j += '[';
    j += e.age;
    j += ',';
    j += e.type;
    j += ',';
    j += e.arg;
    j += ']';
  }
  j += "]}";
  comms::sendTo(cid, j);
}

static String stateJson();

void handle(uint32_t cid, const uint8_t *d, size_t n) {
  if (!n) return;
  uint8_t op = d[0];
  if (op != OP_PING && op != OP_HELLO && op != OP_FRAME_REQ && op != OP_SAY && op != OP_TALK && op != OP_EMOTE &&
      op != OP_DIARY)
    poke();
  switch (op) {
    case OP_HELLO:
      if (n >= 7) {
        uint32_t epoch = d[1] | (d[2] << 8) | (d[3] << 16) | ((uint32_t)d[4] << 24);
        int16_t tz = (int16_t)(d[5] | (d[6] << 8));
        if (epoch > 1700000000UL) setClock(epoch, tz);
        if (n >= 9) {
          uint8_t ul = d[8];
          if (ul > 8 && ul < sizeof SET.appUrl && 9 + ul <= n && !memcmp(d + 9, "https://", 8)) {
            char url[sizeof SET.appUrl];
            memcpy(url, d + 9, ul);
            url[ul] = 0;
            if (strcmp(url, SET.appUrl)) {
              strlcpy(SET.appUrl, url, sizeof SET.appUrl);
              screens::buildQr(SET.appUrl);
              state::markSettings();
            }
          }
        }
      }
      comms::sendTo(cid, stateJson());
      if (!(n >= 8 && (d[7] & HELLO_NO_MIRROR))) comms::requestFrame(cid, true);
      if (mode == M_LIFE && !P.asleep && (cid != comms::BLE_CLIENT || !phoneLeftAt || millis() - phoneLeftAt > BLIP_MS)) {
        joyT = 0.8f;
        say("greet", nullptr, true);
      }
      break;
    case OP_FRAME_REQ: comms::requestFrame(cid, n >= 2 && d[1]); break;
    case OP_FEED: if (n >= 2 && mode == M_LIFE) feed(d[1]); break;
    case OP_PET: if (n >= 4 && mode == M_LIFE) onPet(d[1], d[2], d[3]); break;
    case OP_CARE:
      if (n < 2 || mode != M_LIFE) break;
      switch (d[1]) {
        case CARE_CLEAN:
          if (P.crumbs || fx::crumbs()) {
            fx::sweepCrumbs();
            P.crumbs = 0;
            joyT = 1.0f;
            say("clean_thanks", nullptr, true);
            sfx("sweep");
            markDirty();
          }
          break;
        case CARE_MEDICINE:
          if (P.asleep) wakeUp(true);
          if (eatFood < 0) startEating(100);
          break;
        case CARE_LIGHTS_OFF:
          P.lightsOff = 1;
          lightsDarkT = 0;
          if (P.energy > 80 && !bedtimeNow() && !P.asleep) say("not_sleepy", nullptr, true);
          markDirty();
          break;
        case CARE_LIGHTS_ON:
          P.lightsOff = 0;
          if (P.asleep) wakeUp(P.energy > 60);
          markDirty();
          break;
        case CARE_BOOP: motion(imu::EV_TAP); break;
        case CARE_TICKLE: motion(imu::EV_DOUBLE_TAP); break;
        case CARE_SLEEP:   // tucked in by the owner: lights out, asleep right away
          P.lightsOff = 1;
          lightsDarkT = 0;
          if (!P.asleep && !games::active()) fallAsleep();
          markDirty();
          break;
        case CARE_WAKE:
          P.lightsOff = 0;
          if (P.asleep) wakeUp(P.energy > 25);
          markDirty();
          break;
      }
      break;
    case OP_TRICK:
      if (n < 3 || mode != M_LIFE) break;
      if (d[1] == TM_COMMAND) commandTrick(d[2]);
      else reward(d[1]);
      break;
    case OP_TRICK_DEF: setTrickDef(d + 1, n - 1); break;
    case OP_AVATAR: setAvatar(d + 1, n - 1); break;
    case OP_NAME: if (n >= 2) setName(d + 1, n - 1); break;
    case OP_SET: if (n >= 4) applySetting(d[1], d[2] | (d[3] << 8)); break;
    case OP_GAME:
      if (n < 3 || mode != M_LIFE) break;
      if (d[1] == GC_START) {
        if (P.asleep || P.sick || P.energy < 10) {
          say(P.asleep ? "asleep" : (P.sick ? "too_sick" : "too_tired"), "game", true);
          break;
        }
        act::stop();
        eatFood = -1;
        playing = d[2];
        games::start(d[2]);
      } else if (d[1] == GC_INPUT) games::input(d[2]);
      else games::quit();
      stateDirty = true;
      break;
    case OP_SAY:
      if (n >= 2 && SET.bubbles && mode == M_LIFE && !P.asleep && !games::active()) {
        char t[72];
        size_t len = min(n - 2, sizeof t - 1);
        while (len > 0 && len < n - 2 && (d[2 + len] & 0xC0) == 0x80) len--;
        memcpy(t, d + 2, len);
        t[len] = 0;
        if (d[1] & 1) fx::say(t, constrain(len * 0.09f, 1.8f, 7.0f));
      }
      break;
    case OP_TALK:
      if (n >= 2) {
        talk = d[1] / 255.0f;
        talkAt = millis();
      }
      break;
    case OP_EMOTE: if (n >= 3) emoteMsg(d[1], d[2]); break;
    case OP_SYS: if (n >= 2) system(d[1]); break;
    case OP_CALIB: if (n >= 2) calibrate(d[1]); break;
    case OP_LOOK:
      if (n >= 3) {
        int8_t x = (int8_t)d[1], y = (int8_t)d[2];
        phoneLook = x != -128;
        phoneLX = constrain(x / 100.0f, -1.0f, 1.0f);
        phoneLY = constrain(y / 100.0f, -1.0f, 1.0f);
      }
      break;
    case OP_DIARY: sendDiary(cid); break;
    case OP_TRAITS:
      if (n >= 3 && d[1] < TRAIT_COUNT && d[2] < TRAIT_COUNT && d[1] != d[2]) {
        P.trait[0] = d[1];
        P.trait[1] = d[2];
        markDirty();
      }
      break;
    case OP_PING: comms::sendTo(cid, "{\"t\":\"pong\"}"); break;
    case OP_LISTEN:
      if (n >= 2 && mode == M_LIFE) {
        if (d[1] && !listening() && !P.asleep) surpriseT = 0.25f;   // perks up
        listen = d[1] ? (d[1] - 1) / 254.0f : 0;
        listenAt = d[1] ? millis() | 1 : 0;
      }
      break;
  }
}

void connected(uint32_t cid, bool on) {
  Serial.printf("[link] %s %s\n", cid == comms::BLE_CLIENT ? "Bluetooth" : "USB", on ? "connected" : "disconnected");
  bool blip = cid == comms::BLE_CLIENT && phoneLeftAt && millis() - phoneLeftAt < BLIP_MS;
  if (on) {
    if (card) card = false;
    if (cid == comms::BLE_CLIENT) {
      byeAt = 0;
      if (!blip) screens::toast("Phone connected", 1300);
    }
  } else {
    if (cid == comms::BLE_CLIENT) phoneLeftAt = millis() | 1;
    petting = false;
    phoneLook = false;
    talk = 0;
    listenAt = 0;
    if (previewing && !comms::anyone()) {
      P.av = savedAv;
      previewing = false;
    }
    if (games::active() && !comms::anyone()) games::quit();
    if (cid == comms::BLE_CLIENT) byeAt = millis() + 5000;   // only if it doesn't come right back
  }
  stateDirty = true;
}

// ---- State for the phone ---------------------------------------------------------------------------------
static String stateJson() {
  String j;
  j.reserve(900);
  j += "{\"t\":\"state\",\"v\":\"" FW_VERSION "\",\"name\":";
  jsonStr(j, P.name);
  j += ",\"nr\":"; j += P.nameRandom;
  j += ",\"stage\":"; j += mode == M_EGG ? 0 : P.stage;
  j += ",\"hatching\":"; j += mode == M_HATCH ? 1 : 0;
  j += ",\"age\":"; j += P.ageSec;
  j += ",\"egg\":"; j += P.eggSec;
  j += ",\"n\":[";
  j += (int)lroundf(P.hunger); j += ','; j += (int)lroundf(P.energy); j += ',';
  j += (int)lroundf(P.fun); j += ','; j += (int)lroundf(P.love); j += ','; j += (int)lroundf(P.health);
  j += "],\"sick\":"; j += P.sick;
  j += ",\"sleep\":"; j += P.asleep;
  j += ",\"dark\":"; j += P.lightsOff;
  j += ",\"crumbs\":"; j += P.crumbs;
  j += ",\"mood\":\""; j += moodName();
  j += "\",\"want\":\""; j += WANT_NAMES[want];
  j += "\",\"av\":[";
  const uint8_t *a = (const uint8_t *)&P.av;
  for (int i = 0; i < AVATAR_FIELDS; i++) { if (i) j += ','; j += a[i]; }
  j += "],\"ac\":"; j += P.avatarCustom;
  j += ",\"tr\":["; j += P.trait[0]; j += ','; j += P.trait[1];
  j += "],\"fav\":"; j += P.favFood;
  j += ",\"hate\":"; j += P.hateFood;
  j += ",\"kf\":"; j += P.knownFav;
  j += ",\"sk\":[";
  for (int i = 0; i < TRICK_SLOTS; i++) { if (i) j += ','; j += P.skill[i]; }
  j += "],\"ct\":[";
  for (int i = 0; i < CUSTOM_TRICKS; i++) {
    if (i) j += ',';
    const CustomTrick &c = P.custom[i];
    if (!c.n) { j += "null"; continue; }
    j += '[';
    jsonStr(j, c.name);
    j += ",[";
    for (int k = 0; k < c.n; k++) { if (k) j += ','; j += c.moves[k]; }
    j += "]]";
  }
  j += "],\"set\":{\"c\":"; j += SET.contrast;
  j += ",\"f\":"; j += SET.flip;
  j += ",\"r\":"; j += SET.autoRotate;
  j += ",\"d\":"; j += SET.driver;
  j += ",\"b\":"; j += SET.bedtime;
  j += ",\"w\":"; j += SET.waketime;
  j += ",\"bu\":"; j += SET.bubbles;
  j += ",\"s\":"; j += SET.sens;
  j += ",\"sd\":"; j += SET.sleepDim;
  j += ",\"sm\":"; j += SET.manualSleep;
  j += ",\"cal\":"; j += SET.calibrated;
  j += ",\"ts\":"; j += (int)timeScale;
  j += "},\"st\":{\"fed\":"; j += P.fed;
  j += ",\"pet\":"; j += P.petted;
  j += ",\"sh\":"; j += P.shaken;
  j += ",\"tr\":"; j += P.tricksDone;
  j += ",\"g\":"; j += P.games;
  j += ",\"best\":"; j += P.bestCatch;
  j += ",\"sick\":"; j += P.sickCount;
  j += ",\"n\":"; j += P.nights;
  j += "},\"time\":"; j += timeKnown ? 1 : 0;
  j += ",\"night\":"; j += bedtimeNow() ? 1 : 0;
  j += ",\"busy\":\"";
  j += games::active() ? "game" : (eatFood >= 0 ? "eating" : (act::tag() == act::TAG_TRICK ? "trick" : ""));
  j += "\",\"game\":";
  j += games::active() ? games::stateJson() : String("null");
  j += ",\"url\":"; jsonStr(j, SET.appUrl);
  j += ",\"imu\":\""; j += imu::chipName();
  j += "\",\"mtu\":"; j += comms::bleMtu();
  j += ",\"heap\":"; j += ESP.getFreeHeap();
  j += ",\"lk\":"; comms::linkJson(j);
  j += '}';
  return j;
}

void periodic() {
  uint32_t now = millis();
  if (byeAt && (int32_t)(now - byeAt) > 0) {
    byeAt = 0;
    if (!comms::bleConnected() && mode == M_LIFE && !P.asleep) fx::say("Bye bye!", 1.5f);
  }
  // A phone app that went quiet (screen off) and came back: bring it up to date right away.
  static bool listening = false;
  if (comms::anyone() && !listening) stateDirty = true;
  listening = comms::anyone();
  bool fast = games::active();
  if (comms::anyone() && ((stateDirty && now - lastPush > (fast ? 250u : 400u)) || now - lastPush > 5000)) {
    stateDirty = false;
    lastPush = now;
    comms::send(stateJson());
  }
  if (rebootAt && (int32_t)(now - rebootAt) > 0) ESP.restart();
}

bool rebootDue() { return rebootAt != 0; }
const char *name() { return P.name; }

void boop() {
  if (card) return;
  if (mode == M_LIFE) motion(imu::EV_TAP);
  else if (mode == M_EGG) motion(imu::EV_TAP);
}

void toggleCard() {
  card = !card;
  cardT = 0;
  if (card) screens::buildQr(SET.appUrl);
  Serial.println(card ? "[card] connect card on" : "[card] connect card off");
}

// ---- Boot ------------------------------------------------------------------------------------------------
void begin(bool loaded) {
  if (!loaded) newEgg();
  if (SET.calibrated) imu::setMatrix(SET.calib);
  imu::setSensitivity(SET.sens);
  contrastNow = SET.contrast;
  gfx::setContrast(SET.contrast);
  gfx::setFlip(SET.flip);
  screens::buildQr(SET.appUrl);
  act::setSfxHook(sfx);
  games::setSfxHook(sfx);
  restoreClock();
  // Don't wake up into a sleep that's already done: rested, and not meant to be asleep now
  // (put to bed by hand, or the scheduled night by a clock that survived the restart).
  bool keepSleeping = SET.manualSleep ? P.lightsOff : bedtimeNow();
  if (P.asleep && P.energy > 90 && !keepSleeping) P.asleep = 0;
  if (P.asleep) {
    nightSleep = P.lightsOff && (SET.manualSleep || bedtimeNow());
    sleptAt = millis();
  }
  lastSave = millis();
  mode = M_SPLASH;
  modeT = 0;
  Serial.printf("[pet] %s, stage %u, age %lus, hunger %.0f energy %.0f fun %.0f love %.0f health %.0f\n", P.name,
                P.stage, (unsigned long)P.ageSec, P.hunger, P.energy, P.fun, P.love, P.health);
}

}  // namespace pet
