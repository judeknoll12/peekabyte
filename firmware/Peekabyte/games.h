#pragma once
// Mini-games: Snack Catch (tilt the pet to catch treats) and Which Way?
// (guess which way your pet will look).
#include <Arduino.h>
#include "face.h"
#include "petdata.h"

namespace games {

bool active();
uint8_t current();
void start(uint8_t id);
void input(uint8_t arg);               // phone buttons: 0 = release, 1 = left, 2 = right
void quit();
bool update(float dt);                 // false once the game is over
void draw(const Avatar &av, face::Pose pose);

int score();
bool won();
bool finished();                       // result ready to collect
void collect();                        // clears the finished flag
String stateJson();

typedef void (*SfxHook)(const char *name);
void setSfxHook(SfxHook h);

}  // namespace games
