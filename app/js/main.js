// Peekabyte phone app.
import { call, isNative } from './native.js';
import { awake, keepAwake } from './awake.js';
import { Brain, MODELS, fmtSize } from './brain.js';
import * as connlog from './connlog.js';
import { Link } from './link.js';
import { Listener } from './listen.js';
import { line, starter } from './phrases.js';
import {
  BUILTIN_TRICKS, CALIB, CARE, CUSTOM_TRICKS, DIARY, EMO, FOODS, GAME, HELLO, LOOK_FIELDS, MOVES, SET, STAGES, SYS,
  TRAITS, TRICKS, TRICK_MODE, TRICK_MOVES_MAX, enc, fwAtLeast,
} from './protocol.js';
import * as sfx from './sfx.js';
import { EFFECTS, KOKORO_VOICES, PRESETS, Voice, kokoroDownloadMB, nativeVoiceCheck } from './voice.js';

const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'html') e.innerHTML = v;
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat()) if (kid != null && kid !== false) e.append(kid.nodeType ? kid : document.createTextNode(kid));
  return e;
}

// ---------------------------------------------------------------- preferences ----
const DEFAULTS = {
  owner: '', speak: true, sfx: true, sfxVol: 0.6, brain: '', aiMode: 'special', kokoroOk: false,
  keepAwake: !isNative, lastPet: '', theme: 'system', accent: 'violet', chatty: 'normal', convo: true,
  voice: { preset: 'nemo', engine: 'kokoro', voice: 'af_heart', pitch: 0, speed: 1, fx: 'none', volume: 1, systemVoice: '' },
};
let prefs = structuredClone(DEFAULTS);
try { prefs = { ...prefs, ...JSON.parse(localStorage.getItem('peekabyte') || '{}') }; } catch { /* private mode */ }
prefs.voice = { ...DEFAULTS.voice, ...prefs.voice };
function savePrefs() { try { localStorage.setItem('peekabyte', JSON.stringify(prefs)); } catch { /* ignore */ } }

// ---------------------------------------------------------------- globals ----
const link = new Link();
const voice = new Voice();
const brain = new Brain();
const listener = new Listener();
let S = null;                 // latest state from the pet
const ui = {
  tab: 'home', train: -1, look: null, lookCat: 0, lookDirty: false, lookRandom: false, hatchThenStyle: false,
  sheet: null, gameSheet: false, lastEgg: false, calibStep: 0, diary: null,
  said: null,                 // the last thing said: { who: 'pet' | 'me', text, at }
  talk: 'idle',               // the talk card: idle | listening | thinking | speaking
  convo: false,               // talking back and forth with the microphone
  heard: '',                  // words heard so far while listening
};

// ---------------------------------------------------------------- theme ----
// Light or dark (or whatever the phone uses), plus an accent color. The iPhone app gets told
// too, so its status bar and backgrounds match.
const ACCENTS = {
  violet: ['Violet', '#6c4df5', '#9d80ff'], ocean: ['Ocean', '#0b8fd8', '#38c6f8'], mint: ['Mint', '#0e9f6e', '#34d399'],
  peach: ['Peach', '#ea6a10', '#fb9a4b'], rose: ['Rose', '#e0357f', '#f779b0'],
};
const systemDark = matchMedia('(prefers-color-scheme: dark)');
// iPhone app builds from before themes keep a white status bar: stay dark there unless asked.
let oldShell = isNative && window.PeekaNative?.version === '1.0';
const themeNow = () => (prefs.theme === 'system' ? (systemDark.matches || oldShell ? 'dark' : 'light') : prefs.theme);

function applyTheme() {
  const t = themeNow();
  const root = document.documentElement;
  root.dataset.theme = t;
  root.dataset.accent = ACCENTS[prefs.accent] ? prefs.accent : 'violet';
  const bg = t === 'dark' ? '#0d0e1a' : '#f4f2fb';
  document.querySelector('meta[name="theme-color"]')?.setAttribute('content', bg);
  if (isNative && !oldShell) {
    call('app.theme', { dark: t === 'dark', bg, follow: prefs.theme === 'system' }).catch(() => {
      oldShell = true;
      if (t === 'light') applyTheme();
    });
  }
}
systemDark.addEventListener?.('change', () => { if (prefs.theme === 'system') applyTheme(); });

// ---------------------------------------------------------------- helpers ----
function toast(msg, ms = 2200) {
  const t = $('#toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(toast.tm);
  toast.tm = setTimeout(() => t.classList.remove('show'), ms);
}

function fmtAge(sec) {
  if (sec < 90) return 'brand new';
  if (sec < 3600) return `${Math.round(sec / 60)} min old`;
  if (sec < 86400) return `${Math.floor(sec / 3600)} h ${Math.round((sec % 3600) / 60)} min old`;
  const d = Math.floor(sec / 86400);
  return `${d} day${d === 1 ? '' : 's'} old`;
}
function fmtWhen(sec) {
  if (sec < 60) return 'at birth';
  if (sec < 3600) return `${Math.round(sec / 60)} min`;
  if (sec < 86400) return `${Math.floor(sec / 3600)} h`;
  return `day ${Math.floor(sec / 86400) + 1}`;
}
function hhmm(min) { return `${String(Math.floor(min / 60)).padStart(2, '0')}:${String(min % 60).padStart(2, '0')}`; }
const send = (msg) => link.send(msg);

const MOOD = {
  ecstatic: ['🤩', 'over the moon'], happy: ['😊', 'happy'], okay: ['🙂', 'doing okay'], bored: ['😐', 'bored'],
  lonely: ['🥺', 'lonely'], sad: ['😢', 'sad'], sick: ['🤒', 'sick'], asleep: ['😴', 'asleep'], dizzy: ['😵‍💫', 'dizzy'],
  grumpy: ['😠', 'grumpy'], exhausted: ['🥱', 'exhausted'], starving: ['😫', 'starving'], egg: ['🥚', 'in its egg'],
};
const WANTS = {
  food: ['🍎', (n) => `${n} is hungry!`, 'Feed', 'feed'],
  sleep: ['😴', (n) => `${n} is sleepy. Put it to bed?`, 'Sleep', 'sleep'],
  love: ['💗', (n) => `${n} wants some love. Hold the Pet button, or rock it gently.`, 'Pet', 'pet'],
  play: ['🎮', (n) => `${n} is bored. Play a game!`, 'Play', 'play'],
  medicine: ['🤒', (n) => `${n} is sick and needs medicine.`, 'Medicine', 'medicine'],
  clean: ['🧽', () => 'Crumbs everywhere! Clean up, or tilt the pet to shake them off.', 'Clean', 'clean'],
};
const NEEDS = [
  { name: 'Food', ico: '🍎', c: 'var(--food)' },
  { name: 'Energy', ico: '⚡', c: 'var(--energy)' },
  { name: 'Fun', ico: '🎈', c: 'var(--fun)' },
  { name: 'Love', ico: '💗', c: 'var(--love)' },
  { name: 'Health', ico: '🩺', c: 'var(--health)' },
];

function trickName(id) {
  id = Number(id);
  if (id < BUILTIN_TRICKS) return TRICKS[id]?.name || 'a trick';
  const c = S?.ct?.[id - BUILTIN_TRICKS];
  return c ? c[0] : 'a trick';
}
function trickEmoji(id) { return id < BUILTIN_TRICKS ? TRICKS[id].emoji : '🌟'; }

function describeLook(av) {
  if (!av) return 'nothing special';
  const parts = [];
  for (const i of [6, 7, 8, 9, 11]) if (av[i]) parts.push(LOOK_FIELDS[i].options[av[i]].replace(/^\S+\s/, (m) => (/\p{Extended_Pictographic}/u.test(m) ? '' : m)).toLowerCase());
  return parts.length ? parts.join(', ') : 'nothing but your big eyes';
}

function ctxFor(ev = {}) {
  const traits = (S?.tr || [0, 1]).map((i) => TRAITS[i]?.name || '');
  const tricks = [];
  (S?.sk || []).forEach((v, i) => { if (v >= 80) tricks.push(trickName(i)); });
  const x = ev.x;
  const food = FOODS.find((f) => f.name === x)?.name || x;
  return {
    name: S?.name || 'your pet', owner: prefs.owner || 'friend', stage: STAGES[S?.stage] || 'kid', stageNum: S?.stage ?? 2,
    mood: MOOD[S?.mood]?.[1] || 'okay', fav: FOODS[S?.fav]?.name || 'snacks', hate: FOODS[S?.hate]?.name || 'broccoli',
    traits, wearing: describeLook(S?.av),
    needs: { food: S?.n?.[0] ?? 50, energy: S?.n?.[1] ?? 50, fun: S?.n?.[2] ?? 50, love: S?.n?.[3] ?? 50 },
    sick: !!S?.sick, tricks, food, trick: /^\d+$/.test(x || '') ? trickName(x) : x, score: x,
  };
}

// ---------------------------------------------------------------- confetti ----
function confetti(n = 90) {
  const c = $('#confetti');
  const ctx = c.getContext('2d');
  c.width = innerWidth * devicePixelRatio;
  c.height = innerHeight * devicePixelRatio;
  const colors = ['#5ee7ff', '#b58cff', '#ff7ac6', '#ffd84a', '#6dffb0'];
  const parts = Array.from({ length: n }, () => ({
    x: c.width / 2, y: c.height * 0.35, vx: (Math.random() - 0.5) * 18 * devicePixelRatio,
    vy: (-Math.random() * 16 - 6) * devicePixelRatio, r: (3 + Math.random() * 4) * devicePixelRatio,
    c: colors[(Math.random() * colors.length) | 0], a: Math.random() * 6, va: (Math.random() - 0.5) * 0.4,
  }));
  let frames = 0;
  (function tick() {
    ctx.clearRect(0, 0, c.width, c.height);
    for (const p of parts) {
      p.vy += 0.45 * devicePixelRatio;
      p.x += p.vx;
      p.y += p.vy;
      p.a += p.va;
      ctx.save();
      ctx.translate(p.x, p.y);
      ctx.rotate(p.a);
      ctx.fillStyle = p.c;
      ctx.fillRect(-p.r, -p.r / 2, p.r * 2, p.r);
      ctx.restore();
    }
    if (++frames < 150) requestAnimationFrame(tick);
    else ctx.clearRect(0, 0, c.width, c.height);
  })();
}

// ---------------------------------------------------------------- sheets ----
function openSheet(build, key = 'x') {
  const card = $('#sheetCard');
  card.innerHTML = '';
  card.append(el('div', { class: 'grab' }));
  build(card);
  $('#sheet').classList.remove('hidden');
  $('#sheet').classList.toggle('clear', key === 'game');   // keep the pet's screen visible while playing
  ui.sheet = key;
}
function closeSheet() {
  $('#sheet').classList.add('hidden');
  if (ui.sheet === 'game' && S?.game) send(enc.game(GAME.QUIT, 0));
  ui.sheet = null;
  ui.gameSheet = false;
}
$('#sheet').addEventListener('click', (e) => { if (e.target.id === 'sheet') closeSheet(); });

function confirmSheet(title, text, yes, onYes, danger = false) {
  openSheet((c) => {
    c.append(el('h2', {}, title), el('div', { class: 'sub' }, text),
      el('div', { class: 'row' },
        el('button', { class: 'btn grow', onclick: closeSheet }, 'Cancel'),
        el('button', { class: `btn grow ${danger ? 'danger' : 'primary'}`, onclick: () => { closeSheet(); onYes(); } }, yes)));
  });
}

// ---------------------------------------------------------------- connection ----
function showMain(on) {
  $('#connect').classList.toggle('hidden', on);
  $('#main').classList.toggle('hidden', !on);
}

const signalBars = (rssi) => (rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1);
const signalWord = (rssi) => ['', 'weak', 'fair', 'good', 'excellent'][signalBars(rssi)];
function fmtSecs(s) {
  s = Math.round(s);
  if (s < 60) return `${s} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min${s % 60 && s < 600 ? ` ${s % 60} s` : ''}`;
  return `${Math.floor(s / 3600)} h ${Math.round((s % 3600) / 60)} min`;
}
const fmtMs = (ms) => (ms < 1000 ? `${Math.round(ms)} ms` : `${+(ms / 1000).toFixed(1)} s`);
function fmtClock(t) {
  const d = new Date(t);
  const time = d.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit', second: '2-digit' });
  return d.toDateString() === new Date().toDateString() ? time : `${d.toLocaleDateString([], { month: 'short', day: 'numeric' })} ${time}`;
}

function renderConn() {
  const st = link.state;
  $('#connDot').className = 'dot' + (st === 'connected' ? ' on' : st === 'lost' || st === 'connecting' ? ' warn' : '');
  $('#connText').textContent = st === 'connected' ? 'Connected' : st === 'lost' ? 'Reconnecting…' : st === 'connecting' ? 'Connecting…' : 'Offline';
  const rs = S?.lk?.rs;
  const n = st === 'connected' && link.kind === 'ble' && S?.lk?.on && rs != null && rs < 20 ? signalBars(rs) : 0;
  const bars = $('#connBars');
  bars.classList.toggle('hidden', !n);
  bars.dataset.n = n;
  bars.title = n ? `Signal ${signalWord(rs)} (${rs} dBm)` : '';
}

function updateConnectButton() {
  $('#btnBleText').textContent = prefs.lastPet ? `Connect to ${prefs.lastPet}` : 'Connect with Bluetooth';
}

link.addEventListener('status', (e) => {
  const { state, name, why, dropped, downFor, replaced } = e.detail;
  renderConn();
  if (state === 'connected') {
    showMain(true);
    $('#connectStatus').textContent = '';
    send(enc.hello(location.protocol === 'https:' ? location.origin + location.pathname : '', HELLO.NO_MIRROR));
    if (link.kind === 'ble') {
      keepAwake(prefs.keepAwake);
      if (name && name !== prefs.lastPet) { prefs.lastPet = name; savePrefs(); updateConnectButton(); }
      connlog.log(downFor ? `Reconnected after ${fmtSecs(downFor / 1000)}` : `Connected to ${name || 'the pet'}`, 'ok');
    }
  } else if (state === 'connecting') {
    $('#connectStatus').textContent = `Connecting to ${name || 'your pet'}…`;
  } else if (state === 'lost') {
    if (dropped) connlog.log(`Link lost (${why || 'no reason given'}). Reconnecting…`, 'warn');
  } else if (state === 'idle') {
    if (replaced) {
      keepAwake(false);
      connlog.log('Another phone connected to the pet', 'warn');
      showMain(false);
      $('#connectStatus').textContent = `Another phone connected to ${name || 'your pet'}. Tap Connect to take it back.`;
    } else if (link.userClosed) {
      keepAwake(false);
    }
    if (!S) showMain(false);
  }
  if (ui.sheet === 'conn') openConnection();
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden && link.connected && link.kind === 'ble') connlog.log('App went to the background', 'info');
});

