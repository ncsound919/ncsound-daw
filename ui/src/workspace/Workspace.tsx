import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type {
  AppState,
  AutomationLaneDraft,
  AutomationParam,
  MidiNote,
  PitchState,
  PluginInfo,
  TimelineClip,
  TransportState,
} from "../types";
import { Arrangement, type DawMode } from "./Arrangement";
import { Mixer } from "./Mixer";
import { SettingsPanel, accentHot, loadSettings, type UiSettings } from "./Settings";
import { Transport } from "./Transport";
import { HeaderBar } from "./HeaderBar";
import { StatusBar } from "./StatusBar";
import { Inspector } from "./Inspector";
import { PianoRoll } from "./PianoRoll";
import { Browser } from "./Browser";
import { Automation } from "./Automation";
import { ChannelRackPanel, MasterRackPanel } from "./RackPanel";

import { INSERT_SLOTS, WORKSPACE_VIEWS, type WorkspaceView } from "./format";
import "./workspace.css";
import "./chrome.css";
import "./refine.css";
import "../brand.css";
/* Protocol skin: loaded last so it is the final word on every surface. */
import "../protocol/tokens.css";
import "../protocol/protocol.css";

export interface WorkspaceActions {
  play: (playing: boolean) => void;
  stop: () => void;
  rewind: () => void;
  bpm: (bpm: number) => void;
  loop: (on: boolean) => void;
  metronome: (on: boolean) => void;
  seek: (bar: number) => void;
  gain: (track: number, db: number) => void;
  pan: (track: number, pan: number) => void;
  mute: (track: number, muted: boolean) => void;
  solo: (track: number, soloed: boolean) => void;
  selectTrack: (track: number) => void;
  addTrack: () => void;
  removeTrack: () => void;
  masterGain: (db: number) => void;
  sendA: (track: number, amount: number) => void;
  sendB: (track: number, amount: number) => void;
  arm: (track: number, armed: boolean) => void;
  masterDim: (on: boolean) => void;
  masterMono: (on: boolean) => void;
  globalPitch: (patch: Partial<PitchState>) => void;
  trackPitch: (track: number, patch: Partial<PitchState>) => void;
  instrumentPitch: (track: number, patch: Partial<PitchState>) => void;
  /** Mixology presets. Optional until the host exposes its preset routes. */
  applyMixology?: (index: number) => Promise<string>;
  /** Timeline write path. Optional until the host exposes its clip routes. */
  editClip?: (index: number, patch: Record<string, unknown>) => void;
  splitClip?: (index: number, bar: number) => void;
  duplicateClip?: (index: number) => void;
  deleteClip?: (index: number) => void;
  /** Voice-to-MIDI: transcribes the audio clip into a new MIDI clip. */
  transcribeClip?: (index: number) => void;
  loopRange?: (startBar: number, endBar: number) => void;
  undo?: () => void;
  redo?: () => void;
  addMarker?: () => void;
  nextMarker?: () => void;
  previousMarker?: () => void;
  deleteMarker?: () => void;
  reverseClip?: () => void;
  normalizeClip?: () => void;
}

