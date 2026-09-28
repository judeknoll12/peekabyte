#include "fx.h"
#include "config.h"
#include "gfx.h"
#include "sprites.h"

namespace fx {

struct Part {
  uint8_t kind;
  bool used;
  float x, y, vx, vy, t, life;
};
static Part parts[40];

struct Crumb {
  float x, y, vx, vy;
  bool used, sweeping;
};
static Crumb cr[24];
static int lost = 0;

static char btxt[72];
static uint8_t bIcon = IC_NONE;
static float bT = -1, bDur = 0;

static float frand(float a, float b) { return a + (b - a) * (esp_random() & 0xFFFF) / 65535.0f; }

void spawn(uint8_t kind, float x, float y, float vx, float vy, float life) {
  Part *slot = nullptr;
  for (auto &p : parts)
    if (!p.used) { slot = &p; break; }
  if (!slot) {   // recycle the oldest
    slot = &parts[0];
    for (auto &p : parts)
      if (p.t / p.life > slot->t / slot->life) slot = &p;
  }
  *slot = Part{kind, true, x, y, vx, vy, 0, life};
}

void burst(uint8_t kind, float x, float y, int n, float speed) {
  for (int i = 0; i < n; i++) {
    float a = frand(0, 6.283f), s = frand(speed * 0.5f, speed);
    spawn(kind, x, y, cosf(a) * s, sinf(a) * s - speed * 0.3f, frand(0.6f, 1.1f));
  }
}

void clear() {
  for (auto &p : parts) p.used = false;
}

// ---- Crumbs ---------------------------------------------------------------------
void addCrumbs(float x, float y, int n) {
  for (int i = 0; i < n; i++) {
    for (auto &c : cr) {
      if (c.used) continue;
      c = Crumb{x + frand(-4, 4), y + frand(-1, 2), frand(-18, 18), frand(-20, 0), true, false};
      break;
    }
  }
}

int crumbs() {
  int n = 0;
  for (auto &c : cr) n += c.used;
  return n;
}

int takeLostCrumbs() {
  int n = lost;
  lost = 0;
  return n;
}

void sweepCrumbs() {
  for (auto &c : cr)
    if (c.used) {
      c.sweeping = true;
      c.vx = frand(90, 140);
      c.vy = frand(-30, -5);
    }
}

void setCrumbs(int n) {
  for (auto &c : cr) c.used = false;
  for (int i = 0; i < n && i < 24; i++)
    cr[i] = Crumb{frand(8, SCREEN_W - 8), (float)SCREEN_H - 1, 0, 0, true, false};
}

static void updateCrumbs(float dt, float gx, float gy, float gz) {
  // Lying flat there's no sideways gravity; pretend "down" is the bottom of the screen.
  float ax = gx, ay = gy;
  if (fabsf(gz) > 0.75f) ay = max(ay, 0.6f);
  for (auto &c : cr) {
    if (!c.used) continue;
    if (c.sweeping) {
      c.x += c.vx * dt;
      c.y += c.vy * dt;
      if (random(0, 6) == 0) spawn(FX_SPARKLE, c.x, c.y, 0, 0, 0.3f);
      if (c.x > SCREEN_W + 2 || c.y < -2) c.used = false;
      continue;
    }
    c.vx += ax * 220 * dt;
    c.vy += ay * 220 * dt;
    bool onFloor = c.y >= SCREEN_H - 1.01f && ay >= 0;
    if (onFloor) {
      if (fabsf(ax) < 0.18f) c.vx *= powf(0.02f, dt);   // friction holds them unless tilted
      else c.vx *= powf(0.4f, dt);
    }
    c.x += c.vx * dt;
    c.y += c.vy * dt;
    if (c.y > SCREEN_H - 1) {
      c.y = SCREEN_H - 1;
      c.vy = c.vy > 30 ? -c.vy * 0.3f : 0;
    }
    if (c.x < -2 || c.x > SCREEN_W + 1 || c.y < -2) {
      c.used = false;
      lost++;
    }
  }
}

// ---- Update / draw ----------------------------------------------------------------
void update(float dt, float gx, float gy, float gz) {
  for (auto &p : parts) {
    if (!p.used) continue;
    p.t += dt;
    if (p.t >= p.life) { p.used = false; continue; }
    switch (p.kind) {
      case FX_HEART:
      case FX_NOTE:
        p.vy -= 4 * dt;
        p.x += (p.vx + sinf(p.t * 6) * 6) * dt;
        p.y += p.vy * dt;
        break;
      case FX_ZZZ:
        p.x += 6 * dt;
        p.y -= 7 * dt;
        break;
      case FX_TEAR:
      case FX_SWEAT:
        p.vy += (p.kind == FX_TEAR ? 60 : 12) * dt;
        p.y += p.vy * dt;
        p.x += p.vx * dt;
        break;
      case FX_STAR:
      case FX_SPARKLE:
      case FX_POOF:
        p.vx *= powf(0.2f, dt);
        p.vy *= powf(0.2f, dt);
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        break;
      default:
        break;
    }
  }
  updateCrumbs(dt, gx, gy, gz);
  if (bT >= 0) {
    bT += dt;
    if (bT > bDur) bT = -1;
  }
}

static bool blinkOut(const Part &p) {   // flicker during the last 25% of life
  float r = p.t / p.life;
  return r > 0.75f && ((int)(p.t * 20) & 1);
}

static void drawPart(const Part &p) {
  if (blinkOut(p)) return;
  int x = lroundf(p.x), y = lroundf(p.y);
  switch (p.kind) {
    case FX_HEART: gfx::sprite(x - 3, y - 3, spr::HEART, spr::HEART_H); break;
    case FX_STAR: gfx::sprite(x - 2, y - 2, spr::STAR, spr::STAR_H); break;
    case FX_NOTE: gfx::sprite(x - 2, y - 3, spr::NOTE, spr::NOTE_H); break;
    case FX_SPARKLE:
      if (((int)(p.t * 12)) % 3 != 2) gfx::sprite(x - 2, y - 2, spr::SPARK, spr::SPARK_H);
      break;
    case FX_SWEAT:
    case FX_TEAR: gfx::sprite(x - 2, y - 3, spr::DROP, spr::DROP_H); break;
    case FX_ANGER:
      if (((int)(p.t * 5)) & 1) gfx::sprite(x - 3, y - 3, spr::ANGER, spr::ANGER_H);
      break;
    case FX_ZZZ: {
      const uint8_t *f = p.t < p.life * 0.35f ? u8g2_font_5x7_tf : (p.t < p.life * 0.7f ? u8g2_font_6x10_tf : u8g2_font_7x13B_tf);
      gfx::text(f, x, y, "z");
      break;
    }
    case FX_EXCLAIM:
    case FX_QUESTION: {
      float pop = p.t < 0.15f ? p.t / 0.15f : 1;
      int yy = y - (int)(pop * 3);
      gfx::fillRRect(x - 5, yy - 11, 10, 12, 3, 0);
      gfx::text(u8g2_font_7x13B_tf, x - 3, yy, p.kind == FX_EXCLAIM ? "!" : "?");
      break;
    }
    case FX_DOTS: {
      int n = min(3, 1 + (int)(p.t * 3) % 4);
      for (int i = 0; i < n; i++) gfx::fillRect(x + i * 4, y, 2, 2, 1);
      break;
    }
    case FX_POOF: {
      int r = 1 + (int)(p.t / p.life * 5);
      gfx::circle(x, y, r, 1);
      break;
    }
  }
}

void drawBack() {
  for (auto &c : cr)
    if (c.used) {
      int x = lroundf(c.x), y = lroundf(c.y);
      gfx::px(x, y, 1);
      if (!c.sweeping && ((x ^ y) & 1)) gfx::px(x + 1, y, 1);
    }
}

static void drawBubble() {
  if (bT < 0) return;
  float k = min(1.0f, bT / 0.15f);
  if (bT > bDur - 0.2f) k = max(0.0f, (bDur - bT) / 0.2f);
  if (k < 0.3f) return;
  if (bIcon != IC_NONE) {
    int w = 17, h = 15, x = SCREEN_W - w - 3, y = 1;
    gfx::fillRRect(x - 1, y - 1, w + 2, h + 2, 5, 0);
    gfx::rrect(x, y, w, h, 4, 1);
    gfx::fillTri(x + 2, y + h - 2, x + 7, y + h - 2, x - 1, y + h + 5, 0);
    gfx::line(x + 2, y + h - 1, x - 1, y + h + 4, 1);
    gfx::line(x + 7, y + h - 1, x - 1, y + h + 4, 1);
    int cx = x + w / 2, cy = y + h / 2;
    switch (bIcon) {
      case IC_FOOD: gfx::sprite(cx - 3, cy - 3, spr::MINI_APPLE, spr::MINI_H); break;
      case IC_HEART: gfx::sprite(cx - 3, cy - 3, spr::HEART, spr::HEART_H); break;
      case IC_BALL: gfx::sprite(cx - 3, cy - 3, spr::BALL, spr::BALL_H); break;
      case IC_SLEEP: gfx::text(u8g2_font_6x10_tf, cx - 5, cy + 4, "zZ"); break;
      case IC_PILL: gfx::fillRRect(cx - 5, cy - 2, 10, 5, 2, 1); gfx::vline(cx, cy - 2, 5, 0); break;
      case IC_BROOM: gfx::sprite(cx - 2, cy - 2, spr::SPARK, spr::SPARK_H); gfx::px(cx + 4, cy - 3); break;
    }
    return;
  }
  const uint8_t *font = u8g2_font_5x8_tf;
  int tw = gfx::textW(font, btxt);
  int maxW = SCREEN_W - 6;
  int w = min(tw + 8, maxW), h = 11, x = SCREEN_W - w - 2, y = 1;
  gfx::fillRRect(x - 1, y - 1, w + 2, h + 2, 4, 0);
  gfx::rrect(x, y, w, h, 3, 1);
  int tail = x + w / 3;
  gfx::fillTri(tail, y + h - 1, tail + 5, y + h - 1, tail - 2, y + h + 4, 0);
  gfx::line(tail, y + h - 1, tail - 2, y + h + 4, 1);
  gfx::line(tail + 5, y + h - 1, tail - 2, y + h + 4, 1);
  U8G2 &u = gfx::u8g2();
  u.setClipWindow(x + 2, y + 1, x + w - 2, y + h - 1);
  int tx = x + 4;
  if (tw > w - 8) {   // marquee
    float span = tw - (w - 8);
    float ph = fmodf(max(0.0f, bT - 0.6f) * 22, span + 30);
    tx -= (int)min(ph, span);
  }
  gfx::text(font, tx, y + 8, btxt);
  u.setMaxClipWindow();
}

void drawFront() {
  for (auto &p : parts)
    if (p.used) drawPart(p);
  drawBubble();
}

void say(const char *text, float seconds) {
  strlcpy(btxt, text, sizeof btxt);
  bIcon = IC_NONE;
  bT = 0;
  bDur = seconds;
}

void icon(uint8_t ic, float seconds) {
  bIcon = ic;
  btxt[0] = 0;
  bT = 0;
  bDur = seconds;
}

void hush() { bT = -1; }
bool talking() { return bT >= 0; }

}  // namespace fx
