/**
 * Runs inside your DAW's React/webview layer (or any Node/browser context that can open a WebSocket).
 * Owns the session for remote guests: approval, permissions, validation, fader locks, deltas.
 *
 * Audio-thread safety: this file never touches audio. `backend.apply` is where you cross into JUCE.
 * Route it message-thread -> lock-free FIFO (e.g. juce::AbstractFifo) -> audio thread, with parameter smoothing.
 */
import {
  allowed, validateOp,
  type ClientToServer, type Delta, type GuestMsg, type HostMsg, type Op, type Role, type ServerToClient, type SessionState,
} from './protocol';

export interface HostBackend {
  /** Current session state without `rev` (read from your session store / JUCE bridge). */
  getState(): Omit<SessionState, 'rev'>;
  /** Apply an op that has already been validated and authorised. Return false to reject. */
  apply(op: Op): boolean | Promise<boolean>;
}

export interface GuestInfo { id: string; name: string; role: Role }
export type HostStatus = 'connecting' | 'live' | 'reconnecting' | 'closed';

export interface HostOptions {
  url: string;
  backend: HostBackend;
  /** Show an approve/deny prompt and resolve with the role to grant, or null to deny. Omitted = deny everyone. */
  onJoinRequest?: (req: { guestId: string; name: string }) => Promise<Role | null> | Role | null;
  onGuestsChanged?: (guests: GuestInfo[]) => void;
  onCode?: (code: string, expiresAt: number) => void;
  onStatus?: (s: HostStatus) => void;
  WebSocketImpl?: typeof WebSocket;
}

const LOCK_MS = 1200;          // a fader stays "owned" by whoever touched it last, this long
const MAX_OPS_PER_SEC = 200;   // per guest

function deltaFor(op: Op): Delta | null {
  switch (op.k) {
    case 'param':
    case 'toggle': return { tracks: [{ id: op.track, [op.param]: op.value } as { id: string }] };
    case 'rename': return { tracks: [{ id: op.track, name: op.name }] };
    case 'tempo': return { transport: { bpm: op.bpm } };
    case 'transport':
      if (op.cmd === 'play') return { transport: { playing: true } };
      if (op.cmd === 'stop') return { transport: { playing: false, recording: false } };
      if (op.cmd === 'record') return { transport: { playing: true, recording: true } };
      return { transport: { beats: op.beats ?? 0 } };
  }
}

export class HostSession {
  code = '';
  rev = 0;
  private ws: WebSocket | null = null;
  private creds: { sessionId: string; token: string } | null = null;
  private guests = new Map<string, GuestInfo>();
  private locks = new Map<string, { guestId: string; until: number }>();
  private rate = new Map<string, { start: number; n: number }>();
  private chain: Promise<void> = Promise.resolve();
  private attempt = 0;
  private closed = false;
  private settle: { resolve: (v: { code: string; expiresAt: number }) => void; reject: (e: Error) => void } | null = null;

  constructor(private opts: HostOptions) {}

  start(): Promise<{ code: string; expiresAt: number }> {
    return new Promise((resolve, reject) => { this.settle = { resolve, reject }; this.open(); });
  }

  /** Call when YOUR side changes state (local fader move, transport tick, automation). */
  publish(change: Delta) { this.rev++; this.toGuests({ t: 'delta', rev: this.rev, ...change }); }
  /**
   * Full snapshot for structural changes (track added/removed/reordered).
   * Deltas cannot introduce unknown track ids, so guests need the new picture.
   */
  publishState() { this.sendState(); }
  /** Peak levels per track, 0..1 [L, R]. Call ~20 Hz. Unversioned and droppable. */
  meters(levels: Record<string, [number, number]>) { this.toGuests({ t: 'meters', levels }); }

  kick(guestId: string) { this.sendRaw({ t: 'host.kick', guestId }); }
  newCode() { this.sendRaw({ t: 'host.newCode' }); }
  end() { this.closed = true; this.sendRaw({ t: 'host.end' }); this.ws?.close(); this.opts.onStatus?.('closed'); }
  listGuests(): GuestInfo[] { return [...this.guests.values()]; }

  // ---------------- connection ----------------
  private open() {
    this.opts.onStatus?.(this.creds ? 'reconnecting' : 'connecting');
    const WS = this.opts.WebSocketImpl ?? WebSocket;
    const ws = new WS(this.opts.url);
    this.ws = ws;
    ws.onopen = () => {
      this.attempt = 0;
      this.sendRaw(this.creds ? { t: 'host.resume', ...this.creds } : { t: 'host.create' });
    };
    ws.onmessage = (e) => { try { this.onServer(JSON.parse(String(e.data))); } catch { /* ignore malformed frame */ } };
    ws.onclose = () => {
      if (this.closed) return;
      if (!this.creds) { this.settle?.reject(new Error('relay unreachable')); this.settle = null; return; }
      this.opts.onStatus?.('reconnecting');
      setTimeout(() => this.open(), Math.min(10_000, 500 * 2 ** this.attempt++));
    };
  }

