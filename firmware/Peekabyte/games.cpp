#include "games.h"
#include "config.h"
#include "fx.h"
#include "gfx.h"
#include "imu.h"
#include "protocol.h"
#include "sprites.h"

namespace games {

static uint8_t game = 0;
static bool done = false, winFlag = false;
static int pts = 0, lives = 3, rounds = 0, roundNo = 0;
static float elapsed = 0, endT = -1;
static SfxHook sfx = nullptr;
static uint8_t held = 0;   // phone buttons

void setSfxHook(SfxHook h) { sfx = h; }
static void sound(const char *s) {
  if (sfx) sfx(s);
}

// ---- Snack Catch ---------------------------------------------------------------------
struct Item {
  bool used;
  uint8_t kind;   // 0 apple, 1 cookie, 2 star, 3 mine
  float x, y, vy;
};
static Item items[7];
static float px = 64, spawnT = 0, flash = 0;
static uint8_t flashKind = 0;   // 1 caught, 2 ouch
constexpr float CATCH_TIME = 30;

static void catchStart() {
  for (auto &i : items) i.used = false;
  px = 64;
  spawnT = 0.6f;
  lives = 3;
}

static void catchUpdate(float dt) {
  float target = px;
  if (imu::ok() && !held) target = 64 + imu::tilt() * 56;
  if (held == 1) target = px - 90 * dt;
  if (held == 2) target = px + 90 * dt;
  px += (constrain(target, 8.0f, 120.0f) - px) * min(1.0f, dt * 12);
  spawnT -= dt;
  if (spawnT <= 0) {
    spawnT = max(0.45f, 1.1f - elapsed * 0.02f) * (0.7f + (esp_random() % 60) / 100.0f);
    for (auto &i : items) {
      if (i.used) continue;
      int r = esp_random() % 100;
      uint8_t k = r < 22 ? 3 : (r < 30 ? 2 : (r < 65 ? 0 : 1));
      i = Item{true, k, (float)(10 + esp_random() % 108), -8, 16 + elapsed * 1.3f + (esp_random() % 10)};
      break;
    }
  }
  for (auto &i : items) {
    if (!i.used) continue;
    i.y += i.vy * dt;
    if (i.y > 50 && i.y < 60 && fabsf(i.x - px) < 10) {
      i.used = false;
      if (i.kind == 3) {
        lives--;
        flash = 0.5f;
        flashKind = 2;
        sound("ouch");
        fx::burst(fx::FX_STAR, px, 54, 5, 20);
      } else {
        pts += i.kind == 2 ? 3 : 1;
        flash = 0.35f;
        flashKind = 1;
        sound(i.kind == 2 ? "sparkle" : "catch");
        if (i.kind == 2) fx::burst(fx::FX_SPARKLE, px, 50, 4, 18);
      }
    } else if (i.y > SCREEN_H + 8) {
      i.used = false;
    }
  }
  if (flash > 0) flash -= dt;
  if (lives <= 0 || elapsed >= CATCH_TIME) {
    winFlag = pts >= 10;
    endT = 0;
    sound(winFlag ? "win" : "lose");
  }
}

static void catchDraw() {
  // HUD: score, time bar, lives
  char b[12];
  snprintf(b, sizeof b, "%d", pts);
  gfx::text(u8g2_font_6x10_tf, 2, 9, b);
  float left = max(0.0f, 1 - elapsed / CATCH_TIME);
  gfx::rect(30, 2, 60, 5, 1);
  gfx::fillRect(31, 3, (int)(58 * left), 3, 1);
  for (int i = 0; i < 3; i++)
    if (i < lives) gfx::sprite(SCREEN_W - 9 - i * 9, 1, spr::HEART, spr::HEART_H);
    else gfx::rect(SCREEN_W - 8 - i * 9, 3, 5, 3, 1);
  // falling things
  for (auto &i : items) {
    if (!i.used) continue;
    const char *const *s = i.kind == 0 ? spr::MINI_APPLE : i.kind == 1 ? spr::MINI_COOKIE : i.kind == 2 ? spr::MINI_STAR : spr::MINE;
    gfx::sprite(lroundf(i.x) - 3, lroundf(i.y) - 3, s, spr::MINI_H);
  }
  // the pet, shrunk to a pair of eyes with a wide-open mouth
  int x = lroundf(px);
  bool happy = flash > 0 && flashKind == 1, ouch = flash > 0 && flashKind == 2;
  for (int s = -1; s <= 1; s += 2) {
    int ex = x + s * 5;
    if (happy) gfx::bezier(ex - 3, 56, ex, 51, ex + 3, 56, 1.6f);
    else if (ouch) { gfx::line(ex - 2, 52, ex + 2, 56); gfx::line(ex - 2, 56, ex + 2, 52); }
    else { gfx::fillEllipse(ex, 54, 2.6f, 3.4f, 1); gfx::px(ex + (int)(imu::tilt() * 1.5f), 55, 0); }
  }
  bool near = false;
  for (auto &i : items)
    if (i.used && i.kind != 3 && i.y > 35 && fabsf(i.x - px) < 14) near = true;
  if (near) gfx::ellipse(x, 61, 3, 2.2f, 1, 1);
  else gfx::hline(x - 2, 61, 5, 1);
}

// ---- Which Way? -----------------------------------------------------------------------
enum WState : uint8_t { W_WAIT, W_REVEAL, W_RESULT };
static WState ws = W_WAIT;
static float wt = 0, tiltHold = 0;
static uint8_t guess = 0, answer = 0;

static void wayStart() {
  rounds = 5;
  roundNo = 1;
  ws = W_WAIT;
  wt = 0;
  guess = 0;
}

static void wayGuess(uint8_t g) {
  if (ws != W_WAIT || (g != 1 && g != 2)) return;
  guess = g;
  answer = (esp_random() & 1) ? 1 : 2;
  ws = W_REVEAL;
  wt = 0;
  sound("whoosh");
}

static void wayUpdate(float dt) {
  wt += dt;
  if (ws == W_WAIT) {
    float t = imu::ok() ? imu::tilt() : 0;
    if (fabsf(t) > 0.65f) tiltHold += dt;
    else tiltHold = 0;
    if (tiltHold > 0.35f) {
      tiltHold = 0;
      wayGuess(t < 0 ? 1 : 2);
    }
  } else if (ws == W_REVEAL && wt > 0.9f) {
    ws = W_RESULT;
    wt = 0;
    bool hit = guess == answer;
    if (hit) pts++;
    sound(hit ? "yay" : "nope");
    fx::say(hit ? "You got it!" : "Nope! Hehe", 1.1f);
  } else if (ws == W_RESULT && wt > 1.3f) {
    if (roundNo >= rounds) {
      winFlag = pts >= 3;
      endT = 0;
      sound(winFlag ? "win" : "lose");
    } else {
      roundNo++;
      ws = W_WAIT;
      wt = 0;
      guess = 0;
    }
  }
}

static void wayDraw(const Avatar &av, face::Pose p) {
  p.expr = face::EX_NEUTRAL;
  if (ws == W_WAIT) {
    p.lookX = sinf(wt * 3) * 0.25f;
    p.expr = face::EX_THINK;
  } else {
    float k = min(1.0f, wt / 0.25f);
    float dir = answer == 1 ? -1.0f : 1.0f;
    if (ws == W_REVEAL) {
      p.lookX = dir * k;
      p.dx = dir * 8 * k;
    } else {
      p.lookX = dir;
      p.dx = dir * 8;
      p.expr = guess == answer ? face::EX_STARS : face::EX_JOY;
    }
  }
  face::draw(av, p);
  char b[16];
  snprintf(b, sizeof b, "%d/%d", roundNo, rounds);
  gfx::text(u8g2_font_5x7_tf, 2, 7, b);
  snprintf(b, sizeof b, "%d", pts);
  gfx::text(u8g2_font_5x7_tf, SCREEN_W - 2 - gfx::textW(u8g2_font_5x7_tf, b), 7, b);
  if (ws == W_WAIT && ((int)(wt * 2) & 1)) {
    gfx::fillTri(2, 32, 7, 27, 7, 37, 1);
    gfx::fillTri(SCREEN_W - 3, 32, SCREEN_W - 8, 27, SCREEN_W - 8, 37, 1);
  }
  if (guess && ws != W_WAIT) {   // show what you picked
    int gx = guess == 1 ? 2 : SCREEN_W - 9;
    gfx::rect(gx, 56, 7, 7, 1);
    if (guess == 1) gfx::fillTri(gx + 1, 59, gx + 5, 57, gx + 5, 61, 1);
    else gfx::fillTri(gx + 5, 59, gx + 1, 57, gx + 1, 61, 1);
  }
}

// ---- Common ---------------------------------------------------------------------------
bool active() { return game != 0; }
uint8_t current() { return game; }

void start(uint8_t id) {
  if (id != GAME_CATCH && id != GAME_WHICHWAY) return;
  game = id;
  done = false;
  winFlag = false;
  pts = 0;
  elapsed = 0;
  endT = -1;
  held = 0;
  fx::clear();
  fx::hush();
  if (id == GAME_CATCH) catchStart();
  else wayStart();
  sound("start");
}

void input(uint8_t arg) {
  if (game == GAME_CATCH) held = arg <= 2 ? arg : 0;
  else if (game == GAME_WHICHWAY) wayGuess(arg);
}

void quit() {
  if (!game) return;
  game = 0;
  done = true;
  winFlag = false;
}

bool update(float dt) {
  if (!game) return false;
  if (endT >= 0) {
    endT += dt;
    if (endT > 2.0f) {
      game = 0;
      done = true;
      return false;
    }
    return true;
  }
  elapsed += dt;
  if (game == GAME_CATCH) catchUpdate(dt);
  else wayUpdate(dt);
  return true;
}

void draw(const Avatar &av, face::Pose pose) {
  if (game == GAME_CATCH) catchDraw();
  else wayDraw(av, pose);
  if (endT >= 0) {
    const char *msg = game == GAME_CATCH ? (lives <= 0 ? "Oops!" : "Time!") : (winFlag ? "You win!" : "I win!");
    int w = gfx::textW(u8g2_font_7x13B_tf, msg) + 14;
    gfx::fillRRect((SCREEN_W - w) / 2, 22, w, 20, 5, 0);
    gfx::rrect((SCREEN_W - w) / 2, 22, w, 20, 5, 1);
    gfx::textCenter(u8g2_font_7x13B_tf, SCREEN_W / 2, 36, msg);
  }
}

int score() { return pts; }
bool won() { return winFlag; }
bool finished() { return done; }
void collect() { done = false; }

String stateJson() {
  char b[140];
  snprintf(b, sizeof b, "{\"id\":%u,\"score\":%d,\"lives\":%d,\"round\":%d,\"rounds\":%d,\"t\":%.1f,\"over\":%d}", game,
           pts, lives, roundNo, rounds, elapsed, endT >= 0 ? 1 : 0);
  return String(b);
}

}  // namespace games
