#pragma once
#include <Arduino.h>

#define APP_NAME     "Peekabyte"
#define FW_VERSION   "1.2.0"

// ---- Hardware ---------------------------------------------------------------
#define PIN_I2C_SDA    21
#define PIN_I2C_SCL    22
#define PIN_BOOT_BTN   0            // on-board BOOT button, active low
#define OLED_I2C_ADDR  0x3C
#define I2C_HZ         800000UL     // the OLED and the MPU6050/6500 both cope with 800 kHz
#define MPU_I2C_ADDR   0x68
#define SCREEN_W       128
#define SCREEN_H       64

// ---- Timing -----------------------------------------------------------------
#define FRAME_MS       25           // 40 fps render target
#define IMU_MS         10           // 100 Hz motion sampling
#define SIM_MS         1000         // needs / mood simulation tick
#define SAVE_EVERY_S   300          // periodic save of the slowly drifting needs
#define MIRROR_MIN_MS  70           // fastest screen mirror rate over Bluetooth

// ---- Bluetooth --------------------------------------------------------------
// "7065656b6162" = "peekab"
#define BLE_SERVICE_UUID "f3a10001-5b1e-4c6b-9e0f-7065656b6162"
#define BLE_RX_UUID      "f3a10002-5b1e-4c6b-9e0f-7065656b6162"   // phone -> pet (write)
#define BLE_TX_UUID      "f3a10003-5b1e-4c6b-9e0f-7065656b6162"   // pet -> phone (notify)

// Shown as a QR code on the connect card until the app tells the pet its real address.
#define APP_URL_DEFAULT  "https://judeknoll12.github.io/peekabyte/"

// ---- Limits -----------------------------------------------------------------
#define NAME_MAX         16         // bytes of UTF-8 in the pet's name
#define CUSTOM_TRICKS    8
#define TRICK_MOVES_MAX  10
#define DIARY_LEN        32