// The pet reports its side of the link: past drops (with the reason only it can see) and restarts.
let petBoot = 0;
function trackLink(lk) {
  if (!lk) return;
  const boot = Date.now() - lk.up * 1000;
  const bootKey = Math.round(boot / 20000);
  const why = connlog.RESET_WHY[lk.rr] || `reason ${lk.rr}`;
  if (petBoot && boot - petBoot > 20000) {
    connlog.logOnce(`boot:${bootKey}`, `The pet restarted (${why})`, connlog.BAD_RESETS.has(lk.rr) ? 'warn' : 'info', boot);
  } else if (!petBoot && connlog.BAD_RESETS.has(lk.rr)) {
    connlog.logOnce(`boot:${bootKey}`, `The pet restarted (${why})`, 'warn', boot);
  }
  petBoot = boot;
  for (const [at, lasted, code] of lk.dr || []) {
    connlog.logOnce(`drop:${bootKey}:${at}:${code}`, `Pet: link ended after ${fmtSecs(lasted)}. ${connlog.dropWhy(code)}`,
      code === 0x08 || code === 0x22 ? 'warn' : 'info', boot + at * 1000);
  }
}

// ---------------------------------------------------------------- updates ----
// The app's pages come from GitHub Pages. When a newer version is live, reload into it at a
// quiet moment. In the iPhone app the pet stays connected through the reload (the Bluetooth
// link lives in the app, not the page); in a browser the reload would drop it, so there the
// update waits until the next visit.
const BUILD = document.querySelector('meta[name="peekabyte-build"]')?.content || 'dev';
let updateReady = false;
let updateToldUser = false;

async function checkForUpdate() {
  if (BUILD === 'dev' || location.protocol !== 'https:' || updateReady) return;
  try {
    const live = await fetch('version.json', { cache: 'no-store' }).then((r) => (r.ok ? r.json() : null));
    if (!live?.build || live.build === BUILD) return;
    // Right after a deploy the servers can briefly hand out the old page with the new stamp:
    // reload for a given version at most once every 15 minutes, never in a loop.
    let tried = null;
    try { tried = JSON.parse(sessionStorage.getItem('peekabyte.reloadedFor') || 'null'); } catch { /* ignore */ }
    if (tried?.build === live.build && Date.now() - tried.at < 15 * 60 * 1000) return;
    updateReady = live.build;
    applyUpdate();
  } catch { /* offline */ }
}