  private sendRaw(m: ClientToServer) { if (this.ws?.readyState === 1) this.ws.send(JSON.stringify(m)); }
  private toGuests(msg: HostMsg, guestId?: string) { this.sendRaw({ t: 'to', guestId, msg }); }
  private state(): SessionState { return { ...this.opts.backend.getState(), rev: this.rev }; }
  private sendState(guestId?: string) { this.toGuests({ t: 'state', state: this.state() }, guestId); }
  private ack(guestId: string, id: number, ok: boolean, reason?: string) { this.toGuests({ t: 'ack', id, ok, reason }, guestId); }
  private notifyGuests() { this.opts.onGuestsChanged?.(this.listGuests()); }

  private onServer(m: ServerToClient) {
    switch (m.t) {
      case 'host.created':
        this.creds = { sessionId: m.sessionId, token: m.token };
        this.code = m.code; this.opts.onCode?.(m.code, m.codeExpiresAt); this.opts.onStatus?.('live');
        this.settle?.resolve({ code: m.code, expiresAt: m.codeExpiresAt }); this.settle = null;
        return;
      case 'host.resumed':
        this.code = m.code; this.opts.onCode?.(m.code, m.codeExpiresAt); this.opts.onStatus?.('live');
        this.guests.clear(); // relay re-announces everyone who is still connected
        this.notifyGuests();
        return;
      case 'code.updated': this.code = m.code; this.opts.onCode?.(m.code, m.codeExpiresAt); return;
      case 'join.request': void this.decide(m.guestId, m.name); return;
      case 'guest.online':
        this.guests.set(m.guestId, { id: m.guestId, name: m.name, role: m.role });
        this.notifyGuests(); this.sendState(m.guestId); return;
      case 'guest.left':
        this.guests.delete(m.guestId);
        for (const [k, l] of this.locks) if (l.guestId === m.guestId) this.locks.delete(k);
        this.notifyGuests(); return;
      case 'from':
        this.chain = this.chain.then(() => this.onFrom(m.guestId, m.role, m.msg)).catch(() => {});
        return;
      case 'error':
        if (m.code === 'session_gone') { this.closed = true; this.creds = null; this.opts.onStatus?.('closed'); }
        return;
      case 'session.ended': this.closed = true; this.opts.onStatus?.('closed'); return;
    }
  }

  private async decide(guestId: string, name: string) {
    let role: Role | null = null;
    try { role = (await this.opts.onJoinRequest?.({ guestId, name })) ?? null; } catch { role = null; }
    this.sendRaw(role ? { t: 'host.approve', guestId, role } : { t: 'host.deny', guestId });
  }

  // ---------------- guest ops ----------------
  private underRateLimit(guestId: string): boolean {
    const now = Date.now();
    const r = this.rate.get(guestId);
    if (!r || now - r.start > 1000) { this.rate.set(guestId, { start: now, n: 1 }); return true; }
    return ++r.n <= MAX_OPS_PER_SEC;
  }

  private async onFrom(guestId: string, role: Role, msg: GuestMsg) {
    if (msg.t === 'resync') return this.sendState(guestId);
    if (msg.t === 'release') {
      for (const [k, l] of this.locks) if (l.guestId === guestId && k === `${msg.track}:${msg.param}`) this.locks.delete(k);
      return;
    }
    if (msg.t !== 'op' || typeof msg.id !== 'number') return;

    if (!this.underRateLimit(guestId)) return this.ack(guestId, msg.id, false, 'rate_limited');
    const op = validateOp(msg.op);                                      // 1. shape + ranges
    if (!op) return this.ack(guestId, msg.id, false, 'invalid');
    if (!allowed(role, op)) return this.ack(guestId, msg.id, false, 'forbidden'); // 2. role
    if ('track' in op && !this.opts.backend.getState().tracks.some((t) => t.id === op.track))
      return this.ack(guestId, msg.id, false, 'unknown_track');         // 3. target exists

    // 4. console-style touch lock: whoever grabbed a control owns it until they let go or go idle
    const key = op.k === 'param' || op.k === 'toggle' ? `${op.track}:${op.param}` : null;
    if (key) {
      const l = this.locks.get(key);
      if (l && l.guestId !== guestId && l.until > Date.now()) return this.ack(guestId, msg.id, false, 'locked');
    }

    let ok = false;
    try { ok = await this.opts.backend.apply(op); } catch { ok = false; }
    if (!ok) return this.ack(guestId, msg.id, false, 'rejected');

    if (key) this.locks.set(key, { guestId, until: Date.now() + LOCK_MS });
    const d = deltaFor(op);
    if (d) this.publish(d);
    this.ack(guestId, msg.id, true);
  }
}
