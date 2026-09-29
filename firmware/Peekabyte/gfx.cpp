#include "gfx.h"
#include <Wire.h>

namespace gfx {

static U8G2 *dev = nullptr;
static uint8_t *B = nullptr;
static uint8_t shadow[SCREEN_W * SCREEN_H / 8];
static bool fullRefresh = true;
static bool powered = true;
static bool panelOk = false;
static bool ssd1306 = true;
static bool flipped = false;
static uint8_t contrastVal = 0xCF;
static uint32_t ver = 1;
static uint32_t frames = 0, fpsT0 = 0;
static float fpsVal = 0;
static int cx0 = 0, cy0 = 0, cx1 = SCREEN_W, cy1 = SCREEN_H;   // clip window

static const uint8_t BAYER4[4][4] = {
  {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

bool begin(uint8_t driver) {
  if (driver == DRV_SH1106)
    dev = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
  else
    dev = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
  ssd1306 = driver != DRV_SH1106;
  dev->setI2CAddress(OLED_I2C_ADDR << 1);
  dev->setBusClock(I2C_HZ);
  dev->begin();
  Wire.beginTransmission(OLED_I2C_ADDR);
  panelOk = Wire.endTransmission() == 0;
  B = dev->getBufferPtr();
  dev->clearBuffer();
  dev->setFontMode(1);
  memset(shadow, 0, sizeof shadow);
  fullRefresh = true;
  return panelOk;
}

bool present_ok() { return panelOk; }
U8G2 &u8g2() { return *dev; }
uint8_t *buf() { return B; }
void clear() { memset(B, 0, sizeof shadow); }
void forceFull() { fullRefresh = true; }
uint32_t version() { return ver; }
float fps() { return fpsVal; }

void present() {
  frames++;
  uint32_t now = millis();
  if (now - fpsT0 >= 1000) {
    fpsVal = frames * 1000.0f / (now - fpsT0);
    frames = 0;
    fpsT0 = now;
  }
  bool changed = false;
  for (int page = 0; page < SCREEN_H / 8; page++) {
    const uint8_t *a = B + page * SCREEN_W;
    const uint8_t *b = shadow + page * SCREEN_W;
    int t0 = -1, t1 = -1;
    if (fullRefresh) {
      t0 = 0;
      t1 = SCREEN_W / 8 - 1;
    } else {
      for (int t = 0; t < SCREEN_W / 8; t++) {
        if (memcmp(a + t * 8, b + t * 8, 8) != 0) {
          if (t0 < 0) t0 = t;
          t1 = t;
        }
      }
    }
    if (t0 >= 0) {
      changed = true;
      if (powered) dev->updateDisplayArea(t0, page, t1 - t0 + 1, 1);
    }
  }
  if (changed) {
    memcpy(shadow, B, sizeof shadow);
    ver++;
  }
  fullRefresh = false;
}

void setContrast(uint8_t v) {
  contrastVal = v;
  dev->setContrast(v);
}
void setFlip(bool on) {
  flipped = on;
  dev->setFlipMode(on ? 1 : 0);
  fullRefresh = true;
}
void setPower(bool on) {
  powered = on;
  dev->setPowerSave(on ? 0 : 1);
  if (on) fullRefresh = true;
}

// A dip in the supply (a weak battery, a loose wire) can reset the screen's controller, which then
// sits dark, since "display off" is its power-up state, while the pet carries on. Re-sending its
// settings every few seconds brings it back. Same settings as U8g2's own start-up, minus the
// "display off" it begins with, so this never blanks the screen.
void revive() {
  if (!dev || !ssd1306) return;
  static const uint8_t SEQ[][2] = {
    {0xD5, 0x80}, {0xA8, 0x3F}, {0xD3, 0x00}, {0x8D, 0x14}, {0x20, 0x00}, {0xDA, 0x12}, {0xD9, 0xF1}, {0xDB, 0x40}};
  u8x8_t *u = dev->getU8x8();
  u8x8_cad_StartTransfer(u);
  for (const auto &c : SEQ) {
    u8x8_cad_SendCmd(u, c[0]);
    u8x8_cad_SendArg(u, c[1]);
  }
  u8x8_cad_SendCmd(u, 0x40);                   // start line 0
  u8x8_cad_SendCmd(u, flipped ? 0xA0 : 0xA1);  // segment remap
  u8x8_cad_SendCmd(u, flipped ? 0xC0 : 0xC8);  // scan direction
  u8x8_cad_SendCmd(u, 0x81);
  u8x8_cad_SendArg(u, contrastVal);
  u8x8_cad_SendCmd(u, 0x2E);                   // no scrolling
  u8x8_cad_SendCmd(u, 0xA4);                   // show RAM
  u8x8_cad_SendCmd(u, 0xA6);                   // not inverted
  if (powered) u8x8_cad_SendCmd(u, 0xAF);      // display on
  u8x8_cad_EndTransfer(u);
  fullRefresh = true;                          // its memory may be gone too
}

void setClip(int x0, int y0, int x1, int y1) {
  cx0 = max(0, x0);
  cy0 = max(0, y0);
  cx1 = min(SCREEN_W, x1);
  cy1 = min(SCREEN_H, y1);
}

void resetClip() {
  cx0 = cy0 = 0;
  cx1 = SCREEN_W;
  cy1 = SCREEN_H;
}

// ---------------------------------------------------------------------------
void px(int x, int y, uint8_t c) {
  if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1) return;
  uint8_t *p = B + ((y >> 3) * SCREEN_W) + x;
  uint8_t m = 1u << (y & 7);
  if (c == 1) *p |= m;
  else if (c == 0) *p &= ~m;
  else *p ^= m;
}

bool get(int x, int y) {
  if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H) return false;
  return B[(y >> 3) * SCREEN_W + x] & (1u << (y & 7));
}

void pxd(int x, int y, uint8_t level, uint8_t c) {
  if (level >= 16 || (level > 0 && BAYER4[y & 3][x & 3] < level)) px(x, y, c);
}

void hline(int x, int y, int w, uint8_t c) {
  if (y < cy0 || y >= cy1 || w <= 0) return;
  int x0 = x < cx0 ? cx0 : x;
  int x1 = x + w > cx1 ? cx1 : x + w;
  if (x0 >= x1) return;
  uint8_t *p = B + (y >> 3) * SCREEN_W;
  uint8_t m = 1u << (y & 7);
  for (int i = x0; i < x1; i++) {
    if (c == 1) p[i] |= m;
    else if (c == 0) p[i] &= ~m;
    else p[i] ^= m;
  }
}

void vline(int x, int y, int h, uint8_t c) {
  if (x < cx0 || x >= cx1 || h <= 0) return;
  int y0 = y < cy0 ? cy0 : y;
  int y1 = y + h > cy1 ? cy1 : y + h;
  for (int i = y0; i < y1; i++) px(x, i, c);
}

static bool lineOutside(int x0, int y0, int x1, int y1) {
  return (x0 < 0 && x1 < 0) || (x0 >= SCREEN_W && x1 >= SCREEN_W) ||
         (y0 < 0 && y1 < 0) || (y0 >= SCREEN_H && y1 >= SCREEN_H) ||
         abs(x1 - x0) > 2048 || abs(y1 - y0) > 2048;
}

void line(int x0, int y0, int x1, int y1, uint8_t c) {
  if (lineOutside(x0, y0, x1, y1)) return;
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// A line of width w: stamps small discs (w >= 2) or a plain line (w < 1.5).
void thickLine(float x0, float y0, float x1, float y1, float w, uint8_t c) {
  if (w < 1.5f) {
    line(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), c);
    return;
  }
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int steps = max(1, (int)(len * 1.5f));
  float r = w / 2.0f;
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;
    fillEllipse(x0 + dx * t, y0 + dy * t, r, r, c);
  }
}