function quietMoment() {
  const typing = ['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement?.tagName);
  return !ui.sheet && !ui.lookDirty && !S?.game && !typing && !voice.busy && ui.train < 0 && ui.talk === 'idle' && !ui.convo;
}

function applyUpdate() {
  if (!updateReady) return;
  if (!isNative && link.connected) {
    if (!updateToldUser) { updateToldUser = true; toast('An update is ready: it loads the next time you open Peekabyte', 3500); }
    return;
  }
  if (!quietMoment()) return;   // tried again every few seconds
  try { sessionStorage.setItem('peekabyte.reloadedFor', JSON.stringify({ build: updateReady, at: Date.now() })); } catch { /* ignore */ }
  connlog.log('Updated the app', 'info');
  location.reload();
}

function startUpdates() {
  setTimeout(checkForUpdate, 8000);
  setInterval(() => { if (!document.hidden) checkForUpdate(); }, 10 * 60 * 1000);
  setInterval(applyUpdate, 5000);
  document.addEventListener('visibilitychange', () => { if (!document.hidden) checkForUpdate(); });
}

// ---------------------------------------------------------------- crash guard ----
// Phones kill a web page that uses too much memory (the AI models are big), which drops
// Bluetooth too. If the last visit ended that way while a model was loaded, hold off on it.
const GUARD_KEY = 'peekabyte.alive';
const lastVisit = (() => { try { return JSON.parse(localStorage.getItem(GUARD_KEY) || 'null'); } catch { return null; } })();
const crashedLastTime = !!(lastVisit?.vis && Date.now() - lastVisit.t < 3600e3);
function guardBeat() {
  try {
    localStorage.setItem(GUARD_KEY, JSON.stringify({
      t: Date.now(), vis: !document.hidden,
      ai: !isNative && (brain.state === 'loading' || brain.state === 'ready') ? prefs.brain : '',
      tts: (voice.kState === 'loading' || voice.kState === 'ready') && voice.kBackend !== 'native',
    }));
  } catch { /* private mode */ }
}
function startGuard() {
  guardBeat();
  setInterval(guardBeat, 2000);
  document.addEventListener('visibilitychange', guardBeat);
  addEventListener('pagehide', () => { try { localStorage.removeItem(GUARD_KEY); } catch { /* ignore */ } });
  if (!crashedLastTime) return;
  const heavy = [lastVisit.ai && MODELS.find((m) => m.key === lastVisit.ai)?.name && `the ${MODELS.find((m) => m.key === lastVisit.ai).name} AI brain`,
    lastVisit.tts && 'the natural voice'].filter(Boolean);
  connlog.log(`The app closed unexpectedly${heavy.length ? ` while running ${heavy.join(' and ')}` : ''}`, 'warn', lastVisit.t);
  if (!heavy.length) return;
  ui.aiPaused = true;
  openSheet((c) => {
    c.append(el('h2', {}, '😵 The app closed unexpectedly'),
      el('div', { class: 'sub' }, `Last time, Peekabyte stopped while running ${heavy.join(' and ')}. That usually means the phone ran out of memory, which also drops the Bluetooth link. They're paused for now.`),
      el('div', { class: 'row' },
        el('button', { class: 'btn grow', onclick: () => { ui.aiPaused = false; closeSheet(); applyVoice(); if (prefs.brain) brain.load(prefs.brain); } }, 'Load them anyway'),
        el('button', { class: 'btn primary grow', onclick: () => { prefs.brain = ''; prefs.kokoroOk = false; savePrefs(); ui.aiPaused = false; closeSheet(); renderBrainCard(); } }, 'Turn them off')));
  }, 'crash');
}

link.addEventListener('json', (e) => {
  const m = e.detail;
  if (m.t === 'state') onState(m);
  else if (m.t === 'ev') onEvent(m);
  else if (m.t === 'diary') { ui.diary = m.items; if (ui.sheet === 'diary') openDiary(); }
});

async function connectBle() {
  sfx.unlock();
  try {
    $('#connectStatus').textContent = 'Pick your Peekabyte in the list…';
    await link.pickBle();
  } catch (err) {
    $('#connectStatus').textContent = err?.name === 'NotFoundError' ? 'No pet picked. Is it powered on and nearby?' : `Couldn't connect: ${err?.message || err}`;
  }
}

// ---------------------------------------------------------------- state -> UI ----
function onState(m) {
  const first = !S;
  const wasEgg = S && S.stage === 0;
  const wasAwake = S && !S.sleep;
  S = m;
  if (wasAwake && m.sleep && (ui.convo || listener.active)) endConversation();
  link.setMtu(m.mtu || 23);
  trackLink(m.lk);
  renderConn();
  if (first && !ui.look) ui.look = [...m.av];
  if (wasEgg && m.stage > 0) {
    confetti(140);
    toast(`🐣 Say hi to ${m.name}!`);
    if (ui.hatchThenStyle) { ui.hatchThenStyle = false; setTimeout(() => showTab('style'), 1600); }
  }
  renderAll();
}

function renderAll() {
  if (!S) return;
  renderHeader();
  renderHome();
  renderTricks();
  if (ui.tab === 'style') renderStyle();
  if (ui.tab === 'more') renderSettings();
  if (ui.sheet === 'game') renderGameSheet();
  if (ui.sheet === 'conn') openConnection();
}

function fmtTime(min) {
  return new Date(2000, 0, 1, Math.floor(min / 60), min % 60).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
}
const newFirmware = () => fwAtLeast(S?.v, '1.2');
const onSchedule = () => !S?.set?.sm;
function sleepLine() {
  if (!S?.sleep) return '';
  return S.night && onSchedule() && S.time ? `asleep until ${fmtTime(S.set.w)}` : 'asleep';
}

function renderHeader() {
  $('#petName').textContent = S.name;
  $('#stageBadge').textContent = S.hatching ? 'Hatching' : STAGES[S.stage];
  const mood = MOOD[S.mood] || ['🙂', S.mood];
  $('#moodFace').textContent = S.stage === 0 ? '🥚' : mood[0];
  const zzz = sleepLine();
  $('#petSub').textContent = S.stage === 0 ? 'Waiting to hatch…' : zzz ? `${zzz[0].toUpperCase()}${zzz.slice(1)}` : `${fmtAge(S.age)} · ${mood[1]}`;
}

function renderHome() {
  const egg = S.stage === 0;
  $('#hatchCard').classList.toggle('hidden', !egg);
  if (egg && (!ui.lastEgg || document.activeElement !== $('#hatchName'))) {
    if (document.activeElement !== $('#hatchName')) $('#hatchName').value = S.name;
  }
  ui.lastEgg = egg;
  $('#speech').classList.toggle('hidden', egg);
  $('#needsCard').classList.toggle('hidden', egg);
  $('#actions').classList.toggle('hidden', egg);
  // needs, as rings
  const needs = $('#needs');
  if (!needs.children.length) {
    NEEDS.forEach((n) => needs.append(el('div', { class: 'need', style: `--c:${n.c}` },
      el('div', {
        class: 'ring',
        html: '<svg viewBox="0 0 56 56"><circle class="track" cx="28" cy="28" r="24"/><circle class="val" cx="28" cy="28" r="24" stroke-dasharray="150.8" stroke-dashoffset="150.8"/></svg>',
      }, el('span', { class: 'ico' }, n.ico)),
      el('div', { class: 'lbl' }, n.name), el('div', { class: 'pct' }, ''))));
  }
  S.n.forEach((v, i) => {
    const d = needs.children[i];
    d.querySelector('.val').style.strokeDashoffset = String(150.8 * (1 - Math.max(0, Math.min(100, v)) / 100));
    d.querySelector('.pct').textContent = `${v}%`;
    d.classList.toggle('low', v < 25);
    d.title = `${NEEDS[i].name}: ${v}%`;
  });
  // want
  const w = WANTS[S.want];
  const showWant = !!w && !egg && !(S.sleep && S.want !== 'medicine');
  $('#want').classList.toggle('hidden', !showWant);
  if (w) {
    $('#wantIcon').textContent = w[0];
    $('#wantText').textContent = w[1](S.name);
    $('#wantDo').textContent = w[2];
    $('#wantDo').onclick = () => doAction(w[3]);
  }
  $$('.act').forEach((b) => b.classList.toggle('glow', showWant && b.dataset.act === w[3]));
  $('#sleepIcon').textContent = S.sleep ? '☀️' : '🌙';
  $('#sleepText').textContent = S.sleep ? 'Wake up' : 'Sleep';
  renderTalkCard();
  // about
  $('#aboutName').textContent = S.name;
  const traits = $('#traits');
  traits.innerHTML = '';
  S.tr.forEach((i) => traits.append(el('span', { class: 'chip', title: TRAITS[i].blurb }, `${TRAITS[i].emoji} ${TRAITS[i].name}`)));
  traits.append(el('span', { class: 'chip' }, S.kf ? `❤️ Loves ${FOODS[S.fav].name} ${FOODS[S.fav].emoji}` : '❤️ Favorite food: ???'));
  const st = S.st;
  const mastered = S.sk.filter((v) => v >= 80).length;
  $('#stats').innerHTML = '';
  [[st.fed, 'meals'], [mastered, 'tricks mastered'], [st.best, 'best catch'], [st.g, 'games'], [st.sh, 'shakes'], [st.n, 'nights']]
    .forEach(([v, l]) => $('#stats').append(el('div', { class: 'stat' }, el('b', {}, String(v)), el('span', {}, l))));
}

// ---------------------------------------------------------------- actions ----
function doAction(a) {
  sfx.unlock();
  if (!S) return;
  if (S.stage === 0) return toast('Hatch your egg first! 🥚');
  switch (a) {
    case 'feed': return openFeed();
    case 'play': return openPlay();
    case 'pet': return petBurst();
    case 'tickle': send(enc.care(CARE.TICKLE)); sfx.play('giggle'); return;
    case 'clean':
      if (!S.crumbs) return toast('Already spotless ✨');
      send(enc.care(CARE.CLEAN));
      return;
    case 'sleep': return toggleSleep();
    case 'medicine':
      if (!S.sick) return confirmSheet('Not sick', `${S.name} isn't sick. Give medicine anyway?`, 'Give it', () => send(enc.care(CARE.MEDICINE)));
      send(enc.care(CARE.MEDICINE));
      return;
  }
}
$$('.act').forEach((b) => { if (b.dataset.act !== 'pet') b.addEventListener('click', () => doAction(b.dataset.act)); });

function toggleSleep() {
  if (!S) return;
  if (S.sleep) return send(enc.care(newFirmware() ? CARE.WAKE : CARE.LIGHTS_ON));
  if (S.game) return toast('Finish the game first');
  endConversation();
  send(enc.care(newFirmware() ? CARE.SLEEP : CARE.LIGHTS_OFF));
}

// ---------------------------------------------------------------- petting ----
// Hold the Pet button to stroke the pet (its eyes follow your finger across the button);
// wiggle your finger fast and it tickles.
const PET_W = 128, PET_H = 64;
function initPetButton() {
  const b = $('.act[data-act="pet"]');
  let down = false, x = 64, y = 24, lastMove = 0, lastSend = 0, drift = null, t = 0;
  const where = (e) => {
    const r = b.getBoundingClientRect();
    return [Math.round(8 + ((e.clientX - r.left) / r.width) * (PET_W - 16)), Math.round(10 + ((e.clientY - r.top) / r.height) * (PET_H - 24))];
  };
  const start = (e) => {
    e.preventDefault();
    sfx.unlock();
    if (!S || S.stage === 0) return toast('Hatch your egg first! 🥚');
    down = true;
    try { b.setPointerCapture(e.pointerId); } catch { /* not a live pointer */ }
    b.classList.add('held');
    $('#petText').textContent = 'Petting…';
    [x, y] = where(e);
    send(enc.pet(0, x, y));
    if (!S.sleep) sfx.play('purr');
    lastMove = performance.now();
    // A resting finger still strokes: drift slowly so the pet keeps feeling it.
    drift = setInterval(() => {
      if (performance.now() - lastMove < 300) return;
      t += 0.4;
      send(enc.pet(1, Math.round(x + Math.sin(t) * 4), Math.round(y + Math.cos(t) * 2)));
    }, 250);
  };
  const move = (e) => {
    if (!down) return;
    const now = performance.now();
    lastMove = now;
    if (now - lastSend < 60) return;
    lastSend = now;
    [x, y] = where(e);
    send(enc.pet(1, x, y));
  };
  const end = () => {
    if (!down) return;
    down = false;
    clearInterval(drift);
    b.classList.remove('held');
    $('#petText').textContent = 'Pet';
    send(enc.pet(2, x, y));
    if (!S?.sleep) sfx.play('purr_end');
  };
  b.addEventListener('pointerdown', start);
  b.addEventListener('pointermove', move);
  ['pointerup', 'pointercancel', 'lostpointercapture'].forEach((ev) => b.addEventListener(ev, end));
  b.addEventListener('contextmenu', (e) => e.preventDefault());
}

// A few seconds of gentle petting, for the "wants love" button.
function petBurst() {
  if (!S || S.stage === 0) return;
  let t = 0;
  send(enc.pet(0, 64, 24));
  sfx.play('purr');
  const iv = setInterval(() => {
    t += 1;
    if (t > 14) {
      clearInterval(iv);
      send(enc.pet(2, 64, 24));
      sfx.play('purr_end');
      return;
    }
    send(enc.pet(1, Math.round(64 + Math.sin(t / 2) * 10), 24));
  }, 220);
}

function openFeed() {
  openSheet((c) => {
    c.append(el('h2', {}, `Feed ${S.name}`), el('div', { class: 'sub' }, `Food ${S.n[0]}% · everybody has a favorite (and one they hate)…`));
    const g = el('div', { class: 'foods' });
    FOODS.forEach((f, i) => g.append(el('button', {
      class: 'food',
      onclick: () => { send(enc.feed(i)); closeSheet(); },
    }, S.kf && i === S.fav ? el('span', { class: 'fav' }, '❤️') : null, el('span', { class: 'e' }, f.emoji), f.name)));
    c.append(g);
  }, 'feed');
}

function openPlay() {
  openSheet((c) => {
    c.append(el('h2', {}, 'Play time!'), el('div', { class: 'sub' }, `Fun ${S.n[2]}% · games use energy, so let ${S.name} rest after.`));
    const games = el('div', { class: 'games' });
    games.append(
      el('button', { class: 'game', onclick: () => startGame(GAME.CATCH) }, el('span', { class: 'e' }, '🍎'),
        el('div', {}, el('b', {}, 'Snack Catch'), el('span', {}, 'Tilt your Peekabyte left and right to catch treats. Dodge the spiky ones!'))),
      el('button', { class: 'game', onclick: () => startGame(GAME.WHICHWAY) }, el('span', { class: 'e' }, '👀'),
        el('div', {}, el('b', {}, 'Which Way?'), el('span', {}, 'Guess which way your pet will look. Tap an arrow or tilt the pet.'))),
      el('div', { class: 'game' }, el('span', { class: 'e' }, '🙈'),
        el('div', {}, el('b', {}, 'Peekaboo'), el('span', {}, 'Flip the pet face-down, then back up. Rock it gently for a lullaby, or give it a spin!'))),
    );
    c.append(games);
  }, 'play');
}

function startGame(id) {
  send(enc.game(GAME.START, id));
  ui.gameId = id;
  ui.gameResult = null;
  ui.gamePhase = null;
  openSheet(() => {}, 'game');
  renderGameSheet();
}

function holdButton(label, onDown, onUp) {
  const b = el('button', { class: 'btn' }, label);
  b.addEventListener('pointerdown', (e) => { e.preventDefault(); onDown(); });
  ['pointerup', 'pointerleave', 'pointercancel'].forEach((ev) => b.addEventListener(ev, onUp));
  return b;
}

// The game sheet is built once per phase and then only its numbers change, so
// a finger holding an arrow button isn't interrupted by the 4-per-second updates.
function renderGameSheet() {
  const c = $('#sheetCard');
  const g = S?.game;
  const r = ui.gameResult;
  const isCatch = (g?.id || ui.gameId) === GAME.CATCH;
  const phase = r ? 'result' : g ? (isCatch ? 'catch' : 'way') : 'wait';
  if (ui.gamePhase !== phase) {
    ui.gamePhase = phase;
    c.innerHTML = '';
    c.append(el('div', { class: 'grab' }), el('h2', {}, isCatch ? '🍎 Snack Catch' : '👀 Which Way?'));
    const stat = (id, label) => el('div', {}, el('b', { id }, ''), el('span', {}, label));
    if (phase === 'result') {
      c.append(el('div', { class: 'hud' },
        el('div', {}, el('b', {}, String(r.score)), el('span', {}, 'score')),
        el('div', {}, el('b', {}, r.best ? '🏅' : r.won ? '🎉' : '💪'), el('span', {}, r.best ? 'new record!' : r.won ? 'you won!' : 'nice try'))),
      el('div', { class: 'row' },
        el('button', { class: 'btn grow', onclick: closeSheet }, 'Done'),
        el('button', { class: 'btn primary grow', onclick: () => startGame(ui.gameId) }, 'Play again')));
      return;
    }
    if (phase === 'wait') {
      c.append(el('div', { class: 'sub' }, 'Starting…'));
      return;
    }
    if (phase === 'catch') {
      c.append(el('div', { class: 'sub' }, 'Tilt your Peekabyte to move! No motion sensor? Hold the arrows.'),
        el('div', { class: 'hud' }, stat('gScore', 'score'), stat('gLives', 'lives'), stat('gTime', 'left')),
        el('div', { class: 'pad' },
          holdButton('◀', () => send(enc.game(GAME.INPUT, 1)), () => send(enc.game(GAME.INPUT, 0))),
          holdButton('▶', () => send(enc.game(GAME.INPUT, 2)), () => send(enc.game(GAME.INPUT, 0)))));
    } else {
      c.append(el('div', { class: 'sub', id: 'gRound' }, ''),
        el('div', { class: 'hud' }, stat('gScore', 'correct')),
        el('div', { class: 'pad' },
          el('button', { class: 'btn', onclick: () => send(enc.game(GAME.INPUT, 1)) }, '⬅️'),
          el('button', { class: 'btn', onclick: () => send(enc.game(GAME.INPUT, 2)) }, '➡️')));
    }
    c.append(el('button', { class: 'btn block', style: 'margin-top:12px', onclick: closeSheet }, 'Quit game'));
  }
  if (!g || r) return;
  $('#gScore').textContent = String(g.score);
  if (phase === 'catch') {
    $('#gLives').textContent = '❤️'.repeat(Math.max(0, g.lives)) || '💔';
    $('#gTime').textContent = `${Math.max(0, Math.ceil(30 - g.t))}s`;
  } else {
    $('#gRound').textContent = `Round ${g.round} of ${g.rounds}: which way will ${S.name} look? Tap an arrow or tilt the pet.`;
  }
}

// ---------------------------------------------------------------- events from the pet ----
const SPECIAL = new Set(['greet', 'hatched', 'stage_up', 'learned', 'muse', 'record', 'new_name', 'new_look', 'cured', 'show_off', 'game_win', 'game_lose', 'peekaboo']);

function onEvent(m) {
  switch (m.e) {
    case 'say': return onSay(m);
    case 'sfx': return sfx.play(m.s);
    case 'trick': {
      if (ui.train === m.id) {
        ui.lastOk = !!m.ok;
        $('#trainResult').textContent = m.ok ? `✅ ${S.name} did it! Reward it:` : `😅 Oops, not quite. Encourage it:`;
        $('#trainTreat').classList.add('primary');
      }
      return;
    }
    case 'skill': {
      if (S) S.sk[m.id] = m.v;
      renderTricks();
      if (m.learned) {
        confetti(120);
        toast(`🏆 ${S?.name} mastered ${trickName(m.id)}!`, 3000);
        sfx.play('levelup');
      }
      if (ui.train === m.id) {
        $('#trainResult').textContent = m.learned ? '🏆 Mastered!' : ui.lastOk ? `🌟 Good ${S?.name}! Skill ${m.v}%` : `Skill ${m.v}%. Keep practicing!`;
      }
      $('#trainTreat').classList.remove('primary');
      return;
    }
    case 'game': {
      ui.gameResult = { score: m.score, won: m.won, best: m.best };
      if (m.won || m.best) confetti(m.best ? 150 : 80);
      if (ui.sheet === 'game') renderGameSheet();
      return;
    }
    case 'calib':
      if (m.done) { toast('🧭 Motion calibrated!'); closeSheet(); }
      else if (m.step === CALIB.RESET) toast('Motion calibration reset');
      return;
  }
}

async function onSay(m) {
  if (m.i === 'muse') return;              // the app picks its own moments to chat (see chatter below)
  if (ui.talk === 'listening') return;     // don't talk over you (or into the microphone)
  const want = INTENT_WANT[m.i];
  if (want) {                              // it wants something: say so, but not again right away
    if (Date.now() - (chatter.wantSaid[want] || 0) < chattiness().want * 60000 * 0.8) return;
    chatter.wantSaid[want] = Date.now();
  }
  const ctx = ctxFor(m);
  const useAi = brain.ready && (prefs.aiMode === 'all' || (prefs.aiMode === 'special' && SPECIAL.has(m.i)));
  let res;
  if (useAi) {
    showThinking(true);
    res = await brain.react(m.i, ctx);
    showThinking(false);
  } else {
    res = line(m.i, ctx);
  }
  if (res?.text) petSays(res.text, res.emotion);
}

function petSays(text, emotion, { chat = true } = {}) {
  said('pet', text);
  chatter.lastLine = Date.now();
  if (chat) addMsg('pet', text);
  if (emotion && EMO[emotion]) send(enc.emote(EMO[emotion], 25));
  if (S?.set?.bu) send(enc.say(text, true));
  if (!prefs.speak) return Promise.resolve();
  if (ui.talk === 'idle') setTalk('speaking');
  return voice.speak(text, {
    onLevel: (l) => send(enc.talk(l * 255)),
    onEnd: () => send(enc.talk(0)),
  }).finally(() => { if (ui.talk === 'speaking' && !voice.busy) setTalk('idle'); });
}

// ---------------------------------------------------------------- the talk card ----
// The big card on Home: what was said last, and the microphone button.
function said(who, text) {
  ui.said = { who, text, at: Date.now() };
  renderTalkCard();
}
function setTalk(st) {
  ui.talk = st;
  renderTalkCard();
}
function ago(t) {
  const s = (Date.now() - t) / 1000;
  if (s < 45) return 'just now';
  if (s < 3600) return `${Math.round(s / 60)} min ago`;
  return new Date(t).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
}

function renderTalkCard() {
  if (!S) return;
  const name = S.name, st = ui.talk, s = ui.said;
  const who = $('#saidWho'), q = $('#said'), title = $('#talkTitle'), sub = $('#talkSub'), mic = $('#talkBtn');
  $('#saidWave').classList.toggle('hidden', st !== 'speaking');
  mic.classList.toggle('live', st === 'listening');
  mic.classList.toggle('off', !listener.available && st === 'idle');
  $('#micBtn').classList.toggle('live', st === 'listening');
  if (st === 'listening') {
    who.textContent = 'You';
    q.className = 'quote me';
    q.textContent = ui.heard ? `“${ui.heard}”` : 'Listening…';
    title.textContent = 'Listening…';
    sub.textContent = 'Tap when you’re done, or just stop talking';
    return;
  }
  if (st === 'thinking') {
    who.textContent = name;
    q.className = 'quote soft';
    if (!q.querySelector('.dots')) q.innerHTML = '<span class="dots"><i></i><i></i><i></i></span>';
  } else if (s) {
    who.textContent = s.who === 'me' ? `You · ${ago(s.at)}` : `${name} · ${st === 'speaking' ? 'talking' : ago(s.at)}`;
    q.className = s.who === 'me' ? 'quote me' : 'quote';
    q.textContent = s.who === 'me' ? `“${s.text}”` : s.text;
  } else {
    who.textContent = name;
    q.className = 'quote soft';
    q.textContent = S.sleep ? 'Zzz… 💤' : `Say hi to ${name}!`;
  }
  const asked = s?.who === 'pet' && /\?\s*$/.test(s.text) && Date.now() - s.at < 90000;
  if (ui.convo) {
    title.textContent = st === 'thinking' ? `${name} is thinking…` : st === 'speaking' ? `${name} is answering` : 'Talking together';
    sub.textContent = 'Tap to hang up';
  } else if (S.sleep) {
    title.textContent = `${name} is ${sleepLine()}`;
    sub.textContent = 'Tap to wake it up and talk';
  } else if (!listener.available) {
    title.textContent = `Talk to ${name}`;
    sub.textContent = listener.why || 'Type in the Talk tab';
  } else {
    title.textContent = asked ? 'Tap to answer' : 'Tap to talk';
    sub.textContent = `Say anything, ${name} answers out loud`;
  }
}

// ---------------------------------------------------------------- talking with the microphone ----
// Tap, talk, and the pet answers out loud. With "keep the conversation going" on it then
// listens for your reply, back and forth, until you stop talking or tap to hang up.
async function talkButton() {
  sfx.unlock();
  if (ui.talk === 'listening') return listener.stop();   // done with this turn
  if (ui.convo) {                                         // hang up
    endConversation();
    voice.stop();
    return;
  }
  if (!S || !link.connected) return toast('Connect to your pet first');
  if (S.stage === 0) return toast('Hatch your egg first! 🥚');
  await listener.ready;
  if (!listener.available) return toast(listener.why, 4000);
  if (S.sleep) {
    toggleSleep();
    toast(`Waking ${S.name} up…`);
    await new Promise((r) => setTimeout(r, 2600));   // let it wake up and say so first
    await voice.idle();
  }
  voice.stop();
  ui.convo = prefs.convo;
  listenTurn();
}

async function listenTurn() {
  ui.heard = '';
  setTalk('listening');
  send(enc.listen(1));
  let lastLevel = 0;
  const hints = [S?.name, prefs.owner, ...TRICKS.map((t) => t.name), ...(S?.ct || []).filter(Boolean).map((c) => c[0]), ...FOODS.map((f) => f.name)];
  try {
    const text = await listener.listen({
      hints,
      onPartial: (t) => {
        ui.heard = t;
        renderTalkCard();
        if (ui.tab === 'chat') $('#chatInput').value = t;
      },
      onLevel: (l) => {
        $('#talkBtn').style.setProperty('--lvl', l.toFixed(2));
        const now = performance.now();
        if (now - lastLevel > 150) { lastLevel = now; send(enc.listen(1 + l * 254)); }
      },
    });
    send(enc.listen(0));
    $('#talkBtn').style.setProperty('--lvl', '0');
    if (ui.tab === 'chat') $('#chatInput').value = '';
    if (ui.talk !== 'listening') return;     // hung up meanwhile
    if (!text) return endConversation();    // nothing said: that's the end of the chat
    setTalk('idle');
    await handleUserText(text, { spoken: true });
    if (ui.convo && !document.hidden && link.connected && S && !S.sleep) listenTurn();
    else endConversation();
  } catch (e) {
    send(enc.listen(0));
    endConversation();
    if (/denied|not allowed|turned off|permission|authoriz/i.test(e.message)) openMicHelp(e.message);
    else toast(e.message, 3500);
  }
}

function endConversation() {
  ui.convo = false;
  ui.heard = '';
  if (listener.active) listener.cancel();
  if (ui.talk === 'listening' || ui.talk === 'thinking') setTalk('idle');
  else renderTalkCard();
  clearTimeout(endConversation.later);
  endConversation.later = setTimeout(() => { if (!ui.convo && !listener.active) listener.release(); }, 30000);
}

function openMicHelp(why) {
  openSheet((c) => {
    c.append(el('h2', {}, '🎙️ The microphone is off'),
      el('div', { class: 'sub' }, isNative
        ? `To talk with ${S?.name || 'your pet'}, Peekabyte needs the microphone and speech recognition. Open Settings › Peekabyte and switch both on.`
        : `This page isn't allowed to use the microphone (${why}). Allow it in the browser's site settings, then try again.`),
      el('div', { class: 'row' },
        el('button', { class: 'btn grow', onclick: closeSheet }, 'Not now'),
        isNative ? el('button', { class: 'btn primary grow', onclick: () => { closeSheet(); listener.openSettings(); } }, 'Open Settings') : null));
  }, 'mic');
}

// ---------------------------------------------------------------- the pet talks on its own ----
// It speaks up when it wants something (and again every few minutes until it gets it), and
// now and then it simply feels like chatting. Settings › Talking › Chattiness sets how often.
const CHATTY = {
  quiet: { want: 12, idle: [25, 45], text: 'Speaks up when it needs something, and chats once in a while' },
  normal: { want: 5, idle: [6, 12], text: 'Asks for what it needs and chats every few minutes' },
  chatty: { want: 2.5, idle: [2, 5], text: 'A real chatterbox' },
};
const chattiness = () => CHATTY[prefs.chatty] || CHATTY.normal;
const WANT_INTENT = { food: 'hungry', sleep: 'sleepy', love: 'lonely', play: 'bored', medicine: 'sick', clean: 'messy' };
const INTENT_WANT = Object.fromEntries(Object.entries(WANT_INTENT).map(([w, i]) => [i, w]));
const chatter = { lastLine: 0, lastUser: Date.now(), wantSaid: {}, nextIdle: 0, busy: false };
const between = ([a, b]) => a + Math.random() * (b - a);

function quietEnough() {
  return S && S.stage > 0 && !S.sleep && !S.hatching && !S.game && link.connected && !document.hidden
    && ui.talk === 'idle' && !ui.convo && !listener.active && !voice.busy && !ui.sheet && ui.train < 0
    && !['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement?.tagName);
}

function chatterTick() {
  const cfg = chattiness();
  const now = Date.now();
  if (!chatter.nextIdle) chatter.nextIdle = now + between(cfg.idle) * 60000;
  if (chatter.busy || !quietEnough() || now - chatter.lastLine < 15000 || now - chatter.lastUser < 8000) return;
  const w = S.want;
  if (w && WANT_INTENT[w] && now - (chatter.wantSaid[w] || 0) > cfg.want * 60000) {
    chatter.wantSaid[w] = now;
    return speakUp(WANT_INTENT[w]);
  }
  if (now >= chatter.nextIdle && now - chatter.lastUser > 60000) {
    chatter.nextIdle = now + between(cfg.idle) * 60000;
    return chatUp();
  }
}

async function speakUp(intent) {
  chatter.busy = true;
  try {
    const ctx = ctxFor();
    const res = brain.ready && prefs.aiMode === 'all' ? await brain.react(intent, ctx) : line(intent, ctx);
    if (res?.text && quietEnough()) {
      if (brain.ready) brain.remember(res.text, res.emotion);
      await petSays(res.text, res.emotion);
    }
  } finally {
    chatter.busy = false;
  }
}

async function chatUp() {
  chatter.busy = true;
  try {
    const ctx = ctxFor();
    const idea = starter(ctx);
    let res = brain.ready ? await brain.starter(idea.idea, ctx) : null;
    if (!res) {
      res = idea;
      brain.remember(idea.text, idea.emotion);
    }
    if (res?.text && quietEnough()) await petSays(res.text, res.emotion);
  } finally {
    chatter.busy = false;
  }
}

// ---------------------------------------------------------------- chat ----
function addMsg(who, text) {
  const log = $('#chatLog');
  const m = el('div', { class: `msg ${who}` }, text);
  log.append(m);
  while (log.children.length > 60) log.firstChild.remove();
  if (ui.tab === 'chat') m.scrollIntoView({ block: 'end', behavior: 'smooth' });
  return m;
}

let typingMsg = null;
function showThinking(on) {
  if (on && !typingMsg) typingMsg = addMsg('pet typing', '…');
  if (!on && typingMsg) { typingMsg.remove(); typingMsg = null; }
}

const SYNONYMS = [[/\bspin|turn around/, 0], [/\bjump|\bhop/, 1], [/\bwink/, 2], [/eye ?roll|roll your eyes|\broll\b/, 3],
  [/\bdance|boogie/, 4], [/play dead|\bdead\b/, 5], [/peek|hide/, 6], [/back ?flip|\bflip/, 7], [/moon ?walk/, 8],
  [/\bsing|song/, 9], [/\bbow\b|tip your hat/, 10], [/magic|abracadabra/, 11]];

function trickFromText(t) {
  const q = t.toLowerCase();
  if (S?.ct) for (let i = 0; i < CUSTOM_TRICKS; i++) if (S.ct[i] && q.includes(S.ct[i][0].toLowerCase())) return BUILTIN_TRICKS + i;
  for (const [re, id] of SYNONYMS) if (re.test(q)) return id;
  return -1;
}

// Everything you say to the pet, typed or spoken. Resolves once it has answered out loud.
async function handleUserText(text, { spoken = false } = {}) {
  if (!text || !S) return;
  chatter.lastUser = Date.now();
  addMsg(spoken ? 'me spoken' : 'me', text);
  said('me', text);
  if (S.stage === 0) return petSays('*wiggle wiggle*', null);
  if (/\b(go to (sleep|bed)|good ?night|bed ?time|time for bed|time to sleep|nap time)\b/i.test(text) && !S.sleep) {
    ui.convo = false;
    toggleSleep();   // it says goodnight by itself
    return;
  }
  const trick = trickFromText(text);
  if (trick >= 0) send(enc.trick(TRICK_MODE.COMMAND, trick));
  if (trick >= 0 && !brain.ready) {   // the phrase book just plays along
    const ok = ['Okay, watch this!', `One ${trickName(trick)}, coming up!`, 'Ooh, I\'ll try!'];
    return petSays(ok[Math.floor(Math.random() * ok.length)], 'happy');
  }
  setTalk('thinking');
  if (trick < 0) send(enc.emote(EMO.thinking, 50));
  showThinking(true);
  const res = await brain.chat(trick >= 0 ? `${text} (you are about to try the "${trickName(trick)}" trick)` : text, ctxFor());
  showThinking(false);
  if (ui.talk === 'thinking') setTalk('idle');
  return petSays(res.text, res.emotion);
}

$('#composer').addEventListener('submit', (e) => {
  e.preventDefault();
  sfx.unlock();
  const input = $('#chatInput');
  const text = input.value.trim();
  if (!text || !S) return;
  input.value = '';
  handleUserText(text);
});

function renderSuggest() {
  const s = $('#suggest');
  s.innerHTML = '';
  ['How are you?', 'Tell me a joke!', 'Do a spin!', 'What do you want to eat?', 'I love you!', 'Dance for me!']
    .forEach((q) => s.append(el('button', { class: 'chip', onclick: () => { $('#chatInput').value = q; $('#composer').requestSubmit(); } }, q)));
}

function renderBrainCard() {
  const st = brain.state;
  const m = brain.model;
  $('#brainTitle').textContent = st === 'ready' ? `🧠 ${m.name} is awake (${m.model})` : st === 'loading' ? (brain.stage === 'start' ? `Waking ${m.name} up…` : `Getting ${m.name}…`) : st === 'error' ? 'Brain hiccup' : 'AI brain is off';
  $('#brainSub').textContent = st === 'ready' ? `Open-source AI running on this device · ${brain.info}` : st === 'loading' ? brain.loadingText : st === 'error' ? brain.info : 'Your pet uses its phrase book. Turn on an open-source AI brain in Settings.';
  $('#brainProg').classList.toggle('hidden', st !== 'loading');
  $('#brainProg i').style.width = `${Math.round(brain.progress * 100)}%`;
}
brain.addEventListener('status', () => { renderBrainCard(); if (ui.tab === 'more') renderSettings(); });
voice.addEventListener('status', () => { if (ui.tab === 'more') renderSettings(); });

// ---------------------------------------------------------------- tricks ----
function renderTricks() {
  if (!S) return;
  const list = $('#trickList');
  list.innerHTML = '';
  const add = (id, name, emoji, custom) => {
    const v = S.sk[id] || 0;
    const b = el('button', { class: `trick ${v >= 80 ? 'mastered' : ''} ${ui.train === id ? 'sel' : ''}`, onclick: () => openTrain(id) },
      el('span', { class: 'e' }, emoji),
      el('div', { class: 't' }, el('div', { class: 'n' }, name),
        el('div', { class: 's' }, v >= 80 ? 'Mastered!' : v >= 50 ? 'Getting there' : v > 0 ? 'Learning' : 'Not learned yet'),
        el('div', { class: 'bar' }, el('i', { style: `width:${v}%` }))),
      v >= 80 ? el('span', { class: 'badge' }, '🏆') : el('span', { class: 'pill' }, `${v}%`));
    if (custom) b.addEventListener('contextmenu', (e) => { e.preventDefault(); openStudio(id - BUILTIN_TRICKS); });
    list.append(b);
  };
  TRICKS.forEach((t, i) => add(i, t.name, t.emoji, false));
  (S.ct || []).forEach((c, i) => { if (c) add(BUILTIN_TRICKS + i, c[0], '🌟', true); });
  if (ui.train >= 0) {
    const v = S.sk[ui.train] || 0;
    $('#trainSkill').textContent = v >= 80 ? `Mastered (${v}%)` : `Skill ${v}% · mastered at 80%`;
  }
}

function openTrain(id) {
  if (S.stage === 0) return toast('Hatch your egg first! 🥚');
  ui.train = id;
  $('#train').classList.remove('hidden');
  $('#trainEmoji').textContent = trickEmoji(id);
  $('#trainName').textContent = trickName(id);
  $('#trainResult').textContent = 'Say the command and see what happens!';
  $('#trainCmd').textContent = `📣 ${trickName(id)}!`;
  const editBtn = $('#trainEdit');
  if (editBtn) editBtn.remove();
  if (id >= BUILTIN_TRICKS) {
    $('#trainCmd').after(el('button', { class: 'btn small block', id: 'trainEdit', style: 'margin-top:8px', onclick: () => openStudio(id - BUILTIN_TRICKS) }, '✏️ Edit this trick'));
  }
  renderTricks();
  $('#train').scrollIntoView({ behavior: 'smooth', block: 'start' });
}
$('#trainClose').onclick = () => { ui.train = -1; $('#train').classList.add('hidden'); renderTricks(); };
$('#trainCmd').onclick = () => {
  sfx.unlock();
  send(enc.trick(TRICK_MODE.COMMAND, ui.train));
  $('#trainResult').textContent = `${S.name} is trying…`;
};
$('#trainTreat').onclick = () => send(enc.trick(TRICK_MODE.TREAT, ui.train));
$('#trainPraise').onclick = () => send(enc.trick(TRICK_MODE.PRAISE, ui.train));
$('#newTrick').onclick = () => {
  const slot = (S?.ct || []).findIndex((c) => !c);
  if (slot < 0) return toast('All 8 custom trick slots are full. Edit one instead!');
  openStudio(slot);
};

function openStudio(slot) {
  const existing = S.ct[slot];
  const st = { name: existing ? existing[0] : '', moves: existing ? [...existing[1]] : [] };
  openSheet((c) => {
    c.append(el('h2', {}, existing ? 'Edit trick' : 'Invent a trick'),
      el('div', { class: 'sub' }, 'Chain up to 10 moves, give it a name, then teach it in the Tricks tab.'));
    const name = el('input', { maxlength: 15, placeholder: 'Trick name (e.g. Tornado)', value: st.name, autocomplete: 'off' });
    c.append(el('div', { class: 'field' }, name));
    const seq = el('div', { class: 'seq', style: 'margin:12px 0' });
    const draw = () => {
      seq.innerHTML = '';
      if (!st.moves.length) seq.append(el('span', { class: 'muted', style: 'font-size:14px;padding:6px' }, 'Tap moves below to add them…'));
      st.moves.forEach((mv, i) => seq.append(el('button', {
        class: 'chip on', onclick: () => { st.moves.splice(i, 1); draw(); },
      }, `${MOVES[mv].emoji} ${MOVES[mv].name} ✕`)));
    };
    draw();
    c.append(seq);
    const pal = el('div', { class: 'palette' });
    MOVES.forEach((mv, i) => pal.append(el('button', {
      class: 'chip',
      onclick: () => { if (st.moves.length < TRICK_MOVES_MAX) { st.moves.push(i); draw(); } else toast('10 moves max!'); },
    }, `${mv.emoji} ${mv.name}`)));
    c.append(pal);
    c.append(el('div', { class: 'row', style: 'margin-top:14px' },
      existing ? el('button', {
        class: 'btn danger', onclick: () => {
          send(enc.trickDef(slot, existing[0], []));
          closeSheet();
          if (ui.train === BUILTIN_TRICKS + slot) $('#trainClose').click();
          toast('Trick deleted');
        },
      }, '🗑️') : null,
      el('button', { class: 'btn grow', onclick: closeSheet }, 'Cancel'),
      el('button', {
        class: 'btn primary grow', onclick: () => {
          const n = name.value.trim();
          if (!n) return toast('Give your trick a name!');
          if (!st.moves.length) return toast('Add at least one move!');
          send(enc.trickDef(slot, n, st.moves));
          closeSheet();
          toast(`🌟 "${n}" is ready to teach!`);
          setTimeout(() => openTrain(BUILTIN_TRICKS + slot), 600);
        },
      }, 'Save trick')));
  }, 'studio');
}

// ---------------------------------------------------------------- style ----
const NAMES_A = ['Mo', 'Pi', 'Bo', 'Zu', 'Ki', 'Lu', 'Nu', 'Po', 'Ti', 'Bi', 'Fi', 'Mi', 'Ru', 'Ya', 'Chi', 'Do'];

function randomLook() {
  const r = (n) => Math.floor(Math.random() * n);
  const a = [r(8), 1 + r(3), 1 + r(3), r(10) < 7 ? r(3) : r(7), r(10) < 2 ? 1 + r(2) : 0, r(10) < 3 ? 1 + r(4) : 0,
    r(100) < 45 ? 1 + r(19) : 0, r(100) < 22 ? 1 + r(10) : 0, 0, 0, r(100) < 40 ? 1 + r(4) : 0, 0];
  let extras = (a[6] ? 1 : 0) + (a[7] ? 1 : 0);
  if (extras < 2 && r(100) < 15) { a[8] = 1 + r(7); extras++; }
  if (extras < 2 && r(100) < 30) { a[9] = 1 + r(5); extras++; }
  if (extras < 2 && r(100) < 18) a[11] = 1 + r(5);
  return a;
}

function renderStyle() {
  if (!S) return;
  if (!ui.look) ui.look = [...S.av];
  if (document.activeElement !== $('#nameInput')) $('#nameInput').value = S.name;
  $('#nameNote').textContent = S.nr ? 'This is a random name. Type your own, or roll a new one.' : 'A name you picked. 🎲 rolls a random one.';
  const cats = $('#lookCats');
  cats.innerHTML = '';
  LOOK_FIELDS.forEach((f, i) => cats.append(el('button', {
    class: `chip ${i === ui.lookCat ? 'on' : ''}`, onclick: () => { ui.lookCat = i; renderStyle(); },
  }, f.label)));
  const opts = $('#lookOpts');
  opts.innerHTML = '';
  const f = LOOK_FIELDS[ui.lookCat];
  f.options.forEach((o, i) => opts.append(el('button', {
    class: `chip ${ui.look[ui.lookCat] === i ? 'on' : ''}`,
    onclick: () => { ui.look[ui.lookCat] = i; ui.lookRandom = false; previewLook(); renderStyle(); },
  }, o)));
  $('#saveBar').classList.toggle('hidden', !ui.lookDirty);
  $('#lookNote').textContent = S.ac ? 'Custom look. It stays exactly like this until you change it here.' : 'This look was picked at random when your pet hatched. It stays until you change it here.';
}

function previewLook() {
  if (S?.stage === 0) toast('You\'ll see the look once your pet hatches');
  ui.lookDirty = true;
  send(enc.avatar(ui.look, 2));
}

$('#lookRandom').onclick = () => { ui.look = randomLook(); ui.lookRandom = true; previewLook(); renderStyle(); };
$('#lookUndo').onclick = () => { send(enc.avatar(S.av, 3)); ui.look = [...S.av]; ui.lookDirty = false; renderStyle(); };
$('#lookSave').onclick = () => {
  send(enc.avatar(ui.look, ui.lookRandom ? 0 : 1));
  ui.lookDirty = false;
  toast('💾 Look saved!');
  renderStyle();
};
$('#nameSave').onclick = () => {
  const n = $('#nameInput').value.trim();
  if (!n) return toast('Type a name first');
  send(enc.name(n, false));
  $('#nameInput').blur();
  toast(`✍️ Hello, ${n}!`);
};
$('#nameDice').onclick = () => { send(enc.name('', true)); $('#nameInput').blur(); };

// hatch card
$('#hatchDice').onclick = () => { send(enc.name('', true)); $('#hatchName').blur(); };
function hatchNow(thenStyle) {
  sfx.unlock();
  const n = $('#hatchName').value.trim();
  if (n && n !== S.name) send(enc.name(n, false));
  ui.hatchThenStyle = thenStyle;
  setTimeout(() => send(enc.sys(SYS.HATCH)), 150);
  toast('🥚 Crack… crack…');
}
$('#hatchGo').onclick = () => hatchNow(false);
$('#hatchCustomize').onclick = () => hatchNow(true);

// ---------------------------------------------------------------- tabs ----
function showTab(t) {
  if (ui.tab === 'style' && t !== 'style' && ui.lookDirty) {
    send(enc.avatar(S.av, 3));
    ui.look = [...S.av];
    ui.lookDirty = false;
    toast('Unsaved look changes were undone');
  }
  ui.tab = t;
  $$('.tab').forEach((b) => b.classList.toggle('on', b.dataset.tab === t));
  $$('.view').forEach((v) => v.classList.toggle('on', v.dataset.view === t));
  $('#views').scrollTop = 0;
  if (t === 'style') { ui.look = S ? [...S.av] : ui.look; renderStyle(); }
  if (t === 'more') renderSettings();
  if (t === 'chat') { renderBrainCard(); renderSuggest(); const l = $('#chatLog').lastChild; l?.scrollIntoView({ block: 'end' }); }
}
$$('.tab').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.tab)));

