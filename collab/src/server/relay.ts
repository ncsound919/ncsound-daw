/**
 * Session relay. Owns codes, seats, approval, resume tokens and kick/end.
 * It never applies a mix change itself: guest frames are forwarded to the host,
 * and host frames are broadcast (or addressed) to guests.
 */
import { randomBytes, randomUUID } from 'node:crypto';
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import { hostname as osHostname, networkInterfaces } from 'node:os';
import { WebSocketServer, WebSocket } from 'ws';
import {
  CODE_ALPHABET,
  CODE_LENGTH,
  normalizeCode,
  type ClientToServer,
  type Role,
  type ServerToClient,
} from '../protocol';

export interface RelayOptions {
  port?: number;
  host?: string;
  maxFailuresPerMin?: number;
  /** Upgrade attempts per IP per minute. Default 120. */
  maxUpgradesPerMin?: number;
  /**
   * Extra hostnames (no port) accepted in the Host header and in Origin.
   * Use this for a public domain sitting behind a TLS-terminating proxy.
   */
  allowedHosts?: string[];
  /**
   * Exact browser origins to accept, e.g. `https://daw.example.com`.
   * Required when the relay is behind a proxy and the Origin port is not the
   * port this process listens on.
   */
  allowedOrigins?: string[];
  /**
   * Accept handshakes that omit Origin from non-loopback peers. Default false:
   * only loopback native clients may omit it. Prefer an explicit Origin for
   * remote native clients, because Origin is the CSWSH defence.
   */
  allowMissingOrigin?: boolean;
  codeTtlMs?: number;
  guestGraceMs?: number;
  httpHandler?: (req: IncomingMessage, res: ServerResponse) => void | Promise<void>;
}

export interface Relay {
  port: number;
  close(): Promise<void>;
}

interface Conn {
  ws: WebSocket;
  ip: string;
  kind: 'host' | 'guest' | 'pending' | null;
  sessionId?: string;
  guestId?: string;
}

interface Guest {
  id: string;
  name: string;
  role: Role;
  token: string;
  conn: Conn | null;
  graceTimer?: ReturnType<typeof setTimeout>;
}

interface Pending {
  id: string;
  name: string;
  conn: Conn;
  ip: string;
}

interface Session {
  id: string;
  token: string;
  code: string;
  codeExpiresAt: number;
  host: Conn | null;
  guests: Map<string, Guest>;
  pending: Map<string, Pending>;
  ended: boolean;
}

const MAX_PENDING = 16;
const MAX_PAYLOAD = 128 * 1024;
const MAX_CONNECTIONS = 512;

function normaliseHostname(value: string): string {
  const trimmed = value.trim().toLowerCase().replace(/\.$/, '');
  return trimmed.startsWith('[') && trimmed.endsWith(']') ? trimmed.slice(1, -1) : trimmed;
}

function isLoopbackAddress(value: string | undefined): boolean {
  if (!value) return false;
  const address = value.replace(/^::ffff:/i, '');
  return address === '::1' || address === '127.0.0.1' || address.startsWith('127.');
}

/**
 * Parse an HTTP authority (Host header) without trusting it. `port` is null
 * when the header omitted one, so the caller can apply the origin's scheme.
 */
