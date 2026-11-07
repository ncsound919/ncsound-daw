/**
 * Desktop host console. It simulates the DAW backend but uses the real
 * HostSession: approval, roles, touch locks, deltas and meters all run exactly
 * as they will against the JUCE bridge.
 */
import { HostSession, type GuestInfo, type HostBackend, type HostStatus } from './HostSession';
import type { Op, Role, SessionState, TrackState } from '../protocol';
import { createFader, type FaderHandle } from '../ui/fader';

const wsUrl = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}`;

const TRACK_DEFS: Array<[string, string]> = [
  ['DRUMS', '#e0824d'],
  ['BASS', '#c9b45a'],
  ['KEYS', '#6f9c58'],
  ['VOX', '#5f8fae'],
  ['GTR', '#b06a8a'],
  ['PERC', '#9a7bb5'],
  ['STRINGS', '#7b8f9a'],
  ['PAD', '#c25a2a'],
  ['LEAD', '#d99a4e'],
  ['FX 1', '#8e6fae'],
  ['FX 2', '#6f9c98'],
  ['AUX 1', '#a86a5a'],
];

const tracks: TrackState[] = TRACK_DEFS.map(([name, color], index) => ({
  id: `t${index + 1}`,
  name,
  color,
  volume: 0.62 + ((index * 7) % 20) / 100,
  pan: index % 4 === 0 ? -0.12 : index % 4 === 1 ? 0.1 : 0,
  mute: false,
  solo: false,
  armed: false,
}));

const state: Omit<SessionState, 'rev'> = {
  title: 'NCSOUND DEMO SESSION',
  transport: { playing: false, recording: false, beats: 0, bpm: 120 },
  tracks,
};

const findTrack = (id: string) => state.tracks.find((track) => track.id === id);
const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

const backend: HostBackend = {
  getState: () => state,
  apply(op: Op) {
    switch (op.k) {
      case 'param': {
        const track = findTrack(op.track);
        if (!track) return false;
        (track as unknown as Record<string, number>)[op.param] = op.value;
        break;
      }
      case 'toggle': {
        const track = findTrack(op.track);
        if (!track) return false;
        (track as unknown as Record<string, boolean>)[op.param] = op.value;
        break;
      }
      case 'rename': {
        const track = findTrack(op.track);
        if (!track) return false;
        track.name = op.name;
        break;
      }
      case 'tempo':
        state.transport.bpm = op.bpm;
        break;
      case 'transport':
        if (op.cmd === 'play') state.transport.playing = true;
        else if (op.cmd === 'stop') {
          state.transport.playing = false;
          state.transport.recording = false;
        } else if (op.cmd === 'record') {
          state.transport.playing = true;
          state.transport.recording = true;
        } else if (op.cmd === 'locate') {
          state.transport.beats = op.beats ?? 0;
        }
        break;
    }
    syncStrips();
    syncTransport();
    return true;
  },
};

// ------------------------------------------------------------------ elements

const codeEl = document.getElementById('code')!;
const expiryEl = document.getElementById('expiry')!;
const statusText = document.getElementById('statusText')!;
const statusDot = document.getElementById('statusDot')!;
const guestList = document.getElementById('guests')!;
const guestCount = document.getElementById('guestCount')!;
const requestList = document.getElementById('requests')!;
const stripsEl = document.getElementById('strips')!;
const trackCountEl = document.getElementById('trackCount')!;
const clockEl = document.getElementById('clockValue')!;
const playBtn = document.getElementById('playBtn') as HTMLButtonElement;
const stopBtn = document.getElementById('stopBtn') as HTMLButtonElement;
const recBtn = document.getElementById('recBtn') as HTMLButtonElement;
const tempoInput = document.getElementById('tempoInput') as HTMLInputElement;
const phoneEl = document.getElementById('phoneUrl')!;
const endedEl = document.getElementById('ended')!;

let codeExpiresAt = 0;

interface Strip {
  root: HTMLElement;
  fader: FaderHandle;
  pan: FaderHandle;
  db: HTMLElement;
  meterL: HTMLElement;
  meterR: HTMLElement;
  name: HTMLElement;
  mute: HTMLButtonElement;
  solo: HTMLButtonElement;
  arm: HTMLButtonElement;
}

const strips = new Map<string, Strip>();

const host = new HostSession({
  url: wsUrl,
  backend,
  onJoinRequest: ({ name }) => askRole(name),
  onGuestsChanged: (guests) => renderGuests(guests),
  onCode: (code, expiresAt) => {
    codeExpiresAt = expiresAt;
    codeEl.textContent = formatCode(code);
  },
  onStatus: (status) => setStatus(status),
});

// --------------------------------------------------------------------- build

function escapeHtml(value: string): string {
  return value.replace(/[&<>"']/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]!));
}

function formatCode(code: string): string {
  return code.length === 8 ? `${code.slice(0, 4)}-${code.slice(4)}` : code;
}

function dbLabel(track: TrackState): string {
  if (track.mute) return 'MUTE';
  return `${Math.max(-60, 20 * Math.log10(Math.max(0.0001, track.volume))).toFixed(1)} dB`;
}

function beatsLabel(beats: number): string {
  const bar = Math.floor(beats / 4) + 1;
  const beat = Math.floor(beats % 4) + 1;
  const tick = Math.floor((beats % 1) * 960);
  return `${bar}.${beat}.${String(tick).padStart(3, '0')}`;
}

function buildStrips(): void {
  stripsEl.innerHTML = '';
  trackCountEl.textContent = `${state.tracks.length} CHANNELS / 1 MASTER`;

  for (const track of state.tracks) {
    const root = document.createElement('div');
    root.className = 'strip';
    root.dataset.id = track.id;
    root.innerHTML = `
      <div class="strip-head">
        <span class="strip-dot" style="background:${track.color}"></span>
        <span class="strip-name">${escapeHtml(track.name)}</span>
      </div>
      <div class="strip-pan"></div>
      <div class="strip-mid">
        <div class="strip-meters" aria-hidden="true"><i></i><i></i></div>
        <div class="strip-fader"></div>
      </div>
      <div class="strip-db"></div>
      <div class="strip-btns">
        <button type="button" data-param="mute" aria-label="Mute ${escapeHtml(track.name)}">M</button>
        <button type="button" data-param="solo" aria-label="Solo ${escapeHtml(track.name)}">S</button>
        <button type="button" data-param="armed" class="arm" aria-label="Arm ${escapeHtml(track.name)}">●</button>
      </div>`;

    const fader = createFader({
      value: track.volume,
      vertical: true,
      defaultValue: 0.78,
      label: `${track.name} volume`,
      onChange: (value) => {
        track.volume = value;
        const strip = strips.get(track.id);
        if (strip) strip.db.textContent = dbLabel(track);
        host.publish({ tracks: [{ id: track.id, volume: value }] });
      },
      onStart: () => root.classList.add('active'),
      onCommit: () => root.classList.remove('active'),
    });

    const pan = createFader({
      value: track.pan,
      min: -1,
      max: 1,
      step: 0.02,
      vertical: false,
      defaultValue: 0,
      label: `${track.name} pan`,
      onChange: (value) => {
        track.pan = value;
        host.publish({ tracks: [{ id: track.id, pan: value }] });
      },
    });

    root.querySelector('.strip-fader')!.append(fader.el);
    root.querySelector('.strip-pan')!.append(pan.el);

    const mute = root.querySelector<HTMLButtonElement>('[data-param="mute"]')!;
    const solo = root.querySelector<HTMLButtonElement>('[data-param="solo"]')!;
    const arm = root.querySelector<HTMLButtonElement>('[data-param="armed"]')!;

    for (const button of [mute, solo, arm]) {
      const param = button.dataset.param as 'mute' | 'solo' | 'armed';
      button.addEventListener('click', () => {
        const value = !track[param];
        track[param] = value;
        syncStrips();
        host.publish({ tracks: [{ id: track.id, [param]: value }] });
      });
    }

    stripsEl.append(root);
    strips.set(track.id, {
      root,
      fader,
      pan,
      db: root.querySelector('.strip-db')!,
      meterL: root.querySelectorAll<HTMLElement>('.strip-meters i')[0]!,
      meterR: root.querySelectorAll<HTMLElement>('.strip-meters i')[1]!,
      name: root.querySelector('.strip-name')!,
      mute,
      solo,
      arm,
    });
  }
}

function syncStrips(): void {
  for (const track of state.tracks) {
    const strip = strips.get(track.id);
    if (!strip) continue;
    strip.name.textContent = track.name;
    strip.fader.set(track.volume);
    strip.pan.set(track.pan);
    strip.db.textContent = dbLabel(track);
    strip.mute.classList.toggle('on', track.mute);
    strip.solo.classList.toggle('on', track.solo);
    strip.arm.classList.toggle('on', track.armed);
    strip.root.classList.toggle('muted', track.mute);
    strip.root.classList.toggle('soloed', track.solo);
  }
}

function syncTransport(): void {
  playBtn.classList.toggle('on', state.transport.playing);
  recBtn.classList.toggle('on', state.transport.recording);
  clockEl.textContent = beatsLabel(state.transport.beats);
  if (document.activeElement !== tempoInput) tempoInput.value = String(Math.round(state.transport.bpm));
}

function setStatus(status: HostStatus): void {
  const labels: Record<HostStatus, string> = {
    connecting: 'CONNECTING',
    live: 'LIVE',
    reconnecting: 'RECONNECTING',
    closed: 'CLOSED',
  };
  statusText.textContent = labels[status];
  statusDot.parentElement?.classList.toggle('warn', status !== 'live');
}

function renderGuests(guests: GuestInfo[]): void {
  guestCount.textContent = String(guests.length);
  guestList.innerHTML = '';
  if (!guests.length) {
    guestList.innerHTML = '<p class="muted">No one has joined yet.</p>';
    return;
  }

  for (const guest of guests) {
    const row = document.createElement('div');
    row.className = 'guest';
    row.innerHTML = `
      <span class="guest-name">${escapeHtml(guest.name)}</span>
      <span class="guest-role">${guest.role.toUpperCase()}</span>
      <button class="ghost small kick" type="button">KICK</button>`;
    row.querySelector('.kick')!.addEventListener('click', () => host.kick(guest.id));
    guestList.append(row);
  }
}

function askRole(name: string): Promise<Role | null> {
  return new Promise((resolve) => {
    const placeholder = requestList.querySelector('.muted');
    if (placeholder) placeholder.remove();

    const card = document.createElement('div');
    card.className = 'request';
    card.innerHTML = `
      <div class="request-name">${escapeHtml(name)} <span>wants to join</span></div>
      <div class="request-roles">
        <button type="button" data-role="engineer">ENGINEER</button>
        <button type="button" data-role="mixer">MIXER</button>
        <button type="button" data-role="viewer">VIEWER</button>
      </div>
      <button type="button" class="deny" data-role="deny">DENY</button>`;

    let settled = false;
    const finish = (role: Role | null) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      card.remove();
      if (!requestList.children.length) {
        requestList.innerHTML = '<p class="muted">Approvals appear here.</p>';
      }
      resolve(role);
    };

    card.querySelectorAll<HTMLButtonElement>('button').forEach((button) => {
      button.addEventListener('click', () => {
        const role = button.dataset.role;
        finish(role === 'deny' || !role ? null : (role as Role));
      });
    });

    const timer = setTimeout(() => finish(null), 60_000);
    requestList.append(card);
  });
}

// ----------------------------------------------------------------- transport

function applyTransport(cmd: 'play' | 'stop' | 'record'): void {
  if (cmd === 'play') state.transport.playing = true;
  else if (cmd === 'stop') {
    state.transport.playing = false;
    state.transport.recording = false;
  } else {
    state.transport.playing = true;
    state.transport.recording = true;
  }
  syncTransport();
  host.publish({
    transport: { playing: state.transport.playing, recording: state.transport.recording },
  });
}

playBtn.addEventListener('click', () => applyTransport('play'));
stopBtn.addEventListener('click', () => applyTransport('stop'));
recBtn.addEventListener('click', () => applyTransport('record'));

tempoInput.addEventListener('change', () => {
  const bpm = clamp(Number(tempoInput.value) || 120, 20, 300);
  state.transport.bpm = bpm;
  tempoInput.value = String(Math.round(bpm));
  host.publish({ transport: { bpm } });
});

// -------------------------------------------------------------------- chrome

document.getElementById('copyCode')!.addEventListener('click', async () => {
  const button = document.getElementById('copyCode')!;
  try {
    await navigator.clipboard.writeText(codeEl.textContent ?? '');
    button.textContent = 'COPIED';
    setTimeout(() => (button.textContent = 'COPY'), 1200);
  } catch {
    button.textContent = 'SELECT';
  }
});

document.getElementById('newCode')!.addEventListener('click', () => host.newCode());

document.getElementById('endBtn')!.addEventListener('click', () => {
  host.end();
  endedEl.hidden = false;
});

document.getElementById('reloadBtn')!.addEventListener('click', () => location.reload());

// --------------------------------------------------------------------- clocks

let lastTick = performance.now();
let lastBeatsPublish = 0;

setInterval(() => {
  const now = performance.now();
  const delta = Math.min(0.25, (now - lastTick) / 1000);
  lastTick = now;

  if (!state.transport.playing) return;
  state.transport.beats += (delta * state.transport.bpm) / 60;
  clockEl.textContent = beatsLabel(state.transport.beats);

  if (now - lastBeatsPublish > 250) {
    lastBeatsPublish = now;
    host.publish({ transport: { beats: state.transport.beats } });
  }
}, 100);

let phase = 0;
setInterval(() => {
  phase += 0.18;
  const anySolo = state.tracks.some((track) => track.solo);
  const levels: Record<string, [number, number]> = {};

  state.tracks.forEach((track, index) => {
    const audible = !track.mute && (!anySolo || track.solo);
    const envelope = state.transport.playing && audible
      ? (0.3 + 0.62 * Math.abs(Math.sin(phase * (0.9 + index * 0.03) + index * 1.7))) * track.volume
      : 0;
    const left = clamp(envelope * (1 - Math.max(0, track.pan)), 0, 1);
    const right = clamp(envelope * (1 + Math.min(0, track.pan)), 0, 1);
    levels[track.id] = [left, right];

    const strip = strips.get(track.id);
    if (strip) {
      strip.meterL.style.transform = `scaleY(${left})`;
      strip.meterR.style.transform = `scaleY(${right})`;
    }
  });

  host.meters(levels);
}, 60);

setInterval(() => {
  if (!codeExpiresAt) return;
  const left = Math.max(0, codeExpiresAt - Date.now());
  const minutes = Math.floor(left / 60_000);
  const seconds = Math.floor((left % 60_000) / 1000);
  expiryEl.textContent = left ? `${minutes}:${String(seconds).padStart(2, '0')} LEFT` : 'EXPIRED';
  expiryEl.classList.toggle('expired', left === 0);
}, 1000);

// ----------------------------------------------------------------------- boot

buildStrips();
syncStrips();
syncTransport();
renderGuests([]);

void host.start().catch(() => {
  setStatus('closed');
  expiryEl.textContent = 'RELAY UNREACHABLE';
  expiryEl.classList.add('expired');
});

void fetch('/api/addresses')
  .then((response) => response.json() as Promise<{ addresses?: string[] }>)
  .then((data) => {
    const address = data.addresses?.find((entry) => entry.startsWith('192.168.') || entry.startsWith('10.') || entry.startsWith('172.'))
      ?? data.addresses?.[0];
    const port = location.port ? `:${location.port}` : '';
    phoneEl.textContent = address ? `http://${address}${port}` : location.origin;
  })
  .catch(() => {
    phoneEl.textContent = location.origin;
  });