// ---------------------------------------------------------------- connection page ----
function awakeNote() {
  if (awake.native) return awake.active ? 'On: the screen stays on while the pet is connected' : 'Not needed in the app: Bluetooth stays connected while the phone is locked';
  if (!awake.supported) return "This browser can't keep the screen on. While you play, set Auto-Lock to Never (Settings › Display & Brightness), or the link drops when the phone locks.";
  if (awake.active) return 'On: the phone stays awake while the pet is connected, so the link doesn\'t drop when it would lock';
  if (awake.error) return `The phone said no: ${awake.error}. Tap anywhere to try again.`;
  return 'Stops the phone locking (which drops the link) while the app is open';
}

function connReport() {
  const lk = S?.lk;
  const lines = [
    `Peekabyte connection report, ${new Date().toString()}`,
    `Browser: ${navigator.userAgent}`,
    `Link: ${link.kind || 'none'}, ${link.state}; screen lock ${awake.supported ? (awake.active ? 'held' : `not held${awake.error ? ` (${awake.error})` : ''}`) : 'not supported'}`,
    S ? `Pet: ${S.name}, firmware ${S.v}, packets ${S.mtu} B, free memory ${S.heap} B` : 'Pet: not connected',
    lk ? `Pet link: ${JSON.stringify(lk)}` : '',
    '', 'Recent events:',
    ...connlog.entries().map((it) => `${new Date(it.t).toISOString()}  ${it.text}`),
  ];
  return lines.filter((l) => l !== null).join('\n');
}

