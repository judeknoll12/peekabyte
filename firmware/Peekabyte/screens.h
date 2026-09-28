#pragma once
// Full-screen moments: boot splash, the egg, hatching, and the connect card.
#include <Arduino.h>
#include "face.h"
#include "petdata.h"

namespace screens {

void splash(float t);
bool splashDone(float t);

void egg(float t, float crack, bool waiting);            // crack 0..1
void hatch(float t, const Avatar &av, face::Pose p);     // ~2.6 s
bool hatchDone(float t);

void buildQr(const char *url);
void connectCard(float t, const char *bleName, bool connected);

void toast(const char *msg, uint16_t ms = 1500);
void overlay(float dt);

}  // namespace screens
