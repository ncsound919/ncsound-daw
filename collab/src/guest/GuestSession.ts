/**
 * Guest side (phone, tablet, another studio). Framework-agnostic; React binds to it via
 * useSyncExternalStore(session.subscribe, session.getView).
 */
import {
  normalizeCode,
  type ClientToServer, type Delta, type GuestMsg, type Op, type Role, type ServerToClient, type SessionState,
} from '../protocol';

export type GuestStatus =
  | 'idle' | 'connecting' | 'pending' | 'live' | 'host-offline' | 'reconnecting'
  | 'denied' | 'kicked' | 'ended' | 'error';

export interface GuestView {
  status: GuestStatus;
  role: Role | null;
  state: SessionState | null;
  notice?: string;   // short-lived, e.g. "Someone else is moving that control"
  error?: string;
}

export interface Credentials { sessionId: string; guestId: string; token: string }

const HOLD_MS = 700;      // ignore echoed host values for a control we are actively moving
const FLUSH_MS = 33;      // ~30 Hz cap on fader traffic
const REASONS: Record<string, string> = {
  locked: 'Someone else is moving that control',
  forbidden: 'Your role can’t do that',
  rate_limited: 'Slow down a little',
  rejected: 'The host didn’t accept that change',
  unknown_track: 'That track no longer exists',
  invalid: 'That change wasn’t valid',
};
const TERMINAL: GuestStatus[] = ['denied', 'kicked', 'ended', 'error'];

export class GuestSession {
  private view: GuestView = { status: 'idle', role: null, state: null };
  private levels: Record<string, [number, number]> = {};
  private listeners = new Set<() => void>();
  private meterListeners = new Set<() => void>();
  private ws: WebSocket | null = null;
  private creds: Credentials | null = null;
  private joinArgs: { code: string; name: string } | null = null;
  private nextId = 1;
  private pending = new Map<string, Op>();
  private flushTimer: ReturnType<typeof setTimeout> | null = null;
  private held = new Map<string, number>();
  private attempt = 0;
  private noticeTimer: ReturnType<typeof setTimeout> | null = null;

  constructor(private url: string, private opts: { WebSocketImpl?: typeof WebSocket; onCredentials?: (c: Credentials) => void } = {}) {}

  // ---- external-store plumbing (arrow fns so they can be passed straight to useSyncExternalStore) ----
  subscribe = (fn: () => void) => { this.listeners.add(fn); return () => { this.listeners.delete(fn); }; };
  getView = () => this.view;
  subscribeMeters = (fn: () => void) => { this.meterListeners.add(fn); return () => { this.meterListeners.delete(fn); }; };
  getLevels = () => this.levels;

  private set(patch: Partial<GuestView>) { this.view = { ...this.view, ...patch }; this.listeners.forEach((l) => l()); }
  private notice(text: string) {
    this.set({ notice: text });
    if (this.noticeTimer) clearTimeout(this.noticeTimer);
    this.noticeTimer = setTimeout(() => this.set({ notice: undefined }), 2500);
  }

  // ---- lifecycle ----
  join(code: string, name: string, resume?: Credentials) {
    this.joinArgs = { code: normalizeCode(code), name: name.trim() || 'Guest' };
    this.creds = resume ?? null;
    this.attempt = 0;
    this.set({ status: 'connecting', error: undefined });
    this.open();
  }

  leave() { this.joinArgs = null; this.creds = null; this.ws?.close(); this.set({ status: 'idle', state: null, role: null }); }

  private open() {
    const WS = this.opts.WebSocketImpl ?? WebSocket;
    const ws = new WS(this.url);
    this.ws = ws;
    ws.onopen = () => {
      this.attempt = 0;
      const { code, name } = this.joinArgs!;
      this.raw({ t: 'guest.join', code, name, resume: this.creds ?? undefined });
    };
    ws.onmessage = (e) => { try { this.onServer(JSON.parse(String(e.data))); } catch { /* ignore */ } };
    ws.onclose = () => {
      if (this.ws !== ws || TERMINAL.includes(this.view.status) || !this.joinArgs) return;
      if (!this.creds) return this.set({ status: 'error', error: 'Connection lost before the host approved you' });
      this.set({ status: 'reconnecting' });
      setTimeout(() => this.open(), Math.min(8000, 400 * 2 ** this.attempt++));   // phones drop sockets constantly
    };
  }

