#include "screens.h"
#include "config.h"
#include "fx.h"
#include "gfx.h"
#include "qrcode.h"   // ESP-IDF QR encoder bundled with the ESP32 core

namespace screens {

constexpr int QR_MAX = 37;
static uint8_t qr[QR_MAX * QR_MAX];
static int qrN = 0;
static char toastMsg[40];
static float toastT = -1, toastDur = 0;

static float smooth(float e0, float e1, float x) {
  float t = (x - e0) / (e1 - e0);
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  return t * t * (3 - 2 * t);
}

// ---- Splash: the name types itself, then a pair of eyes peeks over it ---------------
void splash(float t) {
  gfx::clear();
  const uint8_t *font = u8g2_font_helvB12_tf;
  const char *title = APP_NAME;
  int n = strlen(title);
  int shown = min(n, (int)(t / 0.07f));
  char part[16];
  memcpy(part, title, shown);
  part[shown] = 0;
  int w = gfx::textW(font, title);
  int x = (SCREEN_W - w) / 2, yb = 46;
  gfx::text(font, x, yb, part);
  if (t > 0.65f) {
    float up = smooth(0.65f, 1.0f, t) - smooth(1.9f, 2.2f, t);
    float look = t < 1.2f ? 0 : (t < 1.5f ? -0.8f : (t < 1.8f ? 0.8f : 0));
    bool blink = t > 1.55f && t < 1.63f;
    for (int s = -1; s <= 1; s += 2) {
      float ex = SCREEN_W / 2 + s * 9, ey = yb - 16 - up * 10 + 10;
      gfx::setClip(0, 0, SCREEN_W, yb - 14);
      if (blink) gfx::hline(lroundf(ex - 4), lroundf(ey), 9, 1);
      else {
        gfx::fillEllipse(ex, ey, 5, 6.5f, 1);
        gfx::fillEllipse(ex + look * 2, ey + 1, 2.2f, 2.2f, 0);
      }
      gfx::resetClip();
    }
  }
  float fade = 1 - smooth(2.2f, 2.5f, t);
  if (fade < 1) gfx::fadeMask((uint8_t)(fade * 16));
}

bool splashDone(float t) { return t >= 2.5f; }

// ---- The egg ---------------------------------------------------------------------------------
static void eggShape(float cx, float cy, float rx, float ry, float wob, uint8_t c) {
  // Egg = ellipse that's narrower on top; wobble leans the top sideways.
  for (int y = (int)(cy - ry); y <= (int)(cy + ry); y++) {
    float v = (y + 0.5f - cy) / ry;
    if (fabsf(v) >= 1) continue;
    float k = v < 0 ? 0.82f + 0.18f * (1 + v) : 1.0f;
    float hw = rx * sqrtf(1 - v * v) * k;
    float lean = wob * (cy + ry - y) * 0.18f;
    gfx::hline(lroundf(cx - hw + lean), y, lroundf(2 * hw), c);
  }
}

static void eggPattern(float cx, float cy, float ry, float wob) {
  // zig-zag band plus a few spots
  float y0 = cy - ry * 0.05f;
  for (int i = -4; i < 4; i++) {
    float x0 = cx + i * 4.5f, x1 = x0 + 4.5f;
    float lean = wob * (cy + ry - y0) * 0.18f;
    gfx::thickLine(x0 + lean, y0 + ((i & 1) ? 2 : -2), x1 + lean, y0 + ((i & 1) ? -2 : 2), 1.6f, 0);
  }
  float spots[4][3] = {{-7, -11, 2.2f}, {6, -14, 1.6f}, {-3, 11, 2}, {8, 8, 1.8f}};
  for (auto &s : spots) {
    float lean = wob * (cy + ry - (cy + s[1])) * 0.18f;
    gfx::fillEllipse(cx + s[0] + lean, cy + s[1], s[2], s[2], 0);
  }
}

static void cracks(float cx, float cy, float ry, float wob, float amount) {
  static const float C[][2] = {{-12, -4}, {-8, -9}, {-4, -3}, {0, -10}, {4, -4}, {8, -9}, {12, -3}};
  int n = (int)(amount * 7);
  for (int i = 0; i + 1 < min(n + 1, 7); i++) {
    float lean0 = wob * (cy + ry - (cy + C[i][1])) * 0.18f, lean1 = wob * (cy + ry - (cy + C[i + 1][1])) * 0.18f;
    gfx::thickLine(cx + C[i][0] + lean0, cy + C[i][1], cx + C[i + 1][0] + lean1, cy + C[i + 1][1], 1.5f, 0);
  }
}

void egg(float t, float crack, bool waiting) {
  gfx::clear();
  float cx = SCREEN_W / 2, cy = 29, rx = 15, ry = 19;
  float calm = sinf(t * 2.2f) * 0.25f;
  float jiggle = crack > 0.3f && fmodf(t, 2.4f) < 0.5f ? sinf(t * 40) * (0.4f + crack) : 0;
  float wob = calm + jiggle;
  eggShape(cx, cy, rx, ry, wob, 1);
  eggPattern(cx, cy, ry, wob);
  if (crack > 0.05f) cracks(cx, cy, ry, wob, crack);
  gfx::hline(cx - 12, cy + ry + 1, 25, 1);   // the ground
  if (((int)(t * 3)) % 5 == 0) gfx::px(cx + 20 + (int)(sinf(t) * 3), cy - 14, 1);
  if (waiting) {
    const char *m = ((int)(t / 3)) & 1 ? "Open the app" : "to hatch me!";
    gfx::textCenter(u8g2_font_5x7_tf, SCREEN_W / 2, SCREEN_H - 1, m);
  }
}

// ---- Hatching ---------------------------------------------------------------------------------
void hatch(float t, const Avatar &av, face::Pose p) {
  gfx::clear();
  float cx = SCREEN_W / 2, cy = 29, rx = 15, ry = 19;
  if (t < 0.9f) {
    float wob = sinf(t * 45) * (0.6f + t);
    eggShape(cx, cy, rx, ry, wob, 1);
    eggPattern(cx, cy, ry, wob);
    cracks(cx, cy, ry, wob, 1);
    return;
  }
  if (t < 1.05f) {   // flash
    gfx::fillRect(0, 0, SCREEN_W, SCREEN_H, 1);
    return;
  }
  // the pet appears inside the bottom shell, then the shell falls away
  float grow = smooth(1.05f, 2.2f, t);
  p.scale = 0.55f + 0.45f * grow;
  p.dy += (1 - grow) * 8;
  p.expr = t < 1.6f ? face::EX_SURPRISED : face::EX_JOY;
  p.closed = t > 1.3f && t < 1.4f;
  face::draw(av, p);
  float fall = smooth(1.5f, 2.3f, t);
  float by = cy + fall * 40;
  gfx::setClip(0, (int)by + 2, SCREEN_W, SCREEN_H);
  eggShape(cx, by, rx, ry, 0, 1);
  gfx::resetClip();
  for (int i = -2; i < 2; i++)   // jagged rim
    gfx::fillTri(cx + i * 8, by + 2, cx + i * 8 + 8, by + 2, cx + i * 8 + 4, by - 3, 1);
  float fly = smooth(1.05f, 1.8f, t);   // top shell flies off
  if (fly < 1) {
    float ty = cy - 8 - fly * 50, tx = cx + fly * 30;
    gfx::setClip(0, 0, SCREEN_W, (int)(ty + 4));
    eggShape(tx, ty + 10, rx, ry * 0.8f, fly * 2, 1);
    gfx::resetClip();
  }
}

bool hatchDone(float t) { return t >= 2.6f; }

// ---- Connect card --------------------------------------------------------------------------
static void qrCapture(esp_qrcode_handle_t h) {
  int n = esp_qrcode_get_size(h);
  if (n > QR_MAX) { qrN = 0; return; }
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) qr[y * n + x] = esp_qrcode_get_module(h, x, y) ? 1 : 0;
  qrN = n;
}

