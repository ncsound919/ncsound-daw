import { useMemo } from "react";
import { PANEL_H, PANEL_W, panels, type PanelEntry } from "../console-ui/panels";
import { formatValue } from "../console-ui/param";
import { createMemoryStore } from "../console-ui/store";
import type { InsertSlot } from "../types";

/**
 * The six stock Overlay FX plug-ins from the ChordStudio build
 * (plugins/overlay-fx/plugins/*). Each one already has a real console-ui
 * faceplate, so the rack and the inspector render the stock panel rather than
 * a second, invented control set.
 */
export type OverlayModuleId = "strip" | "lofi" | "delay" | "reverb" | "modulator" | "master";

export interface OverlayModule {
  id: OverlayModuleId;
  /** Key in the console-ui panel registry. */
  panel: string;
  /** Faceplate title, as the stock PluginEditor draws it. */
  name: string;
  tag: string;
  short: string;
  blurb: string;
  /** How an insert name the host reports maps onto this stock module. */
  match: RegExp;
}

export const OVERLAY_MODULES: OverlayModule[] = [
  {
    id: "strip",
    panel: "strip",
    name: "Overlay Channel Strip",
    tag: "CHANNEL STRIP",
    short: "STRP",
    blurb: "HP / LP, four dynamic bands, comp, sat, width",
    match: /strip|channel/i,
  },
  {
    id: "lofi",
    panel: "lofi",
    name: "Overlay Lo-Fi",
    tag: "RETRO COLOR",
    short: "LOFI",
    blurb: "noise, wobble, distortion, digital, space, tape",
    match: /retro|lofi|lo-fi/i,
  },
  {
    id: "delay",
    panel: "delay",
    name: "Overlay Delay",
    tag: "DELAY",
    short: "DLY",
    blurb: "dual echo, diffusion, duck, rhythm taps",
    match: /delay|echo/i,
  },
  {
    id: "reverb",
    panel: "reverb",
    name: "Overlay Reverb",
    tag: "REVERB",
    short: "REV",
    blurb: "convolution IR, tilt, modulation, width",
    match: /reverb|verb/i,
  },
  {
    id: "modulator",
    panel: "modulator",
    name: "Overlay Modulator",
    tag: "MODULATOR",
    short: "MOD",
    blurb: "shaper, chorus, flanger, phaser, trem, rotary",
    match: /modulat|chorus|flang|phaser/i,
  },
  {
    id: "master",
    panel: "master",
    name: "Overlay Master",
    tag: "MASTER",
    short: "MSTR",
    blurb: "8-band EQ, dynamics, exciter, image, maximiser",
    match: /master/i,
  },
];

const BY_ID = new Map(OVERLAY_MODULES.map((module) => [module.id, module]));

export function moduleById(id: OverlayModuleId): OverlayModule {
  const found = BY_ID.get(id);
  if (!found) throw new Error(`unknown overlay module ${id}`);
  return found;
}

export function panelFor(id: OverlayModuleId): PanelEntry {
  return panels[moduleById(id).panel];
}

export interface OverlayReadout {
  key: string;
  label: string;
  value: string;
  norm: number;
}

/**
 * Read-only readout of a stock faceplate's controls at its preview values.
 * Used by the inspector's surgical view, which must not pretend to edit a
 * plug-in the bridge has no parameter route for.
 */
export function previewReadout(id: OverlayModuleId, limit = 28): OverlayReadout[] {
  const entry = panelFor(id);
  return Object.entries(entry.schema)
    .slice(0, limit)
    .map(([key, control]) => {
      const norm = entry.preview?.[key] ?? control.def;
      return { key, label: control.label, value: formatValue(norm, control.spec ?? {}), norm };
    });
}

/** The stock module an insert name refers to, or null for a foreign plug-in. */
export function moduleForInsert(name?: string): OverlayModuleId | null {
  if (!name) return null;
  return OVERLAY_MODULES.find((module) => module.match.test(name))?.id ?? null;
}

/** Stock modules the host already reports on this track, in insert order. */
export function chainFromInserts(inserts?: InsertSlot[]): OverlayModuleId[] {
  const found = (inserts ?? [])
    .map((insert) => moduleForInsert(insert?.name))
    .filter((id): id is OverlayModuleId => Boolean(id));
  return [...new Set(found)];
}

/**
 * A stock faceplate at preview values. The panel is the same component the
 * plug-in ships with; the store is a memory store seeded from the panel's own
 * schema defaults and its preview preset, so nothing is written to audio.
 */
export function OverlayFace({
  id,
  scale = 0.5,
  presetName,
  label,
}: {
  id: OverlayModuleId;
  scale?: number;
  presetName?: string;
  label?: string;
}) {
  const entry = panelFor(id);
  const store = useMemo(() => {
    const values: Record<string, number> = {};
    for (const [key, control] of Object.entries(entry.schema)) {
      values[key] = entry.preview?.[key] ?? control.def;
    }
    return createMemoryStore(values, entry.actions);
  }, [entry]);
  const map = useMemo(
    () => Object.fromEntries(Object.keys(entry.schema).map((key) => [key, key])),
    [entry],
  );
  const Panel = entry.Panel;
  const module = moduleById(id);

  return (
    <div
      className="ws-ov-face"
      role="group"
      aria-label={`${label ?? module.name} faceplate preview`}
      style={{ width: Math.round(PANEL_W * scale), height: Math.round(PANEL_H * scale) }}
    >
      <div
        className="ws-ov-face-inner"
        style={{ width: PANEL_W, height: PANEL_H, transform: `scale(${scale})` }}
      >
        <Panel store={store} map={map} presetName={presetName ?? entry.presetName} {...(entry.props ?? {})} />
      </div>
    </div>
  );
}
