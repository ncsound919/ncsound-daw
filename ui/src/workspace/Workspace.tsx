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
import { Arrangement } from "./Arrangement";
import { Mixer } from "./Mixer";
import { SettingsPanel, accentHot, loadSettings, type UiSettings } from "./Settings";
import { Transport } from "./Transport";
import { HeaderBar } from "./HeaderBar";
import { StatusBar } from "./StatusBar";
import { Inspector } from "./Inspector";
import { PianoRoll } from "./PianoRoll";
import { Browser } from "./Browser";
import { Automation } from "./Automation";

import { INSERT_SLOTS, WORKSPACE_VIEWS, type WorkspaceView } from "./format";
import "./workspace.css";
import "./chrome.css";
import "./refine.css";
import "../brand.css";

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
  /** Timeline write path. Optional until the host exposes its clip routes. */
  editClip?: (index: number, patch: Record<string, unknown>) => void;
  splitClip?: (index: number, bar: number) => void;
  duplicateClip?: (index: number) => void;
  deleteClip?: (index: number) => void;
  loopRange?: (startBar: number, endBar: number) => void;
  undo?: () => void;
  redo?: () => void;
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
  onPluginLoad?: (plugin: PluginInfo, track: number, slot: number) => void;
  onMasterPluginLoad?: (plugin: PluginInfo) => void;
  onScan?: () => void;
}

const EMPTY_PITCH: PitchState = { transpose: 0, pitchCents: 0, tuningHz: 440 };
const MAX_TRACKS = 8;

/** Shortcut line per surface: the status strip describes the view you are in. */
const VIEW_HINTS: Partial<Record<WorkspaceView, string>> = {
  arrange: "V SELECT / T TRIM / F FADE / G GAIN / X SPLIT / E ERASE",
  mix: "CLICK A STRIP / DRAG A FADER / DOUBLE-CLICK FOR UNITY",
  inspector: "SELECT AN INSERT / DRAG A KNOB / FOUR INSERT SLOTS",
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
  onPluginLoad,
  onMasterPluginLoad,
  onScan,
}: WorkspaceProps) {
  const [view, setView] = useState<WorkspaceView>(
    WORKSPACE_VIEWS.some((entry) => entry.id === initialView) ? initialView : "arrange",
  );
  const [fitArrange, setFitArrange] = useState<(() => void) | null>(null);
  const [uiSettings, setUiSettings] = useState<UiSettings>(loadSettings);
  const [selectedClip, setSelectedClip] = useState<number | null>(null);
  const [masterGainDb, setMasterGainDb] = useState(loadSettings().masterGain);
  const [recording, setRecording] = useState(false);
  const [status, setStatus] = useState<{ text: string; alert: boolean } | null>(null);
  const [browserTarget, setBrowserTarget] = useState<
    { track: number; slot: number } | { master: true } | null
  >(null);

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
  const selectedTrack = state.tracks.find((track) => track.index === state.selectedTrack) ?? state.tracks[0];
  const trackIndex = selectedTrack?.index ?? 0;

  const clip = selectedClip !== null ? clips[selectedClip] : undefined;
  const clipNotes = selectedClip !== null ? noteDrafts[selectedClip] ?? clip?.notes ?? [] : [];

  const autoParam = autoParams[trackIndex] ?? "volume";
  const autoKey = `${trackIndex}:${autoParam}`;
  const hostLane = state.automation?.find(
    (lane) => lane.track === trackIndex && lane.param === autoParam,
  );
  const autoDraft: AutomationLaneDraft =
    autoDrafts[autoKey] ?? hostLane ?? { param: autoParam, curve: "linear", points: [] };

  const inserts = selectedTrack?.inserts ?? [];
  const firstEmptyInsert =
    INSERT_SLOTS.find((slot) => !inserts[slot]?.name) ?? INSERT_SLOTS[0];

  const flash = useCallback((text: string, alert = false) => setStatus({ text, alert }), []);
  const trackPitch = useMemo<PitchState>(
    () => ({
      transpose: selectedTrack?.transpose ?? 0,
      pitchCents: selectedTrack?.pitchCents ?? 0,
      tuningHz: selectedTrack?.tuningHz ?? 440,
    }),
    [selectedTrack?.transpose, selectedTrack?.pitchCents, selectedTrack?.tuningHz],
  );
  const onChannelParam = useCallback(
    (id: string, value: number) => onChannelChange?.(trackIndex, id, value),
    [onChannelChange, trackIndex],
  );

  const selectView = (next: WorkspaceView) => {
    setView(next);
    flash(`${WORKSPACE_VIEWS.find((entry) => entry.id === next)?.label ?? next} VIEW`);
  };

  const openBrowser = (track: number, slot: number) => {
    // The native engine owns four user slots; never hand the bridge a bank B
    // index, which it would reject after the user picked a plug-in.
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
        onLoadMasterPlugin={openMasterBrowser}
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
        onBpm={actions.bpm}
        onLoop={actions.loop}
        onMetronome={actions.metronome}
        recording={recording}
        onRecord={(next) => {
          setRecording(next);
          flash(next ? "RECORD ARMED" : "RECORD OFF", next);
        }}
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
            onInstrumentPitch={(patch) => selectedTrack && actions.instrumentPitch(selectedTrack.index, patch)}
            onOpenClip={(index) => {
              setSelectedClip(index);
              setView("piano");
            }}
            onRegisterFit={(fit) => setFitArrange(() => fit)}
            defaultTrackHeight={uiSettings.trackHeight}
            defaultFadeBars={uiSettings.fadeBars}
            defaultClipGain={uiSettings.clipGain}
            defaultSnap={uiSettings.snap}
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
            onInsert={openBrowser}
            onMasterRack={() => setView("inspector")}
            onMasterInsert={openMasterBrowser}
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
            channel={state.channel}
            onChannelParam={onChannelParam}
            pitch={trackPitch}
            onPitch={(patch) => selectedTrack && actions.trackPitch(selectedTrack.index, patch)}
            instrumentPitch={selectedTrack?.instrumentPitch ?? EMPTY_PITCH}
            onInstrumentPitch={(patch) => selectedTrack && actions.instrumentPitch(selectedTrack.index, patch)}
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
              <button className="ws-btn is-icon" aria-label="Close plug-in browser" onClick={closeBrowser} autoFocus>
                ✕
              </button>
            </header>
            <Browser
              plugins={state.plugins ?? []}
              targetName={isMasterTarget ? "MASTER" : browserTrack ? `TRACK ${browserTrack.index + 1}` : "TRACK"}
              targetSlot={isMasterTarget ? undefined : browserSlot}
              scanning={state.pluginScan?.scanning}
              progress={state.pluginScan?.progress}
              status={state.pluginScan?.status}
              onScan={onScan}
              onLoad={
                canLoad
                  ? (plugin) => {
                      if (isMasterTarget) {
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

      <StatusBar
        hints={VIEW_HINTS[view] ?? ""}
        message={status?.text}
        alert={status?.alert}
        activeTracks={state.tracks.length}
        trackCapacity={8}
        clipCount={clips.length}
        pluginCount={state.plugins?.length ?? 0}
      />
    </div>
  );
}