function parseAuthority(value: string | undefined): { host: string; port: number | null } | null {
  if (!value || value.length > 255) return null;
  // Userinfo, path, whitespace and comma-joined headers are never valid here.
  if (/[\s/\\@,#]/.test(value)) return null;

  try {
    const url = new URL(`http://${value}`);
    if (url.username || url.password || url.pathname !== '/' || url.search || url.hash) return null;
    const host = normaliseHostname(url.hostname);
    if (!host) return null;
    return { host, port: url.port ? Number(url.port) : null };
  } catch {
    return null;
  }
}

/** Parse a browser Origin. Only scheme://host:port is a valid origin value. */
function parseOrigin(value: string | undefined): URL | null {
  if (!value || value === 'null') return null;
  try {
    const url = new URL(value);
    if (url.protocol !== 'http:' && url.protocol !== 'https:') return null;
    if (url.username || url.password || url.pathname !== '/' || url.search || url.hash) return null;
    if (!normaliseHostname(url.hostname)) return null;
    return url;
  } catch {
    return null;
  }
}

function originPort(url: URL): number {
  if (url.port) return Number(url.port);
  return url.protocol === 'https:' ? 443 : 80;
}

function localHostnames(bindHost: string): Set<string> {
  const names = new Set<string>();
  const add = (value: string | undefined) => {
    if (!value) return;
    const host = normaliseHostname(value);
    if (host) names.add(host);
  };

  add('localhost');
  add('127.0.0.1');
  add('::1');
  add(osHostname());
  for (const list of Object.values(networkInterfaces())) {
    for (const net of list ?? []) if (!net.internal) add(net.address);
  }
  if (bindHost && bindHost !== '0.0.0.0' && bindHost !== '::') add(bindHost);
  return names;
}

class WindowLimiter {
  private readonly records = new Map<string, { start: number; count: number }>();

  constructor(private readonly max: number, private readonly windowMs = 60_000) {}

  allow(key: string): boolean {
    const now = Date.now();
    let record = this.records.get(key);
    if (!record || now - record.start > this.windowMs) {
      record = { start: now, count: 0 };
      this.records.set(key, record);
    }
    if (record.count >= this.max) return false;
    record.count++;
    return true;
  }
}

function rejectUpgrade(socket: import('node:stream').Duplex, status: number, reason: string): void {
  if (socket.writable) {
    socket.write(`HTTP/1.1 ${status} ${reason}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
  }
  socket.destroy();
}

function makeCode(taken: Set<string>): string {
  for (let attempt = 0; attempt < 100; attempt++) {
    const bytes = randomBytes(CODE_LENGTH);
    let code = '';
    for (let i = 0; i < CODE_LENGTH; i++) code += CODE_ALPHABET[bytes[i]! % CODE_ALPHABET.length];
    if (!taken.has(code)) return code;
  }
  throw new Error('could not allocate a unique session code');
}

function cleanName(name: unknown): string {
  const value = String(name ?? '').trim().replace(/\s+/g, ' ');
  return (value || 'Guest').slice(0, 32);
}

class FailureTracker {
  private readonly records = new Map<string, { count: number; start: number; blockedUntil: number }>();

  constructor(private readonly max: number, private readonly windowMs = 60_000, private readonly blockMs = 60_000) {}

  isBlocked(ip: string): boolean {
    const record = this.records.get(ip);
    return !!record && record.blockedUntil > Date.now();
  }

  fail(ip: string): void {
    const now = Date.now();
    let record = this.records.get(ip);
    if (!record || now - record.start > this.windowMs) record = { count: 0, start: now, blockedUntil: 0 };
    record.count++;
    if (record.count >= this.max) record.blockedUntil = now + this.blockMs;
    this.records.set(ip, record);
  }

  clear(ip: string): void {
    this.records.delete(ip);
  }
}

export async function createRelay(options: RelayOptions = {}): Promise<Relay> {
  const port = options.port ?? 0;
  const bindHost = options.host ?? '0.0.0.0';
  const codeTtlMs = options.codeTtlMs ?? 15 * 60_000;
  const guestGraceMs = options.guestGraceMs ?? 2 * 60_000;
  const failures = new FailureTracker(options.maxFailuresPerMin ?? 6);
  const upgrades = new WindowLimiter(options.maxUpgradesPerMin ?? 120);

  const sessions = new Map<string, Session>();
  const codes = new Map<string, string>();
  const conns = new Map<WebSocket, Conn>();

  // DNS-rebinding defence: the Host header must name this machine (or an
  // explicitly configured domain), not an attacker-controlled hostname.
  const allowedHostnames = localHostnames(bindHost);
  for (const host of options.allowedHosts ?? []) {
    const name = normaliseHostname(host);
    if (name) allowedHostnames.add(name);
  }

  // CSWSH defence: exact Origin allowlist. Same-port local origins work out of
  // the box; a proxied deployment supplies its public origin explicitly.
  const allowedOrigins = new Set<string>();
  for (const value of options.allowedOrigins ?? []) {
    const origin = parseOrigin(value);
    if (origin) {
      allowedOrigins.add(origin.origin);
      allowedHostnames.add(normaliseHostname(origin.hostname));
    }
  }

  let boundPort = 0;

  const hostAllowed = (req: IncomingMessage): boolean => {
    const authority = parseAuthority(req.headers.host);
    return !!authority && allowedHostnames.has(authority.host);
  };

  const originAllowed = (req: IncomingMessage): boolean => {
    const raw = req.headers.origin;
    if (raw === undefined) {
      // Native clients do not send Origin. Trust that omission only on the
      // loopback interface, where a browser cannot be the caller.
      return options.allowMissingOrigin === true || isLoopbackAddress(req.socket.remoteAddress);
    }
    const origin = parseOrigin(raw);
    if (!origin || !allowedHostnames.has(normaliseHostname(origin.hostname))) return false;
    if (allowedOrigins.has(origin.origin)) return true;
    return originPort(origin) === boundPort;
  };

  const defaultHandler = (req: IncomingMessage, res: ServerResponse) => {
    if (req.url === '/health') {
      res.writeHead(200, { 'content-type': 'application/json', 'x-content-type-options': 'nosniff' });
      res.end(JSON.stringify({ ok: true, sessions: sessions.size }));
      return;
    }
    res.writeHead(404, { 'content-type': 'text/plain', 'x-content-type-options': 'nosniff' });
    res.end('Not found');
  };

  const server = createServer((req, res) => {
    // HTTP is reachable from a browser too, so apply the same Host check.
    if (!hostAllowed(req)) {
      res.writeHead(403, { 'content-type': 'text/plain', 'x-content-type-options': 'nosniff' });
      res.end('Forbidden');
      return;
    }
    void Promise.resolve((options.httpHandler ?? defaultHandler)(req, res)).catch(() => {
      if (!res.headersSent) res.writeHead(500);
      res.end();
    });
  });

  // `noServer` plus an explicit upgrade handler is the ws-recommended place to
  // enforce Origin/Host before the handshake (verifyClient is discouraged).
  const wss = new WebSocketServer({ noServer: true, maxPayload: MAX_PAYLOAD, perMessageDeflate: false });

  server.on('upgrade', (req, socket, head) => {
    socket.on('error', () => {});
    const ip = req.socket.remoteAddress ?? 'unknown';
    if (!upgrades.allow(ip)) return rejectUpgrade(socket, 429, 'Too Many Requests');
    if (!hostAllowed(req) || !originAllowed(req)) return rejectUpgrade(socket, 403, 'Forbidden');
    if (wss.clients.size >= MAX_CONNECTIONS) return rejectUpgrade(socket, 503, 'Service Unavailable');
    try {
      wss.handleUpgrade(req, socket, head, (ws) => wss.emit('connection', ws, req));
    } catch {
      rejectUpgrade(socket, 400, 'Bad Request');
    }
  });

  const send = (target: WebSocket | Conn | null | undefined, msg: ServerToClient): void => {
    const ws = target && 'ws' in target ? target.ws : target;
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    try {
      ws.send(JSON.stringify(msg));
    } catch {
      // A dying socket is handled by its close event.
    }
  };

  const sendError = (conn: Conn, code: Extract<ServerToClient, { t: 'error' }>['code']): void => {
    send(conn, { t: 'error', code });
  };

  const forEachGuest = (session: Session, fn: (guest: Guest) => void): void => {
    for (const guest of session.guests.values()) fn(guest);
  };

  const announceGuestsToHost = (session: Session): void => {
    if (!session.host) return;
    forEachGuest(session, (guest) => {
      send(session.host, { t: 'guest.online', guestId: guest.id, name: guest.name, role: guest.role });
    });
    for (const pending of session.pending.values()) {
      send(session.host, { t: 'join.request', guestId: pending.id, name: pending.name });
    }
  };

  const issueCode = (session: Session): void => {
    const taken = new Set(codes.keys());
    const code = makeCode(taken);
    if (session.code) codes.delete(session.code);
    session.code = code;
    session.codeExpiresAt = Date.now() + codeTtlMs;
    codes.set(code, session.id);
  };

  const endSession = (session: Session, notifyHost = true): void => {
    if (session.ended) return;
    session.ended = true;
    session.guests.forEach((guest) => {
      if (guest.graceTimer) clearTimeout(guest.graceTimer);
    });

    const targets: WebSocket[] = [];
    if (session.host) targets.push(session.host.ws);
    forEachGuest(session, (guest) => {
      if (guest.conn) targets.push(guest.conn.ws);
    });
    for (const pending of session.pending.values()) targets.push(pending.conn.ws);

    codes.delete(session.code);
    sessions.delete(session.id);
    session.host = null;

    for (const ws of targets) {
      send(ws, { t: 'session.ended' });
    }
    // Let the frame flush before the socket goes away.
    setTimeout(() => {
      for (const ws of targets) {
        if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) ws.close(1000, 'session ended');
      }
    }, 60).unref?.();

    void notifyHost;
  };

  const attachGuest = (session: Session, guest: Guest, conn: Conn): void => {
    if (guest.graceTimer) {
      clearTimeout(guest.graceTimer);
      guest.graceTimer = undefined;
    }

    if (guest.conn && guest.conn !== conn) {
      // A second tab presented the same resume token. Only one seat is active.
      send(guest.conn, { t: 'kicked' });
      guest.conn.ws.close(4001, 'seat taken');
    }

    guest.conn = conn;
    conn.kind = 'guest';
    conn.sessionId = session.id;
    conn.guestId = guest.id;
    failures.clear(conn.ip);

    send(conn, {
      t: 'join.approved',
      sessionId: session.id,
      guestId: guest.id,
      token: guest.token,
      role: guest.role,
    });
    send(session.host, { t: 'guest.online', guestId: guest.id, name: guest.name, role: guest.role });
  };

  const joinSession = (conn: Conn, msg: Extract<ClientToServer, { t: 'guest.join' }>): void => {
    if (failures.isBlocked(conn.ip)) return sendError(conn, 'rate_limited');

    if (msg.resume) {
      const session = sessions.get(msg.resume.sessionId);
      const guest = session?.guests.get(msg.resume.guestId);
      if (session && !session.ended && guest && guest.token === msg.resume.token) {
        attachGuest(session, guest, conn);
        return;
      }
    }

    const code = normalizeCode(msg.code);
    const sessionId = codes.get(code);
    const session = sessionId ? sessions.get(sessionId) : undefined;

    if (!session || session.ended || Date.now() > session.codeExpiresAt) {
      failures.fail(conn.ip);
      return sendError(conn, session ? 'session_gone' : 'bad_code');
    }

    if (conn.kind === 'pending') return;

    if (session.pending.size >= MAX_PENDING) return sendError(conn, 'too_many_pending');

    const pending: Pending = {
      id: randomUUID(),
      name: cleanName(msg.name),
      conn,
      ip: conn.ip,
    };
    session.pending.set(pending.id, pending);
    conn.kind = 'pending';
    conn.sessionId = session.id;
    conn.guestId = pending.id;

    send(conn, { t: 'join.pending' });
    send(session.host, { t: 'join.request', guestId: pending.id, name: pending.name });
  };

  const createSession = (conn: Conn): void => {
    const id = randomUUID();
    const token = randomBytes(32).toString('hex');
    const session: Session = {
      id,
      token,
      code: '',
      codeExpiresAt: 0,
      host: conn,
      guests: new Map(),
      pending: new Map(),
      ended: false,
    };
    issueCode(session);
    sessions.set(id, session);
    conn.kind = 'host';
    conn.sessionId = id;
    conn.guestId = undefined;

    send(conn, { t: 'host.created', sessionId: id, token, code: session.code, codeExpiresAt: session.codeExpiresAt });
  };

  const hostSession = (conn: Conn): Session | undefined => {
    if (conn.kind !== 'host' || !conn.sessionId) return undefined;
    const session = sessions.get(conn.sessionId);
    return session && session.host === conn && !session.ended ? session : undefined;
  };

  const handleHostMessage = (conn: Conn, msg: ClientToServer): void => {
    if (msg.t === 'host.create') return createSession(conn);
    if (msg.t === 'host.resume') {
      const session = sessions.get(msg.sessionId);
      if (!session || session.ended || session.token !== msg.token) return sendError(conn, 'session_gone');

      if (session.host && session.host !== conn) session.host.ws.close(4002, 'host replaced');
      session.host = conn;
      conn.kind = 'host';
      conn.sessionId = session.id;
      conn.guestId = undefined;

      send(conn, { t: 'host.resumed', sessionId: session.id, code: session.code, codeExpiresAt: session.codeExpiresAt });
      forEachGuest(session, (guest) => send(guest.conn, { t: 'host.online' }));
      announceGuestsToHost(session);
      return;
    }

    const session = hostSession(conn);
    if (!session) return;

    switch (msg.t) {
      case 'host.approve': {
        const pending = session.pending.get(msg.guestId);
        if (!pending) return;
        session.pending.delete(pending.id);

        if (!['engineer', 'mixer', 'viewer'].includes(msg.role) || pending.conn.ws.readyState !== WebSocket.OPEN) {
          send(pending.conn, { t: 'join.denied' });
          return;
        }

        const guest: Guest = {
          id: pending.id,
          name: pending.name,
          role: msg.role,
          token: randomBytes(24).toString('hex'),
          conn: null,
        };
        session.guests.set(guest.id, guest);
        attachGuest(session, guest, pending.conn);
        return;
      }
      case 'host.deny': {
        const pending = session.pending.get(msg.guestId);
        if (!pending) return;
        session.pending.delete(pending.id);
        send(pending.conn, { t: 'join.denied' });
        setTimeout(() => pending.conn.ws.close(4003, 'denied'), 60).unref?.();
        return;
      }
      case 'host.kick': {
        const guest = session.guests.get(msg.guestId);
        if (!guest) return;
        session.guests.delete(guest.id);
        if (guest.graceTimer) clearTimeout(guest.graceTimer);
        if (guest.conn) {
          send(guest.conn, { t: 'kicked' });
          setTimeout(() => guest.conn?.ws.close(4004, 'kicked'), 60).unref?.();
        }
        send(session.host, { t: 'guest.left', guestId: guest.id });
        return;
      }
      case 'host.newCode': {
        issueCode(session);
        send(session.host, { t: 'code.updated', code: session.code, codeExpiresAt: session.codeExpiresAt });
        return;
      }
      case 'host.end':
        endSession(session);
        return;
      case 'to': {
        if (!msg.msg || typeof msg.msg !== 'object') return;
        if (msg.guestId) {
          const guest = session.guests.get(msg.guestId);
          if (guest?.conn) send(guest.conn, { t: 'msg', msg: msg.msg });
          return;
        }
        forEachGuest(session, (guest) => {
          if (guest.conn) send(guest.conn, { t: 'msg', msg: msg.msg });
        });
        return;
      }
      default:
        return;
    }
  };

  const handleGuestMessage = (conn: Conn, msg: ClientToServer): void => {
    if (!conn.sessionId || !conn.guestId) return;
    const session = sessions.get(conn.sessionId);
    if (!session || session.ended) return;
    const guest = session.guests.get(conn.guestId);
    if (!guest || guest.conn !== conn) return;

    if (msg.t === 'up') {
      if (!msg.msg || typeof msg.msg !== 'object') return;
      send(session.host, { t: 'from', guestId: guest.id, role: guest.role, msg: msg.msg });
      return;
    }
    if (msg.t === 'guest.join') {
      // Already seated; a resume frame on a live socket is a no-op.
      send(conn, {
        t: 'join.approved',
        sessionId: session.id,
        guestId: guest.id,
        token: guest.token,
        role: guest.role,
      });
    }
  };

  const handle = (conn: Conn, raw: string): void => {
    let parsed: unknown;
    try {
      parsed = JSON.parse(raw);
    } catch {
      return;
    }
    if (!parsed || typeof parsed !== 'object' || !('t' in parsed)) return;
    const msg = parsed as ClientToServer;

    switch (msg.t) {
      case 'host.create':
      case 'host.resume':
      case 'host.approve':
      case 'host.deny':
      case 'host.kick':
      case 'host.newCode':
      case 'host.end':
      case 'to':
        handleHostMessage(conn, msg);
        return;
      case 'guest.join':
        joinSession(conn, msg);
        return;
      case 'up':
        handleGuestMessage(conn, msg);
        return;
      default:
        return;
    }
  };

  const onClose = (ws: WebSocket): void => {
    const conn = conns.get(ws);
    if (!conn) return;
    conns.delete(ws);

    if (!conn.sessionId) return;
    const session = sessions.get(conn.sessionId);
    if (!session) return;

    if (conn.kind === 'host' && session.host === conn) {
      session.host = null;
      forEachGuest(session, (guest) => send(guest.conn, { t: 'host.offline' }));
      return;
    }

    if (conn.kind === 'pending') {
      session.pending.delete(conn.guestId ?? '');
      return;
    }

    if (conn.kind === 'guest' && conn.guestId) {
      const guest = session.guests.get(conn.guestId);
      if (!guest || guest.conn !== conn) return;
      guest.conn = null;
      send(session.host, { t: 'guest.left', guestId: guest.id });
      guest.graceTimer = setTimeout(() => {
        if (!guest.conn) session.guests.delete(guest.id);
      }, guestGraceMs);
      guest.graceTimer.unref?.();
    }
  };

  wss.on('connection', (ws, req) => {
    const conn: Conn = {
      ws,
      ip: req.socket.remoteAddress ?? 'unknown',
      kind: null,
    };
    conns.set(ws, conn);

    // Liveness has to live on the socket, because the reaper only ever sees
    // `wss.clients` (raw sockets) and never this Conn wrapper. The flag used to
    // be written to Conn and read from the socket, so it never matched: a dead
    // peer was pinged forever and never terminated.
    const live = ws as WebSocket & { isAlive: boolean };
    live.isAlive = true;
    ws.on('pong', () => {
      live.isAlive = true;
    });
    ws.on('message', (data) => {
      try {
        handle(conn, data.toString());
      } catch {
        // Never let one malformed frame take the relay down.
      }
    });
    ws.on('error', () => {});
    ws.on('close', () => onClose(ws));
  });

  const heartbeat = setInterval(() => {
    for (const ws of wss.clients) {
      const live = ws as WebSocket & { isAlive: boolean };
      if (live.isAlive === false) {
        ws.terminate();
        continue;
      }
      live.isAlive = false;
      try {
        ws.ping();
      } catch {
        // closed underneath us
      }
    }
  }, 30_000);
  heartbeat.unref?.();

  await new Promise<void>((resolve, reject) => {
    const onError = (error: Error) => reject(error);
    server.once('error', onError);
    server.listen(port, bindHost, () => {
      server.off('error', onError);
      const address = server.address();
      boundPort = typeof address === 'object' && address ? address.port : port;
      resolve();
    });
  });

  let closed = false;

  return {
    port: boundPort,
    async close() {
      if (closed) return;
      closed = true;
      clearInterval(heartbeat);
      for (const session of sessions.values()) {
        for (const guest of session.guests.values()) if (guest.graceTimer) clearTimeout(guest.graceTimer);
      }
      for (const ws of wss.clients) ws.terminate();
      await new Promise<void>((resolve) => wss.close(() => resolve()));
      await new Promise<void>((resolve) => {
        server.close(() => resolve());
        server.closeAllConnections?.();
        setTimeout(resolve, 300).unref?.();
      });
    },
  };
}
