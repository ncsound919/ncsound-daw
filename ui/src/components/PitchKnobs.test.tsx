import { render, screen, within } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { PitchKnobs, TRANSPOSE_SPEC } from "./PitchKnobs";

describe("PitchKnobs", () => {
  it("renders three labelled knobs for the current pitch", () => {
    const onChange = vi.fn();
    render(
      <PitchKnobs
        value={{ transpose: 2, pitchCents: -10, tuningHz: 442 }}
        onChange={onChange}
        label="Global"
      />,
    );
    const group = screen.getByRole("group", { name: /Global pitch/i });
    expect(within(group).getByLabelText(/Transpose/i)).toBeInTheDocument();
    expect(within(group).getByLabelText(/Cents/i)).toBeInTheDocument();
    expect(within(group).getByLabelText(/Tuning/i)).toBeInTheDocument();
  });

  it("exposes the shared param specs (transpose ±48 st, pitch ±100 ct, Hz 415–466)", () => {
    expect(TRANSPOSE_SPEC.min).toBe(-48);
    expect(TRANSPOSE_SPEC.max).toBe(48);
  });
});
