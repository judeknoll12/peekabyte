#pragma once
// Phone <-> pet message format. Keep in sync with app/js/protocol.js.
//
// Phone -> pet: binary, first byte is the op.
// Pet -> phone: JSON text ("{...}") or a binary screen mirror frame (0x81 / 0x82).
#include <Arduino.h>

enum Op : uint8_t {
  OP_HELLO = 0x01,       // u32 epoch (s), i16 utc offset (min), u8 flags, u8 url len, url
  OP_FRAME_REQ = 0x02,   // u8 1 = send a full keyframe
  OP_FEED = 0x03,        // u8 food
  OP_PET = 0x04,         // u8 phase (0 start, 1 move, 2 end), u8 x, u8 y (screen pixels)
  OP_CARE = 0x05,        // u8 CareAction
  OP_TRICK = 0x06,       // u8 TrickMode, u8 trick id
  OP_TRICK_DEF = 0x07,   // u8 slot, u8 name len, name, u8 move count, moves
  OP_AVATAR = 0x08,      // AVATAR_FIELDS bytes, u8 1 = custom look (0 = back to random)
  OP_NAME = 0x09,        // u8 1 = random name, UTF-8 name (ignored when random)
  OP_SET = 0x0A,         // u8 SetKey, u16 value (little endian)
  OP_GAME = 0x0B,        // u8 GameCmd, u8 arg
  OP_SAY = 0x0C,         // u8 flags (1 = show bubble), UTF-8 text the phone is speaking
  OP_TALK = 0x0D,        // u8 voice level 0..255 while the phone speaks (0 = done)
  OP_EMOTE = 0x0E,       // u8 Emote, u8 duration in tenths of a second
  OP_SYS = 0x0F,         // u8 SysAction
  OP_CALIB = 0x10,       // u8 CalibStep
  OP_LOOK = 0x11,        // i8 x, i8 y in -100..100; x = -128 lets go
  OP_DIARY = 0x12,       // ask for the diary
  OP_TRAITS = 0x13,      // u8 trait, u8 trait
  OP_PING = 0x14,
};

enum CareAction : uint8_t {
  CARE_CLEAN = 1,
  CARE_MEDICINE = 2,
  CARE_LIGHTS_OFF = 3,
  CARE_LIGHTS_ON = 4,
  CARE_BOOP = 5,
  CARE_TICKLE = 6,
};

enum TrickMode : uint8_t { TM_COMMAND = 0, TM_TREAT = 1, TM_PRAISE = 2 };

enum SetKey : uint8_t {
  SET_CONTRAST = 1,
  SET_FLIP = 2,
  SET_AUTOROTATE = 3,
  SET_DRIVER = 4,
  SET_BEDTIME = 5,      // minutes after midnight
  SET_WAKETIME = 6,
  SET_BUBBLES = 7,      // speech bubbles on the OLED
  SET_SENS = 8,         // motion sensitivity 0..4
  SET_SLEEPDIM = 9,
  SET_TIMESCALE = 10,   // developer: needs run this many times faster
  SET_HARDCORE = 11,    // reserved
};

enum GameCmd : uint8_t { GC_START = 1, GC_INPUT = 2, GC_QUIT = 3 };
enum GameId : uint8_t { GAME_CATCH = 1, GAME_WHICHWAY = 2 };

enum SysAction : uint8_t {
  SYS_REBOOT = 1,
  SYS_NEW_EGG = 2,
  SYS_FACTORY = 3,
  SYS_HATCH = 4,
  SYS_CONNECT_CARD = 5,
};

enum CalibStep : uint8_t { CAL_UPRIGHT = 1, CAL_FLAT = 2, CAL_RESET = 3 };

// Expressions the phone (or its AI) can ask for.
enum Emote : uint8_t {
  EMO_NONE = 0, EMO_HAPPY, EMO_JOY, EMO_SAD, EMO_ANGRY, EMO_SURPRISED, EMO_SCARED,
  EMO_SLEEPY, EMO_LOVE, EMO_STARS, EMO_THINK, EMO_SMUG, EMO_SILLY, EMO_CRY, EMO_WINK,
  EMO_COUNT
};

#define MSG_KEYFRAME 0x81
#define MSG_DELTA    0x82
