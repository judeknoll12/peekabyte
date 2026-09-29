// Inside the Peekabyte iPhone app (ios/), this page runs in a native shell that lends it
// Bluetooth that survives the phone locking, and an open-source AI on the iPhone's GPU.
// This module talks to that shell and, when it's there, installs a Web Bluetooth stand-in so
// link.js works unchanged. In an ordinary browser it does nothing.

import { SERVICE } from './link.js';

const host = window.webkit?.messageHandlers?.peeka;
const shell = window.PeekaNative;
export const isNative = !!(host && shell);

let nextId = 1;
const waiting = new Map();
const listeners = new Map();

// Ask the app for something; resolves with its answer.
export function call(cmd, args = {}) {
  if (!isNative) return Promise.reject(new Error('Only available in the iPhone app'));
  return new Promise((resolve, reject) => {
    const id = nextId++;
    waiting.set(id, { resolve, reject });
    host.postMessage({ ...args, cmd, rid: id });   // rid, not id: many requests carry a device id
  });
}

// Listen for news from the app (Bluetooth packets, download progress, ...).
export function on(name, fn) {
  if (!listeners.has(name)) listeners.set(name, new Set());
  listeners.get(name).add(fn);
  return () => listeners.get(name)?.delete(fn);
}

function dispatch(name, data) {
  for (const fn of listeners.get(name) || []) {
    try { fn(data); } catch (e) { console.error(e); }
  }
}

let infoPromise = null;
export function appInfo() {
  if (!isNative) return Promise.resolve(null);
  infoPromise ||= call('app.info').catch(() => null);
  return infoPromise;
}

if (isNative) {
  shell._reply = (id, ok, value) => {
    const w = waiting.get(id);
    if (!w) return;
    waiting.delete(id);
    if (ok) w.resolve(value);
    else w.reject(new Error(value || 'Something went wrong'));
  };
  const early = shell.queue || [];
  shell.queue = [];
  shell._event = dispatch;
  early.forEach(([name, data]) => dispatch(name, data));
  document.documentElement.classList.add('native');
  installBluetooth();
}

// ---------------------------------------------------------------- Web Bluetooth stand-in ----
const norm = (u) => String(u).toLowerCase();
const toBytes = (d) => (d instanceof Uint8Array ? d : ArrayBuffer.isView(d) ? new Uint8Array(d.buffer, d.byteOffset, d.byteLength) : new Uint8Array(d));
function toB64(u8) {
  let s = '';
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode(...u8.subarray(i, i + 0x8000));
  return btoa(s);
}
function fromB64(text) {
  const bin = atob(text);
  const u8 = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
  return u8;
}

