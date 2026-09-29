// Phone <-> pet message format. Keep in sync with firmware/Peekabyte/protocol.h.

export const OP = {
  HELLO: 0x01, FRAME_REQ: 0x02, FEED: 0x03, PET: 0x04, CARE: 0x05, TRICK: 0x06, TRICK_DEF: 0x07,
  AVATAR: 0x08, NAME: 0x09, SET: 0x0a, GAME: 0x0b, SAY: 0x0c, TALK: 0x0d, EMOTE: 0x0e, SYS: 0x0f,
  CALIB: 0x10, LOOK: 0x11, DIARY: 0x12, TRAITS: 0x13, PING: 0x14, LISTEN: 0x15,
};
export const HELLO = { NO_MIRROR: 1 };
export const CARE = { CLEAN: 1, MEDICINE: 2, LIGHTS_OFF: 3, LIGHTS_ON: 4, BOOP: 5, TICKLE: 6, SLEEP: 7, WAKE: 8 };
export const TRICK_MODE = { COMMAND: 0, TREAT: 1, PRAISE: 2 };
export const SET = {
  CONTRAST: 1, FLIP: 2, AUTOROTATE: 3, DRIVER: 4, BEDTIME: 5, WAKETIME: 6, BUBBLES: 7, SENS: 8, SLEEPDIM: 9,
  TIMESCALE: 10, MANUALSLEEP: 12,
};
export const GAME = { START: 1, INPUT: 2, QUIT: 3, CATCH: 1, WHICHWAY: 2 };
export const SYS = { REBOOT: 1, NEW_EGG: 2, FACTORY: 3, HATCH: 4, CONNECT_CARD: 5 };
export const CALIB = { UPRIGHT: 1, FLAT: 2, RESET: 3 };
export const EMO = {
  none: 0, happy: 1, joy: 2, sad: 3, angry: 4, surprised: 5, scared: 6, sleepy: 7, love: 8, stars: 9,
  thinking: 10, smug: 11, silly: 12, cry: 13, wink: 14,
};

const utf8 = new TextEncoder();

export function bytes(...parts) {
  const out = [];
  for (const p of parts) {
    if (typeof p === 'number') out.push(p & 0xff);
    else for (const b of p) out.push(b);
  }
  return new Uint8Array(out);
}

export const enc = {
  hello(url, flags = 0) {
    const epoch = Math.floor(Date.now() / 1000);
    const tz = -new Date().getTimezoneOffset();
    const u = url.startsWith('https://') ? utf8.encode(url).slice(0, 99) : new Uint8Array(0);
    return bytes(OP.HELLO, epoch & 0xff, (epoch >> 8) & 0xff, (epoch >> 16) & 0xff, (epoch >>> 24) & 0xff,
      tz & 0xff, (tz >> 8) & 0xff, flags, u.length, u);
  },
  frameReq: (key) => bytes(OP.FRAME_REQ, key ? 1 : 0),
  feed: (food) => bytes(OP.FEED, food),
  pet: (phase, x, y) => bytes(OP.PET, phase, Math.max(0, Math.min(127, x | 0)), Math.max(0, Math.min(63, y | 0))),
  care: (a) => bytes(OP.CARE, a),
  trick: (mode, id) => bytes(OP.TRICK, mode, id),
  trickDef(slot, name, moves) {
    const n = utf8.encode(name).slice(0, 15);
    return bytes(OP.TRICK_DEF, slot, n.length, n, moves.length, moves);
  },
  avatar: (fields, how) => bytes(OP.AVATAR, fields, how),   // how: 0 random, 1 custom, 2 preview, 3 cancel
  name: (name, random) => bytes(OP.NAME, random ? 1 : 0, utf8.encode(name).slice(0, 16)),
  set: (key, v) => bytes(OP.SET, key, v & 0xff, (v >> 8) & 0xff),
  game: (cmd, arg) => bytes(OP.GAME, cmd, arg),
  say: (text, bubble) => bytes(OP.SAY, bubble ? 1 : 0, utf8.encode(text).slice(0, 70)),
  talk: (level) => bytes(OP.TALK, Math.max(0, Math.min(255, level | 0))),
  emote: (e, tenths) => bytes(OP.EMOTE, e, tenths),
  sys: (a) => bytes(OP.SYS, a),
  calib: (s) => bytes(OP.CALIB, s),
  look: (x, y) => bytes(OP.LOOK, x & 0xff, y & 0xff),
  lookRelease: () => bytes(OP.LOOK, 0x80, 0),
  diary: () => bytes(OP.DIARY),
  traits: (a, b) => bytes(OP.TRAITS, a, b),
  ping: () => bytes(OP.PING),
  listen: (level) => bytes(OP.LISTEN, level ? Math.max(1, Math.min(255, level | 0)) : 0),   // 0 = done
};

