import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { backend } from "./lib/bridge";
import type {
  AppState,
  AutomationLaneDraft,
  MidiNote,
  PitchState,
  PluginInfo,
  TimelineClip,
  TrackInfo,
  TransportState,
} from "./types";
import { ConsoleRoot, ErrorBoundary } from "./console-ui";
import { Workspace, type WorkspaceActions } from "./workspace/Workspace";

const EMPTY_PITCH: PitchState = { transpose: 0, pitchCents: 0, tuningHz: 440 };
const MAX_TRACKS = 8;

const MOCK_TRACKS = ["DRUMS", "BASS", "KEYS", "VOX", "GTR", "PERC", "AUX 1", "AUX 2"].map(
  (name, index) => ({
    index,
    name,
    instrumentName: index % 3 === 2 ? "" : name.toLowerCase(),
    gainDb: 0,
    pan: 0,
    muted: false,
    soloed: false,
    transpose: 0,
    pitchCents: 0,
    tuningHz: 440,
    instrumentPitch: EMPTY_PITCH,
    sendA: index < 6 ? 0.18 : 0,
    sendB: index < 6 ? 0.08 : 0,
    armed: false,
    inserts: [
      { name: index === 3 ? "VoxStudio" : "Overlay Retro Color", missing: false },
      { name: index === 2 ? "Overlay Delay" : "", missing: false },
      { name: "", missing: false },
      { name: "", missing: false },
    ],
  }),
);

/** Standalone-preview only: a plausible envelope so the mock timeline shows
    waveform shapes. Inside the host the real peaks come from the engine. */
function mockPeaks(seed: number): number[] {
  return Array.from({ length: 96 }, (_, i) => {
    const envelope = Math.sin((i / 95) * Math.PI);
    const body = Math.abs(Math.sin(i * 0.55 + seed) * 0.6 + Math.sin(i * 1.31 + seed * 2) * 0.35);
    return Math.max(0.04, Math.min(1, envelope * (0.32 + body * 0.72)));
  });
}

const MOCK_CLIPS: TimelineClip[] = [
  { track: 0, startBar: 1, lengthBars: 4, assetId: 0, gainDb: 0, name: "INTRO GROOVE" },
  { track: 0, startBar: 5, lengthBars: 4, assetId: 0, gainDb: 0, name: "MAIN GROOVE" },
  { track: 1, startBar: 1, lengthBars: 8, assetId: 1, gainDb: 0, name: "SUB MOTION" },
  { track: 2, startBar: 1, lengthBars: 4, assetId: 2, gainDb: 0, name: "GLASS BED" },
  { track: 2, startBar: 9, lengthBars: 2, assetId: 2, gainDb: 0, name: "STAB" },
  { track: 3, startBar: 5, lengthBars: 4, assetId: 3, gainDb: 0, name: "VERSE 1" },
  { track: 4, startBar: 1, lengthBars: 4, assetId: 4, gainDb: 0, name: "CLEAN RIFF" },
  { track: 5, startBar: 1, lengthBars: 16, assetId: 5, gainDb: 0, name: "SHAKER" },
].map((clip, index) => ({
  ...clip,
  peaks: mockPeaks(index + 1),
  fadeInBars: 0.25,
  fadeOutBars: 0.25,
}));

const MOCK_STATE: AppState = {
  bpm: 120,
  selectedTrack: 0,
  globalPitch: EMPTY_PITCH,
  tracks: MOCK_TRACKS,
  clips: MOCK_CLIPS,
};

const MOCK_TRANSPORT: TransportState = {
  playing: false,
  positionSeconds: 0,
  positionBar: 1,
  bpm: 120,
  loopEnabled: true,
  metronome: false,
  masterPeakLeft: 0,
  masterPeakRight: 0,
  masterDim: false,
  masterMono: false,
  trackPeaks: MOCK_TRACKS.map(() => 0),
};

