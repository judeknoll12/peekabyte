#include "comms.h"
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include <nvs.h>
#include "config.h"
#include "gfx.h"
#include "protocol.h"
#include "mbedtls/base64.h"

namespace comms {

static MessageFn onMsg = nullptr;
static ConnectFn onConn = nullptr;

// ---- Inbox: Bluetooth callbacks run on the BT task, handlers on loop() ---------
enum Kind : uint8_t { K_DATA, K_CONNECT, K_DISCONNECT, K_OLD_GONE };
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
// One phone at a time: the newest connection wins and the older one is closed (it was
// usually the same phone, left over from before its page reloaded). The pet keeps
// advertising while connected, so a phone can always find it again.
constexpr uint16_t NO_CONN = 0xFFFF;
constexpr uint32_t QUIET_MS = 15000;     // a phone app this quiet is asleep (screen off, app in the background)
constexpr uint16_t WHY_REPLACED = 0xF0;  // our own drop reason: another phone took over

static BLEServer *server = nullptr;
static BLECharacteristic *txChar = nullptr;
static volatile bool bleConn = false, congested = false;
static volatile uint16_t mtu = 23;
static volatile uint16_t activeConn = NO_CONN, oldConn = NO_CONN;
static esp_bd_addr_t activeBda, oldBda;
static volatile bool kickOld = false;
static volatile uint32_t lastRxMs = 0;
static uint32_t advRestartAt = 0, kickAt = 0;
static char advName[32];
static bool nameChanged = false;

static uint8_t rxBuf[1100];
static size_t rxLen = 0;
static bool rxActive = false;

// Link health, reported to the phone's Connection page.
struct Drop {
  uint32_t at;       // pet uptime (s) when the link dropped
  uint32_t lasted;   // how long that link had been up (s)
  uint16_t why;      // Bluetooth disconnect reason (0x08 timeout, 0x13 phone closed it, ...)
};
static Drop drops[6];
static uint8_t dropCount = 0, dropNext = 0;
static uint16_t connects = 0;
static volatile uint16_t linkInt = 0, linkLat = 0, linkTo = 0;   // 1.25 ms units, events, 10 ms units
static volatile int8_t linkRssi = 127;                           // 127 = not measured yet
static volatile uint32_t linkSince = 0;
static volatile uint8_t paramTries = 0;
static volatile uint32_t paramAt = 0;

static void logDrop(uint16_t why, uint32_t since) {
  uint32_t now = millis();
  drops[dropNext] = Drop{now / 1000, since ? (now - since) / 1000 : 0, why};
  dropNext = (dropNext + 1) % 6;
  if (dropCount < 6) dropCount++;
  Serial.printf("[ble] link dropped after %lus, reason 0x%02X\n", (unsigned long)(since ? (now - since) / 1000 : 0), why);
}

// Ask for a link that shrugs off radio hiccups. iPhones only accept requests that follow
// Apple's rules: interval a multiple of 15 ms, max >= min + 15 ms, timeout 2-6 s.
static void requestParams() {
  esp_ble_conn_update_params_t cp = {};
  memcpy(cp.bda, activeBda, sizeof(esp_bd_addr_t));
  cp.min_int = 12;    // 15 ms
  cp.max_int = 24;    // 30 ms
  cp.latency = 0;
  cp.timeout = 500;   // 5 s without hearing each other before the link counts as lost
  esp_ble_gap_update_conn_params(&cp);
  paramTries++;
  paramAt = millis();
}

// The library remembers each phone's notification switch in flash, keyed by the phone's
// Bluetooth address. iPhones change that address every few minutes, so the keys pile up
// until the flash is full and the pet can't save anymore. We never pair, so drop them.
static void forgetSubscriptions() { BLE2902::deleteAllPersistedValues(); }

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
    if (bleConn && activeConn != param->connect.conn_id) {
      oldConn = activeConn;
      memcpy(oldBda, activeBda, sizeof(esp_bd_addr_t));
      kickOld = true;
    }
    activeConn = param->connect.conn_id;
    memcpy(activeBda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    mtu = 23;
    congested = false;
    rxActive = false;
    lastRxMs = millis();
    linkSince = millis();
    linkInt = param->connect.conn_params.interval;
    linkLat = param->connect.conn_params.latency;
    linkTo = param->connect.conn_params.timeout;
    linkRssi = 127;
    paramTries = 0;
    bleConn = true;
    requestParams();
    enqueue(BLE_CLIENT, K_CONNECT, nullptr, 0);
  }
  void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
    uint16_t why = param->disconnect.reason;
    if (param->disconnect.conn_id != activeConn) {   // the link we replaced
      enqueue(BLE_CLIENT, K_OLD_GONE, (const uint8_t *)&why, sizeof why);
      return;
    }
    bleConn = false;
    activeConn = NO_CONN;
    enqueue(BLE_CLIENT, K_DISCONNECT, (const uint8_t *)&why, sizeof why);
  }
  void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
    if (param->mtu.conn_id == activeConn) mtu = param->mtu.mtu;
  }
  void onConnParamsUpdate(esp_bd_addr_t bda, uint16_t interval, uint16_t latency, uint16_t timeout,
                          esp_bt_status_t status) override {
    if (status != ESP_BT_STATUS_SUCCESS || !bleConn || memcmp(bda, activeBda, sizeof(esp_bd_addr_t))) return;
    linkInt = interval;
    linkLat = latency;
    linkTo = timeout;
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c, esp_ble_gatts_cb_param_t *param) override {
    if (param->write.conn_id != activeConn) return;   // a replaced phone still talking
    lastRxMs = millis();
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

static esp_gatt_if_t gattsIf = ESP_GATT_IF_NONE;

static void gattsHook(esp_gatts_cb_event_t event, esp_gatt_if_t gif, esp_ble_gatts_cb_param_t *param) {
  if (gif != ESP_GATT_IF_NONE) gattsIf = gif;
  if (event == ESP_GATTS_CONGEST_EVT && param->congest.conn_id == activeConn) congested = param->congest.congested;
}

static void gapHook(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (event == ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT && param->read_rssi_cmpl.status == ESP_BT_STATUS_SUCCESS &&
      !memcmp(param->read_rssi_cmpl.remote_addr, activeBda, sizeof(esp_bd_addr_t)))
    linkRssi = param->read_rssi_cmpl.rssi;
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

// Tell a phone that's about to be replaced, so its app doesn't fight to get the pet back.
static void sayGoodbye(uint16_t conn) {
  static const char BYE[] = "{\"t\":\"bye\"}";   // short enough for the smallest packet size
  uint8_t pkt[sizeof BYE];
  pkt[0] = F_FIRST | F_LAST;
  memcpy(pkt + 1, BYE, sizeof BYE - 1);
  if (gattsIf != ESP_GATT_IF_NONE) esp_ble_gatts_send_indicate(gattsIf, conn, txChar->getHandle(), sizeof pkt, pkt, false);
}

// Quick to find while nobody is connected; slow and quiet (but still findable) while a phone is.
static void advertise(bool fast) {
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->stop();
  if (nameChanged) {
    nameChanged = false;
    esp_ble_gap_set_device_name(advName);
    BLEAdvertisementData sr;
    sr.setName(advName);
    adv->setScanResponseData(sr);
  }
  adv->setMinInterval(fast ? 0x20 : 0xF4);    // 20 ms   / 152.5 ms (Apple's recommended steps)
  adv->setMaxInterval(fast ? 0x40 : 0x150);   // 40 ms   / 210 ms
  adv->start();
}

static void bleBegin(const char *name) {
  strlcpy(advName, name, sizeof advName);
  outLock = xSemaphoreCreateMutex();
  forgetSubscriptions();
  BLEDevice::init(advName);
  BLEDevice::setMTU(247);
  // Full power (+9 dBm): the default +3 dBm drops out at the far side of a room.
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  BLEDevice::setCustomGattsHandler(gattsHook);
  BLEDevice::setCustomGapHandler(gapHook);
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  BLEService *svc = server->createService(BLE_SERVICE_UUID);
  txChar = svc->createCharacteristic(BLE_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());
  BLECharacteristic *rx =
    svc->createCharacteristic(BLE_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCB());
  svc->start();
  // Advert: flags + our service; the name goes in the scan response so it can change.
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  BLEAdvertisementData ad;
  ad.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  ad.setCompleteServices(BLEUUID(BLE_SERVICE_UUID));
  adv->setAdvertisementData(ad);
  nameChanged = true;
  advertise(true);
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
  if (!strcmp(l, "@@link")) {   // link health, without counting as a visitor
    String j;
    linkJson(j);
    j += '\n';
    Serial.print("#L");
    Serial.print(j);
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
    uint16_t why = m->len >= 2 ? (uint16_t)(m->data[0] | (m->data[1] << 8)) : 0;
    switch (m->kind) {
      case K_CONNECT:
        connects++;
        Serial.printf("[ble] phone connected (link %.1f ms, timeout %u ms)\n", linkInt * 1.25f, linkTo * 10u);
        bleFlushQueue();
        mirBle.wants = false;
        mirBle.key = true;
        if (kickOld) {
          kickOld = false;
          sayGoodbye(oldConn);
          kickAt = millis() + 150;
        }
        advertise(false);
        if (onConn) onConn(m->cid, true);
        break;
      case K_DISCONNECT:
        logDrop(why, linkSince);
        bleFlushQueue();
        forgetSubscriptions();
        advRestartAt = millis() + 300;
        if (onConn) onConn(m->cid, false);
        break;
      case K_OLD_GONE:
        logDrop(WHY_REPLACED, 0);
        forgetSubscriptions();
        break;
      default:
        if (onMsg) onMsg(m->cid, m->data, m->len);
    }
    free(m);
  }
  blePump();
}

void periodic() {
  uint32_t now = millis();
  if (advRestartAt && (int32_t)(now - advRestartAt) > 0) {
    advRestartAt = 0;
    advertise(!bleConn);
  }
  if (kickAt && (int32_t)(now - kickAt) > 0) {
    kickAt = 0;
    esp_ble_gap_disconnect(oldBda);
  }
  if (bleConn) {
    // Phones sometimes ignore the first request; ask again while the link is still flimsy.
    if (paramTries < 3 && now - paramAt > 6000 && (linkTo < 200 || linkInt > 40)) requestParams();
    static uint32_t rssiAt = 0;
    if (now - rssiAt > 2000) {
      rssiAt = now;
      esp_ble_gap_read_rssi(activeBda);
    }
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
bool anyone() { return serialClient || (bleConn && millis() - lastRxMs < QUIET_MS); }
uint16_t bleMtu() { return mtu; }

void setName(const char *bleName) {
  if (!strcmp(bleName, advName)) return;
  strlcpy(advName, bleName, sizeof advName);
  nameChanged = true;
  advRestartAt = millis() + 100;
}

void linkJson(String &j) {
  static nvs_stats_t nv = {};
  static uint32_t nvAt = 0;
  uint32_t now = millis();
  if (!nvAt || now - nvAt > 30000) {
    nvAt = now | 1;
    nvs_get_stats(NULL, &nv);
  }
  j += "{\"up\":"; j += now / 1000;
  j += ",\"rr\":"; j += (int)esp_reset_reason();
  j += ",\"on\":"; j += bleConn ? 1 : 0;
  j += ",\"ci\":"; j += linkInt;
  j += ",\"lat\":"; j += linkLat;
  j += ",\"to\":"; j += linkTo;
  j += ",\"rs\":"; j += linkRssi;
  j += ",\"cs\":"; j += bleConn ? (now - linkSince) / 1000 : 0;
  j += ",\"cn\":"; j += connects;
  j += ",\"dr\":[";
  for (int i = 0; i < dropCount; i++) {
    const Drop &d = drops[(dropNext + 6 - dropCount + i) % 6];
    if (i) j += ',';
    j += '['; j += d.at; j += ','; j += d.lasted; j += ','; j += d.why; j += ']';
  }
  j += "],\"nv\":["; j += nv.used_entries; j += ','; j += nv.total_entries;
  j += "],\"mh\":"; j += ESP.getMinFreeHeap();
  j += '}';
}

}  // namespace comms