function openConnection() {
  const keep = ui.sheet === 'conn' ? $('#sheetCard').scrollTop : 0;
  openSheet((c) => {
    const lk = S?.lk;
    const ble = link.kind === 'ble';
    const title = link.connected ? 'Connected' : link.state === 'lost' ? 'Reconnecting…' : link.state === 'connecting' ? 'Connecting…' : 'Not connected';
    c.append(el('h2', {}, `📶 ${title}`),
      el('div', { class: 'sub' }, link.kind === 'bridge' ? 'Using the USB bridge on this computer.' : `Bluetooth · ${link.device?.name || prefs.lastPet || 'no pet yet'}`));

    const facts = [];
    if (lk?.on && ble && link.connected) {
      if (lk.rs < 20) facts.push(['Signal', `${signalWord(lk.rs)} (${lk.rs} dBm)`]);
      if (lk.ci) facts.push(['Link', `talks every ${fmtMs(lk.ci * 1.25)}, waits ${fmtMs(lk.to * 10)} before giving up`]);
      facts.push(['This link', `up for ${fmtSecs(lk.cs)}`]);
    }
    if (lk) facts.push(['Pet', `running for ${fmtSecs(lk.up)}, last start: ${connlog.RESET_WHY[lk.rr] || lk.rr}`]);
    if (S) facts.push(['Device', `firmware ${S.v} · ${S.imu} · ${S.mtu} B packets · ${Math.round(S.heap / 1024)} KB free`]);
    if (facts.length) {
      c.append(el('div', { class: 'card facts', style: 'margin-bottom:12px' },
        ...facts.map(([k, v]) => el('div', { class: 'fact' }, el('span', {}, k), el('b', {}, v)))));
    }

    if (ble || !link.kind) {
      c.append(el('div', { class: 'card', style: 'margin-bottom:12px;padding:4px 16px' },
        setRow('Keep the screen on', awakeNote(), toggle(prefs.keepAwake, (on) => {
          prefs.keepAwake = on;
          savePrefs();
          keepAwake(on && link.state !== 'idle').then(() => { if (ui.sheet === 'conn') openConnection(); });
        }))));
    }

    const items = connlog.entries().slice(-14).reverse();
    const log = el('div', { class: 'connlog' });
    if (!items.length) log.append(el('div', { class: 'muted' }, 'Nothing yet.'));
    items.forEach((it) => log.append(el('div', { class: `item ${it.kind}` }, el('span', { class: 'when' }, fmtClock(it.t)), el('span', {}, it.text))));
    c.append(el('h3', { class: 'minihead' }, 'Recent'), log);

    c.append(el('div', { class: 'row wrap', style: 'margin-top:14px' },
      el('button', {
        class: 'btn small',
        onclick: async () => {
          const text = connReport();
          try { await navigator.clipboard.writeText(text); toast('Report copied'); } catch {
            openSheet((s) => s.append(el('h2', {}, 'Connection report'), el('div', { class: 'sub' }, 'Select and copy this:'),
              el('textarea', { class: 'report', readonly: true }, text)), 'report');
          }
        },
      }, '📋 Copy report'),
      el('button', { class: 'btn small', onclick: () => { connlog.clear(); openConnection(); } }, '🧹 Clear'),
      S && link.connected ? el('button', { class: 'btn small', onclick: () => { send(enc.sys(SYS.CONNECT_CARD)); closeSheet(); } }, '📇 Connect card') : null,
      link.connected || link.state === 'lost'
        ? el('button', { class: 'btn small danger', onclick: () => { closeSheet(); link.disconnect(); S = null; showMain(false); } }, 'Disconnect')
        : el('button', { class: 'btn small primary', onclick: () => { closeSheet(); connectBle(); } }, 'Connect')));
  }, 'conn');
  $('#sheetCard').scrollTop = keep;
}
$('#connChip').onclick = openConnection;