void bezier(float x0, float y0, float qx, float qy, float x1, float y1, float w, uint8_t c) {
  float len = fabsf(qx - x0) + fabsf(qy - y0) + fabsf(x1 - qx) + fabsf(y1 - qy);
  int steps = max(2, (int)(len / 2.0f));
  float px0 = x0, py0 = y0;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / steps, u = 1 - t;
    float x = u * u * x0 + 2 * u * t * qx + t * t * x1;
    float y = u * u * y0 + 2 * u * t * qy + t * t * y1;
    thickLine(px0, py0, x, y, w, c);
    px0 = x;
    py0 = y;
  }
}

void rect(int x, int y, int w, int h, uint8_t c) {
  if (w <= 0 || h <= 0) return;
  hline(x, y, w, c);
  if (h > 1) hline(x, y + h - 1, w, c);
  if (h > 2) {
    vline(x, y + 1, h - 2, c);
    if (w > 1) vline(x + w - 1, y + 1, h - 2, c);
  }
}

void fillRect(int x, int y, int w, int h, uint8_t c) {
  for (int j = 0; j < h; j++) hline(x, y + j, w, c);
}

// Horizontal inset of a rounded-corner row `j` (0 = first row) for radius r.
static int cornerInset(int j, int h, int r) {
  int dy = 0;
  if (j < r) dy = r - j;
  else if (j >= h - r) dy = j - (h - 1 - r);
  if (dy <= 0) return 0;
  float fy = dy - 0.5f;
  float s = (float)r * r - fy * fy;
  return r - (int)(sqrtf(s > 0 ? s : 0) + 0.5f);
}