  private raw(m: ClientToServer) { if (this.ws?.readyState === 1) this.ws.send(JSON.stringify(m)); }
  private up(msg: GuestMsg) { this.raw({ t: 'up', msg }); }

  private onServer(m: ServerToClient) {
    switch (m.t) {
      case 'join.pending': return this.set({ status: 'pending' });
      case 'join.approved':
        this.creds = { sessionId: m.sessionId, guestId: m.guestId, token: m.token };
        this.opts.onCredentials?.(this.creds);
        return this.set({ status: 'live', role: m.role, error: undefined });
      case 'join.denied': return this.set({ status: 'denied' });
      case 'kicked': return this.set({ status: 'kicked' });
      case 'session.ended': return this.set({ status: 'ended' });
      case 'host.offline': return this.set({ status: 'host-offline' });
      case 'host.online': return this.set({ status: 'live' });
      case 'error':
        if (m.code === 'session_gone') return this.set({ status: 'ended' });
        return this.set({
          status: 'error',
          error: m.code === 'bad_code' ? 'That code isn’t valid or has expired'
            : m.code === 'rate_limited' ? 'Too many attempts. Wait a minute and try again'
            : m.code === 'too_many_pending' ? 'The host has too many waiting requests' : 'Something went wrong',
        });
      case 'msg': return this.onHost(m.msg);
    }
  }

  private onHost(msg: import('../protocol').HostMsg) {
    switch (msg.t) {
      case 'state': this.set({ state: msg.state }); return;
      case 'meters': this.levels = msg.levels; this.meterListeners.forEach((l) => l()); return;
      case 'ack':
        if (!msg.ok) { this.notice(REASONS[msg.reason ?? ''] ?? 'Change rejected'); this.up({ t: 'resync' }); }
        return;
      case 'delta': {
        const s = this.view.state;
        if (!s || msg.rev <= s.rev) return;                         // stale / duplicate
        if (msg.rev > s.rev + 1) return this.up({ t: 'resync' });    // gap: ask for a fresh snapshot
        this.set({ state: this.applyDelta(s, msg, msg.rev) });
      }
    }
  }

  private applyDelta(s: SessionState, d: Delta, rev: number): SessionState {
    const now = Date.now();
    const tracks = d.tracks
      ? s.tracks.map((t) => {
          const p = d.tracks!.find((x) => x.id === t.id);
          if (!p) return t;
          const next = { ...t };
          for (const [k, v] of Object.entries(p)) {
            if (k === 'id' || (this.held.get(`${t.id}:${k}`) ?? 0) > now) continue;   // don't fight the user's finger
            (next as Record<string, unknown>)[k] = v;
          }
          return next;
        })
      : s.tracks;
    return { ...s, rev, tracks, transport: d.transport ? { ...s.transport, ...d.transport } : s.transport };
  }

  // ---- commands ----
  /** Fader / pan / mute / solo / arm. Applied locally right away, sent at <= 30 Hz, last value always wins. */
  setControl(track: string, param: 'volume' | 'pan' | 'mute' | 'solo' | 'armed', value: number | boolean) {
    const s = this.view.state; if (!s) return;
    this.held.set(`${track}:${param}`, Date.now() + HOLD_MS);
    this.set({ state: { ...s, tracks: s.tracks.map((t) => (t.id === track ? { ...t, [param]: value } : t)) } });
    const op = (param === 'volume' || param === 'pan'
      ? { k: 'param', track, param, value } : { k: 'toggle', track, param, value }) as Op;
    this.pending.set(`${track}:${param}`, op);
    this.flushTimer ??= setTimeout(() => this.flush(), FLUSH_MS);
  }

  /** Finger lifted: flush the last value, then free the host-side lock. */
  release(track: string, param: string) {
    this.flush();
    this.held.set(`${track}:${param}`, Date.now() + 150);
    this.up({ t: 'release', track, param });
  }

  /** One-shot ops: transport, tempo, rename. */
  send(op: Op) { this.up({ t: 'op', id: this.nextId++, op }); }

  private flush() {
    if (this.flushTimer) { clearTimeout(this.flushTimer); this.flushTimer = null; }
    for (const op of this.pending.values()) this.up({ t: 'op', id: this.nextId++, op });
    this.pending.clear();
  }
}
