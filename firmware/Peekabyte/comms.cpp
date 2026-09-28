#include "comms.h"
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "config.h"
#include "gfx.h"
#include "protocol.h"
#include "mbedtls/base64.h"

namespace comms {

static MessageFn onMsg = nullptr;
static ConnectFn onConn = nullptr;

// ---- Inbox: Bluetooth callbacks run on the BT task, handlers on loop() ---------
enum Kind : uint8_t { K_DATA, K_CONNECT, K_DISCONNECT };
struct Msg {
  uint32_t cid;
  uint8_t kind;
  uint16_t len;
  uint8_t data[1];
};
static QueueHandle_t inbox;

static void enqueue(uint32_t cid, uint8_t kind, const uint8_t *d, size_t n) {
  Msg *m = (Msg *)malloc(sizeof(Msg) + n);
  if (!m) return;
  m->cid = cid;
  m->kind = kind;
  m->len = (uint16_t)n;
  if (n) memcpy(m->data, d, n);
  if (xQueueSend(inbox, &m, 0) != pdTRUE) free(m);
}

// ---- Framing: every BLE packet starts with a header byte -----------------------
constexpr uint8_t F_FIRST = 0x80, F_LAST = 0x40;

// ---- Bluetooth -------------------------------------------------------------------
static BLEServer *server = nullptr;
static BLECharacteristic *txChar = nullptr;
static volatile bool bleConn = false, congested = false;
static volatile uint16_t mtu = 23;
static uint32_t advRestartAt = 0;
static char advName[32];
static bool nameChanged = false;

static uint8_t rxBuf[1100];
static size_t rxLen = 0;
static bool rxActive = false;

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
    bleConn = true;
    mtu = 23;
    congested = false;
    rxActive = false;
    // Ask for a snappy connection interval (15-30 ms); the phone has the final say.
    esp_ble_conn_update_params_t cp = {};
    memcpy(cp.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    cp.min_int = 12;
    cp.max_int = 24;
    cp.latency = 0;
    cp.timeout = 500;
    esp_ble_gap_update_conn_params(&cp);
    enqueue(BLE_CLIENT, K_CONNECT, nullptr, 0);
  }
  void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *) override {
    bleConn = false;
    enqueue(BLE_CLIENT, K_DISCONNECT, nullptr, 0);
  }
  void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *param) override { mtu = param->mtu.mtu; }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    const uint8_t *d = c->getData();
    size_t n = c->getLength();
    if (n < 1) return;
    uint8_t h = d[0];
    if (h & F_FIRST) {
      rxLen = 0;
      rxActive = true;
    }
    if (!rxActive) return;
    if (rxLen + n - 1 > sizeof rxBuf) {
      rxActive = false;
      return;
    }
    memcpy(rxBuf + rxLen, d + 1, n - 1);
    rxLen += n - 1;
    if (h & F_LAST) {
      rxActive = false;
      enqueue(BLE_CLIENT, K_DATA, rxBuf, rxLen);
    }
  }
};

static void gattsHook(esp_gatts_cb_event_t event, esp_gatt_if_t, esp_ble_gatts_cb_param_t *param) {
  if (event == ESP_GATTS_CONGEST_EVT) congested = param->congest.congested;
}

// Outgoing messages wait here and trickle out as notifications.
struct Out {
  uint8_t *data;
  uint16_t len, pos;
};
static Out outq[20];
static uint8_t outHead = 0, outTail = 0;
static SemaphoreHandle_t outLock;

static int outCount() { return (outHead - outTail + 20) % 20; }

static void bleQueue(const uint8_t *d, size_t n, bool droppable) {
  if (!bleConn) return;
  xSemaphoreTake(outLock, portMAX_DELAY);
  if (outCount() >= 19 || (droppable && outCount() >= 12)) {
    xSemaphoreGive(outLock);
    return;
  }
  uint8_t *copy = (uint8_t *)malloc(n);
  if (copy) {
    memcpy(copy, d, n);
    outq[outHead] = Out{copy, (uint16_t)n, 0};
    outHead = (outHead + 1) % 20;
  }
  xSemaphoreGive(outLock);
}

static void bleFlushQueue() {
  xSemaphoreTake(outLock, portMAX_DELAY);
  while (outTail != outHead) {
    free(outq[outTail].data);
    outTail = (outTail + 1) % 20;
  }
  xSemaphoreGive(outLock);
}

