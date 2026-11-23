import { useEffect, useMemo, useRef, useState } from "react";
import { backend } from "../lib/bridge";
import { HostSession, type GuestInfo, type HostStatus } from "./HostSession";
import type { Op, Role, TrackDelta } from "./protocol";

/**
 * NCSound Remote host panel — desktop side.
 *
 * Runs inside the DAW's React workspace, so this is the same HostSession the
 * collab pilot tested; the only new code is the bridge adapter that reads the
 * live session and applies guest ops through the existing native routes.
 *
 * Transport: the relay is the separate Node process in `collab/` (or the
 * embedded relay on an NC Studio phone). Both speak the same protocol.
 */

interface PendingJoin {
  guestId: string;
  name: string;
  resolve: (role: Role | null) => void;
}

const TRACK_COLORS = ["#ff8a00", "#35d0d8", "#55d18b", "#e0567a", "#b98cff", "#e0a356"];

const dbToVolume = (db: number) => (db <= -60 ? 0 : Math.min(1, Math.max(0, 10 ** (db / 20))));

export interface RemoteState {
  status: HostStatus | "idle";
  code: string;
  expiresAt: number;
  guests: GuestInfo[];
  pending: PendingJoin[];
  notice: string;
}

export function useRemoteHost() {
  const [state, setState] = useState<RemoteState>({
    status: "idle",
    code: "",
    expiresAt: 0,
    guests: [],
    pending: [],
    notice: "",
  });
  const sessionRef = useRef<HostSession | null>(null);
  // Synchronous snapshot for the host backend: HostSession.getState() is sync,
  // the native bridge is async, so the panel keeps the latest picture cached.
  const snapshot = useRef<{ tracks: { id: string; name: string; color: string; volume: number; pan: number; mute: boolean; solo: boolean; armed: boolean }[]; title: string; transport: { playing: boolean; recording: boolean; beats: number; bpm: number } }>({
    tracks: [],
    title: "NC Sound Hub",
    transport: { playing: false, recording: false, beats: 0, bpm: 120 },
  });
  const pendingRef = useRef<PendingJoin[]>([]);

  async function refreshSnapshot() {
    try {
      const [app, transport] = await Promise.all([backend.getState(), backend.getTransport()]);
      snapshot.current = {
        title: "NC Sound Hub",
        transport: {
          playing: !!transport?.playing,
          recording: !!transport?.recording,
          // Collab counts beats; the bridge reports bars (1-based, 4/4 default).
          beats: Number(transport?.positionBar ?? 0) * 4,
          bpm: Number(transport?.bpm ?? app?.bpm ?? 120),
        },
        tracks: (app?.tracks ?? []).map((t, i) => ({
          id: `t${t.index}`,
          name: t.name,
          color: TRACK_COLORS[i % TRACK_COLORS.length],
          volume: dbToVolume(t.gainDb),
          pan: t.pan,
          mute: t.muted,
          solo: t.soloed,
          armed: !!t.armed,
        })),
      };
    } catch {
      // Bridge unavailable (dev browser): keep the last snapshot.
    }
  }

  function trackIndex(id: string): number | null {
    const n = Number(id.replace(/^t/, ""));
    return Number.isFinite(n) ? n : null;
  }

  async function applyOp(op: Op): Promise<boolean> {
    const index = "track" in op ? trackIndex(op.track) : null;
    switch (op.k) {
      case "param": {
        if (index === null) return false;
        if (op.param === "volume") {
          await backend.setTrackGain(index, op.value <= 0.001 ? -60 : 20 * Math.log10(op.value));
        } else {
          await backend.setTrackPan(index, op.value);
        }
        break;
      }
      case "toggle": {
        if (index === null) return false;
        if (op.param === "mute") await backend.setTrackMuted(index, op.value);
        else if (op.param === "solo") await backend.setTrackSolo(index, op.value);
        else if (op.param === "armed") await backend.setRecordArmed(index, op.value);
        else return false;
        break;
      }
      case "transport": {
        if (op.cmd === "play") await backend.setPlaying(true);
        else if (op.cmd === "stop") {
          await backend.stopRecording();
          await backend.setPlaying(false);
        } else if (op.cmd === "record") await backend.startRecording();
        else if (op.cmd === "locate") await backend.setPositionBar((op.beats ?? 0) / 4);
        else return false;
        break;
      }
      case "tempo":
        await backend.setBpm(op.bpm);
        break;
      case "rename":
        return false; // no native rename route yet: reject honestly
    }
    await refreshSnapshot();
    return true;
  }

  async function start(relayUrl: string) {
    if (sessionRef.current) return;
    await refreshSnapshot();
    const session = new HostSession({
      url: relayUrl,
      backend: {
        getState: () => snapshot.current,
        apply: applyOp,
      },
      onJoinRequest: ({ guestId, name }) =>
        new Promise<Role | null>((resolve) => {
          pendingRef.current = [...pendingRef.current, { guestId, name, resolve }];
          setState((s) => ({ ...s, pending: pendingRef.current }));
        }),
      onGuestsChanged: (guests) => setState((s) => ({ ...s, guests })),
      onCode: (code, expiresAt) => setState((s) => ({ ...s, code, expiresAt })),
      onStatus: (status) => setState((s) => ({ ...s, status })),
    });
    sessionRef.current = session;
    try {
      const { code, expiresAt } = await session.start();
      setState((s) => ({ ...s, code, expiresAt, status: "live" }));
    } catch (e) {
      setState((s) => ({ ...s, status: "closed", notice: `relay unreachable: ${String(e)}` }));
      sessionRef.current = null;
    }
  }

  function approve(guestId: string, role: Role) {
    const p = pendingRef.current.find((x) => x.guestId === guestId);
    p?.resolve(role);
    pendingRef.current = pendingRef.current.filter((x) => x.guestId !== guestId);
    setState((s) => ({ ...s, pending: pendingRef.current }));
  }

  function deny(guestId: string) {
    const p = pendingRef.current.find((x) => x.guestId === guestId);
    p?.resolve(null);
    pendingRef.current = pendingRef.current.filter((x) => x.guestId !== guestId);
    setState((s) => ({ ...s, pending: pendingRef.current }));
  }

  function kick(guestId: string) {
    sessionRef.current?.kick(guestId);
  }

  function stop() {
    sessionRef.current?.end();
    sessionRef.current = null;
    pendingRef.current = [];
    setState({ status: "idle", code: "", expiresAt: 0, guests: [], pending: [], notice: "" });
  }

  // Meter stream to guests at ~20 Hz. Peaks ride on getTransport(), the same
  // numbers the desktop's own meters use, so phone meters cannot drift.
  useEffect(() => {
    const timer = window.setInterval(async () => {
      const s = sessionRef.current;
      if (!s) return;
      try {
        const t = await backend.getTransport();
        const peaks = t?.trackPeaks ?? [];
        if (peaks.length === 0) return;
        const levels: Record<string, [number, number]> = {};
        snapshot.current.tracks.forEach((track, i) => {
          const mono = Math.min(1, Math.max(0, Number(peaks[i] ?? 0)));
          const pan = track.pan;
          levels[track.id] = [
            pan > 0 ? mono * (1 - pan) : mono,
            pan < 0 ? mono * (1 + pan) : mono,
          ];
        });
        s.meters(levels);
      } catch {
        // Bridge unavailable: skip this frame.
      }
    }, 50);
    return () => window.clearInterval(timer);
  }, []);

  // Local edits on the desktop must reach the guests: poll and publish deltas.
  useEffect(() => {    const timer = window.setInterval(async () => {
      const s = sessionRef.current;
      if (!s) return;
      const before = snapshot.current.tracks;
      await refreshSnapshot();
      const after = snapshot.current.tracks;
      if (before.length === 0) {
        s.publishState();
        return;
      }
      const idsBefore = before.map((t) => t.id).join(",");
      const idsAfter = after.map((t) => t.id).join(",");
      if (idsBefore !== idsAfter) {
        // Track list changed: only a snapshot can teach a guest the new ids.
        s.publishState();
        return;
      }
      const changed = after
        .map((t) => {
          const prev = before.find((p) => p.id === t.id);
          if (!prev) return { id: t.id };
          const delta: Record<string, unknown> = { id: t.id };
          let any = false;
          if (Math.abs(prev.volume - t.volume) > 0.001) { delta.volume = t.volume; any = true; }
          if (Math.abs(prev.pan - t.pan) > 0.001) { delta.pan = t.pan; any = true; }
          if (prev.mute !== t.mute) { delta.mute = t.mute; any = true; }
          if (prev.solo !== t.solo) { delta.solo = t.solo; any = true; }
          if (prev.armed !== t.armed) { delta.armed = t.armed; any = true; }
          return any ? delta : null;
        })
        .filter((d): d is { id: string } => !!d);
      if (changed.length > 0) s.publish({ tracks: changed as TrackDelta[] });
    }, 250);
    return () => window.clearInterval(timer);
  }, []);

  return useMemo(
    () => ({ state, start, stop, approve, deny, kick }),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [state],
  );
}

