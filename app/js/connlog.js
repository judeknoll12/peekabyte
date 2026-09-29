// A short history of the Bluetooth link (when it dropped and why), shown under Settings >
// Connection and copyable as a report. Kept in localStorage so it survives page reloads.

const KEY = 'peekabyte.connlog';
const MAX = 40;
let items = [];
let seen = new Set();
try {
  const saved = JSON.parse(localStorage.getItem(KEY) || '{}');
  items = Array.isArray(saved.items) ? saved.items : [];
  seen = new Set(Array.isArray(saved.seen) ? saved.seen : []);
} catch { /* private mode or junk */ }

function save() {
  try { localStorage.setItem(KEY, JSON.stringify({ items, seen: [...seen].slice(-60) })); } catch { /* ignore */ }
}

// kind: 'ok' (connected), 'warn' (dropped), 'info'
export function log(text, kind = 'info', t = Date.now()) {
  items.push({ t, text, kind });
  items.sort((a, b) => a.t - b.t);
  if (items.length > MAX) items = items.slice(-MAX);
  save();
}

// Log something only once, even across reloads (e.g. a drop the pet reports again and again).
export function logOnce(key, text, kind, t) {
  if (seen.has(key)) return false;
  seen.add(key);
  log(text, kind, t);
  return true;
}

export function entries() { return items; }

export function clear() {
  items = [];
  save();
}

// Why the pet says a link ended (Bluetooth disconnect reason codes, plus our own 0xF0).
export const DROP_WHY = {
  0x08: 'Signal lost: too far away, or radio interference.',
  0x13: 'The phone closed it: app in the background, screen locked, or Bluetooth turned off.',
  0x15: 'The phone turned Bluetooth off.',
  0x16: 'The pet closed it.',
  0x22: 'The phone stopped answering.',
  0x3b: 'The phone refused the link settings.',
  0x3d: 'A packet arrived scrambled.',
  0x3e: 'It never got going.',
  0xf0: 'Another phone connected.',
};
export const dropWhy = (code) => DROP_WHY[code] || `Reason code 0x${Number(code).toString(16).padStart(2, '0')}.`;

// Why the pet last restarted (ESP32 reset reasons).
export const RESET_WHY = {
  1: 'power on', 2: 'reset button', 3: 'restarted on purpose', 4: 'crash', 5: 'froze (watchdog)',
  6: 'froze (watchdog)', 7: 'froze (watchdog)', 8: 'woke from deep sleep', 9: 'power dip (brownout)',
  14: 'power glitch', 15: 'crash',
};
export const BAD_RESETS = new Set([4, 5, 6, 7, 9, 14, 15]);
