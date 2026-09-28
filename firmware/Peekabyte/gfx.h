#pragma once
// Display driver + clipped drawing primitives that write straight into the
// U8g2 page buffer (8 pages x 128 columns, bit 0 = top pixel of the page).
#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"

namespace gfx {

enum Driver : uint8_t { DRV_SSD1306 = 0, DRV_SH1106 = 1 };

bool begin(uint8_t driver);
bool present_ok();              // panel answered on I2C at boot
U8G2 &u8g2();
uint8_t *buf();                 // 1024-byte frame buffer

void clear();
void present();                 // push only the tiles that changed
void forceFull();               // resend everything on next present()
uint32_t version();             // bumps whenever the panel content changed
float fps();

void setContrast(uint8_t v);
void setFlip(bool on);
void setPower(bool on);

// Everything below is clipped to the screen and to the clip window.
void setClip(int x0, int y0, int x1, int y1);   // inclusive-exclusive: [x0,x1) x [y0,y1)
void resetClip();

// Colors: 0 = off, 1 = on, 2 = xor.
void px(int x, int y, uint8_t c = 1);
bool get(int x, int y);
void pxd(int x, int y, uint8_t level, uint8_t c = 1);   // ordered-dither pixel, level 0..16
void hline(int x, int y, int w, uint8_t c = 1);
void vline(int x, int y, int h, uint8_t c = 1);
void line(int x0, int y0, int x1, int y1, uint8_t c = 1);
void thickLine(float x0, float y0, float x1, float y1, float w, uint8_t c = 1);
void bezier(float x0, float y0, float cx, float cy, float x1, float y1, float w, uint8_t c = 1);
void rect(int x, int y, int w, int h, uint8_t c = 1);
void fillRect(int x, int y, int w, int h, uint8_t c = 1);
void fillRRect(int x, int y, int w, int h, int r, uint8_t c = 1);
void rrect(int x, int y, int w, int h, int r, uint8_t c = 1);
void disc(int cx, int cy, int r, uint8_t c = 1);
void circle(int cx, int cy, int r, uint8_t c = 1);
void fillEllipse(float cx, float cy, float rx, float ry, uint8_t c = 1);
void ellipse(float cx, float cy, float rx, float ry, float w, uint8_t c = 1);   // ring of thickness w
void ditherEllipse(float cx, float cy, float rx, float ry, uint8_t level);
void ditherRect(int x, int y, int w, int h, uint8_t level);
void fillTri(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c = 1);
void fadeMask(uint8_t level);   // dissolve the whole screen: keep pixels whose dither threshold < level

// ASCII sprites: '#' = on, 'o' = off (punches a hole), anything else = transparent.
// Rows are drawn top to bottom from (x, y); flip mirrors horizontally.
void sprite(int x, int y, const char *const *rows, int h, bool flip = false, uint8_t on = 1);
int spriteW(const char *const *rows);

// Text helpers on top of U8g2 (fonts are U8g2 font pointers).
int textW(const uint8_t *font, const char *s);
void text(const uint8_t *font, int x, int yBase, const char *s, uint8_t c = 1);
void textCenter(const uint8_t *font, int cx, int yBase, const char *s, uint8_t c = 1);

}  // namespace gfx