function installBluetooth() {
  const devices = new Map();

  class Characteristic extends EventTarget {
    constructor(device, service, uuid) {
      super();
      Object.assign(this, { device, serviceUuid: service, uuid, value: null });
    }
    get service() { return { uuid: this.serviceUuid, device: this.device }; }
    args(extra) { return { id: this.device.id, service: this.serviceUuid, char: this.uuid, ...extra }; }
    async startNotifications() { await call('ble.subscribe', this.args({ on: true })); return this; }
    async stopNotifications() { await call('ble.subscribe', this.args({ on: false })); return this; }
    async writeValueWithoutResponse(data) { await call('ble.write', this.args({ data: toB64(toBytes(data)), response: false })); }
    async writeValueWithResponse(data) { await call('ble.write', this.args({ data: toB64(toBytes(data)), response: true })); }
    writeValue(data) { return this.writeValueWithResponse(data); }
  }

  class Service {
    constructor(device, uuid) { Object.assign(this, { device, uuid, isPrimary: true }); }
    async getCharacteristic(uuid) { return this.device.characteristic(this.uuid, norm(uuid)); }
  }

  class Gatt {
    constructor(device) { this.device = device; this.connected = false; }
    async connect() {
      await call('ble.connect', { id: this.device.id, service: this.device.serviceUuid });
      this.connected = true;
      return this;
    }
    disconnect() { call('ble.disconnect', { id: this.device.id }).catch(() => {}); }
    async getPrimaryService(uuid) {
      if (!this.connected) throw new DOMException('Not connected', 'NetworkError');
      return new Service(this.device, norm(uuid));
    }
  }

  class Device extends EventTarget {
    constructor(id, name, serviceUuid) {
      super();
      Object.assign(this, { id, name, serviceUuid, chars: new Map() });
      this.gatt = new Gatt(this);
    }
    characteristic(service, uuid) {
      const key = `${service}/${uuid}`;
      if (!this.chars.has(key)) this.chars.set(key, new Characteristic(this, service, uuid));
      return this.chars.get(key);
    }
  }

  const device = (id, name, service = SERVICE) => {
    let d = devices.get(id);
    if (!d) devices.set(id, (d = new Device(id, name, norm(service))));
    else if (name) d.name = name;
    return d;
  };

  on('ble.notify', ({ id, char, data }) => {
    const d = devices.get(id);
    if (!d) return;
    const u = norm(char);
    for (const c of d.chars.values()) {
      if (c.uuid !== u) continue;
      const bytes = fromB64(data);
      c.value = new DataView(bytes.buffer);
      c.dispatchEvent(new Event('characteristicvaluechanged'));
    }
  });
  on('ble.disconnected', ({ id }) => {
    const d = devices.get(id);
    if (!d || !d.gatt.connected) return;
    d.gatt.connected = false;
    d.dispatchEvent(new Event('gattserverdisconnected'));
  });

  const bluetooth = {
    async getAvailability() { return (await call('ble.state')) !== 'unsupported'; },
    async getDevices() { return (await call('ble.known')).map((x) => device(x.id, x.name)); },
    async requestDevice(options = {}) {
      const service = options.filters?.flatMap((f) => f.services || [])[0] || options.optionalServices?.[0] || SERVICE;
      const picked = await choose(norm(service));
      return device(picked.id, picked.name, service);
    },
  };
  Object.defineProperty(navigator, 'bluetooth', { value: bluetooth, configurable: true });
}

// The "choose a device" list a browser would show.
function choose(service) {
  return new Promise((resolve, reject) => {
    const sheet = document.createElement('div');
    sheet.className = 'sheet';
    sheet.innerHTML = `<div class="sheet-card"><div class="grab"></div><h2>Choose your pet</h2>
      <div class="sub">Looking for Peekabytes nearby…</div><div class="picker"></div>
      <button class="btn block" style="margin-top:14px">Cancel</button></div>`;
    const list = sheet.querySelector('.picker');
    const sub = sheet.querySelector('.sub');
    const rows = new Map();
    let offFound = () => {}, offError = () => {};
    const finish = (err, pick) => {
      offFound();
      offError();
      call('ble.scan', { on: false }).catch(() => {});
      sheet.remove();
      if (err) reject(err);
      else resolve(pick);
    };
    const cancel = () => finish(new DOMException('No pet picked', 'NotFoundError'));
    sheet.querySelector('button').onclick = cancel;
    sheet.addEventListener('click', (e) => { if (e.target === sheet) cancel(); });
    offFound = on('ble.found', ({ id, name, rssi }) => {
      let row = rows.get(id);
      if (!row) {
        row = document.createElement('button');
        row.className = 'pick';
        row.onclick = () => finish(null, { id, name: row.dataset.name });
        rows.set(id, row);
        list.append(row);
      }
      row.dataset.name = name;
      const bars = !rssi ? 'connected' : rssi >= -60 ? '▂▄▆█' : rssi >= -70 ? '▂▄▆' : rssi >= -80 ? '▂▄' : '▂';
      row.replaceChildren(Object.assign(document.createElement('b'), { textContent: name }),
        Object.assign(document.createElement('span'), { textContent: bars }));
      sub.textContent = 'Tap your pet to connect.';
    });
    offError = on('ble.scanError', ({ error }) => { sub.textContent = error; });
    document.body.append(sheet);
    call('ble.scan', { on: true, service }).catch((e) => { sub.textContent = e.message; });
  });
}
