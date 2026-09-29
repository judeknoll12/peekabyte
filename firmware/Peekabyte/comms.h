#pragma once
// Talking to the phone: Bluetooth LE (the real thing) and the USB serial bridge
// (for development). Both carry the same messages; see protocol.h.
#include <Arduino.h>

namespace comms {

constexpr uint32_t SERIAL_CLIENT = 1;
constexpr uint32_t BLE_CLIENT = 2;

typedef void (*MessageFn)(uint32_t cid, const uint8_t *d, size_t n);
typedef void (*ConnectFn)(uint32_t cid, bool connected);

void begin(const char *bleName, MessageFn onMessage, ConnectFn onConnect);
void poll();                              // runs the handlers; call from loop()
void periodic();

void send(const String &json);            // to every connected phone
void sendTo(uint32_t cid, const String &json);
void requestFrame(uint32_t cid, bool keyframe);
void pushFrames();                        // call right after gfx::present()

bool bleConnected();                      // a Bluetooth link is up
bool serialActive();
bool anyone();                            // someone is listening: the USB bridge, or a phone app that's awake
uint16_t bleMtu();
void setName(const char *bleName);        // takes effect when advertising restarts
void linkJson(String &j);                 // link health for the phone's Connection page

}  // namespace comms