// ---------------------------------------------------------------- settings ----
function toggle(on, onChange) {
  const t = el('button', { class: `toggle ${on ? 'on' : ''}`, 'aria-pressed': on ? 'true' : 'false' });
  t.onclick = () => { const v = !t.classList.contains('on'); t.classList.toggle('on', v); onChange(v); };
  return t;
}
function setRow(title, sub, control) {
  return el('div', { class: 'set' }, el('div', { class: 'l' }, el('b', {}, title), sub ? el('span', {}, sub) : null), control);
}
function select(options, value, onChange) {
  const s = el('select', { onchange: () => onChange(s.value) });
  options.forEach(([v, label]) => s.append(el('option', { value: v, selected: String(v) === String(value) }, label)));
  return s;
}
function slider(min, max, step, value, onInput, fmt) {
  const out = el('span', { class: 'pill' }, fmt(value));
  const r = el('input', { type: 'range', min, max, step, value });
  r.oninput = () => { out.textContent = fmt(Number(r.value)); onInput(Number(r.value), false); };
  r.onchange = () => onInput(Number(r.value), true);
  return { r, out };
}

function applyVoice() {
  voice.configure(prefs.voice);
  if (prefs.voice.engine === 'kokoro' && prefs.kokoroOk && voice.kState === 'off' && !ui.aiPaused) voice.loadKokoro();
}

function seg(options, value, onChange) {
  const s = el('div', { class: 'seg', role: 'radiogroup' });
  options.forEach(([v, label]) => {
    const on = String(v) === String(value);
    s.append(el('button', { class: on ? 'on' : '', role: 'radio', 'aria-checked': on ? 'true' : 'false', onclick: () => onChange(v) }, label));
  });
  return s;
}
function stackRow(title, sub, control) {
  return el('div', { class: 'set stack' }, el('div', { class: 'l' }, el('b', {}, title), sub ? el('span', {}, sub) : null), control);
}
function timeInput(min, onChange) {
  const t = el('input', { type: 'time', value: hhmm(min) });
  t.onchange = () => {
    if (!t.value) return;
    const [h, m] = t.value.split(':').map(Number);
    onChange(h * 60 + m);
  };
  return t;
}

