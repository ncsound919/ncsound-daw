/**
 * Wire protocol shared by the relay, the host (inside the DAW) and every guest.
 *
 * Design rules:
 *  - the relay is a dumb pipe: it enforces session membership, codes and roles,
 *    but never trusts a guest message;
 *  - the host validates every op against the live session and the guest's role;
 *  - deltas are versioned, snapshots are authoritative, meters are droppable.
 */

export const CODE_ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
export const CODE_LENGTH = 8;
export const CODE_PATTERN = /^[0-9A-HJKMNP-TV-Z]{8}$/;

export type Role = 'engineer' | 'mixer' | 'viewer';
export const ROLES: readonly Role[] = ['engineer', 'mixer', 'viewer'];

export interface TrackState {
  id: string;
  name: string;
  color: string;
  volume: number; // 0..1 linear
  pan: number;    // -1..1
  mute: boolean;
  solo: boolean;
  armed: boolean;
}

export interface TransportState {
  playing: boolean;
  recording: boolean;
  beats: number;
  bpm: number;
}

export interface SessionState {
  title: string;
  transport: TransportState;
  tracks: TrackState[];
  rev: number;
}

/** Partial track update keyed by id. The id is never changed by a delta. */
export type TrackDelta = { id: string } & Partial<Omit<TrackState, 'id'>>;

export interface Delta {
  tracks?: TrackDelta[];
  transport?: Partial<TransportState>;
}

export type Op =
  | { k: 'param'; track: string; param: 'volume' | 'pan'; value: number }
  | { k: 'toggle'; track: string; param: 'mute' | 'solo' | 'armed'; value: boolean }
  | { k: 'rename'; track: string; name: string }
  | { k: 'tempo'; bpm: number }
  | { k: 'transport'; cmd: 'play' | 'stop' | 'record' | 'locate'; beats?: number };

export type GuestMsg =
  | { t: 'op'; id: number; op: Op }
  | { t: 'release'; track: string; param: string }
  | { t: 'resync' };

export type HostMsg =
  | { t: 'state'; state: SessionState }
  | { t: 'meters'; levels: Record<string, [number, number]> }
  | { t: 'ack'; id: number; ok: boolean; reason?: string }
  | { t: 'delta'; rev: number; tracks?: TrackDelta[]; transport?: Partial<TransportState> };

export type ClientToServer =
  | { t: 'host.create' }
  | { t: 'host.resume'; sessionId: string; token: string }
  | { t: 'host.approve'; guestId: string; role: Role }
  | { t: 'host.deny'; guestId: string }
  | { t: 'host.kick'; guestId: string }
  | { t: 'host.newCode' }
  | { t: 'host.end' }
  | { t: 'guest.join'; code: string; name: string; resume?: { sessionId: string; guestId: string; token: string } }
  | { t: 'up'; msg: GuestMsg }
  | { t: 'to'; guestId?: string; msg: HostMsg };

export type ServerToClient =
  | { t: 'host.created'; sessionId: string; token: string; code: string; codeExpiresAt: number }
  | { t: 'host.resumed'; sessionId: string; code: string; codeExpiresAt: number }
  | { t: 'code.updated'; code: string; codeExpiresAt: number }
  | { t: 'join.request'; guestId: string; name: string }
  | { t: 'join.pending' }
  | { t: 'join.approved'; sessionId: string; guestId: string; token: string; role: Role }
  | { t: 'join.denied' }
  | { t: 'kicked' }
  | { t: 'session.ended' }
  | { t: 'host.offline' }
  | { t: 'host.online' }
  | { t: 'guest.online'; guestId: string; name: string; role: Role }
  | { t: 'guest.left'; guestId: string }
  | { t: 'from'; guestId: string; role: Role; msg: GuestMsg }
  | { t: 'msg'; msg: HostMsg }
  | { t: 'error'; code: 'bad_code' | 'rate_limited' | 'too_many_pending' | 'session_gone' | 'host_offline' };

/** Uppercase, strip punctuation, and fold characters a phone keyboard confuses. */
export function normalizeCode(input: string): string {
  return String(input ?? '')
    .toUpperCase()
    .replace(/[^0-9A-Z]/g, '')
    .replace(/O/g, '0')
    .replace(/[IL]/g, '1');
}

export function isCode(value: string): boolean {
  return CODE_PATTERN.test(value);
}

const isObject = (v: unknown): v is Record<string, unknown> =>
  typeof v === 'object' && v !== null && !Array.isArray(v);
const isNumber = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
const isTrackId = (v: unknown): v is string => typeof v === 'string' && v.length > 0 && v.length <= 64;

/**
 * Shape + range validation. This is the trust boundary: anything the host
 * receives from a guest passes through here before it can reach the DAW.
 */
export function validateOp(raw: unknown): Op | null {
  if (!isObject(raw) || typeof raw.k !== 'string') return null;

  switch (raw.k) {
    case 'param': {
      if (!isTrackId(raw.track)) return null;
      if (raw.param !== 'volume' && raw.param !== 'pan') return null;
      if (!isNumber(raw.value)) return null;
      if (raw.param === 'volume' && (raw.value < 0 || raw.value > 1)) return null;
      if (raw.param === 'pan' && (raw.value < -1 || raw.value > 1)) return null;
      return { k: 'param', track: raw.track, param: raw.param, value: raw.value };
    }
    case 'toggle': {
      if (!isTrackId(raw.track)) return null;
      if (raw.param !== 'mute' && raw.param !== 'solo' && raw.param !== 'armed') return null;
      if (typeof raw.value !== 'boolean') return null;
      return { k: 'toggle', track: raw.track, param: raw.param, value: raw.value };
    }
    case 'rename': {
      if (!isTrackId(raw.track) || typeof raw.name !== 'string') return null;
      const name = raw.name.trim();
      if (name.length < 1 || name.length > 64) return null;
      return { k: 'rename', track: raw.track, name };
    }
    case 'tempo': {
      if (!isNumber(raw.bpm) || raw.bpm < 20 || raw.bpm > 300) return null;
      return { k: 'tempo', bpm: raw.bpm };
    }
    case 'transport': {
      if (raw.cmd === 'play' || raw.cmd === 'stop' || raw.cmd === 'record') return { k: 'transport', cmd: raw.cmd };
      if (raw.cmd === 'locate') {
        if (!isNumber(raw.beats) || raw.beats < 0 || raw.beats > 1_000_000) return null;
        return { k: 'transport', cmd: 'locate', beats: raw.beats };
      }
      return null;
    }
    default:
      return null;
  }
}

/**
 * Console-style permissions.
 *  - engineer: full control, including record/arm, rename and tempo;
 *  - mixer: level, pan, mute/solo, playback and location;
 *  - viewer: read-only.
 */
export function allowed(role: Role, op: Op): boolean {
  if (role === 'engineer') return true;
  if (role === 'viewer') return false;

  switch (op.k) {
    case 'param':
      return true;
    case 'toggle':
      return op.param === 'mute' || op.param === 'solo';
    case 'transport':
      return op.cmd !== 'record';
    default:
      return false;
  }
}
