// Connection to the pet: Bluetooth LE (Web Bluetooth - Chrome/Android, Bluefy on iPhone)
// or, for development, the USB bridge's WebSocket (tools/usb_bridge.py).

import { OP } from './protocol.js';

export const SERVICE = 'f3a10001-5b1e-4c6b-9e0f-7065656b6162';
const RX = 'f3a10002-5b1e-4c6b-9e0f-7065656b6162';
const TX = 'f3a10003-5b1e-4c6b-9e0f-7065656b6162';
const F_FIRST = 0x80, F_LAST = 0x40;
const COALESCE = new Set([OP.TALK, OP.LOOK, OP.FRAME_REQ]);   // only the newest one matters

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
    this.retry = 0;
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
      return true;
    } catch (e) {
      console.warn('auto-connect failed', e);
      this.setStatus('idle');
      return false;
    }
  }

  async connectBle(device) {
    this.kind = 'ble';
    this.userClosed = false;
    this.device = device;
    this.chunk = 20;
    if (!device._peekaHooked) {
      device._peekaHooked = true;
      device.addEventListener('gattserverdisconnected', () => this.onBleLost());
    }
    this.setStatus('connecting', { name: device.name });
    const server = await withTimeout(device.gatt.connect(), 12000, 'Bluetooth connection timed out');
    const svc = await server.getPrimaryService(SERVICE);
    this.rx = await svc.getCharacteristic(RX);
    this.tx = await svc.getCharacteristic(TX);
    await this.tx.startNotifications();
    this.tx.addEventListener('characteristicvaluechanged', (e) => this.onPacket(new Uint8Array(e.target.value.buffer)));
    this.retry = 0;
    this.queue = [];
    this.setStatus('connected', { name: device.name });
  }

  onBleLost() {
    if (this.kind !== 'ble') return;
    this.rx = this.tx = null;
    this.queue = [];
    if (this.userClosed) { this.setStatus('idle'); return; }
    this.setStatus('lost', { name: this.device?.name });
    const wait = Math.min(15000, 1500 * 2 ** this.retry++);
    setTimeout(async () => {
      if (this.userClosed || this.state === 'connected') return;
      try { await this.connectBle(this.device); } catch (e) { console.warn('reconnect failed', e); this.onBleLost(); }
    }, wait);
  }

  onPacket(p) {
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
    ws.onopen = () => { this.ws = ws; this.setStatus('connected', { name: 'USB bridge' }); };
    ws.onmessage = (e) => {
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
    this.pump();
    return true;
  }

  async pump() {
    if (this.writing) return;
    this.writing = true;
    while (this.queue.length && this.rx) {
      const pkt = this.queue.shift();
      try {
        if (this.rx.writeValueWithoutResponse) await this.rx.writeValueWithoutResponse(pkt);
        else await this.rx.writeValue(pkt);
      } catch (e) {
        try { await this.rx.writeValue(pkt); } catch (e2) { console.warn('write failed', e2); }
      }
    }
    this.writing = false;
  }

  disconnect() {
    this.userClosed = true;
    if (this.kind === 'ble') this.device?.gatt?.disconnect();
    if (this.kind === 'bridge') this.ws?.close();
    this.setStatus('idle');
  }
}

function withTimeout(p, ms, msg) {
  return Promise.race([p, new Promise((_, rej) => setTimeout(() => rej(new Error(msg)), ms))]);
}
