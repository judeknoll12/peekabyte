#pragma once
// Draws the pet: two expressive eyes plus whatever it's wearing.
#include <Arduino.h>
#include "petdata.h"

namespace face {

enum Expr : uint8_t {
  EX_NEUTRAL, EX_CONTENT, EX_HAPPY, EX_JOY, EX_SAD, EX_ANGRY, EX_SURPRISED, EX_SCARED, EX_SLEEPY, EX_ASLEEP,
  EX_LOVE, EX_STARS, EX_DIZZY, EX_KO, EX_SICK, EX_SQUINT, EX_CRY, EX_BORED, EX_THINK, EX_CHOMP, EX_SMUG,
  EX_WINK, EX_YUCK, EX_COUNT
};

struct Pose {
  float dx = 0, dy = 0;        // face offset in pixels
  float sx = 1, sy = 1;        // squash / stretch (negative sx mirrors, for turning around)
  float scale = 1;             // overall size
  float lookX = 0, lookY = 0;  // gaze, -1..1
  float swayX = 0, swayY = 0;  // googly push from motion
  float cross = 0;             // cross-eyed 0..1
  float talk = 0;              // voice level 0..1
  float hatLift = 0;           // raise the hat (tipping it)
  float blush = 0;             // extra blush 0..1
  uint8_t expr = EX_NEUTRAL;
  bool closed = false;         // hold both eyes shut (not asleep)
  bool hidden = false;         // off screen (peekaboo)
  bool dark = false;           // lights off: glowing outlines only
  uint8_t stage = ST_KID;
};

void update(float dt, const Pose &p);        // blink timer and eyelid springs
void draw(const Avatar &av, const Pose &p);

// Geometry of the last drawn frame, for placing effects around the face.
float cx();
float cy();
float eyeX(int i);   // screen x of eye i (0 = left)
float eyeY();
float eyeW();
float eyeH();
float mouthY();
float hatTop();

}  // namespace face