function renderSettings() {
  const v = $('#moreView');
  const scroll = $('#views').scrollTop;
  v.innerHTML = '';

  // --- Appearance
  const ap = el('div', { class: 'card' }, el('h3', {}, '🎨 Appearance'));
  ap.append(stackRow('Theme', oldShell ? 'Reinstall the latest iPhone app for Auto and a matching status bar'
    : prefs.theme === 'system' ? `Follows your phone (${themeNow()} right now)` : null,
    seg([['system', 'Auto'], ['light', '☀️ Light'], ['dark', '🌙 Dark']], prefs.theme, (x) => { prefs.theme = x; savePrefs(); applyTheme(); renderSettings(); })));
  const sw = el('div', { class: 'swatches' });
  Object.entries(ACCENTS).forEach(([key, [name, c1, c2]]) => sw.append(el('button', {
    class: `swatch ${prefs.accent === key ? 'on' : ''}`, title: name, 'aria-label': name, style: `--s1:${c1};--s2:${c2}`,
    onclick: () => { prefs.accent = key; savePrefs(); applyTheme(); renderSettings(); },
  })));
  ap.append(stackRow('Color', ACCENTS[prefs.accent]?.[0] || 'Violet', sw));
  v.append(ap);

  // --- Sleep
  if (S) {
    const sl = el('div', { class: 'card' }, el('h3', {}, '🌙 Sleep'));
    const fw = newFirmware();
    const auto = !fw || onSchedule();
    sl.append(setRow('Sleep on a schedule',
      auto ? `Goes to bed at ${fmtTime(S.set.b)} and wakes up at ${fmtTime(S.set.w)} all by itself` : `Off: ${S.name} sleeps when you put it to bed and stays asleep until you wake it`,
      fw ? toggle(auto, (on) => send(enc.set(SET.MANUALSLEEP, on ? 0 : 1))) : null));
    if (auto) {
      sl.append(setRow('Bedtime', null, timeInput(S.set.b, (m) => send(enc.set(SET.BEDTIME, m)))),
        setRow('Wake-up time', null, timeInput(S.set.w, (m) => send(enc.set(SET.WAKETIME, m)))));
    }
    sl.append(el('button', { class: 'btn block tinted', style: 'margin-top:10px', onclick: () => { toggleSleep(); } },
      S.sleep ? `☀️ Wake ${S.name} up` : `🌙 Put ${S.name} to bed now`));
    const notes = [];
    if (!fw) notes.push(`Your pet has firmware ${S.v}. Update it to 1.2 to sleep and wake exactly on schedule; until then bedtime only makes it sleepy.`);
    else if (!S.time) notes.push('The pet learns the time from this phone whenever they connect.');
    if (notes.length) sl.append(el('p', { class: 'note', style: 'margin:10px 0 0' }, notes.join(' ')));
    v.append(sl);
  }

  // --- Talking
  const tk = el('div', { class: 'card' }, el('h3', {}, '💬 Talking'));
  tk.append(stackRow('Chattiness', chattiness().text,
    seg([['quiet', 'Quiet'], ['normal', 'Normal'], ['chatty', 'Chatty']], prefs.chatty, (x) => { prefs.chatty = x; chatter.nextIdle = 0; savePrefs(); renderSettings(); })));
  tk.append(setRow('Speak out loud', 'Your pet talks through the phone speaker', toggle(prefs.speak, (on) => { prefs.speak = on; savePrefs(); if (!on) voice.stop(); })));
  tk.append(setRow('Keep the conversation going', 'After it answers, the microphone listens for your reply', toggle(prefs.convo, (on) => { prefs.convo = on; savePrefs(); })));
  const micNote = listener.kind === 'native'
    ? (listener.onDevice ? 'Your voice is turned into words right on this iPhone' : "Uses Apple's speech recognition")
    : listener.kind === 'web' ? "Uses this browser's speech recognition" : listener.why;
  tk.append(setRow('Microphone', micNote, listener.available
    ? el('button', { class: 'btn small', onclick: () => { showTab('home'); talkButton(); } }, '🎙️ Try it') : null));
  v.append(tk);

  // --- Voice
  const vc = el('div', { class: 'card' }, el('h3', {}, '🗣️ Voice'));
  const presets = el('div', { class: 'presets', style: 'margin:0 0 12px' });
  PRESETS.forEach((p) => presets.append(el('button', {
    class: `preset ${prefs.voice.preset === p.id ? 'on' : ''}`,
    onclick: () => {
      prefs.voice = { ...prefs.voice, ...p, preset: p.id };
      delete prefs.voice.name;
      delete prefs.voice.desc;
      delete prefs.voice.id;
      savePrefs();
      applyVoice();
      renderSettings();
      testVoice();
    },
  }, el('b', {}, p.name), el('span', {}, p.desc))));
  vc.append(presets);
  const k = voice.status();
  if (prefs.voice.engine === 'kokoro') {
    if (!prefs.kokoroOk || k.kokoro === 'off') {
      vc.append(el('div', { class: 'card', style: 'background:rgba(94,231,255,.07);margin-bottom:10px' },
        el('b', {}, `Natural voices need a one-time download (~${kokoroDownloadMB()} MB).`),
        el('div', { class: 'muted', style: 'font-size:13px;margin:4px 0 10px' }, 'Kokoro is an open-source voice model that runs on this phone. Until it\'s downloaded, the phone voice fills in.'),
        el('button', { class: 'btn primary block', onclick: () => { prefs.kokoroOk = true; savePrefs(); voice.loadKokoro(); renderSettings(); } }, '⬇️ Download natural voices')));
    } else {
      const retry = () => el('button', { class: 'btn small', style: 'margin:0 0 10px', onclick: () => { voice.kTooSlow = false; voice.kMisses = 0; if (voice.kState === 'error') { voice.kState = 'off'; voice.loadKokoro(); } voice.emit(); testVoice(); } }, '🔁 Try again');
      if (k.kokoro === 'ready' && k.tooSlow) {
        vc.append(el('div', { class: 'muted', style: 'font-size:13px;margin-bottom:6px;color:#ffd9a8' },
          `⚠️ The natural voice was too slow here, so the phone voice is filling in.${isNative && k.backend === 'web' ? ' Reinstall the latest iPhone app for the fast built-in natural voice.' : ''}`), retry());
      } else if (k.kokoro === 'error') {
        vc.append(el('div', { class: 'muted', style: 'font-size:13px;margin-bottom:6px;color:#ffc2ca' }, `⚠️ ${k.info || 'The voice stopped working'}. The phone voice is filling in.`), retry());
      } else {
        vc.append(el('div', { class: 'muted', style: 'font-size:13px;margin-bottom:6px' },
          k.kokoro === 'ready' ? `✅ Natural voices ready (${k.info})` : k.kokoro === 'loading' ? (k.progress >= 0.999 ? 'Warming the voice up…' : `Downloading voices… ${Math.round(k.progress * 100)}%`) : ''));
      }
      if (k.kokoro === 'loading') vc.append(el('div', { class: 'progress' }, el('i', { style: `width:${Math.round(k.progress * 100)}%` })));
    }
  }
  const custom = (key, val) => { prefs.voice[key] = val; prefs.voice.preset = 'custom'; savePrefs(); applyVoice(); };
  vc.append(setRow('Engine', null, select([['kokoro', 'Natural (Kokoro)'], ['system', 'Phone voice'], ['babble', 'Babble']], prefs.voice.engine, (x) => { custom('engine', x); renderSettings(); })));
  if (prefs.voice.engine === 'kokoro') vc.append(setRow('Voice', null, select(KOKORO_VOICES, prefs.voice.voice, (x) => custom('voice', x))));
  if (prefs.voice.engine === 'system' && window.speechSynthesis) {
    const vs = speechSynthesis.getVoices().filter((x) => x.lang?.startsWith('en'));
    vc.append(setRow('Voice', null, select([['', 'Default'], ...vs.map((x) => [x.name, x.name])], prefs.voice.systemVoice, (x) => custom('systemVoice', x))));
  }
  const pitch = slider(-12, 12, 1, prefs.voice.pitch, (x, done) => { custom('pitch', x); if (done) testVoice(); }, (x) => `${x > 0 ? '+' : ''}${x}`);
  vc.append(el('div', { class: 'set', style: 'display:block' }, el('div', { class: 'row' }, el('b', { class: 'grow' }, 'Pitch'), pitch.out), pitch.r));
  const speed = slider(0.6, 1.6, 0.05, prefs.voice.speed, (x, done) => { custom('speed', x); if (done) testVoice(); }, (x) => `${x.toFixed(2)}×`);
  vc.append(el('div', { class: 'set', style: 'display:block' }, el('div', { class: 'row' }, el('b', { class: 'grow' }, 'Speed'), speed.out), speed.r));
  if (prefs.voice.engine !== 'system') vc.append(setRow('Effect', null, select(EFFECTS, prefs.voice.fx, (x) => { custom('fx', x); testVoice(); })));
  vc.append(el('button', { class: 'btn block', style: 'margin-top:10px', onclick: testVoice }, '▶️ Test voice'));
  v.append(vc);

  // --- Brain
  const bc = el('div', { class: 'card' }, el('h3', {}, '🧠 AI brain (open source, on-device)'));
  bc.append(el('div', { class: 'muted', style: 'font-size:13px;margin-bottom:12px;line-height:1.5' },
    isNative
      ? `An open-source language model runs on this iPhone's graphics chip: no account, no internet after the download, and your chats never leave the phone.${brain.memoryGB ? ` This iPhone has ${Math.round(brain.memoryGB)} GB of memory, so ${brain.recommended().name} is the best fit.` : ''}`
      : 'A small open-source language model runs right on this phone: no account, no internet after the first download, and your chats never leave the device.'));
  const off = el('button', { class: `model ${!prefs.brain ? 'on' : ''}`, onclick: () => { prefs.brain = ''; savePrefs(); brain.unload(); renderSettings(); } },
    el('span', { style: 'font-size:24px' }, '📖'), el('div', { class: 'grow' }, el('b', {}, 'Off - phrase book'), el('span', {}, 'Instant built-in lines. No download.')));
  bc.append(off);
  MODELS.forEach((m) => {
    const on = prefs.brain === m.key;
    const have = brain.files.has(m.file);
    const loading = on && brain.state === 'loading';
    const status = on ? (brain.state === 'ready' ? '✅ ready' : loading ? (brain.stage === 'start' ? 'waking…' : `${Math.round(brain.progress * 100)}%`) : brain.state === 'error' ? '⚠️ error' : '') : have ? 'downloaded' : fmtSize(m.mb);
    const tooBig = isNative && m.ramGB && brain.memoryGB && brain.memoryGB < m.ramGB;
    bc.append(el('button', {
      class: `model ${on ? 'on' : ''}`, style: 'margin-top:8px',
      onclick: () => {
        if (on && brain.state !== 'error') return;
        const go = () => { prefs.brain = m.key; savePrefs(); brain.load(m.key); renderSettings(); };
        if (have) return go();
        confirmSheet(`Download ${m.name}?`, `${m.model} (${m.license}) is about ${fmtSize(m.mb)}. It downloads once over Wi-Fi and stays on this ${isNative ? 'iPhone' : 'device'}.${isNative ? ' Keep the app open while it downloads.' : ''}${tooBig ? ` It needs about ${m.ramGB + 0.5} GB of memory and this iPhone has ${Math.round(brain.memoryGB)} GB, so iOS may close it.` : ''}${m.big ? ' It needs a powerful GPU and will likely not run on a phone.' : ''}`, 'Download', go);
      },
    }, el('span', { style: 'font-size:24px' }, m.big ? '🐉' : '🧠'),
    el('div', { class: 'grow' }, el('b', {}, `${m.name} · ${m.model}`), el('span', {}, m.blurb + (isNative && brain.recommended().key === m.key ? ' Best fit for this iPhone.' : ''))),
    el('span', { class: 'pill' }, status)));
    if (loading) {
      bc.append(el('div', { class: 'muted', style: 'font-size:12.5px;margin:6px 4px 0' }, brain.loadingText),
        el('div', { class: 'progress' }, el('i', { style: `width:${Math.round(brain.progress * 100)}%` })));
      if (isNative && brain.stage === 'download') {
        bc.append(el('button', { class: 'btn small', style: 'margin-top:8px', onclick: () => { brain.cancelDownload(); prefs.brain = ''; savePrefs(); brain.unload(); renderSettings(); } }, 'Stop download'));
      }
    }
  });
  if (prefs.brain && brain.state === 'error') bc.append(el('div', { class: 'muted', style: 'font-size:13px;margin-top:8px;color:#ffc2ca' }, brain.info));
  const stored = isNative ? MODELS.filter((m) => brain.files.has(m.file) && m.key !== prefs.brain) : [];
  if (stored.length) {
    bc.append(el('div', { class: 'row wrap', style: 'margin-top:10px' },
      ...stored.map((m) => el('button', {
        class: 'btn small',
        onclick: () => confirmSheet(`Delete ${m.name}?`, `Frees ${fmtSize(m.mb)}. You can download it again any time.`, 'Delete', async () => { await brain.deleteFile(m); renderSettings(); }, true),
      }, `🗑️ Delete ${m.name} (${fmtSize(m.mb)})`))));
  }
  bc.append(setRow('AI speaks for', 'The phrase book answers instantly; the AI takes a few seconds', select([['special', 'Chats + big moments'], ['all', 'Everything'], ['off', 'Only chats']], prefs.aiMode, (x) => { prefs.aiMode = x; savePrefs(); })));
  v.append(bc);

  // --- Pet
  const pc = el('div', { class: 'card' }, el('h3', {}, '🐾 Your pet'));
  const owner = el('input', { value: prefs.owner, placeholder: 'friend', maxlength: 20, style: 'background:rgba(0,0,0,.3);border:1px solid var(--line);border-radius:12px;padding:8px 10px;width:45%;user-select:text;-webkit-user-select:text' });
  owner.onchange = () => { prefs.owner = owner.value.trim(); savePrefs(); };
  pc.append(setRow('What it calls you', null, owner));
  if (S) {
    const tr = S.tr;
    pc.append(setRow('Personality', TRAITS[tr[0]].blurb + '; ' + TRAITS[tr[1]].blurb,
      el('div', { style: 'display:flex;flex-direction:column;gap:6px' },
        select(TRAITS.map((t, i) => [i, `${t.emoji} ${t.name}`]), tr[0], (x) => { if (Number(x) !== tr[1]) send(enc.traits(Number(x), tr[1])); }),
        select(TRAITS.map((t, i) => [i, `${t.emoji} ${t.name}`]), tr[1], (x) => { if (Number(x) !== tr[0]) send(enc.traits(tr[0], Number(x))); }))));
    pc.append(el('button', { class: 'btn block', style: 'margin-top:10px', onclick: () => { send(enc.diary()); ui.diary = null; openDiary(); } }, '📔 Diary'));
  }
  v.append(pc);

  // --- Display & motion
  if (S) {
    const dc = el('div', { class: 'card' }, el('h3', {}, '💡 Screen & motion'));
    const br = slider(5, 255, 5, S.set.c, (x, done) => { if (done) send(enc.set(SET.CONTRAST, x)); }, (x) => `${Math.round((x / 255) * 100)}%`);
    dc.append(el('div', { class: 'set', style: 'display:block' }, el('div', { class: 'row' }, el('b', { class: 'grow' }, 'Brightness'), br.out), br.r));
    dc.append(setRow('Speech bubbles on the pet', 'Show what it says on its own screen', toggle(!!S.set.bu, (on) => send(enc.set(SET.BUBBLES, on ? 1 : 0)))));
    dc.append(setRow('Dim while sleeping', null, toggle(!!S.set.sd, (on) => send(enc.set(SET.SLEEPDIM, on ? 1 : 0)))));
    dc.append(setRow('Auto-rotate', 'Flip the picture when held upside down', toggle(!!S.set.r, (on) => send(enc.set(SET.AUTOROTATE, on ? 1 : 0)))));
    dc.append(setRow('Flip screen', 'If your screen is mounted upside down', toggle(!!S.set.f, (on) => send(enc.set(SET.FLIP, on ? 1 : 0)))));
    const sens = slider(0, 4, 1, S.set.s, (x, done) => { if (done) send(enc.set(SET.SENS, x)); }, (x) => ['Gentle', 'Low', 'Normal', 'High', 'Twitchy'][x]);
    dc.append(el('div', { class: 'set', style: 'display:block' }, el('div', { class: 'row' }, el('b', { class: 'grow' }, 'Motion sensitivity'), sens.out), sens.r));
    dc.append(setRow('Motion calibration', S.set.cal ? 'Calibrated ✅' : 'Teach it which way is up', el('button', { class: 'btn small', onclick: openCalib }, 'Calibrate')));
    dc.append(setRow('Screen controller', 'Try SH1106 if the picture looks shifted', select([[0, 'SSD1306'], [1, 'SH1106']], S.set.d, (x) => send(enc.set(SET.DRIVER, Number(x))))));
    v.append(dc);
  }

  // --- Sounds
  const sc = el('div', { class: 'card' }, el('h3', {}, '🔊 Sounds'));
  sc.append(setRow('Sound effects', 'Chomps, boings, purrs…', toggle(prefs.sfx, (on) => { prefs.sfx = on; savePrefs(); sfx.setEnabled(on); })));
  const vol = slider(0, 1, 0.05, prefs.sfxVol, (x) => { prefs.sfxVol = x; savePrefs(); sfx.setVolume(x); }, (x) => `${Math.round(x * 100)}%`);
  sc.append(el('div', { class: 'set', style: 'display:block' }, el('div', { class: 'row' }, el('b', { class: 'grow' }, 'Volume'), vol.out), vol.r));
  v.append(sc);

  // --- Device
  const dv = el('div', { class: 'card' }, el('h3', {}, '⚙️ Device'));
  dv.append(setRow('Connection', 'Signal, keep-awake, and why the link dropped', el('button', { class: 'btn small', onclick: openConnection }, '📶 Open')));
  if (S) {
    dv.append(setRow('Time speed', 'For testing: needs change faster', select([[1, 'Normal'], [10, '10×'], [60, '60×'], [600, '600×']], S.set.ts, (x) => send(enc.set(SET.TIMESCALE, Number(x))))));
    dv.append(el('div', { class: 'row wrap', style: 'margin-top:12px' },
      el('button', { class: 'btn small', onclick: () => send(enc.sys(SYS.CONNECT_CARD)) }, '📇 Connect card'),
      el('button', { class: 'btn small', onclick: () => confirmSheet('Restart the pet?', 'It saves everything first.', 'Restart', () => send(enc.sys(SYS.REBOOT))) }, '🔄 Restart'),
      el('button', { class: 'btn small danger', onclick: () => confirmSheet('Start over with a new egg?', `${S.name} will be gone for good: name, look, tricks and diary. Settings stay.`, 'New egg', () => send(enc.sys(SYS.NEW_EGG)), true) }, '🥚 New egg'),
      el('button', { class: 'btn small danger', onclick: () => confirmSheet('Factory reset?', 'Erases the pet AND all settings on the device.', 'Erase everything', () => send(enc.sys(SYS.FACTORY)), true) }, '🧨 Factory reset')));
  }
  dv.append(el('p', { class: 'muted', style: 'font-size:12px;line-height:1.6;margin:14px 0 0' },
    isNative
      ? 'Peekabyte runs open-source models on this iPhone: Qwen3 (Alibaba) and Gemma 3 (Google) with llama.cpp, and the Kokoro voice with sherpa-onnx. Your voice is turned into words by the iPhone\'s own speech recognition. Nothing is sent to our servers (there aren\'t any).'
      : 'Peekabyte runs open-source models in your browser with Transformers.js: SmolLM2 (Hugging Face), Gemma 3 (Google), Qwen3 (Alibaba), Nemotron 3 Nano (NVIDIA) and the Kokoro voice model. Nothing is sent to a server.'));
  v.append(dv);
  $('#views').scrollTop = scroll;
}

