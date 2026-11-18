import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { HeaderBar } from "./HeaderBar";

function headerProps(overrides = {}) {
  return {
    view: "mix" as const,
    onView: vi.fn(),
    onNewSession: vi.fn(),
    onAddTrack: vi.fn(),
    onRemoveTrack: vi.fn(),
    onMuteSelected: vi.fn(),
    onSoloSelected: vi.fn(),
    ...overrides,
  };
}

describe("HeaderBar master A/B", () => {
  it("captures and recalls the master snapshot from the EDIT menu", () => {
    const onCaptureMasterAb = vi.fn();
    const onRecallMasterAb = vi.fn();
    render(
      <HeaderBar
        {...headerProps({ onCaptureMasterAb, onRecallMasterAb, canCaptureMasterAb: true })}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Capture Master A/B" }));
    expect(onCaptureMasterAb).toHaveBeenCalledTimes(1);

    // The menu closes after each action, like the native menus do.
    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Recall Master A/B" }));
    expect(onRecallMasterAb).toHaveBeenCalledTimes(1);
  });

  it("hides the entries when the host has no routes", () => {
    render(<HeaderBar {...headerProps()} />);

    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    expect(screen.queryByRole("menuitem", { name: "Capture Master A/B" })).toBeNull();
    expect(screen.queryByRole("menuitem", { name: "Recall Master A/B" })).toBeNull();
  });

  it("disables capture until a master plugin is loaded", () => {
    render(
      <HeaderBar {...headerProps({ onCaptureMasterAb: vi.fn(), canCaptureMasterAb: false })} />,
    );

    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    expect(screen.getByRole("menuitem", { name: "Capture Master A/B" })).toBeDisabled();
  });
});

describe("HeaderBar status reporting", () => {
  it("renders status when provided", () => {
    render(
      <HeaderBar {...headerProps({ status: "EXPORT / no active tracks", statusIsError: true })} />,
    );
    const statusEl = screen.getByRole("status");
    expect(statusEl).toHaveTextContent("EXPORT / no active tracks");
    expect(statusEl).toHaveAttribute("aria-invalid", "true");
  });

  it("does not render role status when no status is provided", () => {
    render(<HeaderBar {...headerProps()} />);
    expect(screen.queryByRole("status")).toBeNull();
  });

  it("renders status without aria-invalid when status is not an error", () => {
    render(<HeaderBar {...headerProps({ status: "SESSION / cancelled", statusIsError: false })} />);
    const statusEl = screen.getByRole("status");
    expect(statusEl).toHaveTextContent("SESSION / cancelled");
    expect(statusEl).not.toHaveAttribute("aria-invalid");
  });
});