void fillRRect(int x, int y, int w, int h, int r, uint8_t c) {
  if (w <= 0 || h <= 0) return;
  int lim = (w < h ? w : h) / 2;
  if (r > lim) r = lim;
  if (r < 0) r = 0;
  for (int j = 0; j < h; j++) {
    int in = r ? cornerInset(j, h, r) : 0;
    hline(x + in, y + j, w - 2 * in, c);
  }
}

// One or more quarter arcs of a midpoint circle. quad bits: 1 TL, 2 TR, 4 BR, 8 BL.
static void cornerArc(int cx, int cy, int r, uint8_t quad, uint8_t c) {
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  while (x <= y) {
    if (quad & 1) { px(cx - y, cy - x, c); px(cx - x, cy - y, c); }
    if (quad & 2) { px(cx + x, cy - y, c); px(cx + y, cy - x, c); }
    if (quad & 4) { px(cx + x, cy + y, c); px(cx + y, cy + x, c); }
    if (quad & 8) { px(cx - y, cy + x, c); px(cx - x, cy + y, c); }
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++;
    ddx += 2;
    f += ddx;
  }
}

void rrect(int x, int y, int w, int h, int r, uint8_t c) {
  if (w <= 0 || h <= 0) return;
  int lim = ((w < h ? w : h) - 1) / 2;
  if (r > lim) r = lim;
  if (r < 0) r = 0;
  hline(x + r, y, w - 2 * r, c);
  hline(x + r, y + h - 1, w - 2 * r, c);
  vline(x, y + r, h - 2 * r, c);
  vline(x + w - 1, y + r, h - 2 * r, c);
  if (r > 0) {
    cornerArc(x + r, y + r, r, 1, c);
    cornerArc(x + w - 1 - r, y + r, r, 2, c);
    cornerArc(x + w - 1 - r, y + h - 1 - r, r, 4, c);
    cornerArc(x + r, y + h - 1 - r, r, 8, c);
  }
}