function testVoice() {
  sfx.unlock();
  const name = S?.name || 'Peekabyte';
  petSays(`Hi! I'm ${name}. Do you like my voice?`, 'happy', { chat: false });
}

function openDiary() {
  openSheet((c) => {
    c.append(el('h2', {}, `📔 ${S.name}'s diary`), el('div', { class: 'sub' }, fmtAge(S.age)));
    const d = el('div', { class: 'diary' });
    if (!ui.diary) d.append(el('div', { class: 'muted' }, 'Loading…'));
    else if (!ui.diary.length) d.append(el('div', { class: 'muted' }, 'Nothing yet. Go make some memories!'));
    else [...ui.diary].reverse().forEach(([age, type, arg]) => {
      const f = DIARY[type];
      if (f) d.append(el('div', { class: 'item' }, el('span', { class: 'when' }, fmtWhen(age)), el('span', {}, f(arg, S.name, trickName))));
    });
    c.append(d, el('button', { class: 'btn block', style: 'margin-top:14px', onclick: closeSheet }, 'Close'));
  }, 'diary');
}

function openCalib() {
  ui.calibStep = 1;
  const draw = () => openSheet((c) => {
    c.append(el('h2', {}, '🧭 Motion setup'));
    if (ui.calibStep === 1) {
      c.append(el('div', { class: 'sub' }, 'Step 1 of 2: hold your Peekabyte upright, screen facing you, the way you like to look at it. Keep it still, then tap Next.'),
        el('button', { class: 'btn primary block', onclick: () => { send(enc.calib(CALIB.UPRIGHT)); ui.calibStep = 2; draw(); } }, 'Next'));
    } else {
      c.append(el('div', { class: 'sub' }, 'Step 2 of 2: now lay it flat on a table, screen facing up. Keep it still, then tap Done.'),
        el('button', { class: 'btn primary block', onclick: () => send(enc.calib(CALIB.FLAT)) }, 'Done'));
    }
    c.append(el('button', { class: 'btn block', style: 'margin-top:10px', onclick: () => { send(enc.calib(CALIB.RESET)); closeSheet(); } }, 'Reset to default'));
  }, 'calib');
  draw();
}

// ---------------------------------------------------------------- start ----
function init() {
  applyTheme();
  initPetButton();
  startGuard();
  startUpdates();
  sfx.setVolume(prefs.sfxVol);
  sfx.setEnabled(prefs.sfx);
  applyVoice();
  nativeVoiceCheck.then(() => { if (ui.tab === 'more') renderSettings(); });
  if (prefs.brain && !ui.aiPaused) brain.load(prefs.brain);
  renderBrainCard();
  updateConnectButton();
  // Keep the sound engine running: iOS parks it ("interrupted") after calls, other apps'
  // audio or a trip to the background, and only a tap or a return to the app revives it.
  document.addEventListener('pointerdown', () => { sfx.unlock(); chatter.lastUser = Date.now(); });
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) sfx.unlock();
    else if (ui.convo || listener.active) endConversation();
  });
  window.speechSynthesis?.addEventListener?.('voiceschanged', () => { if (ui.tab === 'more') renderSettings(); });

  // Talking: the microphone buttons, and the pet speaking up by itself.
  $('#talkBtn').onclick = talkButton;
  $('#micBtn').onclick = talkButton;
  listener.ready.then(() => { renderTalkCard(); if (ui.tab === 'more') renderSettings(); });
  setInterval(chatterTick, 4000);
  setInterval(() => { if (ui.tab === 'home' && ui.talk === 'idle') renderTalkCard(); }, 30000);

  $('#thisUrl').textContent = location.href.replace(/#.*$/, '');
  $('#copyUrl').onclick = async () => {
    try { await navigator.clipboard.writeText(location.href); toast('Link copied - paste it into Bluefy'); } catch { toast(location.href, 5000); }
  };
  $('#btnBle').onclick = connectBle;
  if (isNative) $('#bleHint').textContent = 'Power up your Peekabyte, then tap Connect and choose it. Your phone can stay on Wi-Fi, and the pet stays connected while the phone is locked.';
  if (!Link.bleSupported()) {
    $('#btnBle').classList.add('hidden');
    $('#bleHint').classList.add('hidden');
    $('#noBle').classList.remove('hidden');
  }
  if (Link.bridgeAvailable() && !isNative) {
    window.peekabyteDebug = { chatter, ui, prefs, listener, voice, chatUp, speakUp, talkButton };   // for testing on this computer
    $('#btnBridge').classList.remove('hidden');
    $('#btnBridge').onclick = () => { sfx.unlock(); link.connectBridge(); };
    link.connectBridge();
  } else if (Link.bleSupported()) {
    link.autoBle();
  }
  if ('serviceWorker' in navigator && location.protocol === 'https:') navigator.serviceWorker.register('sw.js').catch(() => {});
  // The iPhone app shows its own splash screen until the page has drawn itself.
  if (isNative) requestAnimationFrame(() => requestAnimationFrame(() => call('app.ready').catch(() => {})));
}

init();
