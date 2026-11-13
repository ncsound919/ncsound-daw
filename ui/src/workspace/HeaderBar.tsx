import { useEffect, useRef, useState } from "react";
import { WORKSPACE_VIEWS, type WorkspaceView } from "./format";

export interface HeaderBarProps {
  view: WorkspaceView;
  onView: (view: WorkspaceView) => void;
  onNewSession: () => void;
  onAddTrack: () => void;
  onRemoveTrack: () => void;
  onMuteSelected: () => void;
  onSoloSelected: () => void;
  canAddTrack?: boolean;
  canRemoveTrack?: boolean;
  dspLoad?: number;
  /** Clip actions for the selected clip; undefined keeps the menu item disabled. */
  hasSelection?: boolean;
  onSplitSelected?: () => void;
  onDuplicateSelected?: () => void;
  onDeleteSelected?: () => void;
  onScanPlugins?: () => void;
  onLoadMasterPlugin?: () => void;
  /** Mixology presets; the menu hides the section without them. */
  mixologyPresets?: string[];
  onApplyMixology?: (index: number) => void;
  /** Import Audio hook; the menu item stays disabled without it. */
  /** Session and export hooks; each menu entry stays disabled without its route. */
  onOpenSession?: () => void;
  onSaveSession?: () => void;
  onSaveSessionAs?: () => void;
  onMixdownWav?: () => void;
  onExportStems?: () => void;
  onCollectMedia?: () => void;
  /** Master-insert hook; the remove entry stays disabled without it. */
  onRemoveMasterPlugin?: () => void;
  canRemoveMasterPlugin?: boolean;
  onUndo?: () => void;
  onRedo?: () => void;
  undoDepth?: number;
  redoDepth?: number;
  onTranscribeSelected?: () => void;
  onSelectPrevious?: () => void;
  onSelectNext?: () => void;
  onImportAudio?: () => void;
  onFit?: () => void;
}

type Item = {
  label: string;
  run?: () => void;
  disabled?: boolean;
  checked?: boolean;
  hint?: string;
};
type Entry = Item | "separator" | { heading: string };

/** Native MainComponent header: NCSound lockup, five menus, hardware buttons.
    Items that need a native route are disabled and say so instead of failing. */