export interface WorkspaceProps {
  state: AppState;
  transport: TransportState;
  actions: WorkspaceActions;
  initialView?: WorkspaceView;
  /** Optional persistence hooks. The host wires these when its routes exist. */
  onNotesChanged?: (clip: number, notes: MidiNote[]) => void;
  onAutomationChanged?: (track: number, draft: AutomationLaneDraft) => void;
  onChannelChange?: (track: number, param: string, value: number) => void;
  onMasterChange?: (param: string, value: number) => void;
  /** Arms MIDI learn on a host parameter id ("track.N.param"). */
  onMidiLearn?: (target: string) => void;
  onPluginLoad?: (plugin: PluginInfo, track: number, slot: number) => void;
  /** Alt-click on an insert slot bypasses it without unloading the plug-in. */
  onInsertBypassed?: (track: number, slot: number, bypassed: boolean) => void;
  /** Drag a loaded insert onto an empty slot to reorder the chain. */
  onInsertMove?: (track: number, from: number, to: number) => void;
  /** Double-click an insert slot to open the hosted plug-in editor. */
  onOpenPluginEditor?: (track: number, slot: number) => Promise<{ ok: boolean; error?: string }>;
  /** Master A/B hooks; the menu entries hide without them. */
  onCaptureMasterAb?: () => void;
  onRecallMasterAb?: () => void;
  /** Per-track AUTO switch; the strip button hides without it. */
  onAutomationRead?: (track: number, enabled: boolean) => void;
  onMasterPluginLoad?: (plugin: PluginInfo) => void;
  onScan?: () => void;
  /** Mixology preset names from the host; the entries hide without them. */
  mixologyPresets?: string[];
  /** Import Audio hook; the menu item stays disabled without it. */
  onImportAudio?: () => void;
  /** Session and export hooks; each menu entry stays disabled without its route. */
  onOpenSession?: () => void;
  onSaveSession?: () => void;
  onSaveSessionAs?: () => void;
  onMixdownWav?: () => void;
  onExportStems?: () => void;
  onCollectMedia?: () => void;
  /** Master-insert hook; the remove entry stays disabled without it. */
  onRemoveMasterPlugin?: () => void;
  /** Starts/stops capture on the armed tracks. */
  onRecord?: (recording: boolean) => void;
}

const EMPTY_PITCH: PitchState = { transpose: 0, pitchCents: 0, tuningHz: 440 };
/** Mirrors nc::limits::maxTracks, the engine's hard ceiling. */
const MAX_TRACKS = 64;

/** Shortcut line per surface: the status strip describes the view you are in. */
const VIEW_HINTS: Partial<Record<WorkspaceView, string>> = {
  arrange: "V SELECT / T TRIM / F FADE / G GAIN / X SPLIT / E ERASE",
  mix: "CLICK A STRIP / DRAG A FADER / DOUBLE-CLICK FOR UNITY",
  inspector: "SELECT AN INSERT / DRAG A KNOB / EIGHT SLOTS IN TWO BANKS",
  piano: "DRAW / PAINT / ERASE / RIGHT-CLICK REMOVES / CTRL + WHEEL ZOOMS",
  automation: "CLICK TO ADD A POINT / ALT-CLICK REMOVES",
};