export default function App() {
  const [state, setState] = useState<AppState>(MOCK_STATE);
  const [transport, setTransport] = useState<TransportState>(MOCK_TRANSPORT);

  useEffect(() => {
    if (!backend.embedded) return;

    let alive = true;
    backend
      .getState()
      .then((next) => {
        if (!alive) return;
        setState(next);
        if (next.transport) setTransport(next.transport);
      })
      .catch(console.error);

    const offState = backend.onStateChanged((next) => {
      setState(next);
      if (next.transport) setTransport(next.transport);
    });
    const offTransport = backend.onTransportChanged((next) => setTransport(next));

    return () => {
      alive = false;
      offState();
      offTransport();
    };
  }, []);

  // Standalone preview: advance the playhead so `npm run dev` looks like a
  // running session. Inside the host the engine owns this state.
  useEffect(() => {
    if (backend.embedded) return;
    const timer = setInterval(() => {
      setTransport((current) => {
        if (!current.playing) return current;
        const barsPerSecond = current.bpm / 240;
        return { ...current, positionBar: current.positionBar + barsPerSecond / 30 };
      });
    }, 1000 / 30);
    return () => clearInterval(timer);
  }, []);

  // Host writes. Every one answers with the full snapshot, so the state push
  // is the only source of truth once the call resolves.
  const saveChannelTimers = useRef(new Map<string, ReturnType<typeof setTimeout>>());

  // Note and automation gestures already commit once per pointer-up. Channel
  // knobs stream per pointermove, so only those need coalescing.
  const scheduleSave = (
    timers: Map<string, ReturnType<typeof setTimeout>>,
    key: string,
    run: () => void,
  ) => {
    const prior = timers.get(key);
    if (prior) clearTimeout(prior);
    timers.set(
      key,
      setTimeout(() => {
        timers.delete(key);
        run();
      }, 300),
    );
  };

  useEffect(
    () => () => {
      for (const timer of saveChannelTimers.current.values()) clearTimeout(timer);
    },
    [],
  );

  const handleNotes = useCallback((clip: number, notes: MidiNote[]) => {
    if (!backend.embedded) return;
    backend.setClipNotes(clip, notes).then(setState).catch(console.error);
  }, []);

  const handleAutomation = useCallback((track: number, lane: AutomationLaneDraft) => {
    if (!backend.embedded) return;
    backend.setAutomationLane(track, lane).then(setState).catch(console.error);
  }, []);

  const handlePluginLoad = useCallback((plugin: PluginInfo, track: number, slot: number) => {
    if (!backend.embedded) return;
    backend.loadPlugin(track, slot, plugin.id).then(setState).catch(console.error);
  }, []);

  const handleMasterPluginLoad = useCallback((plugin: PluginInfo) => {
    if (!backend.embedded) return;
    backend.loadMasterPlugin(plugin.id).then(setState).catch(console.error);
  }, []);

  const handleScan = useCallback(() => {
    if (!backend.embedded) return;
    backend.scanPlugins().then(setState).catch(console.error);
  }, []);

  const handleChannel = useCallback((track: number, param: string, value: number) => {
    if (!backend.embedded) return;
    scheduleSave(saveChannelTimers.current, `${track}:${param}`, () => {
      backend.setChannelParam(track, param, value).then(setState).catch(console.error);
    });
  }, []);

  const actions = useMemo<WorkspaceActions>(() => {
    const send = (task: () => Promise<AppState>) => {
      if (!backend.embedded) return;
      task().catch(console.error);
    };

    const patchTrack = (track: number, patch: Partial<AppState["tracks"][number]>) =>
      setState((current) => ({
        ...current,
        tracks: current.tracks.map((entry) =>
          entry.index === track ? { ...entry, ...patch } : entry,
        ),
      }));

    return {
      play(playing) {
        setTransport((current) => ({ ...current, playing }));
        send(() => backend.setPlaying(playing));
      },
      stop() {
        setTransport((current) => ({
          ...current,
          playing: false,
          positionBar: 1,
          positionSeconds: 0,
        }));
        send(() => backend.setPlaying(false));
        send(() => backend.setPositionBar(1));
      },
      rewind() {
        setTransport((current) => ({ ...current, positionBar: 1, positionSeconds: 0 }));
        send(() => backend.setPositionBar(1));
      },
      bpm(bpm) {
        setTransport((current) => ({ ...current, bpm }));
        setState((current) => ({ ...current, bpm }));
        send(() => backend.setBpm(bpm));
      },
      loop(on) {
        setTransport((current) => ({ ...current, loopEnabled: on }));
        send(() => backend.setLoopEnabled(on));
      },
      metronome(on) {
        setTransport((current) => ({ ...current, metronome: on }));
        send(() => backend.setMetronome(on));
      },
      seek(bar) {
        setTransport((current) => ({
          ...current,
          positionBar: bar,
          positionSeconds: ((bar - 1) * 240) / Math.max(1, current.bpm),
        }));
        send(() => backend.setPositionBar(bar));
      },
      gain(track, db) {
        patchTrack(track, { gainDb: db });
        send(() => backend.setTrackGain(track, db));
      },
      pan(track, pan) {
        patchTrack(track, { pan });
        send(() => backend.setTrackPan(track, pan));
      },
      mute(track, muted) {
        patchTrack(track, { muted });
        send(() => backend.setTrackMuted(track, muted));
      },
      solo(track, soloed) {
        patchTrack(track, { soloed });
        send(() => backend.setTrackSolo(track, soloed));
      },
      selectTrack(track) {
        setState((current) => ({ ...current, selectedTrack: track }));
        send(() => backend.setSelectedTrack(track));
      },
      addTrack() {
        if (state.tracks.length >= MAX_TRACKS) return;
        const index = state.tracks.length;
        const track: TrackInfo = {
          index,
          name: `TRACK ${index + 1}`,
          instrumentName: "",
          gainDb: 0,
          pan: 0,
          muted: false,
          soloed: false,
          transpose: 0,
          pitchCents: 0,
          tuningHz: 440,
          instrumentPitch: EMPTY_PITCH,
          sendA: 0,
          sendB: 0,
          armed: false,
          inserts: [
            { name: "", missing: false },
            { name: "", missing: false },
            { name: "", missing: false },
            { name: "", missing: false },
          ],
        };
        setState((current) => ({
          ...current,
          selectedTrack: index,
          tracks: [...current.tracks, track],
        }));
        send(() => backend.setActiveTrackCount(index + 1));
      },
      removeTrack() {
        if (state.tracks.length <= 1) return;
        setState((current) => {
          const tracks = current.tracks.slice(0, -1);
          return {
            ...current,
            tracks,
            selectedTrack: tracks.some((track) => track.index === current.selectedTrack)
              ? current.selectedTrack
              : (tracks[tracks.length - 1]?.index ?? 0),
          };
        });
        send(() => backend.setActiveTrackCount(Math.max(1, state.tracks.length - 1)));
      },
      masterGain(db) {
        send(() => backend.setMasterGain(db));
      },
      sendA(track, amount) {
        patchTrack(track, { sendA: amount });
        send(() => backend.setTrackSendA(track, amount));
      },
      sendB(track, amount) {
        patchTrack(track, { sendB: amount });
        send(() => backend.setTrackSendB(track, amount));
      },
      arm(track, armed) {
        patchTrack(track, { armed });
        send(() => backend.setRecordArmed(track, armed));
      },
      masterDim(on) {
        setTransport((current) => ({ ...current, masterDim: on }));
        send(() => backend.setMasterDim(on));
      },
      masterMono(on) {
        setTransport((current) => ({ ...current, masterMono: on }));
        send(() => backend.setMasterMono(on));
      },
      globalPitch(patch: Partial<PitchState>) {
        setState((current) => ({
          ...current,
          globalPitch: { ...(current.globalPitch ?? EMPTY_PITCH), ...patch },
        }));
        send(() => backend.setGlobalPitch(patch));
      },
      trackPitch(track, patch) {
        patchTrack(track, patch);
        send(() => backend.setTrackPitch(track, patch));
      },
      editClip(index, patch) {
        if (!backend.embedded) return;
        backend.editTimelineClip(index, patch).then(setState).catch(console.error);
      },
      splitClip(index, bar) {
        if (!backend.embedded) return;
        backend.splitTimelineClip(index, bar).then(setState).catch(console.error);
      },
      duplicateClip(index) {
        if (!backend.embedded) return;
        backend.duplicateTimelineClip(index).then(setState).catch(console.error);
      },
      deleteClip(index) {
        if (!backend.embedded) return;
        backend.deleteTimelineClip(index).then(setState).catch(console.error);
      },
      undo() {
        if (!backend.embedded) return;
        backend.undo().then(setState).catch(console.error);
      },
      redo() {
        if (!backend.embedded) return;
        backend.redo().then(setState).catch(console.error);
      },
      loopRange(startBar, endBar) {
        setTransport((current) => ({ ...current, loopStartBar: startBar, loopEndBar: endBar }));
        if (!backend.embedded) return;
        backend.setLoopBars(startBar, endBar).then(setState).catch(console.error);
      },
      instrumentPitch(track, patch) {
        setState((current) => ({
          ...current,
          tracks: current.tracks.map((entry) =>
            entry.index === track
              ? {
                  ...entry,
                  instrumentPitch: { ...(entry.instrumentPitch ?? EMPTY_PITCH), ...patch },
                }
              : entry,
          ),
        }));
        send(() => backend.setInstrumentPitch(track, patch));
      },
    };
  }, [state.tracks.length]);

  return (
    <ConsoleRoot>
      <ErrorBoundary label="NCSound">
        <Workspace
          state={state}
          transport={transport}
          actions={actions}
          onNotesChanged={handleNotes}
          onAutomationChanged={handleAutomation}
          onChannelChange={handleChannel}
          onPluginLoad={handlePluginLoad}
          onMasterPluginLoad={handleMasterPluginLoad}
          onScan={handleScan}
        />
      </ErrorBoundary>
    </ConsoleRoot>
  );
}
