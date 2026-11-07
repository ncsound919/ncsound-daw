/**
 * Phone remote. Vanilla DOM around GuestSession so the bundle stays tiny and
 * the page opens instantly on a phone that just joined the studio Wi-Fi.
 */
import { GuestSession, type Credentials, type GuestView } from '../guest/GuestSession';
import type { Role, TrackState } from '../protocol';
import { createFader, type FaderHandle } from '../ui/fader';

const wsUrl = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}`;
const codeKey = 'ncsound.remote.code';
const nameKey = 'ncsound.remote.name';
const credsKey = 'ncsound.remote.creds';

const session = new GuestSession(wsUrl, { onCredentials: saveCreds });
const root = document.getElementById('app')!;

type Screen = 'join' | 'pending' | 'mixer';
let screen: Screen | null = null;
let codeValue = read(codeKey) ?? '';
let nameValue = read(nameKey) ?? '';
let toastText = '';
let toastUntil = 0;
let meterFrame = 0;

interface TrackRow {
  root: HTMLDivElement;
  name: HTMLElement;
  dot: HTMLElement;
  db: HTMLElement;
  meterL: HTMLElement;
  meterR: HTMLElement;
  volume: FaderHandle;
  pan: FaderHandle;
  mute: HTMLButtonElement;
  solo: HTMLButtonElement;
  arm: HTMLButtonElement;
}

const rows = new Map<string, TrackRow>();

function read(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

function write(key: string, value: string): void {
  try {
    localStorage.setItem(key, value);
  } catch {
    // storage can be unavailable; the session still works for this page
  }
}

function saveCreds(creds: Credentials): void {
  try {
    sessionStorage.setItem(credsKey, JSON.stringify({ ...creds, code: codeValue, name: nameValue }));
  } catch {
    // ignore
  }
}

function loadCreds(): { creds: Credentials; code: string; name: string } | null {
  try {
    const raw = sessionStorage.getItem(credsKey);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as Credentials & { code?: string; name?: string };
    if (!parsed.sessionId || !parsed.guestId || !parsed.token) return null;
    return { creds: parsed, code: parsed.code ?? codeValue, name: parsed.name ?? nameValue };
  } catch {
    return null;
  }
}

function clearCreds(): void {
  try {
    sessionStorage.removeItem(credsKey);
  } catch {
    // ignore
  }
}

const db = (value: number) => (value <= 0.0001 ? '−∞' : `${(20 * Math.log10(value)).toFixed(1)}`);

const beatsLabel = (beats: number) => {
  const bar = Math.floor(beats / 4) + 1;
  const beat = Math.floor(beats % 4) + 1;
  const tick = Math.floor((beats % 1) * 960);
  return `${bar}.${beat}.${String(tick).padStart(3, '0')}`;
};

function roleLabel(role: Role | null): string {
  return role ? role.toUpperCase() : 'GUEST';
}

// ---------------------------------------------------------------- join screen

function showJoin(view: GuestView): void {
  if (screen !== 'join') {
    screen = 'join';
    root.innerHTML = '';
    const wrap = document.createElement('div');
    wrap.className = 'join-wrap';
    wrap.innerHTML = `
      <div class="join-card">
        <div class="logo">NCSOUND<span>REMOTE</span></div>
        <p class="kicker">COLLABORATIVE MIX SESSION</p>
        <form id="joinForm" autocomplete="off">
          <label class="field">
            <span>SESSION CODE</span>
            <input id="codeInput" type="text" inputmode="text" autocapitalize="characters"
                   autocorrect="off" spellcheck="false" maxlength="12" placeholder="ABCD-EFGH" />
          </label>
          <label class="field">
            <span>YOUR NAME</span>
            <input id="nameInput" type="text" maxlength="32" placeholder="Engineer, artist, friend…" />
          </label>
          <button id="connectBtn" class="primary" type="button">CONNECT</button>
          <p id="joinMsg" class="join-msg" hidden></p>
        </form>
        <p class="hint">The engineer at the desk approves this device before it can touch the mix.</p>
      </div>`;
    root.append(wrap);

    const form = wrap.querySelector<HTMLFormElement>('#joinForm')!;
    const codeInput = wrap.querySelector<HTMLInputElement>('#codeInput')!;
    const nameInput = wrap.querySelector<HTMLInputElement>('#nameInput')!;
    codeInput.value = codeValue;
    nameInput.value = nameValue;
    codeInput.addEventListener('input', () => {
      codeInput.value = codeInput.value.toUpperCase();
    });

    const connect = () => {
      if (session.getView().status === 'connecting') return;
      codeValue = codeInput.value.trim();
      nameValue = nameInput.value.trim() || 'Guest';
      write(codeKey, codeValue);
      write(nameKey, nameValue);
      const saved = loadCreds();
      session.join(codeValue, nameValue, saved && saved.code === codeValue ? saved.creds : undefined);
    };

    form.addEventListener('submit', (event) => {
      event.preventDefault();
      connect();
    });
    wrap.querySelector<HTMLButtonElement>('#connectBtn')!.addEventListener('click', connect);
  }

  const connect = root.querySelector<HTMLButtonElement>('#connectBtn');
  if (connect) {
    const connecting = view.status === 'connecting';
    connect.disabled = connecting;
    connect.textContent = connecting ? 'CONNECTING…' : 'CONNECT';
  }

  const msg = root.querySelector<HTMLElement>('#joinMsg');
  if (msg) {
    const text =
      view.status === 'denied' ? 'The host declined this device.'
        : view.status === 'kicked' ? 'You were removed from the session.'
        : view.status === 'ended' ? 'This session has ended.'
        : view.status === 'error' ? (view.error ?? 'Could not reach the session.')
        : '';
    msg.hidden = !text;
    msg.textContent = text;
  }
}

// ------------------------------------------------------------- pending screen

function showPending(): void {
  if (screen === 'pending') return;
  screen = 'pending';
  root.innerHTML = '';
  const wrap = document.createElement('div');
  wrap.className = 'join-wrap';
  wrap.innerHTML = `
    <div class="join-card pending-card">
      <div class="logo">NCSOUND<span>REMOTE</span></div>
      <div class="spinner" aria-hidden="true"></div>
      <h2>WAITING FOR APPROVAL</h2>
      <p class="pending-code">${formatCode(codeValue)}</p>
      <p class="hint">Ask the engineer at the desk to approve <b>${escapeHtml(nameValue || 'this device')}</b>.</p>
      <button id="cancelJoin" class="ghost" type="button">CANCEL</button>
    </div>`;
  root.append(wrap);
  wrap.querySelector<HTMLButtonElement>('#cancelJoin')!.addEventListener('click', () => {
    clearCreds();
    screen = null;
    session.leave();
    render(session.getView());
  });
}

// --------------------------------------------------------------- mixer screen

let titleEl: HTMLElement | null = null;
let roleEl: HTMLElement | null = null;
let statusEl: HTMLElement | null = null;
let clockEl: HTMLElement | null = null;
let tempoEl: HTMLElement | null = null;
let offlineEl: HTMLElement | null = null;
let recBtn: HTMLButtonElement | null = null;
let playBtn: HTMLButtonElement | null = null;
let toastEl: HTMLElement | null = null;
let trackListEl: HTMLElement | null = null;

function showMixer(view: GuestView): void {
  if (screen !== 'mixer') {
    screen = 'mixer';
    rows.clear();
    root.innerHTML = '';
    const shell = document.createElement('div');
    shell.className = 'mixer';
    shell.innerHTML = `
      <div class="mix-top">
        <div class="mix-brand">NCSOUND<span>REMOTE</span></div>
        <div class="mix-meta">
          <span id="mixTitle" class="mix-title">SESSION</span>
          <span id="roleChip" class="chip">GUEST</span>
        </div>
        <button id="leaveBtn" class="ghost small" type="button">EXIT</button>
      </div>
      <div id="statusStrip" class="status-strip"><i></i><span id="statusText">LIVE</span></div>
      <div class="transport">
        <button id="playBtn" class="transport-btn" type="button" aria-label="Play">▶</button>
        <button id="stopBtn" class="transport-btn" type="button" aria-label="Stop">■</button>
        <button id="recBtn" class="transport-btn rec" type="button" aria-label="Record">●</button>
        <div class="clock">
          <b id="clockValue">1.1.000</b>
          <span id="tempoValue">120 BPM</span>
        </div>
      </div>
      <div class="offline" id="offlineBar" hidden>
        <span>HOST OFFLINE</span><small>The desk is reconnecting…</small>
      </div>
      <div class="list-head"><span>CHANNEL</span><span>VOLUME</span></div>
      <div id="trackList" class="track-list"></div>
      <div id="toast" class="toast" hidden></div>`;
    root.append(shell);

    titleEl = shell.querySelector('#mixTitle');
    roleEl = shell.querySelector('#roleChip');
    statusEl = shell.querySelector('#statusText');
    clockEl = shell.querySelector('#clockValue');
    tempoEl = shell.querySelector('#tempoValue');
    offlineEl = shell.querySelector('#offlineBar');
    recBtn = shell.querySelector('#recBtn');
    const recordButton = shell.querySelector<HTMLButtonElement>('#recBtn')!;
    playBtn = shell.querySelector('#playBtn');
    toastEl = shell.querySelector('#toast');
    trackListEl = shell.querySelector('#trackList');

    shell.querySelector<HTMLButtonElement>('#leaveBtn')!.addEventListener('click', () => {
      clearCreds();
      session.leave();
      screen = null;
      render(session.getView());
    });

    shell.querySelector<HTMLButtonElement>('#playBtn')!.addEventListener('click', () => {
      session.send({ k: 'transport', cmd: 'play' });
    });
    shell.querySelector<HTMLButtonElement>('#stopBtn')!.addEventListener('click', () => {
      session.send({ k: 'transport', cmd: 'stop' });
    });
    recordButton.addEventListener('click', () => {
      session.send({ k: 'transport', cmd: 'record' });
    });

    startMeters();
  }

  const state = view.state;
  if (!state) return;

  if (titleEl) titleEl.textContent = state.title;
  if (roleEl) roleEl.textContent = roleLabel(view.role);
  if (clockEl) clockEl.textContent = beatsLabel(state.transport.beats);
  if (tempoEl) tempoEl.textContent = `${Math.round(state.transport.bpm)} BPM`;
  if (playBtn) playBtn.classList.toggle('on', state.transport.playing);
  if (recBtn) {
    recBtn.hidden = view.role !== 'engineer';
    recBtn.classList.toggle('on', state.transport.recording);
  }
  if (offlineEl) offlineEl.hidden = view.status !== 'host-offline' && view.status !== 'reconnecting';
  if (statusEl) {
    statusEl.textContent =
      view.status === 'host-offline' ? 'HOST OFFLINE'
        : view.status === 'reconnecting' ? 'RECONNECTING'
        : 'LIVE';
  }
  root.querySelector('#statusStrip')?.classList.toggle('warn', view.status !== 'live');

  const readOnly = view.role === 'viewer';
  root.classList.toggle('readonly', readOnly);

  const wanted = new Set(state.tracks.map((t) => t.id));
  for (const [id, row] of rows) {
    if (!wanted.has(id)) {
      row.root.remove();
      rows.delete(id);
    }
  }

  for (const track of state.tracks) {
    let row = rows.get(track.id);
    if (!row) {
      row = buildRow(track, view.role);
      rows.set(track.id, row);
      trackListEl!.append(row.root);
    }
    syncRow(row, track, view.role);
  }

  if (view.notice && view.notice !== toastText) {
    toastText = view.notice;
    toastUntil = Date.now() + 2400;
  }
  if (toastEl) {
    const visible = Date.now() < toastUntil;
    toastEl.hidden = !visible;
    if (visible) toastEl.textContent = toastText;
  }
}

function buildRow(track: TrackState, role: Role | null): TrackRow {
  const row = document.createElement('div');
  row.className = 'track';
  row.dataset.id = track.id;
  row.innerHTML = `
    <div class="track-head">
      <span class="track-dot"></span>
      <span class="track-name"></span>
      <span class="track-db"></span>
    </div>
    <div class="meter" aria-hidden="true"><i></i><i></i></div>
    <div class="track-main">
      <div class="fader-slot"></div>
      <div class="track-btns">
        <button type="button" data-param="mute" aria-label="Mute">M</button>
        <button type="button" data-param="solo" aria-label="Solo">S</button>
        <button type="button" data-param="armed" class="arm" aria-label="Record arm">●</button>
      </div>
    </div>
    <div class="pan-row"><span>L</span><div class="pan-slot"></div><span>R</span></div>`;

  const volume = createFader({
    value: track.volume,
    label: `${track.name} volume`,
    vertical: false,
    defaultValue: 0.78,
    onChange: (v) => session.setControl(track.id, 'volume', v),
    onCommit: () => session.release(track.id, 'volume'),
  });
  const pan = createFader({
    value: track.pan,
    min: -1,
    max: 1,
    step: 0.02,
    label: `${track.name} pan`,
    vertical: false,
    defaultValue: 0,
    onChange: (v) => session.setControl(track.id, 'pan', v),
    onCommit: () => session.release(track.id, 'pan'),
  });

  row.querySelector('.fader-slot')!.append(volume.el);
  row.querySelector('.pan-slot')!.append(pan.el);

  const mute = row.querySelector<HTMLButtonElement>('[data-param="mute"]')!;
  const solo = row.querySelector<HTMLButtonElement>('[data-param="solo"]')!;
  const arm = row.querySelector<HTMLButtonElement>('[data-param="armed"]')!;

  for (const button of [mute, solo, arm]) {
    const param = button.dataset.param as 'mute' | 'solo' | 'armed';
    button.addEventListener('click', () => {
      const current = session.getView().state?.tracks.find((t) => t.id === track.id);
      if (!current) return;
      session.setControl(track.id, param, !current[param]);
      session.release(track.id, param);
    });
  }

  arm.hidden = role !== 'engineer';
  const readOnly = role === 'viewer';
  for (const button of [mute, solo, arm]) button.disabled = readOnly;
  volume.setDisabled(readOnly);
  pan.setDisabled(readOnly);

  return {
    root: row,
    name: row.querySelector('.track-name')!,
    dot: row.querySelector('.track-dot')!,
    db: row.querySelector('.track-db')!,
    meterL: row.querySelectorAll<HTMLElement>('.meter i')[0]!,
    meterR: row.querySelectorAll<HTMLElement>('.meter i')[1]!,
    volume,
    pan,
    mute,
    solo,
    arm,
  };
}

function syncRow(row: TrackRow, track: TrackState, role: Role | null): void {
  row.name.textContent = track.name;
  row.dot.style.background = track.color;
  row.db.textContent = `${db(track.volume)} dB`;
  row.volume.set(track.volume);
  row.pan.set(track.pan);
  row.mute.classList.toggle('on', track.mute);
  row.solo.classList.toggle('on', track.solo);
  row.arm.classList.toggle('on', track.armed);
  row.arm.hidden = role !== 'engineer';
  row.root.classList.toggle('muted', track.mute);
  row.root.classList.toggle('soloed', track.solo);
}

function startMeters(): void {
  if (meterFrame) return;
  const tick = () => {
    const levels = session.getLevels();
    for (const [id, row] of rows) {
      const level = levels[id];
      const left = level ? Math.min(1, Math.max(0, level[0])) : 0;
      const right = level ? Math.min(1, Math.max(0, level[1])) : 0;
      row.meterL.style.transform = `scaleX(${left})`;
      row.meterR.style.transform = `scaleX(${right})`;
    }
    meterFrame = requestAnimationFrame(tick);
  };
  meterFrame = requestAnimationFrame(tick);
}

// -------------------------------------------------------------------- screens

function render(view: GuestView): void {
  if (view.status === 'denied' || view.status === 'kicked' || view.status === 'ended') clearCreds();

  if (view.status === 'pending') return showPending();
  if (view.status === 'live' || view.status === 'host-offline' || view.status === 'reconnecting') return showMixer(view);

  // 'idle' | 'connecting' | terminal
  if (screen !== 'join') rows.clear();
  showJoin(view);
}

function formatCode(code: string): string {
  const normalized = code.replace(/[^0-9A-Z]/g, '').toUpperCase();
  return normalized.length === 8 ? `${normalized.slice(0, 4)}-${normalized.slice(4)}` : normalized;
}

function escapeHtml(value: string): string {
  return value.replace(/[&<>"']/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]!));
}

// ---------------------------------------------------------------------- boot

session.subscribe(() => render(session.getView()));
session.subscribeMeters(() => {
  // Meter painting is driven by the animation frame; this keeps the API honest.
});

const saved = loadCreds();
if (saved) {
  codeValue = saved.code;
  nameValue = saved.name;
  session.join(saved.code, saved.name, saved.creds);
} else {
  render(session.getView());
}