// Firmware version checks ("1.2.0" >= "1.2").
export function fwAtLeast(v, want) {
  const a = String(v || '0').split('.').map(Number);
  const b = String(want).split('.').map(Number);
  for (let i = 0; i < 3; i++) if ((a[i] || 0) !== (b[i] || 0)) return (a[i] || 0) > (b[i] || 0);
  return true;
}

// ---- The pet's world ---------------------------------------------------------------
export const FOODS = [
  { name: 'Apple', emoji: '🍎' }, { name: 'Cookie', emoji: '🍪' }, { name: 'Pizza', emoji: '🍕' },
  { name: 'Sushi', emoji: '🍣' }, { name: 'Carrot', emoji: '🥕' }, { name: 'Cake', emoji: '🍰' },
  { name: 'Burger', emoji: '🍔' }, { name: 'Ice cream', emoji: '🍦' }, { name: 'Broccoli', emoji: '🥦' },
  { name: 'Candy', emoji: '🍬' }, { name: 'Taco', emoji: '🌮' }, { name: 'Milk', emoji: '🥛' },
];

export const TRAITS = [
  { name: 'Playful', emoji: '🎈', blurb: 'always up for a game and learns tricks fast' },
  { name: 'Sleepy', emoji: '😴', blurb: 'loves naps and tires quickly' },
  { name: 'Foodie', emoji: '🍩', blurb: 'gets hungry fast and adores snacks' },
  { name: 'Cuddly', emoji: '🧸', blurb: 'needs lots of pets and attention' },
  { name: 'Curious', emoji: '🔭', blurb: 'gets bored when nothing happens' },
  { name: 'Sassy', emoji: '💅', blurb: 'a little dramatic and cheeky' },
  { name: 'Shy', emoji: '🙈', blurb: 'startles easily but warms up with love' },
  { name: 'Brave', emoji: '🦸', blurb: 'loves being shaken, spun and tossed' },
];

export const STAGES = ['Egg', 'Baby', 'Kid', 'Teen', 'Adult'];

export const TRICKS = [
  { name: 'Spin', emoji: '🌀' }, { name: 'Jump', emoji: '⬆️' }, { name: 'Wink', emoji: '😉' },
  { name: 'Eye roll', emoji: '🙄' }, { name: 'Dance', emoji: '💃' }, { name: 'Play dead', emoji: '💀' },
  { name: 'Peekaboo', emoji: '🙈' }, { name: 'Backflip', emoji: '🤸' }, { name: 'Moonwalk', emoji: '🕺' },
  { name: 'Sing', emoji: '🎵' }, { name: 'Bow', emoji: '🎩' }, { name: 'Magic', emoji: '✨' },
];
export const BUILTIN_TRICKS = 12, CUSTOM_TRICKS = 8, TRICK_MOVES_MAX = 10;

