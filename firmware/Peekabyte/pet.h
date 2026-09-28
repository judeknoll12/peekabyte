#pragma once
// The pet itself: needs, moods, sleep, growing up, reactions, tricks - and
// deciding what goes on the screen every frame.
#include <Arduino.h>

namespace pet {

void begin(bool loaded);                                // loaded = a saved pet was found
const char *name();
void frame(float dt);                                   // update + draw one frame
void tick();                                            // 1 Hz simulation step
void handle(uint32_t cid, const uint8_t *d, size_t n);  // message from a phone
void connected(uint32_t cid, bool on);
void boop();                                            // BOOT button tap
void toggleCard();                                      // BOOT button hold
void periodic();                                        // state pushes to the phone
bool rebootDue();

}  // namespace pet