void disc(int cx, int cy, int r, uint8_t c) {
  if (r <= 0) { px(cx, cy, c); return; }
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    hline(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}

void circle(int cx, int cy, int r, uint8_t c) {
  if (r <= 0) { px(cx, cy, c); return; }
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    px(cx + x, cy + y, c); px(cx - x, cy + y, c);
    px(cx + x, cy - y, c); px(cx - x, cy - y, c);
    if (x != y) {
      px(cx + y, cy + x, c); px(cx - y, cy + x, c);
      px(cx + y, cy - x, c); px(cx - y, cy - x, c);
    }
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

// Scanline ellipse with sub-pixel centre/radii so shapes scale smoothly.
void fillEllipse(float cx, float cy, float rx, float ry, uint8_t c) {
  if (rx < 0.5f || ry < 0.5f) {
    px(lroundf(cx), lroundf(cy), c);
    return;
  }
  int y0 = (int)ceilf(cy - ry - 0.5f), y1 = (int)floorf(cy + ry - 0.5f);
  for (int y = y0; y <= y1; y++) {
    float fy = (y + 0.5f - cy) / ry;
    float s = 1 - fy * fy;
    if (s <= 0) continue;
    float hw = rx * sqrtf(s);
    int xa = (int)ceilf(cx - hw - 0.5f), xb = (int)floorf(cx + hw - 0.5f);
    hline(xa, y, xb - xa + 1, c);
  }
}

void ellipse(float cx, float cy, float rx, float ry, float w, uint8_t c) {
  int y0 = (int)ceilf(cy - ry - 0.5f), y1 = (int)floorf(cy + ry - 0.5f);
  float irx = rx - w, iry = ry - w;
  for (int y = y0; y <= y1; y++) {
    float fy = (y + 0.5f - cy) / ry;
    float s = 1 - fy * fy;
    if (s <= 0) continue;
    float hw = rx * sqrtf(s);
    int xa = (int)ceilf(cx - hw - 0.5f), xb = (int)floorf(cx + hw - 0.5f);
    float hi = -1;
    if (irx > 0 && iry > 0) {
      float gy = (y + 0.5f - cy) / iry;
      float si = 1 - gy * gy;
      if (si > 0) hi = irx * sqrtf(si);
    }
    if (hi < 0) {
      hline(xa, y, xb - xa + 1, c);
    } else {
      int ia = (int)ceilf(cx - hi - 0.5f), ib = (int)floorf(cx + hi - 0.5f);
      hline(xa, y, ia - xa, c);
      hline(ib + 1, y, xb - ib, c);
    }
  }
}

void ditherEllipse(float cx, float cy, float rx, float ry, uint8_t level) {
  int y0 = (int)ceilf(cy - ry - 0.5f), y1 = (int)floorf(cy + ry - 0.5f);
  for (int y = y0; y <= y1; y++) {
    float fy = (y + 0.5f - cy) / ry;
    float s = 1 - fy * fy;
    if (s <= 0) continue;
    float hw = rx * sqrtf(s);
    int xa = (int)ceilf(cx - hw - 0.5f), xb = (int)floorf(cx + hw - 0.5f);
    for (int x = xa; x <= xb; x++) pxd(x, y, level);
  }
}

void ditherRect(int x, int y, int w, int h, uint8_t level) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) pxd(i, j, level);
}

void fillTri(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c) {
  if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
  if (y1 > y2) { std::swap(y1, y2); std::swap(x1, x2); }
  if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
  if (y2 == y0) {
    int a = min(x0, min(x1, x2)), b = max(x0, max(x1, x2));
    hline(a, y0, b - a + 1, c);
    return;
  }
  int ys = y0 < 0 ? 0 : y0, ye = y2 >= SCREEN_H ? SCREEN_H - 1 : y2;
  for (int y = ys; y <= ye; y++) {
    float t = (float)(y - y0) / (y2 - y0);
    float xa = x0 + (x2 - x0) * t, xb;
    if (y < y1) xb = (y1 == y0) ? x1 : x0 + (x1 - x0) * (float)(y - y0) / (y1 - y0);
    else xb = (y2 == y1) ? x1 : x1 + (x2 - x1) * (float)(y - y1) / (y2 - y1);
    if (xa > xb) std::swap(xa, xb);
    int ia = (int)lroundf(xa), ib = (int)lroundf(xb);
    hline(ia, y, ib - ia + 1, c);
  }
}

void fadeMask(uint8_t level) {
  if (level >= 16) return;
  for (int j = 0; j < SCREEN_H; j++)
    for (int i = 0; i < SCREEN_W; i++)
      if (BAYER4[j & 3][i & 3] >= level) px(i, j, 0);
}

void sprite(int x, int y, const char *const *rows, int h, bool flip, uint8_t on) {
  for (int j = 0; j < h; j++) {
    const char *r = rows[j];
    int w = strlen(r);
    for (int i = 0; i < w; i++) {
      char ch = r[flip ? w - 1 - i : i];
      if (ch == '#') px(x + i, y + j, on);
      else if (ch == 'o') px(x + i, y + j, on ? 0 : 1);
    }
  }
}

int spriteW(const char *const *rows) { return strlen(rows[0]); }

int textW(const uint8_t *font, const char *s) {
  dev->setFont(font);
  return dev->getUTF8Width(s);
}

void text(const uint8_t *font, int x, int yBase, const char *s, uint8_t c) {
  dev->setFont(font);
  dev->setFontMode(1);
  dev->setDrawColor(c);
  dev->drawUTF8(x, yBase, s);
  dev->setDrawColor(1);
}

void textCenter(const uint8_t *font, int cx, int yBase, const char *s, uint8_t c) {
  text(font, cx - textW(font, s) / 2, yBase, s, c);
}

}  // namespace gfx
