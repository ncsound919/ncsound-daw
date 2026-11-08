import { Knob } from "../console-ui/Knob";
import { toNorm, toValue, type ParamSpec } from "../console-ui/param";
import type { PitchState } from "../types";

export const TRANSPOSE_SPEC: ParamSpec = { min: -48, max: 48, decimals: 0, unit: "st" };
export const PITCH_SPEC: ParamSpec = { min: -100, max: 100, decimals: 0, unit: "ct" };
export const HZ_SPEC: ParamSpec = { min: 415, max: 466, decimals: 1, unit: "Hz" };

/**
 * Shared pitch knobs — ported from ChordStudio MixView PitchKnobs.
 * Uses shared @chordstudio/console-ui Knob + ParamSpec. The visible labels are
 * short so they fit under each cap; the group carries the qualified name.
 */
export function PitchKnobs({
  value,
  onChange,
  label = "Pitch",
  compact = false,
}: {
  value: PitchState;
  onChange: (patch: Partial<PitchState>) => void;
  label?: string;
  compact?: boolean;
}) {
  const size = compact ? "m" : "l";
  return (
    <div className={`ws-pitch-row${compact ? " is-compact" : ""}`} role="group" aria-label={`${label} pitch`}>
      <Knob
        label="Transpose"
        size={size}
        showValue
        bipolar
        spec={TRANSPOSE_SPEC}
        value={toNorm(value.transpose, TRANSPOSE_SPEC)}
        defaultValue={toNorm(0, TRANSPOSE_SPEC)}
        onChange={(v) => onChange({ transpose: Math.round(toValue(v, TRANSPOSE_SPEC)) })}
      />
      <Knob
        label="Cents"
        size={size}
        showValue
        bipolar
        spec={PITCH_SPEC}
        value={toNorm(value.pitchCents, PITCH_SPEC)}
        defaultValue={toNorm(0, PITCH_SPEC)}
        onChange={(v) => onChange({ pitchCents: Math.round(toValue(v, PITCH_SPEC)) })}
      />
      <Knob
        label="Tuning"
        size={size}
        showValue
        editable
        spec={HZ_SPEC}
        value={toNorm(value.tuningHz, HZ_SPEC)}
        defaultValue={toNorm(440, HZ_SPEC)}
        onChange={(v) => onChange({ tuningHz: Math.round(toValue(v, HZ_SPEC) * 10) / 10 })}
      />
    </div>
  );
}