static void blePump() {
  static uint32_t windowStart = 0;
  static int sentInWindow = 0;
  if (!bleConn || congested) return;
  uint32_t now = millis();
  if (now - windowStart >= 10) {
    windowStart = now;
    sentInWindow = 0;
  }
  uint8_t pkt[520];
  int chunk = max(20, min(512, (int)mtu - 3)) - 1;
  while (sentInWindow < 4 && outTail != outHead && !congested) {
    Out &o = outq[outTail];
    int n = min(chunk, o.len - o.pos);
    pkt[0] = (o.pos == 0 ? F_FIRST : 0) | (o.pos + n >= o.len ? F_LAST : 0);
    memcpy(pkt + 1, o.data + o.pos, n);
    txChar->setValue(pkt, n + 1);
    txChar->notify();
    o.pos += n;
    sentInWindow++;
    if (o.pos >= o.len) {
      xSemaphoreTake(outLock, portMAX_DELAY);
      free(o.data);
      outTail = (outTail + 1) % 20;
      xSemaphoreGive(outLock);
    }
  }
}

static void bleBegin(const char *name) {
  strlcpy(advName, name, sizeof advName);
  outLock = xSemaphoreCreateMutex();
  BLEDevice::init(advName);
  BLEDevice::setMTU(247);
  BLEDevice::setCustomGattsHandler(gattsHook);
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  BLEService *svc = server->createService(BLE_SERVICE_UUID);
  txChar = svc->createCharacteristic(BLE_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());
  BLECharacteristic *rx =
    svc->createCharacteristic(BLE_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCB());
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);   // helps iPhones pick a quick connection interval
  adv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
}

// ---- USB serial bridge --------------------------------------------------------------
// '#J{json}\n' for text, '#B<base64>\n' for binary, each written in one call.
static bool serialClient = false, serialFast = false;
static uint32_t serialSeenMs = 0;

static void serialLine(char tag, const uint8_t *d, size_t n, bool b64) {
  static uint8_t out[2400];
  size_t olen = 0;
  out[0] = '#';
  out[1] = (uint8_t)tag;
  if (b64) {
    if (mbedtls_base64_encode(out + 2, sizeof(out) - 3, &olen, d, n) != 0) return;
  } else {
    if (n > sizeof(out) - 3) return;
    memcpy(out + 2, d, n);
    olen = n;
  }
  out[2 + olen] = '\n';
  Serial.write(out, 3 + olen);
}

static void serialCommand(char *l) {
  if (l[0] != '@') return;
  serialSeenMs = millis();
  if (!strcmp(l, "@@ping")) return;
  if (!strcmp(l, "@@hello")) {
    serialClient = true;
    if (onConn) onConn(SERIAL_CLIENT, true);
    return;
  }
  if (!strcmp(l, "@@bye")) {
    if (serialClient && onConn) onConn(SERIAL_CLIENT, false);
    serialClient = false;
    return;
  }
  if (!strcmp(l, "@@peek")) {   // one raw frame, without counting as a visitor
    static uint8_t f[1 + SCREEN_W * SCREEN_H / 8];
    f[0] = 0x80;
    memcpy(f + 1, gfx::buf(), SCREEN_W * SCREEN_H / 8);
    serialLine('B', f, sizeof f, true);
    return;
  }
  if (!strncmp(l, "@@baud ", 7)) {
    long b = atol(l + 7);
    if (b >= 9600) {
      Serial.flush();
      Serial.updateBaudRate(b);
      serialFast = b != 115200;
    }
    return;
  }
  static uint8_t bin[800];
  size_t n = 0;
  if (mbedtls_base64_decode(bin, sizeof bin, &n, (const uint8_t *)l + 1, strlen(l + 1)) == 0 && n && onMsg)
    onMsg(SERIAL_CLIENT, bin, n);
}

static void serialPoll() {
  static char line[1200];
  static size_t len = 0;
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (len) {
        line[len] = 0;
        serialCommand(line);
        len = 0;
      }
    } else if (len < sizeof(line) - 1) {
      line[len++] = (char)ch;
    } else {
      len = 0;
    }
  }
}

// ---- Screen mirror ----------------------------------------------------------------
// Frames are run-length coded: 0x00-0x7F = skip n+1 bytes, 0x80-0xFF = n-0x7F literal
// bytes follow. Keyframes code the raw screen, deltas code the XOR with the last frame sent.
struct Mirror {
  bool wants, key;
  uint32_t sentVer, lastSend;
  uint8_t shadow[SCREEN_W * SCREEN_H / 8];
};
static Mirror mirBle, mirSer;

