#pragma once
// Little particles around the pet (hearts, Zzz, stars...), crumbs that slide
// around with gravity, and speech / thought bubbles.
#include <Arduino.h>

namespace fx {

enum Kind : uint8_t {
  FX_HEART, FX_STAR, FX_NOTE, FX_ZZZ, FX_SWEAT, FX_TEAR, FX_ANGER, FX_SPARKLE, FX_EXCLAIM, FX_QUESTION,
  FX_DOTS, FX_POOF
};

void spawn(uint8_t kind, float x, float y, float vx = 0, float vy = 0, float life = 1.4f);
void burst(uint8_t kind, float x, float y, int n, float speed = 22);
void clear();                       // particles only

// Crumbs: persistent until they slide off the screen or get swept.
void addCrumbs(float x, float y, int n);
int crumbs();
int takeLostCrumbs();               // crumbs that left the screen since the last call
void sweepCrumbs();                 // tidy-up animation that removes them all
void setCrumbs(int n);              // restore after boot

void update(float dt, float gx, float gy, float gz);
void drawBack();                    // behind the face
void drawFront();                   // in front of the face

// Speech bubble with a line of text (scrolls when long) or a single icon.
enum Icon : uint8_t { IC_NONE, IC_FOOD, IC_HEART, IC_BALL, IC_SLEEP, IC_PILL, IC_BROOM };
void say(const char *text, float seconds);
void icon(uint8_t ic, float seconds);
void hush();
bool talking();

}  // namespace fx
