// Connection to the pet: Bluetooth LE (Web Bluetooth - Chrome/Android, Bluefy on iPhone)
// or, for development, the USB bridge's WebSocket (tools/usb_bridge.py).
//
// Phones drop Bluetooth links for all sorts of reasons (screen lock, app switch, a burst
// of Wi-Fi), so the link looks after itself: it reconnects on its own, pings the pet to
// catch a link that died without saying so, and never lets one stuck write block the rest.

import { OP, enc } from './protocol.js';

export const SERVICE = 'f3a10001-5b1e-4c6b-9e0f-7065656b6162';
const RX = 'f3a10002-5b1e-4c6b-9e0f-7065656b6162';
const TX = 'f3a10003-5b1e-4c6b-9e0f-7065656b6162';
const F_FIRST = 0x80, F_LAST = 0x40;
const COALESCE = new Set([OP.TALK, OP.LOOK, OP.FRAME_REQ, OP.PING]);   // only the newest one matters
const PING_MS = 2500;        // heartbeat while connected
const SILENT_MS = 8000;      // nothing from the pet for this long (while we're watching) = dead link
const WRITE_MS = 2500;       // one write taking longer than this is stuck
const RETRY_MS = [300, 1000, 2000, 3000, 5000];

const td = new TextDecoder();

export class Link extends EventTarget {
  constructor() {
    super();
    this.kind = null;          // 'ble' | 'bridge'
    this.state = 'idle';       // idle | connecting | connected | lost
    this.chunk = 20;           // payload bytes per BLE packet (grows once we learn the MTU)
    this.queue = [];
    this.writing = false;
    this.rxBuf = null;
    this.device = null;
    this.userClosed = false;
    this.replaced = false;     // the pet said another phone took over: don't fight for it
    this.retry = 0;
    this.gen = 0;              // bumps whenever the link changes, so stale loops stop
    this.lastRx = 0;
    this.lastBeat = 0;
    this.writeFails = 0;
    this.lostAt = 0;
    this.timer = null;
    setInterval(() => this.heartbeat(), PING_MS);
    document.addEventListener('visibilitychange', () => this.onVisible());
    addEventListener('pageshow', () => this.onVisible());
    addEventListener('online', () => this.onVisible());
  }

  static bleSupported() { return !!(navigator.bluetooth && navigator.bluetooth.requestDevice); }
  static bridgeAvailable() { return ['localhost', '127.0.0.1'].includes(location.hostname); }

  get connected() { return this.state === 'connected'; }

  setStatus(state, extra = {}) {
    this.state = state;
    this.dispatchEvent(new CustomEvent('status', { detail: { state, kind: this.kind, ...extra } }));
  }

  setMtu(mtu) {
    if (this.kind === 'ble' && mtu > 23) this.chunk = Math.min(180, mtu - 4);
  }