export const MOVES = [
  ['Hop', '🐇'], ['Spin', '🌀'], ['Squish', '🫠'], ['Stretch', '🧘'], ['Look left', '👈'], ['Look right', '👉'],
  ['Look up', '👆'], ['Look down', '👇'], ['Blink', '😌'], ['Wink', '😉'], ['Wiggle', '〰️'], ['Flip', '🤸'],
  ['Shake head', '🙅'], ['Nod', '🙆'], ['Hearts', '💕'], ['Stars', '🤩'], ['Sparkle', '✨'], ['Sing', '🎵'],
  ['Tip hat', '🎩'], ['Hide', '🙈'], ['Bounce', '🏀'], ['Grow', '🔍'], ['Shrink', '🤏'], ['Cross-eyed', '😵‍💫'],
  ['Eye roll', '🙄'], ['Dash left', '⬅️'], ['Dash right', '➡️'], ['Giggle', '😆'], ['Love', '😍'], ['Faint', '💫'],
  ['Dizzy', '😵'], ['Wobble', '🍮'],
].map(([name, emoji]) => ({ name, emoji }));

// Look editor: field order matches the firmware's Avatar struct.
export const LOOK_FIELDS = [
  { key: 'eyes', label: 'Eyes', options: ['Round', 'Oval', 'Robo', 'Wide', 'Droopy', 'Anime', 'Cat', 'Pixel'] },
  { key: 'size', label: 'Size', options: ['XS', 'S', 'M', 'L', 'XL'] },
  { key: 'gap', label: 'Spacing', options: ['Close', 'Snug', 'Normal', 'Wide', 'Far'] },
  { key: 'pupil', label: 'Pupils', options: ['Dot', 'Big', 'Tiny', 'Ring', 'Sparkle', 'None', 'Slit'] },
  { key: 'lashes', label: 'Lashes', options: ['None', 'Flirty', 'Full'] },
  { key: 'brows', label: 'Brows', options: ['None', 'Thin', 'Bold', 'Fluffy', 'Unibrow'] },
  {
    key: 'hat', label: 'Hat', options: ['None', '🎩 Top hat', '🥳 Party hat', '👑 Crown', '🧶 Beanie', '🧢 Cap',
      '🤠 Cowboy', '🧙 Wizard', '👨‍🍳 Chef', '🎀 Bow', '🌼 Flower', '🎧 Headphones', '😇 Halo', '😈 Horns',
      '📡 Antenna', '🐰 Bunny ears', '🐱 Cat ears', '🚁 Propeller', '🏴‍☠️ Pirate', '⚔️ Viking'],
  },
  {
    key: 'glasses', label: 'Glasses', options: ['None', '👓 Round', '🔲 Square', '🕶️ Shades', '💖 Hearts',
      '⭐ Stars', '🧐 Monocle', '🎬 3D', '🤓 Nerd', '🤖 Visor', '🥽 Goggles'],
  },
  { key: 'face', label: 'Face', options: ['None', 'Moustache', 'Handlebar', 'Walrus', 'Pencil', 'Curly', 'Beard', 'Goatee'] },
  { key: 'mouth', label: 'Mouth', options: ['None', 'Smile', 'Grin', 'Cat :3', 'Fangs', 'Tongue'] },
  { key: 'cheeks', label: 'Cheeks', options: ['None', 'Blush', 'Freckles', 'Blush + freckles', 'Whiskers'] },
  { key: 'neck', label: 'Neck', options: ['None', 'Bow tie', 'Scarf', 'Collar', 'Bandana', 'Pearls'] },
];

export const DIARY = {
  1: (a, n) => `${n} hatched! 🐣`,
  2: (a, n) => `${n} grew into a ${STAGES[a] || 'bigger pet'}! 🌱`,
  3: (a, n, tn) => `${n} mastered a trick: ${tn(a)} 🏆`,
  4: (a, n) => `${n} caught a cold 🤒`,
  5: (a, n) => (a ? `${n} got better on their own 💪` : `${n} took medicine and got better 💊`),
  6: (a, n) => `${n} discovered a favorite food: ${FOODS[a]?.name || '?'} ${FOODS[a]?.emoji || ''}`,
  7: (a, n) => `New Snack Catch record: ${a} 🏅`,
  8: (a, n) => `Got a new name: ${n} ✍️`,
  9: (a, n) => `${n}'s first full night of sleep 🌙`,
  10: (a, n, tn) => `${n} learned the moves for a new trick: ${tn(a)} 🎓`,
  11: (a, n) => `${n} got a new look ✨`,
};