/** Renders inside the Settings / overflow surface. */
export function RemoteHostPanel({ remote, relayUrl }: { remote: ReturnType<typeof useRemoteHost>; relayUrl: string }) {
  const { state } = remote;
  const [url, setUrl] = useState(relayUrl);
  return (
    <div className="ws-remote-panel" data-testid="remote-panel">
      {state.status !== "live" ? (
        <div className="ws-remote-start">
          <p className="ws-remote-hint">
            Start the relay (collab server, or an NC Studio phone in relay mode), then share this session.
          </p>
          <input
            aria-label="Relay URL"
            value={url}
            onChange={(e) => setUrl(e.target.value)}
            placeholder="ws://192.168.1.20:8787"
            className="ws-remote-input"
          />
          <div className="ws-remote-actions">
            <button type="button" onClick={() => void remote.start(url)}>
              START SHARING
            </button>
          </div>
          {state.notice ? <p className="ws-remote-hint">{state.notice}</p> : null}
        </div>
      ) : (
        <div className="ws-remote-live">
          <div className="ws-remote-code" data-testid="remote-code">
            {state.code.slice(0, 4)}-{state.code.slice(4)}
          </div>
          <p className="ws-remote-hint">Phones join with this code via Phone Beat → REMOTE.</p>
          {state.pending.length > 0 ? (
            <div className="ws-remote-pending">
              {state.pending.map((p) => (
                <div key={p.guestId} className="ws-remote-row">
                  <span>{p.name}</span>
                  <button type="button" onClick={() => remote.approve(p.guestId, "mixer")}>MIXER</button>
                  <button type="button" onClick={() => remote.approve(p.guestId, "engineer")}>ENGINEER</button>
                  <button type="button" onClick={() => remote.deny(p.guestId)}>DENY</button>
                </div>
              ))}
            </div>
          ) : null}
          {state.guests.length > 0 ? (
            <div className="ws-remote-guests">
              {state.guests.map((g) => (
                <div key={g.id} className="ws-remote-row">
                  <span>{g.name} · {g.role}</span>
                  <button type="button" onClick={() => remote.kick(g.id)}>KICK</button>
                </div>
              ))}
            </div>
          ) : null}
          <div className="ws-remote-actions">
            <button type="button" onClick={() => remote.stop()}>STOP</button>
          </div>
        </div>
      )}
    </div>
  );
}