  // ---- Bluetooth ------------------------------------------------------------------
  async pickBle() {
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE] }, { namePrefix: 'Peeka' }],
      optionalServices: [SERVICE],
    });
    return this.connectBle(device);
  }

  async autoBle() {
    if (!navigator.bluetooth?.getDevices) return false;
    let devices = [];
    try { devices = await navigator.bluetooth.getDevices(); } catch { return false; }
    const d = devices.find((x) => (x.name || '').startsWith('Peeka')) || devices[0];
    if (!d) return false;
    try {
      await this.connectBle(d);
      return this.connected;
    } catch (e) {
      console.warn('auto-connect failed', e);
      this.setStatus('idle');
      return false;
    }
  }

  async connectBle(device) {
    const gen = ++this.gen;
    clearTimeout(this.timer);
    this.kind = 'ble';
    this.userClosed = false;
    this.replaced = false;
    this.device = device;
    this.chunk = 20;
    this.rx = this.tx = null;
    this.queue = [];
    this.writing = false;
    this.rxBuf = null;
    if (!device._peekaHooked) {
      device._peekaHooked = true;
      device.addEventListener('gattserverdisconnected', () => this.lost(device, 'Bluetooth disconnected'));
    }
    this.setStatus('connecting', { name: device.name });
    try {
      const server = await withTimeout(device.gatt.connect(), 12000, 'Bluetooth connection timed out');
      if (gen !== this.gen) return;
      const svc = await withTimeout(server.getPrimaryService(SERVICE), 10000, 'The pet did not answer');
      const rx = await withTimeout(svc.getCharacteristic(RX), 6000, 'The pet did not answer');
      const tx = await withTimeout(svc.getCharacteristic(TX), 6000, 'The pet did not answer');
      if (!tx._peekaHooked) {   // once per object: some browsers hand back the same one on every reconnect
        tx._peekaHooked = true;
        tx.addEventListener('characteristicvaluechanged', (e) => {
          if (tx !== this.tx) return;   // a characteristic from an earlier link
          const v = e.target.value;
          this.onPacket(new Uint8Array(v.buffer, v.byteOffset, v.byteLength));
        });
      }
      this.tx = tx;   // before notifications start, so the first packets aren't ignored
      await withTimeout(tx.startNotifications(), 8000, 'The pet did not answer');
      if (gen !== this.gen) return;
      this.rx = rx;
      this.retry = 0;
      this.writeFails = 0;
      this.lastRx = performance.now();
      const downFor = this.lostAt ? performance.now() - this.lostAt : 0;
      this.lostAt = 0;
      this.setStatus('connected', { name: device.name, downFor });
    } catch (e) {
      if (gen === this.gen) {
        this.gen++;
        this.tx = null;
        try { device.gatt.disconnect(); } catch { /* not connected */ }   // cancel a connect that's still pending
        this.setStatus('idle', { name: device.name });   // reconnectNow() turns this back into 'lost' and retries
      }
      throw e;
    }
  }

  // The one place a lost Bluetooth link is handled, however we found out.
  lost(device, why) {
    if (this.kind !== 'ble' || device !== this.device) return;
    const was = this.state;
    if (was === 'idle' || was === 'lost') return;   // nothing was up, and any retry is already arranged
    this.gen++;
    this.rx = this.tx = null;
    this.queue = [];
    this.writing = false;
    this.rxBuf = null;
    if (this.userClosed) { this.setStatus('idle'); return; }
    if (this.replaced) { this.setStatus('idle', { name: device.name, replaced: true }); return; }
    if (was === 'connected') this.lostAt = performance.now();
    this.setStatus('lost', { name: device.name, why, dropped: was === 'connected' });
    this.scheduleReconnect();
  }

  // Force a link we no longer trust down, then build a new one.
  drop(why) {
    if (this.kind !== 'ble' || !this.device || this.state !== 'connected') return;
    console.warn('dropping the link:', why);
    const d = this.device;
    this.lost(d, why);
    try { d.gatt.disconnect(); } catch { /* already gone */ }
  }

  scheduleReconnect() {
    clearTimeout(this.timer);
    if (this.userClosed || this.replaced || !this.device) return;
    const wait = RETRY_MS[Math.min(this.retry, RETRY_MS.length - 1)];
    this.retry++;
    this.timer = setTimeout(() => this.reconnectNow(), wait);
  }

  async reconnectNow() {
    clearTimeout(this.timer);
    if (this.kind !== 'ble' || this.userClosed || this.replaced || !this.device) return;
    if (this.state === 'connected' || this.state === 'connecting') return;
    if (document.hidden) return;   // phones won't connect from the background; onVisible() picks it up
    try {
      await this.connectBle(this.device);
    } catch (e) {
      console.warn('reconnect failed', e);
      if (this.state !== 'connected' && !this.userClosed) {
        this.setStatus('lost', { name: this.device?.name, why: String(e?.message || e) });
        this.scheduleReconnect();
      }
    }
  }

  onVisible() {
    if (document.hidden || this.kind !== 'ble') return;
    if (this.state === 'connected') {
      this.lastRx = performance.now();   // give it a moment to prove it's still alive
      this.send(enc.ping());
    } else if (this.state === 'lost') {
      this.retry = 0;
      this.reconnectNow();
    }
  }

  heartbeat() {
    const now = performance.now();
    const frozen = now - this.lastBeat > PING_MS * 3;   // the page was asleep; timers just woke up
    this.lastBeat = now;
    if (this.state !== 'connected') return;
    if (frozen || document.hidden) this.lastRx = now;
    this.send(enc.ping());
    if (this.kind === 'ble' && now - this.lastRx > SILENT_MS) this.drop(`no reply from the pet for ${Math.round(SILENT_MS / 1000)} s`);
  }

  onPacket(p) {
    this.lastRx = performance.now();
    if (!p.length) return;
    const h = p[0];
    if (h & F_FIRST) this.rxBuf = [];
    if (!this.rxBuf) return;
    for (let i = 1; i < p.length; i++) this.rxBuf.push(p[i]);
    if (h & F_LAST) {
      const msg = new Uint8Array(this.rxBuf);
      this.rxBuf = null;
      this.deliver(msg);
    }
  }

  // ---- USB bridge -------------------------------------------------------------------
  connectBridge() {
    this.kind = 'bridge';
    this.userClosed = false;
    this.setStatus('connecting', { name: 'USB bridge' });
    const ws = new WebSocket(`ws://${location.host}/ws`);
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => { this.ws = ws; this.lastRx = performance.now(); this.setStatus('connected', { name: 'USB bridge' }); };
    ws.onmessage = (e) => {
      this.lastRx = performance.now();
      if (typeof e.data === 'string') this.deliverJson(e.data);
      else this.deliver(new Uint8Array(e.data));
    };
    ws.onclose = () => {
      this.ws = null;
      if (this.userClosed) { this.setStatus('idle'); return; }
      this.setStatus('lost', { name: 'USB bridge' });
      setTimeout(() => { if (!this.userClosed) this.connectBridge(); }, 1500);
    };
  }

  // ---- Common -------------------------------------------------------------------------
  deliver(msg) {
    if (msg[0] === 0x7b) this.deliverJson(td.decode(msg));
    else this.dispatchEvent(new CustomEvent('binary', { detail: msg }));
  }

  deliverJson(text) {
    let obj;
    try { obj = JSON.parse(text); } catch { console.warn('bad json', text); return; }
    if (obj.t === 'pong') return;
    if (obj.t === 'bye') { this.replaced = true; return; }   // another phone connected; the pet is about to hang up
    this.dispatchEvent(new CustomEvent('json', { detail: obj }));
  }

  send(msg) {
    if (!this.connected) return false;
    if (this.kind === 'bridge') {
      this.ws?.send(msg);
      return true;
    }
    if (msg.length <= this.chunk && COALESCE.has(msg[0])) {
      this.queue = this.queue.filter((p) => !(p.length === msg.length + 1 && p[0] === (F_FIRST | F_LAST) && p[1] === msg[0]));
    }
    for (let pos = 0; pos < msg.length || pos === 0; pos += this.chunk) {
      const part = msg.subarray(pos, pos + this.chunk);
      const pkt = new Uint8Array(part.length + 1);
      pkt[0] = (pos === 0 ? F_FIRST : 0) | (pos + this.chunk >= msg.length ? F_LAST : 0);
      pkt.set(part, 1);
      this.queue.push(pkt);
      if (msg.length === 0) break;
    }
    if (this.queue.length > 400) this.queue.splice(0, this.queue.length - 400);
    this.pump();
    return true;
  }

  async pump() {
    if (this.writing || !this.rx) return;
    this.writing = true;
    const gen = this.gen;
    while (this.queue.length && this.rx && gen === this.gen) {
      const pkt = this.queue[0];
      let failed = null;
      try {
        await withTimeout(this.write(pkt), WRITE_MS, 'a write got stuck');
      } catch (e) {
        failed = e;
      }
      if (gen !== this.gen) return;   // the link changed under us; the new one has its own pump
      this.queue.shift();
      if (!failed) {
        this.writeFails = 0;
      } else {
        console.warn('write failed', failed);
        if (++this.writeFails >= 2) { this.writing = false; this.drop('writes to the pet keep failing'); return; }
      }
    }
    if (gen === this.gen) this.writing = false;
  }

  async write(pkt) {
    const rx = this.rx;
    if (rx.writeValueWithoutResponse) {
      try { return await rx.writeValueWithoutResponse(pkt); } catch { /* try the slower kind */ }
    }
    return rx.writeValue(pkt);
  }

  disconnect() {
    this.userClosed = true;
    clearTimeout(this.timer);
    this.gen++;
    this.rx = this.tx = null;
    this.queue = [];
    this.writing = false;
    if (this.kind === 'ble') try { this.device?.gatt?.disconnect(); } catch { /* already gone */ }
    if (this.kind === 'bridge') this.ws?.close();
    this.setStatus('idle');
  }
}

function withTimeout(p, ms, msg) {
  let t;
  return Promise.race([p, new Promise((_, rej) => { t = setTimeout(() => rej(new Error(msg)), ms); })]).finally(() => clearTimeout(t));
}