export function Workspace({
  state,
  transport,
  actions,
  initialView = "arrange",
  onNotesChanged,
  onAutomationChanged,
  onChannelChange,
  onMasterChange,
  onMidiLearn,
  onPluginLoad,
  onInsertBypassed,
  onInsertMove,
  onOpenPluginEditor,
  onCaptureMasterAb,
  onRecallMasterAb,
  onAutomationRead,
  onMasterPluginLoad,
  onScan,
  mixologyPresets,
  onImportAudio,
  onRecord,
  onOpenSession,
  onSaveSession,
  onSaveSessionAs,
  onMixdownWav,
  onExportStems,
  onCollectMedia,
  onRemoveMasterPlugin,
}: WorkspaceProps) {
  const [view, setView] = useState<WorkspaceView>(() => {
    // Design-review affordance: ?view=mix opens a surface directly, so each
    // screen can be audited at a fixed size without clicking through the shell.
    let requested: string | null = null;
    try {
      requested = new URLSearchParams(window.location.search).get("view");
    } catch {
      requested = null;
    }
    const candidate = (requested ?? initialView) as WorkspaceView;
    return WORKSPACE_VIEWS.some((entry) => entry.id === candidate) ? candidate : "arrange";
  });
  const [rackDialog, setRackDialog] = useState<"channel" | "master" | null>(null);
  const [mode, setMode] = useState<DawMode>(() => {
    try {
      const saved = localStorage.getItem("nc.daw.mode");
      if (saved === "recording" || saved === "mixing") return saved;
    } catch {
      // No storage available; the session still starts in production.
    }
    return "production";
  });
  const [fitArrange, setFitArrange] = useState<(() => void) | null>(null);
  // Stable identity: an inline arrow here re-registered the arrangement's FIT
  // callback on every render, and registering stores state, so the two pinged
  // each other forever.
  const registerFit = useCallback((fit: () => void) => setFitArrange(() => fit), []);
  const [uiSettings, setUiSettings] = useState<UiSettings>(loadSettings);
  const [selectedClip, setSelectedClip] = useState<number | null>(null);
  const [masterGainDb, setMasterGainDb] = useState(loadSettings().masterGain);
  const [status, setStatus] = useState<{ text: string; alert: boolean } | null>(null);
  const [browserTarget, setBrowserTarget] = useState<
    { track: number; slot: number } | { master: true } | null
  >(null);
  // Deep instrument sync: tracks whose instrument should follow the track pitch.
  const [pitchSync, setPitchSync] = useState<Record<number, boolean>>({});

  // A mode is a working preset, not a label: it moves the app to the view that
  // task lives in and stamps the shell so density and panel rules can follow.
  const changeMode = (next: DawMode) => {
    setMode(next);
    setView(next === "mixing" ? "mix" : "arrange");
    flash(`${next.toUpperCase()} MODE`);
    try {
      localStorage.setItem("nc.daw.mode", next);
    } catch {
      // Nothing to persist to; the switch still applies for this session.
    }
  };

  const canAddTrack = state.tracks.length < MAX_TRACKS;
  const canRemoveTrack = state.tracks.length > 1;

  // Drafts live above the surfaces so an edit survives switching views. The
  // host still owns the truth: these are what a view has to hand back.
  const [noteDrafts, setNoteDrafts] = useState<Record<number, MidiNote[]>>({});
  const [clipDraft, setClipDraft] = useState<TimelineClip[] | null>(null);
  const [autoParams, setAutoParams] = useState<Record<number, AutomationParam>>({});
  const [autoDrafts, setAutoDrafts] = useState<Record<string, AutomationLaneDraft>>({});

  // A successful host round-trip supersedes the optimistic timeline copy.
  // Without this, the draft would mask every later state push forever.
  useEffect(() => {
    setClipDraft(null);
    setNoteDrafts({});
  }, [state.clips]);

  useEffect(() => {
    setAutoDrafts({});
  }, [state.automation]);

  // DAW settings drive live transport/master defaults, but only after the
  // user changes them — the session's saved tempo/loop-click always win on
  // load. Each effect skips its own first run so defaults never stomp the host.
  const bpmReady = useRef(false);
  const loopReady = useRef(false);
  const metroReady = useRef(false);
  const masterReady = useRef(false);
  useEffect(() => {
    if (!bpmReady.current) {
      bpmReady.current = true;
      return;
    }
    if (Math.abs(uiSettings.bpm - transport.bpm) > 0.05) actions.bpm(uiSettings.bpm);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [uiSettings.bpm]);
  useEffect(() => {
    if (!loopReady.current) {
      loopReady.current = true;
      return;
    }
    if (uiSettings.loop !== transport.loopEnabled) actions.loop(uiSettings.loop);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [uiSettings.loop]);
  useEffect(() => {
    if (!metroReady.current) {
      metroReady.current = true;
      return;
    }
    if (uiSettings.metronome !== transport.metronome) actions.metronome(uiSettings.metronome);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [uiSettings.metronome]);
  useEffect(() => {
    if (!masterReady.current) {
      masterReady.current = true;
      return;
    }
    setMasterGainDb(uiSettings.masterGain);
    actions.masterGain(uiSettings.masterGain);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [uiSettings.masterGain]);

  const clips = clipDraft ?? state.clips ?? [];

  // A delete or split can shrink the list under the selection; an index that
  // no longer addresses a clip must not be handed to a write route.
  useEffect(() => {
    if (selectedClip !== null && selectedClip >= clips.length) setSelectedClip(null);
  }, [clips.length, selectedClip]);
  const selectedTrack =
    state.tracks.find((track) => track.index === state.selectedTrack) ?? state.tracks[0];
  const trackIndex = selectedTrack?.index ?? 0;

  const clip = selectedClip !== null ? clips[selectedClip] : undefined;
  const clipNotes = selectedClip !== null ? (noteDrafts[selectedClip] ?? clip?.notes ?? []) : [];

  const autoParam = autoParams[trackIndex] ?? "volume";
  const autoKey = `${trackIndex}:${autoParam}`;
  const hostLane = state.automation?.find(
    (lane) => lane.track === trackIndex && lane.param === autoParam,
  );
  const autoDraft: AutomationLaneDraft = autoDrafts[autoKey] ??
    hostLane ?? { param: autoParam, curve: "linear", points: [] };

  const inserts = selectedTrack?.inserts ?? [];
  const firstEmptyInsert = INSERT_SLOTS.find((slot) => !inserts[slot]?.name) ?? INSERT_SLOTS[0];

  const flash = useCallback((text: string, alert = false) => setStatus({ text, alert }), []);
  const trackPitch = useMemo<PitchState>(
    () => ({
      transpose: selectedTrack?.transpose ?? 0,
      pitchCents: selectedTrack?.pitchCents ?? 0,
      tuningHz: selectedTrack?.tuningHz ?? 440,
    }),
    [selectedTrack?.transpose, selectedTrack?.pitchCents, selectedTrack?.tuningHz],
  );

  // With sync on, every track-pitch change is mirrored onto the instrument's
  // own pitch stack, so a transposed track keeps its plug-in in tune.
  const instrumentSynced = Boolean(pitchSync[trackIndex]);
  useEffect(() => {
    if (!instrumentSynced) return;
    actions.instrumentPitch(trackIndex, {
      transpose: trackPitch.transpose,
      pitchCents: trackPitch.pitchCents,
      tuningHz: trackPitch.tuningHz,
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [
    instrumentSynced,
    trackIndex,
    trackPitch.transpose,
    trackPitch.pitchCents,
    trackPitch.tuningHz,
  ]);
  const onChannelParam = useCallback(
    (id: string, value: number) => onChannelChange?.(trackIndex, id, value),
    [onChannelChange, trackIndex],
  );

  const selectView = (next: WorkspaceView) => {
    setView(next);

    // Mode and view stay in step both ways: the console is the mixing mode, the
    // arrangement is production, so the chips read true whichever way you move.
    if (next === "mix") setMode("mixing");
    else if (next === "arrange") setMode("production");

    flash(`${WORKSPACE_VIEWS.find((entry) => entry.id === next)?.label ?? next} VIEW`);
  };

  const openBrowser = (track: number, slot: number) => {
    // The rack owns eight user slots; clamp to the last valid index rather
    // than handing the bridge a slot it would reject after a pick.
    const safeSlot = Number.isFinite(slot)
      ? Math.max(0, Math.min(INSERT_SLOTS[INSERT_SLOTS.length - 1], Math.trunc(slot)))
      : 0;
    setBrowserTarget({ track, slot: safeSlot });
    flash(`BROWSER / TRACK ${track + 1} / INS ${safeSlot + 1}`);
  };

  const openMasterBrowser = () => {
    // Browser is a dialog, not a tab. Staying on the current view keeps the
    // surface alive behind it (previously this blanked the workspace).
    setBrowserTarget({ master: true });
    flash("BROWSER / MASTER INSERT");
  };

  const closeBrowser = () => setBrowserTarget(null);

  // Focus trap + return for the browser dialog.
  const browserOpener = useRef<HTMLElement | null>(null);
  useEffect(() => {
    if (!browserTarget) return;
    browserOpener.current = document.activeElement as HTMLElement | null;
    const modal = document.querySelector(".ws-browser-modal-panel") as HTMLElement | null;
    const focusables = () =>
      (modal?.querySelectorAll<HTMLElement>(
        'button:not(:disabled), [href], input:not(:disabled), select:not(:disabled), [tabindex]:not([tabindex="-1"])',
      ) ?? []) as NodeListOf<HTMLElement>;
    focusables()[0]?.focus();
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        closeBrowser();
        return;
      }
      if (event.key !== "Tab") return;
      const items = Array.from(focusables());
      if (items.length === 0) return;
      const first = items[0];
      const last = items[items.length - 1];
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => {
      window.removeEventListener("keydown", onKey);
      browserOpener.current?.focus();
    };
  }, [browserTarget]);

  const isMasterTarget = browserTarget !== null && "master" in browserTarget;
  const browserTrack =
    browserTarget !== null && "track" in browserTarget
      ? state.tracks.find((entry) => entry.index === browserTarget.track)
      : selectedTrack;
  const browserSlot =
    browserTarget !== null && "slot" in browserTarget ? browserTarget.slot : firstEmptyInsert;
  // A load button with no destination or no host handler is a dead control.
  const canLoad = isMasterTarget
    ? Boolean(onMasterPluginLoad)
    : Boolean(browserTrack && onPluginLoad);

  // Space and Home stay on the workspace, not on a control, so the transport is
  // reachable from anywhere on the surface without stealing text entry.
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      // Never steal Space/Home from a focused control. Otherwise Space on a
      // Mute button would start the transport instead of pressing the button.
      if (
        !target ||
        target.isContentEditable ||
        target.closest(
          "button, a, input, textarea, select, [role='button'], [role='slider'], [role='tab'], [role='spinbutton'], [role='option'], [role='menuitem']",
        )
      )
        return;
      if (event.repeat) return;
      if (event.code === "Space") {
        event.preventDefault();
        actions.play(!transport.playing);
      } else if (event.code === "Home") {
        event.preventDefault();
        actions.rewind();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [actions, transport.playing]);

  return (
    <div
      className="ws"
      style={
        {
          "--ws-accent": uiSettings.accent,
          "--ws-accent-hot": accentHot(uiSettings.accent),
          "--ws-tint-amt": `${uiSettings.tintAmount}%`,
          "--ws-clip-bold": `${uiSettings.clipBoldness}%`,
          "--ws-loop-strength": `${uiSettings.loopStrength}%`,
        } as React.CSSProperties
      }
      data-density={uiSettings.density}
      data-mode={mode}
    >
      <HeaderBar
        view={view}
        onView={selectView}
        onFit={view === "arrange" && fitArrange != null ? fitArrange : undefined}
        canAddTrack={canAddTrack}
        canRemoveTrack={canRemoveTrack}
        hasSelection={selectedClip !== null}
        onSplitSelected={
          actions.splitClip && selectedClip !== null
            ? () => {
                actions.splitClip?.(selectedClip, transport.positionBar);
                flash("CLIP SPLIT AT PLAYHEAD");
              }
            : undefined
        }
        onDuplicateSelected={
          actions.duplicateClip && selectedClip !== null
            ? () => {
                actions.duplicateClip?.(selectedClip);
                flash("CLIP DUPLICATED");
              }
            : undefined
        }
        onDeleteSelected={
          actions.deleteClip && selectedClip !== null
            ? () => {
                actions.deleteClip?.(selectedClip);
                setSelectedClip(null);
                flash("CLIP DELETED");
              }
            : undefined
        }
        onScanPlugins={onScan}
        onImportAudio={onImportAudio}
        onOpenSession={onOpenSession}
        onSaveSession={onSaveSession}
        onSaveSessionAs={onSaveSessionAs}
        onMixdownWav={onMixdownWav}
        onExportStems={onExportStems}
        onCollectMedia={onCollectMedia}
        onUndo={actions.undo}
        onRedo={actions.redo}
        onAddMarker={actions.addMarker}
        onNextMarker={actions.nextMarker}
        onPreviousMarker={actions.previousMarker}
        onDeleteMarker={actions.deleteMarker}
        onReverseClip={actions.reverseClip}
        onNormalizeClip={actions.normalizeClip}
        undoDepth={state.undoDepth ?? 0}
        redoDepth={state.redoDepth ?? 0}
        onTranscribeSelected={
          clip?.kind !== "midi" && actions.transcribeClip && selectedClip !== null
            ? () => actions.transcribeClip?.(selectedClip)
            : undefined
        }
        onSelectPrevious={
          state.selectedTrack > 0 ? () => actions.selectTrack(state.selectedTrack - 1) : undefined
        }
        onSelectNext={
          state.selectedTrack < state.tracks.length - 1
            ? () => actions.selectTrack(state.selectedTrack + 1)
            : undefined
        }
        onRemoveMasterPlugin={onRemoveMasterPlugin}
        canRemoveMasterPlugin={Boolean(state.hasMasterPlugin)}
        onLoadMasterPlugin={openMasterBrowser}
        onCaptureMasterAb={onCaptureMasterAb}
        onRecallMasterAb={onRecallMasterAb}
        canCaptureMasterAb={Boolean(state.hasMasterPlugin)}
        mixologyPresets={mixologyPresets}
        onApplyMixology={
          actions.applyMixology
            ? (index) => {
                actions
                  .applyMixology?.(index)
                  .then((report) => flash(report))
                  .catch(console.error);
              }
            : undefined
        }
        onNewSession={() => {
          actions.stop();
          actions.rewind();
          flash("TRANSPORT RESET / BAR 1", true);
        }}
        onAddTrack={() => {
          if (!canAddTrack) return;
          actions.addTrack();
          flash("TRACK ADDED");
        }}
        onRemoveTrack={() => {
          if (!canRemoveTrack) return;
          actions.removeTrack();
          flash("TRACK REMOVED");
        }}
        onMuteSelected={() => {
          if (!selectedTrack) return;
          actions.mute(selectedTrack.index, !selectedTrack.muted);
          flash(`${selectedTrack.name} ${selectedTrack.muted ? "UNMUTED" : "MUTED"}`);
        }}
        onSoloSelected={() => {
          if (!selectedTrack) return;
          actions.solo(selectedTrack.index, !selectedTrack.soloed);
          flash(`${selectedTrack.name} ${selectedTrack.soloed ? "UNSOLOED" : "SOLOED"}`);
        }}
      />

      <Transport
        transport={transport}
        onPlay={actions.play}
        onStop={actions.stop}
        onRewind={actions.rewind}
        onRecord={onRecord}
        recording={transport.recording ?? false}
        onBpm={actions.bpm}
        onLoop={actions.loop}
        onMetronome={actions.metronome}
        globalPitch={state.globalPitch ?? EMPTY_PITCH}
        onGlobalPitch={actions.globalPitch}
      />

      <div
        className="ws-surface"
        id="ws-surface-panel"
        role="tabpanel"
        aria-labelledby={`ws-tab-${view}`}
      >
        {view === "arrange" && (
          <Arrangement
            tracks={state.tracks}
            clips={clips}
            selectedTrack={state.selectedTrack}
            selectedClip={selectedClip}
            transport={transport}
            inspectorTrack={selectedTrack}
            onSelectTrack={actions.selectTrack}
            onSelectClip={setSelectedClip}
            onSeek={actions.seek}
            onMute={actions.mute}
            onSolo={actions.solo}
            onArm={actions.arm}
            onGain={actions.gain}
            onPan={actions.pan}
            onSendA={actions.sendA}
            onSendB={actions.sendB}
            onInsert={openBrowser}
            onRack={() => setView("inspector")}
            onClipsChanged={setClipDraft}
            onEditClip={actions.editClip}
            onSplitClip={actions.splitClip}
            onDuplicateClip={actions.duplicateClip}
            onDeleteClip={actions.deleteClip}
            onLoopRange={actions.loopRange}
            undoDepth={state.undoDepth}
            redoDepth={state.redoDepth}
            onUndo={actions.undo}
            onRedo={actions.redo}
            channel={state.channel}
            onChannelParam={onChannelParam}
            pitch={trackPitch}
            onPitch={(patch) => selectedTrack && actions.trackPitch(selectedTrack.index, patch)}
            instrumentPitch={selectedTrack?.instrumentPitch ?? EMPTY_PITCH}
            onInstrumentPitch={(patch) =>
              selectedTrack && actions.instrumentPitch(selectedTrack.index, patch)
            }
            onOpenClip={(index) => {
              setSelectedClip(index);
              setView("piano");
            }}
            onRegisterFit={registerFit}
            defaultTrackHeight={uiSettings.trackHeight}
            defaultFadeBars={uiSettings.fadeBars}
            defaultClipGain={uiSettings.clipGain}
            defaultSnap={uiSettings.snap}
            mode={mode}
            onMode={changeMode}
          />
        )}

        {view === "mix" && (
          <Mixer
            tracks={state.tracks}
            selectedTrack={state.selectedTrack}
            transport={transport}
            masterGainDb={masterGainDb}
            onSelectTrack={actions.selectTrack}
            onGain={actions.gain}
            onPan={actions.pan}
            onMute={actions.mute}
            onSolo={actions.solo}
            onSendA={actions.sendA}
            onSendB={actions.sendB}
            onArm={actions.arm}
            onMasterDim={actions.masterDim}
            onMasterMono={actions.masterMono}
            onRack={() => setView("inspector")}
            onOpenStrip={() => setRackDialog("channel")}
            onInsert={openBrowser}
            onBypass={onInsertBypassed}
            onAutomationRead={onAutomationRead}
            onMove={onInsertMove}
            onOpenEditor={onOpenPluginEditor}
            onMasterRack={() => setView("inspector")}
            onOpenMasterPanel={() => setRackDialog("master")}
            onMasterInsert={openMasterBrowser}
            mixologyPresets={mixologyPresets}
            onApplyMixology={actions.applyMixology}
            onMasterGain={(db) => {
              setMasterGainDb(db);
              actions.masterGain(db);
            }}
          />
        )}

        {view === "inspector" && (
          <Inspector
            track={selectedTrack}
            transport={transport}
            onGain={actions.gain}
            onPan={actions.pan}
            onMute={actions.mute}
            onSolo={actions.solo}
            onArm={actions.arm}
            onSendA={actions.sendA}
            onSendB={actions.sendB}
            onInsert={(slot) => openBrowser(trackIndex, slot)}
            pitchSync={instrumentSynced}
            onPitchSync={(synced) =>
              setPitchSync((current) => ({ ...current, [trackIndex]: synced }))
            }
            channel={state.channel}
            onChannelParam={onChannelParam}
            onMidiLearn={onMidiLearn ? (id) => onMidiLearn(`track.${trackIndex}.${id}`) : undefined}
            pitch={trackPitch}
            onPitch={(patch) => selectedTrack && actions.trackPitch(selectedTrack.index, patch)}
            instrumentPitch={selectedTrack?.instrumentPitch ?? EMPTY_PITCH}
            onInstrumentPitch={(patch) =>
              selectedTrack && actions.instrumentPitch(selectedTrack.index, patch)
            }
          />
        )}

        {view === "piano" &&
          (clip ? (
            <PianoRoll
              key={`${selectedClip}:${clip.track}:${clip.startBar}:${clip.lengthBars}`}
              clipName={clip.name || `TRACK ${trackIndex + 1}`}
              lengthBeats={Math.max(1, clip.lengthBars * 4)}
              initialNotes={clipNotes}
              persisted={Boolean(onNotesChanged)}
              onTranscribe={
                clip.kind === "audio" && actions.transcribeClip && selectedClip !== null
                  ? () => actions.transcribeClip?.(selectedClip)
                  : undefined
              }
              onEdited={(notes) => {
                if (selectedClip === null) return;
                setNoteDrafts((current) => ({ ...current, [selectedClip]: notes }));
                onNotesChanged?.(selectedClip, notes);
              }}
            />
          ) : (
            <PianoRoll
              key={`empty-${trackIndex}`}
              clipName={`TRACK ${trackIndex + 1}`}
              lengthBeats={16}
              initialNotes={[]}
              persisted={false}
              onEdited={() => {}}
            />
          ))}

        {view === "settings" && <SettingsPanel settings={uiSettings} onChange={setUiSettings} />}

        {view === "automation" && (
          <Automation
            key={`${trackIndex}:${autoParam}`}
            track={trackIndex}
            draft={autoDraft}
            lengthBeats={Math.max(4, clip?.lengthBars ? clip.lengthBars * 4 : 16)}
            persisted={Boolean(onAutomationChanged)}
            onParam={(param) => setAutoParams((current) => ({ ...current, [trackIndex]: param }))}
            onDraft={(draft) => {
              const key = `${trackIndex}:${draft.param}`;
              setAutoDrafts((current) => ({ ...current, [key]: draft }));
              onAutomationChanged?.(trackIndex, draft);
            }}
          />
        )}
      </div>

      {browserTarget && (
        <div className="ws-browser-modal" role="dialog" aria-modal="true" aria-label="Load plug-in">
          <div className="ws-browser-modal-backdrop" onClick={closeBrowser} aria-hidden="true" />
          <div className="ws-browser-modal-panel">
            <header className="ws-browser-modal-head">
              <span>LOAD PLUG-IN</span>
              <button
                className="ws-btn is-icon"
                aria-label="Close plug-in browser"
                onClick={closeBrowser}
                autoFocus
              >
                ✕
              </button>
            </header>
            <Browser
              plugins={state.plugins ?? []}
              targetName={
                isMasterTarget
                  ? "MASTER"
                  : browserTrack
                    ? `TRACK ${browserTrack.index + 1}`
                    : "TRACK"
              }
              targetSlot={isMasterTarget ? undefined : browserSlot}
              scanning={state.pluginScan?.scanning}
              progress={state.pluginScan?.progress}
              status={state.pluginScan?.status}
              onScan={onScan}
              onLoad={
                canLoad
                  ? (plugin) => {
                      if (isMasterTarget) {
                        if (plugin.category === "instrument") {
                          flash("INSTRUMENTS LOAD ON TRACKS, NOT ON THE MASTER");
                          return;
                        }
                        onMasterPluginLoad?.(plugin);
                        closeBrowser();
                        return;
                      }
                      if (!browserTrack) return;
                      onPluginLoad?.(plugin, browserTrack.index, browserSlot);
                      closeBrowser();
                    }
                  : undefined
              }
            />
          </div>
        </div>
      )}

      {rackDialog === "channel" && (
        <ChannelRackPanel
          channel={state.channel}
          onParam={(id, value) => onChannelChange?.(trackIndex, id, value)}
          onClose={() => setRackDialog(null)}
        />
      )}

      {rackDialog === "master" && (
        <MasterRackPanel
          master={state.master}
          onParam={(id, value) => onMasterChange?.(id, value)}
          onClose={() => setRackDialog(null)}
        />
      )}

      <StatusBar
        hints={VIEW_HINTS[view] ?? ""}
        message={status?.text ?? state.statusMessage}
        alert={status?.alert ?? state.statusAlert ?? false}
        activeTracks={state.tracks.length}
        trackCapacity={MAX_TRACKS}
        clipCount={clips.length}
        pluginCount={state.plugins?.length ?? 0}
      />
    </div>
  );
}