void buildQr(const char *url) {
  esp_qrcode_config_t cfg;
  cfg.display_func = qrCapture;
  cfg.max_qrcode_version = 5;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  qrN = 0;
  esp_qrcode_generate(&cfg, url);
}

void connectCard(float t, const char *bleName, bool connected) {
  gfx::clear();
  if (qrN > 0) {
    int sc = qrN * 2 <= 62 ? 2 : 1;
    int size = qrN * sc, off = (64 - size) / 2;
    gfx::fillRect(0, 0, 64, 64, 1);
    for (int y = 0; y < qrN; y++)
      for (int x = 0; x < qrN; x++)
        if (qr[y * qrN + x]) gfx::fillRect(off + x * sc, off + y * sc, sc, sc, 0);
  }
  const int tx = 68;
  gfx::text(u8g2_font_helvB08_tf, tx, 9, APP_NAME);
  for (int x = tx; x < 126; x += 2) gfx::px(x, 13, 1);
  if (connected) {
    gfx::text(u8g2_font_4x6_tf, tx, 22, "Phone connected!");
    gfx::text(u8g2_font_4x6_tf, tx, 32, "Hold BOOT to");
    gfx::text(u8g2_font_4x6_tf, tx, 39, "close this card.");
  } else {
    gfx::text(u8g2_font_4x6_tf, tx, 21, "Scan for the app,");
    gfx::text(u8g2_font_4x6_tf, tx, 28, "open it in Bluefy");
    gfx::text(u8g2_font_4x6_tf, tx, 35, "and tap Connect:");
  }
  U8G2 &u = gfx::u8g2();
  u.setClipWindow(tx, 40, SCREEN_W, 50);
  int sw = gfx::textW(u8g2_font_5x7_tf, bleName);
  int sx = tx;
  if (sw > SCREEN_W - tx - 1) sx = tx - (int)fmodf(t * 15, sw + 20) + (int)min(10.0f, t * 15);
  gfx::text(u8g2_font_5x7_tf, sx, 48, bleName);
  u.setMaxClipWindow();
  int dots = ((int)(t * 3)) % 4;
  for (int i = 0; i < dots; i++) gfx::fillRect(tx + i * 4, 58, 2, 2, 1);
}

// ---- Toast -------------------------------------------------------------------------------------
void toast(const char *msg, uint16_t ms) {
  strlcpy(toastMsg, msg, sizeof toastMsg);
  toastT = 0;
  toastDur = ms / 1000.0f;
}

void overlay(float dt) {
  if (toastT < 0) return;
  toastT += dt;
  if (toastT >= toastDur) { toastT = -1; return; }
  float k = min(smooth(0, 0.2f, toastT), 1 - smooth(toastDur - 0.2f, toastDur, toastT));
  int w = gfx::textW(u8g2_font_6x10_tf, toastMsg) + 14, h = 15;
  int x = (SCREEN_W - w) / 2, y = SCREEN_H - (int)(k * (h + 2) + 0.5f);
  gfx::fillRRect(x - 1, y - 1, w + 2, h + 2, 5, 0);
  gfx::rrect(x, y, w, h, 4, 1);
  gfx::text(u8g2_font_6x10_tf, x + 7, y + 11, toastMsg);
}

}  // namespace screens