static size_t encodeFrame(Mirror &m, uint8_t *out) {
  const uint8_t *cur = gfx::buf();
  const int N = SCREEN_W * SCREEN_H / 8;
  bool key = m.key;
  auto val = [&](int i) -> uint8_t { return key ? cur[i] : (uint8_t)(cur[i] ^ m.shadow[i]); };
  size_t o = 0;
  out[o++] = key ? MSG_KEYFRAME : MSG_DELTA;
  int i = 0;
  while (i < N) {
    int z = 0;
    while (i + z < N && z < 128 && val(i + z) == 0) z++;
    if (z > 0) {
      if (i + z >= N) break;   // trailing zeros are implied
      out[o++] = (uint8_t)(z - 1);
      i += z;
      continue;
    }
    int s = i, L = 0;
    while (i < N && L < 128) {
      if (val(i) == 0 && (i + 1 >= N || val(i + 1) == 0)) break;
      L++;
      i++;
    }
    out[o++] = 0x80 | (L - 1);
    for (int k = 0; k < L; k++) out[o++] = val(s + k);
  }
  memcpy(m.shadow, cur, N);
  m.key = false;
  return o;
}

void requestFrame(uint32_t cid, bool keyframe) {
  Mirror &m = cid == BLE_CLIENT ? mirBle : mirSer;
  m.wants = true;
  if (keyframe) m.key = true;
}

void pushFrames() {
  static uint8_t out[1 + 1024 + 16];
  uint32_t v = gfx::version(), now = millis();
  if (bleConn && mirBle.wants && (mirBle.key || mirBle.sentVer != v) && now - mirBle.lastSend >= MIRROR_MIN_MS &&
      outCount() < 3) {
    size_t n = encodeFrame(mirBle, out);
    bleQueue(out, n, false);
    mirBle.wants = false;
    mirBle.sentVer = v;
    mirBle.lastSend = now;
  }
  if (serialClient && mirSer.wants && (mirSer.key || mirSer.sentVer != v) && now - mirSer.lastSend >= 35) {
    size_t n = encodeFrame(mirSer, out);
    serialLine('B', out, n, true);
    mirSer.wants = false;
    mirSer.sentVer = v;
    mirSer.lastSend = now;
  }
}

// ---- Public --------------------------------------------------------------------------
void begin(const char *bleName, MessageFn m, ConnectFn c) {
  onMsg = m;
  onConn = c;
  inbox = xQueueCreate(64, sizeof(Msg *));
  bleBegin(bleName);
}

void poll() {
  serialPoll();
  Msg *m;
  for (int budget = 32; budget > 0 && xQueueReceive(inbox, &m, 0) == pdTRUE; budget--) {
    if (m->kind == K_CONNECT) {
      mirBle.wants = false;
      mirBle.key = true;
      if (onConn) onConn(m->cid, true);
    } else if (m->kind == K_DISCONNECT) {
      bleFlushQueue();
      advRestartAt = millis() + 300;
      if (onConn) onConn(m->cid, false);
    } else if (onMsg) {
      onMsg(m->cid, m->data, m->len);
    }
    free(m);
  }
  blePump();
}

void periodic() {
  uint32_t now = millis();
  if (advRestartAt && (int32_t)(now - advRestartAt) > 0 && !bleConn) {
    advRestartAt = 0;
    if (nameChanged) {
      nameChanged = false;
      esp_ble_gap_set_device_name(advName);
    }
    BLEDevice::startAdvertising();
  }
  // A USB bridge that vanished without saying goodbye: forget it and go back to 115200.
  if ((serialFast || serialClient) && now - serialSeenMs > 15000) {
    if (serialClient && onConn) onConn(SERIAL_CLIENT, false);
    serialClient = false;
    if (serialFast) {
      Serial.flush();
      Serial.updateBaudRate(115200);
      serialFast = false;
    }
  }
}

void sendTo(uint32_t cid, const String &json) {
  if (cid == SERIAL_CLIENT) {
    if (serialClient) serialLine('J', (const uint8_t *)json.c_str(), json.length(), false);
  } else if (cid == BLE_CLIENT) {
    bool droppable = json.startsWith("{\"t\":\"state\"") || json.startsWith("{\"t\":\"stats\"");
    bleQueue((const uint8_t *)json.c_str(), json.length(), droppable);
  }
}

void send(const String &json) {
  sendTo(SERIAL_CLIENT, json);
  sendTo(BLE_CLIENT, json);
}

bool bleConnected() { return bleConn; }
bool serialActive() { return serialClient; }
bool anyone() { return bleConn || serialClient; }
uint16_t bleMtu() { return mtu; }

void setName(const char *bleName) {
  if (!strcmp(bleName, advName)) return;
  strlcpy(advName, bleName, sizeof advName);
  nameChanged = true;
  if (!bleConn) {
    BLEDevice::stopAdvertising();
    advRestartAt = millis() + 100;
  }
}

}  // namespace comms