export function HeaderBar({
  view,
  onView,
  onNewSession,
  onAddTrack,
  onRemoveTrack,
  onMuteSelected,
  onSoloSelected,
  canAddTrack = true,
  canRemoveTrack = true,
  dspLoad,
  hasSelection = false,
  onSplitSelected,
  onDuplicateSelected,
  onDeleteSelected,
  onScanPlugins,
  onLoadMasterPlugin,
  mixologyPresets,
  onApplyMixology,
  onOpenSession,
  onSaveSession,
  onSaveSessionAs,
  onMixdownWav,
  onExportStems,
  onCollectMedia,
  onRemoveMasterPlugin,
  canRemoveMasterPlugin = false,
  onUndo,
  onRedo,
  undoDepth = 0,
  redoDepth = 0,
  onTranscribeSelected,
  onSelectPrevious,
  onSelectNext,
  onImportAudio,
  onFit,
}: HeaderBarProps) {
  const [open, setOpen] = useState<number | null>(null);
  const [dialog, setDialog] = useState<"about" | "keys" | "prefs" | null>(null);
  const navRef = useRef<HTMLDivElement>(null);
  const tabRefs = useRef<Partial<Record<WorkspaceView, HTMLButtonElement | null>>>({});

  useEffect(() => {
    if (open === null) return;
    const close = (event: PointerEvent) => {
      if (!navRef.current?.contains(event.target as Node)) setOpen(null);
    };
    document.addEventListener("pointerdown", close);
    return () => document.removeEventListener("pointerdown", close);
  }, [open]);

  // Focus trap + return for header dialogs.
  const dialogOpener = useRef<HTMLElement | null>(null);
  useEffect(() => {
    if (!dialog) return;
    dialogOpener.current = document.activeElement as HTMLElement | null;
    const dialogEl = document.querySelector(".ws-dialog") as HTMLElement | null;
    dialogEl?.querySelector<HTMLButtonElement>("button")?.focus();
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        setDialog(null);
        return;
      }
      if (event.key !== "Tab") return;
      const items = Array.from(
        dialogEl?.querySelectorAll<HTMLElement>(
          "button:not(:disabled), [href], [tabindex]:not([tabindex='-1'])",
        ) ?? [],
      );
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
      dialogOpener.current?.focus();
    };
  }, [dialog]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.key !== "Escape") return;
      if (dialog) setDialog(null);
      else if (open !== null) setOpen(null);
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [open, dialog]);

  const onMenuKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    if (!["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) return;
    const items = Array.from(
      event.currentTarget.querySelectorAll<HTMLButtonElement>(".ws-menu-item:not(:disabled)"),
    );
    if (items.length === 0) return;
    event.preventDefault();
    const index = items.indexOf(document.activeElement as HTMLButtonElement);
    const next =
      event.key === "Home"
        ? 0
        : event.key === "End"
          ? items.length - 1
          : event.key === "ArrowDown"
            ? (index + 1) % items.length
            : (index - 1 + items.length) % items.length;
    items[next]?.focus();
  };

  const run = (item: Item) => {
    if (item.disabled || !item.run) return;
    setOpen(null);
    item.run();
  };

  // Roving-tab keyboard model, same as the old view bar: the tabs live in the
  // header now, so they keep their own arrow/Home/End handling here.
  const onViewKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    const index = WORKSPACE_VIEWS.findIndex((entry) => entry.id === view);
    let nextIndex = index;

    if (event.key === "ArrowRight") nextIndex = (index + 1) % WORKSPACE_VIEWS.length;
    else if (event.key === "ArrowLeft")
      nextIndex = (index - 1 + WORKSPACE_VIEWS.length) % WORKSPACE_VIEWS.length;
    else if (event.key === "Home") nextIndex = 0;
    else if (event.key === "End") nextIndex = WORKSPACE_VIEWS.length - 1;
    else return;

    event.preventDefault();
    const next = WORKSPACE_VIEWS[nextIndex];
    onView(next.id);
    tabRefs.current[next.id]?.focus();
  };
  // A host-wired entry when its route exists, otherwise an honest placeholder.
  const wired = (label: string, run?: () => void): Item =>
    run ? { label, run } : { label, disabled: true, hint: "Not wired to the bridge yet" };

  const menus: { name: string; entries: Entry[] }[] = [
    {
      name: "FILE",
      entries: [
        { heading: "SESSION" },
        { label: "Reset Transport", run: onNewSession },
        wired("Open Session…", onOpenSession),
        wired("Save Session", onSaveSession),
        wired("Save Session As…", onSaveSessionAs),
        wired("Import Audio…", onImportAudio),
        wired("Collect Media…", onCollectMedia),
        "separator",
        { heading: "EXPORT" },
        wired("Mixdown WAV…", onMixdownWav),
        wired("Export Stems…", onExportStems),
        { label: "Mixdown FLAC…", disabled: true, hint: "Desktop app FILE → Export" },
        { label: "Mixdown MP3…", disabled: true, hint: "Desktop app FILE → Export" },
        { label: "Audition Stems…", disabled: true, hint: "Desktop app FILE → Export" },
        "separator",
        { heading: "AGENT" },
        { label: "Apply Mix Plan…", disabled: true, hint: "Desktop app FILE → Agent" },
        { label: "Ask Mix Assistant…", disabled: true, hint: "Desktop app FILE → Agent" },
      ],
    },
    {
      name: "EDIT",
      entries: [
        { heading: "CLIP" },
        {
          label: "Split at Playhead",
          run: onSplitSelected,
          disabled: !hasSelection || !onSplitSelected,
          hint: hasSelection && onSplitSelected ? undefined : "Select a clip first",
        },
        {
          label: "Duplicate Clip",
          run: onDuplicateSelected,
          disabled: !hasSelection || !onDuplicateSelected,
          hint: hasSelection && onDuplicateSelected ? undefined : "Select a clip first",
        },
        {
          label: "Delete Clip",
          run: onDeleteSelected,
          disabled: !hasSelection || !onDeleteSelected,
          hint: hasSelection && onDeleteSelected ? undefined : "Select a clip first",
        },
        {
          label: "Select All Clips",
          disabled: true,
          hint: "Desktop app EDIT → Select All",
        },
        "separator",
        { heading: "UNDO" },
        {
          label: `Undo${undoDepth ? ` (${undoDepth})` : ""}`,
          run: onUndo,
          disabled: !onUndo || !undoDepth,
          hint: !onUndo ? "Not wired to the bridge yet" : "Nothing to undo",
        },
        {
          label: `Redo${redoDepth ? ` (${redoDepth})` : ""}`,
          run: onRedo,
          disabled: !onRedo || !redoDepth,
          hint: !onRedo ? "Not wired to the bridge yet" : "Nothing to redo",
        },
        "separator",
        { heading: "MIDI" },
        { label: "Edit MIDI Clip", run: () => onView("piano") },
        { label: "Edit Automation Lanes", run: () => onView("automation") },
        onTranscribeSelected
          ? { label: "Transcribe Clip to MIDI", run: onTranscribeSelected }
          : {
              label: "Transcribe Clip to MIDI",
              disabled: true,
              hint: "Select an audio clip first",
            },
        { label: "Import MIDI File…", disabled: true, hint: "Desktop app FILE → Import" },
        { label: "MIDI Learn…", disabled: true, hint: "Desktop app EDIT → MIDI" },
        "separator",
        { heading: "MARKERS" },
        {
          label: "Add Marker at Playhead",
          disabled: true,
          hint: "Desktop app EDIT → Markers",
        },
        {
          label: "Go to Next Marker",
          disabled: true,
          hint: "Desktop app EDIT → Markers",
        },
        "separator",
        { heading: "AUDIOSUITE" },
        { label: "Reverse Clip", disabled: true, hint: "Desktop app EDIT → Audiosuite" },
        { label: "Normalize Clip", disabled: true, hint: "Desktop app EDIT → Audiosuite" },
        "separator",
        { heading: "MASTER INSERT" },
        ...(onLoadMasterPlugin
          ? ([{ label: "Load Master Plugin…", run: onLoadMasterPlugin }] as Entry[])
          : []),
        onRemoveMasterPlugin && canRemoveMasterPlugin
          ? { label: "Remove Master Plugin", run: onRemoveMasterPlugin }
          : {
              label: "Remove Master Plugin",
              disabled: true,
              hint: !onRemoveMasterPlugin
                ? "Not wired to the bridge yet"
                : "No master plugin loaded",
            },
        "separator",
        { heading: "MASTER RACK" },
        { label: "Automix Snapshot", disabled: true, hint: "Desktop app EDIT → Master Rack" },
        ...(onScanPlugins
          ? (["separator", { label: "Scan Plugins (VST3)", run: onScanPlugins }] as Entry[])
          : []),
        ...(mixologyPresets && mixologyPresets.length > 0 && onApplyMixology
          ? ([
              "separator",
              { heading: "MIXOLOGY" },
              ...mixologyPresets.map((name, index) => ({
                label: name,
                run: () => onApplyMixology(index),
              })),
            ] as Entry[])
          : []),
      ],
    },
    {
      name: "TRACK",
      entries: [
        { heading: "TRACK" },
        {
          label: "Add Instrument Track",
          run: onAddTrack,
          disabled: !canAddTrack,
          hint: canAddTrack ? undefined : "Maximum 8 tracks",
        },
        {
          label: "Add Audio Track",
          run: onAddTrack,
          disabled: !canAddTrack,
          hint: canAddTrack ? undefined : "Maximum 8 tracks",
        },
        {
          label: "Add Return Track",
          run: onAddTrack,
          disabled: !canAddTrack,
          hint: canAddTrack ? undefined : "Maximum 8 tracks",
        },
        {
          label: "Remove Last Track",
          run: onRemoveTrack,
          disabled: !canRemoveTrack,
          hint: canRemoveTrack ? undefined : "At least one track",
        },
        wired("Select Previous Track", onSelectPrevious),
        wired("Select Next Track", onSelectNext),
        "separator",
        { label: "Mute Selected Track", run: onMuteSelected },
        { label: "Solo Selected Track", run: onSoloSelected },
      ],
    },
    {
      name: "HELP",
      entries: [
        { label: "About NCSound Music Hub", run: () => setDialog("about") },
        { label: "Preferences", run: () => setDialog("prefs") },
        { label: "Documentation", disabled: true, hint: "Ships with the desktop app" },
        { label: "Keyboard Shortcuts", run: () => setDialog("keys") },
      ],
    },
  ];

  const dsp = dspLoad === undefined ? undefined : Math.max(0, Math.min(1, dspLoad));

  return (
    <header className="ws-header">
      <div className="ws-logo">
        <svg className="ws-logo-mark" viewBox="0 0 28 28" aria-hidden="true" focusable="false">
          <circle cx="14" cy="14" r="12.5" fill="none" stroke="currentColor" strokeWidth="1.6" />
          <path
            d="M8 15.5v-3M11.5 18V10M15 16.5v-5M18.5 19V9M22 15v-2"
            stroke="currentColor"
            strokeWidth="1.8"
            strokeLinecap="round"
          />
        </svg>
        <span className="ws-logo-text">
          <b>NCSOUND</b>
          <i>MUSIC HUB / DAW</i>
        </span>
      </div>

      <div className="ws-menubar" ref={navRef}>
        {menus.map((menu, index) => (
          <div className="ws-menu-wrap" key={menu.name}>
            <button
              className="ws-menubtn"
              aria-haspopup="menu"
              aria-expanded={open === index}
              onClick={() => setOpen(open === index ? null : index)}
              onKeyDown={(event) => {
                if (event.key !== "ArrowDown") return;
                event.preventDefault();
                setOpen(index);
                window.setTimeout(() => {
                  navRef.current
                    ?.querySelector<HTMLButtonElement>(".ws-menu-item:not(:disabled)")
                    ?.focus();
                }, 0);
              }}
            >
              {menu.name}
            </button>

            {open === index && (
              <div className="ws-menu" role="menu" aria-label={menu.name} onKeyDown={onMenuKeyDown}>
                {menu.entries.map((entry, entryIndex) => {
                  if (entry === "separator")
                    return (
                      <span key={`sep-${entryIndex}`} className="ws-menu-sep" role="separator" />
                    );
                  if ("heading" in entry) {
                    return (
                      <span key={`head-${entryIndex}`} className="ws-menu-heading">
                        {entry.heading}
                      </span>
                    );
                  }
                  return (
                    <button
                      key={entry.label}
                      className="ws-menu-item"
                      role={entry.checked === undefined ? "menuitem" : "menuitemradio"}
                      disabled={entry.disabled}
                      aria-checked={entry.checked === undefined ? undefined : entry.checked}
                      title={entry.hint}
                      onClick={() => run(entry)}
                    >
                      <span className="ws-menu-tick">{entry.checked ? "•" : ""}</span>
                      <span className="ws-menu-label">{entry.label}</span>
                      {entry.hint && /native|route/i.test(entry.hint) && (
                        <span className="ws-menu-hint">native</span>
                      )}
                    </button>
                  );
                })}
              </div>
            )}
          </div>
        ))}
      </div>

      <div className="ws-tabs" role="tablist" aria-label="Workspace view" onKeyDown={onViewKeyDown}>
        {WORKSPACE_VIEWS.map((entry) => (
          <button
            key={entry.id}
            id={`ws-tab-${entry.id}`}
            className="ws-viewtab"
            role="tab"
            aria-selected={view === entry.id}
            aria-controls="ws-surface-panel"
            tabIndex={view === entry.id ? 0 : -1}
            title={entry.title}
            ref={(node) => {
              tabRefs.current[entry.id] = node;
            }}
            onClick={() => onView(entry.id)}
          >
            {entry.label}
          </button>
        ))}
      </div>

      <span className="ws-spacer" />

      <div className="ws-dsp" title="DSP load reported by the engine">
        <span className="ws-dsp-label">DSP</span>
        <span className="ws-dsp-bars" aria-hidden="true">
          {Array.from({ length: 10 }, (_, bar) => (
            <i key={bar} className={bar < Math.round((dsp ?? 0) * 10) ? "is-on" : ""} />
          ))}
        </span>
        <span className="ws-dsp-read">{dsp === undefined ? "—" : `${Math.round(dsp * 100)}%`}</span>
      </div>

      {onFit && (
        <button className="ws-btn" onClick={onFit} title="Fit the session to the window">
          FIT
        </button>
      )}

      <button
        className="ws-btn ws-new"
        onClick={onNewSession}
        title="Stop the transport and return to bar 1; clips stay"
      >
        NEW
      </button>

      {dialog && (
        <div className="ws-dialog-backdrop" onClick={() => setDialog(null)}>
          <div
            className="ws-dialog"
            role="dialog"
            aria-modal="true"
            aria-label={
              dialog === "about"
                ? "About"
                : dialog === "prefs"
                  ? "Preferences"
                  : "Keyboard shortcuts"
            }
            onClick={(event) => event.stopPropagation()}
          >
            <header>
              <span>
                {dialog === "about"
                  ? "ABOUT NCSOUND MUSIC HUB"
                  : dialog === "prefs"
                    ? "PREFERENCES"
                    : "KEYBOARD SHORTCUTS"}
              </span>
              <button
                className="ws-btn is-icon"
                aria-label="Close"
                autoFocus
                onClick={() => setDialog(null)}
              >
                ✕
              </button>
            </header>
            {dialog === "about" ? (
              <p>
                NCSound Music Hub / DAW. Transport, mixer and arrange live here. Plug-in catalogue,
                automation and clip editing follow your selection.
              </p>
            ) : dialog === "prefs" ? (
              <dl className="ws-keys">
                <div>
                  <dt>View</dt>
                  <dd>{WORKSPACE_VIEWS.find((entry) => entry.id === view)?.label ?? view}</dd>
                </div>
                <div>
                  <dt>DSP</dt>
                  <dd>{dsp === undefined ? "—" : `${Math.round(dsp * 100)}%`}</dd>
                </div>
                <div>
                  <dt>Tracks</dt>
                  <dd>{canAddTrack ? "up to 8" : "8 / 8 — full"}</dd>
                </div>
                <div>
                  <dt>Loop</dt>
                  <dd>Drag the loop lane in the ruler</dd>
                </div>
              </dl>
            ) : (
              <dl className="ws-keys">
                <div>
                  <dt>Space</dt>
                  <dd>Play / pause (Pro Tools transport)</dd>
                </div>
                <div>
                  <dt>Home</dt>
                  <dd>Return to bar 1</dd>
                </div>
                <div>
                  <dt>1 · 2 · 3 · 4 · 5 · 6</dt>
                  <dd>Grabber / Trim / Fade / Volume / X / Erase (V/T/F/G/X/E aliases)</dd>
                </div>
                <div>
                  <dt>Cmd/Ctrl + Z</dt>
                  <dd>Undo</dd>
                </div>
                <div>
                  <dt>Cmd/Ctrl + Shift + Z</dt>
                  <dd>Redo (Ctrl+Y too)</dd>
                </div>
                <div>
                  <dt>+ / −</dt>
                  <dd>Zoom timeline</dd>
                </div>
                <div>
                  <dt>L</dt>
                  <dd>Loop enable (in ruler — arrow keys nudge)</dd>
                </div>
                <div>
                  <dt>Delete</dt>
                  <dd>Remove the selected clip / piano note</dd>
                </div>
                <div>
                  <dt>Alt-click</dt>
                  <dd>Delete an automation point</dd>
                </div>
                <div>
                  <dt>Ctrl / ⌘ + wheel</dt>
                  <dd>Zoom the arrangement</dd>
                </div>
              </dl>
            )}
          </div>
        </div>
      )}
    </header>
  );
}
