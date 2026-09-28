#pragma once
// MPU6050 / MPU6500 motion sensing: gravity direction, shakes, taps, drops,
// flips, rocking and spins, all expressed in screen coordinates
// (x = right, y = down, z = into the screen).
#include <Arduino.h>

namespace imu {

enum Event : uint8_t {
  EV_NONE = 0,
  EV_TAP,
  EV_DOUBLE_TAP,
  EV_SHAKE,        // started shaking
  EV_BIG_SHAKE,    // long or violent shake (once per shake)
  EV_SHAKE_END,
  EV_FREEFALL,
  EV_LAND,
  EV_FACE_DOWN,
  EV_FACE_UP,
  EV_UPSIDE_DOWN,
  EV_UPRIGHT,
  EV_PICKUP,       // moved after lying still for a while
  EV_ROCK_START,   // gentle side-to-side rocking
  EV_ROCK_END,
  EV_SPIN,
};

bool begin();
bool ok();
const char *chipName();
void poll(uint32_t now);                  // call often; samples at 100 Hz

float gx();                               // gravity in screen space (unit vector)
float gy();
float gz();
float tilt();                             // left/right tilt, -1..1 (about 30 degrees = full)
float swayX();                            // quick motion in screen x/y, for googly pupils
float swayY();
float shakeLevel();                       // smoothed motion energy
bool shaking();
bool faceDown();
bool upsideDown();
bool rocking();
float stillSeconds();
float tempC();
uint8_t nextEvent();                      // EV_NONE when the queue is empty
void setSensitivity(uint8_t s);           // 0 (least) .. 4 (most), 2 = default

// Calibration: hold the pet upright (screen facing you), then lay it flat face-up.
bool captureUpright();
bool captureFlat();                       // returns true once both poses are captured
void resetCalibration();
void setMatrix(const float m[9]);
void getMatrix(float m[9]);

}  // namespace imu
