#pragma once
// Moves are tiny animations (hop, spin, wink...). Tricks and reactions are
// sequences of moves, played one after another on top of the idle face.
#include <Arduino.h>
#include "face.h"

namespace act {

enum Move : uint8_t {
  MV_HOP, MV_SPIN, MV_SQUISH, MV_STRETCH, MV_LOOK_L, MV_LOOK_R, MV_LOOK_U, MV_LOOK_D,
  MV_BLINK, MV_WINK, MV_WIGGLE, MV_FLIP, MV_NO, MV_NOD, MV_HEARTS, MV_STARS,
  MV_SPARKLE, MV_NOTE, MV_TIPHAT, MV_HIDE, MV_BOUNCE, MV_GROW, MV_SHRINK, MV_CROSS,
  MV_ROLL, MV_DASH_L, MV_DASH_R, MV_JOY, MV_LOVE, MV_KO, MV_DIZZY, MV_WOBBLE,
  MV_COUNT
};

enum Tag : uint8_t { TAG_NONE, TAG_TRICK, TAG_FUMBLE, TAG_REACT, TAG_SHOWOFF };

typedef void (*SfxHook)(const char *name);
void setSfxHook(SfxHook h);

void play(const uint8_t *moves, uint8_t n, uint8_t tag, int8_t trick = -1);
void stop();
bool busy();
uint8_t tag();
int8_t trick();                  // trick being played, -1 if none
void update(float dt, face::Pose &p);

// Built-in tricks (ids match the app's list).
const uint8_t *builtinTrick(uint8_t id, uint8_t &n);
void fumble(uint8_t trickFirstMove);   // a failed attempt at a trick

}  // namespace act
