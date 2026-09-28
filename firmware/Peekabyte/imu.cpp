#include "imu.h"
#include <Wire.h>
#include "config.h"

namespace imu {

static bool present = false, is6500 = false;
static uint32_t lastSample = 0;

// chip -> screen rotation (rows are the screen x, y, z axes in chip coordinates).
// Default: sensor lying flat under the screen, chip X to the right, chip Y up.
static const float M_DEFAULT[9] = {1, 0, 0, 0, -1, 0, 0, 0, -1};
static float M[9];

static float bias[3] = {0, 0, 0};                 // gyro bias, dps
static float g[3] = {0, 0, 1};                    // gravity estimate, chip coords, toward earth
static float gs[3] = {0, 0, 1};                   // same, screen coords
static float lin[3], ws[3];                       // linear accel (g) and rotation (dps), screen coords
static float aPrev[3];
static float shakeLvl = 0, sway[2] = {0, 0};
static float tempVal = 0;
static float sens = 1.0f;

static bool isShaking = false, bigShakeSent = false;
static float shakeOnT = 0, shakeOffT = 0, shakeDur = 0;
static bool inFreefall = false;
static int freefallN = 0;
static uint32_t freefallStart = 0;
static bool isFaceDown = false, isUpside = false;
static float faceDownT = 0, faceUpT = 0, upsideT = 0, uprightT = 0;
static float stillT = 0;
static float quietT = 0;
static uint32_t tapSpikeAt = 0, lastTapAt = 0;
static bool tapPending = false;
static float rot[3] = {0, 0, 0};                  // decaying integrated rotation, degrees
static float rockBase = 0, rockPeak = 0;
static int rockSign = 0, rockCount = 0;
static uint32_t rockLastFlip = 0;
static bool isRocking = false;

static uint8_t evq[16];
static uint8_t evHead = 0, evTail = 0;

static float capUp[3], capFlat[3];
static bool haveUp = false, haveFlat = false;

static void push(uint8_t e) {
  uint8_t n = (evHead + 1) & 15;
  if (n == evTail) return;   // full: drop
  evq[evHead] = e;
  evHead = n;
}

uint8_t nextEvent() {
  if (evHead == evTail) return EV_NONE;
  uint8_t e = evq[evTail];
  evTail = (evTail + 1) & 15;
  return e;
}

static bool wr(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(MPU_I2C_ADDR);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static int rd(uint8_t reg, uint8_t *buf, int n) {
  Wire.beginTransmission(MPU_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  int got = Wire.requestFrom((int)MPU_I2C_ADDR, n);
  for (int i = 0; i < got; i++) buf[i] = Wire.read();
  return got;
}

static bool readRaw(float a[3], float w[3], float *t) {
  uint8_t b[14];
  if (rd(0x3B, b, 14) != 14) return false;
  int16_t v[7];
  for (int k = 0; k < 7; k++) v[k] = (int16_t)((b[2 * k] << 8) | b[2 * k + 1]);
  for (int k = 0; k < 3; k++) {
    a[k] = v[k] / 8192.0f;          // +-4 g
    w[k] = v[4 + k] / 65.5f;        // +-500 dps
  }
  *t = is6500 ? v[3] / 333.87f + 21.0f : v[3] / 340.0f + 36.53f;
  return true;
}

static void toScreen(const float c[3], float s[3]) {
  for (int r = 0; r < 3; r++) s[r] = M[r * 3] * c[0] + M[r * 3 + 1] * c[1] + M[r * 3 + 2] * c[2];
}

static float norm3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

bool begin() {
  memcpy(M, M_DEFAULT, sizeof M);
  uint8_t who = 0;
  if (rd(0x75, &who, 1) != 1) return false;
  if (who != 0x68 && who != 0x70 && who != 0x71 && who != 0x73 && who != 0x98 && who != 0x72) {
    Serial.printf("[imu] unexpected WHO_AM_I 0x%02X\n", who);
  }
  is6500 = who != 0x68;
  wr(0x6B, 0x80);   // reset
  delay(100);
  wr(0x6B, 0x01);   // wake, PLL on X gyro
  wr(0x1A, 0x03);   // DLPF ~44 Hz
  wr(0x19, 0x09);   // 100 Hz sample rate
  wr(0x1B, 0x08);   // gyro +-500 dps
  wr(0x1C, 0x08);   // accel +-4 g
  if (is6500) wr(0x1D, 0x03);   // accel DLPF ~44 Hz (6500 only)
  delay(50);

  // Gyro bias: average while (hopefully) lying still at power-up.
  float a[3], w[3], t, sum[3] = {0, 0, 0}, acc[3] = {0, 0, 0};
  int n = 0;
  for (int i = 0; i < 60; i++) {
    if (readRaw(a, w, &t)) {
      for (int k = 0; k < 3; k++) { sum[k] += w[k]; acc[k] += a[k]; }
      n++;
    }
    delay(4);
  }
  if (n == 0) return false;
  for (int k = 0; k < 3; k++) {
    bias[k] = sum[k] / n;
    g[k] = -acc[k] / n;
    aPrev[k] = acc[k] / n;
  }
  float gn = norm3(g);
  if (gn > 0.2f) for (int k = 0; k < 3; k++) g[k] /= gn;
  toScreen(g, gs);
  present = true;
  Serial.printf("[imu] %s ready (WHO_AM_I 0x%02X), gravity (%.2f %.2f %.2f)\n", chipName(), who, gs[0], gs[1],
                gs[2]);
  return true;
}

bool ok() { return present; }
const char *chipName() { return !present ? "none" : (is6500 ? "MPU6500" : "MPU6050"); }

static void sample(float dt, uint32_t now) {
  float a[3], w[3], t;
  if (!readRaw(a, w, &t)) return;
  tempVal = tempVal == 0 ? t : tempVal + (t - tempVal) * 0.01f;
  for (int k = 0; k < 3; k++) w[k] -= bias[k];
  float amag = norm3(a);

  // Gravity: low-pass of -a, trusting the accelerometer less while it's being thrown around.
  float alpha = fabsf(amag - 1.0f) < 0.12f ? 0.05f : 0.008f;
  float down[3] = {-a[0], -a[1], -a[2]};
  for (int k = 0; k < 3; k++) g[k] += alpha * (down[k] - g[k]);
  float gn = norm3(g);
  if (gn > 0.2f) for (int k = 0; k < 3; k++) g[k] /= gn;
  float linc[3] = {down[0] - g[0], down[1] - g[1], down[2] - g[2]};
  toScreen(g, gs);
  toScreen(linc, lin);
  toScreen(w, ws);

  // Slowly re-learn the gyro bias while resting.
  float wmag = norm3(w);
  float linMag = norm3(linc);
  if (stillT > 2.0f && wmag < 3.0f)
    for (int k = 0; k < 3; k++) bias[k] += 0.002f * w[k];

  // Motion energy and googly-eye sway.
  float e = linMag + wmag / 500.0f;
  float k = e > shakeLvl ? 0.35f : 0.06f;
  shakeLvl += k * (e - shakeLvl);
  sway[0] += 0.3f * (lin[0] - sway[0]);
  sway[1] += 0.3f * (lin[1] - sway[1]);

  // Shakes.
  float thr = 0.33f * sens;
  if (!isShaking) {
    shakeOnT = shakeLvl > thr ? shakeOnT + dt : 0;
    if (shakeOnT > 0.25f) {
      isShaking = true;
      bigShakeSent = false;
      shakeDur = 0;
      shakeOffT = 0;
      push(EV_SHAKE);
    }
  } else {
    shakeDur += dt;
    if (!bigShakeSent && (shakeDur > 1.3f || shakeLvl > 1.9f * sens)) {
      bigShakeSent = true;
      push(EV_BIG_SHAKE);
    }
    shakeOffT = shakeLvl < thr * 0.6f ? shakeOffT + dt : 0;
    if (shakeOffT > 0.4f) {
      isShaking = false;
      shakeOnT = 0;
      push(EV_SHAKE_END);
    }
  }

  // Free fall and landing.
  if (!inFreefall) {
    freefallN = amag < 0.35f ? freefallN + 1 : 0;
    if (freefallN >= 5) {
      inFreefall = true;
      freefallStart = now;
      push(EV_FREEFALL);
    }
  } else if (amag > 1.5f || now - freefallStart > 1500) {
    inFreefall = false;
    freefallN = 0;
    push(EV_LAND);
  }

  // Taps: a sharp jolt out of calm that settles right away.
  float jerk = sqrtf((a[0] - aPrev[0]) * (a[0] - aPrev[0]) + (a[1] - aPrev[1]) * (a[1] - aPrev[1]) +
                     (a[2] - aPrev[2]) * (a[2] - aPrev[2]));
  memcpy(aPrev, a, sizeof aPrev);
  quietT = (linMag < 0.08f && wmag < 25) ? quietT + dt : 0;
  if (!tapPending && !isShaking && !inFreefall && jerk > 0.45f * sens && now - tapSpikeAt > 120) {
    tapPending = true;
    tapSpikeAt = now;
  }
  if (tapPending && now - tapSpikeAt > 160) {
    tapPending = false;
    if (!isShaking && shakeLvl < thr) {
      if (lastTapAt && now - lastTapAt < 500) {
        push(EV_DOUBLE_TAP);
        lastTapAt = 0;
      } else {
        push(EV_TAP);
        lastTapAt = now;
      }
    }
  }

  // Face down / up.
  if (!isFaceDown) {
    faceDownT = gs[2] < -0.75f ? faceDownT + dt : 0;
    if (faceDownT > 0.4f) { isFaceDown = true; push(EV_FACE_DOWN); }
  } else {
    faceUpT = gs[2] > -0.45f ? faceUpT + dt : 0;
    if (faceUpT > 0.15f) { isFaceDown = false; faceUpT = 0; push(EV_FACE_UP); }
  }

  // Upside down (only meaningful while the screen is fairly upright).
  if (fabsf(gs[2]) < 0.85f) {
    if (!isUpside) {
      upsideT = gs[1] < -0.5f ? upsideT + dt : 0;
      if (upsideT > 0.4f) { isUpside = true; uprightT = 0; push(EV_UPSIDE_DOWN); }
    } else {
      uprightT = gs[1] > 0.3f ? uprightT + dt : 0;
      if (uprightT > 0.4f) { isUpside = false; upsideT = 0; push(EV_UPRIGHT); }
    }
  }

  // Picked up after resting.
  bool still = shakeLvl < 0.045f && wmag < 5.0f;
  if (still) stillT += dt;
  else {
    if (stillT > 6.0f && shakeLvl > 0.1f) push(EV_PICKUP);
    if (shakeLvl > 0.1f) stillT = 0;
  }

  // Spins: rotation integrated with a short memory.
  for (int i = 0; i < 3; i++) {
    rot[i] = rot[i] * 0.985f + ws[i] * dt;
    if (fabsf(rot[i]) > 300.0f) {
      push(EV_SPIN);
      rot[0] = rot[1] = rot[2] = 0;
      break;
    }
  }

  // Rocking: slow side-to-side oscillation of the left/right tilt.
  rockBase += 0.01f * (gs[0] - rockBase);
  float hp = gs[0] - rockBase;
  if (fabsf(hp) > fabsf(rockPeak)) rockPeak = hp;
  int sgn = hp > 0.03f ? 1 : (hp < -0.03f ? -1 : 0);
  if (sgn != 0 && sgn != rockSign) {
    uint32_t half = now - rockLastFlip;
    bool good = rockSign != 0 && half > 250 && half < 1800 && fabsf(rockPeak) > 0.07f && fabsf(rockPeak) < 0.7f &&
                !isShaking;
    rockCount = good ? rockCount + 1 : 0;
    rockSign = sgn;
    rockLastFlip = now;
    rockPeak = 0;
    if (!isRocking && rockCount >= 4) { isRocking = true; push(EV_ROCK_START); }
  }
  if (isRocking && (now - rockLastFlip > 2500 || isShaking)) {
    isRocking = false;
    rockCount = 0;
    push(EV_ROCK_END);
  }
}

void poll(uint32_t now) {
  if (!present) return;
  if (now - lastSample < IMU_MS) return;
  float dt = lastSample ? min(0.05f, (now - lastSample) / 1000.0f) : IMU_MS / 1000.0f;
  lastSample = now;
  sample(dt, now);
}

float gx() { return gs[0]; }
float gy() { return gs[1]; }
float gz() { return gs[2]; }
float tilt() { return constrain(gs[0] / 0.5f, -1.0f, 1.0f); }
float swayX() { return sway[0]; }
float swayY() { return sway[1]; }
float shakeLevel() { return shakeLvl; }
bool shaking() { return isShaking; }
bool faceDown() { return isFaceDown; }
bool upsideDown() { return isUpside; }
bool rocking() { return isRocking; }
float stillSeconds() { return stillT; }
float tempC() { return tempVal; }

void setSensitivity(uint8_t s) {
  static const float K[5] = {1.6f, 1.25f, 1.0f, 0.8f, 0.62f};
  sens = K[s > 4 ? 2 : s];
}

// ---------------------------------------------------------------- calibration --
static void cross(const float a[3], const float b[3], float o[3]) {
  o[0] = a[1] * b[2] - a[2] * b[1];
  o[1] = a[2] * b[0] - a[0] * b[2];
  o[2] = a[0] * b[1] - a[1] * b[0];
}

static bool solve() {
  float y[3], z[3], x[3];
  memcpy(y, capUp, sizeof y);
  float d = capFlat[0] * y[0] + capFlat[1] * y[1] + capFlat[2] * y[2];
  for (int k = 0; k < 3; k++) z[k] = capFlat[k] - d * y[k];
  float zn = norm3(z);
  if (zn < 0.5f) return false;   // the two poses were too similar
  for (int k = 0; k < 3; k++) z[k] /= zn;
  cross(y, z, x);
  for (int k = 0; k < 3; k++) {
    M[k] = x[k];
    M[3 + k] = y[k];
    M[6 + k] = z[k];
  }
  return true;
}

bool captureUpright() {
  memcpy(capUp, g, sizeof capUp);
  haveUp = true;
  return haveFlat ? solve() : false;
}

bool captureFlat() {
  memcpy(capFlat, g, sizeof capFlat);
  haveFlat = true;
  return haveUp ? solve() : false;
}

void resetCalibration() {
  memcpy(M, M_DEFAULT, sizeof M);
  haveUp = haveFlat = false;
}

void setMatrix(const float m[9]) { memcpy(M, m, sizeof M); }
void getMatrix(float m[9]) { memcpy(m, M, sizeof M); }

}  // namespace imu
