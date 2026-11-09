import { render } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { Browser } from "./Browser";
import type { PluginInfo } from "../types";

function plugin(name: string, vendor = "Acme", path?: string): PluginInfo {
  return {
    id: `${vendor}.${name}`.toLowerCase(),
    name,
    vendor,
    category: "effect",
    format: "VST3",
    path: path ?? `C:/VST3/${name}.vst3`,
  };
}

describe("Browser", () => {
  it("sorts the catalogue by name instead of scan order", () => {
    render(<Browser plugins={[plugin("Zebra"), plugin("Alpha"), plugin("Mango")]} />);

    const rows = Array.from(document.querySelectorAll(".ws-browser-row-name")).map(
      (node) => node.textContent,
    );

    expect(rows).toEqual(["Alpha", "Mango", "Zebra"]);
  });

  it("marks a plug-in that exists in more than one copy", () => {
    render(
      <Browser
        plugins={[
          plugin("Vox", "Acme", "C:/a/Vox.vst3"),
          plugin("Vox", "Acme", "C:/b/Vox.vst3"),
          plugin("Solo"),
        ]}
      />,
    );

    const copies = Array.from(document.querySelectorAll(".ws-browser-row-tag"))
      .map((node) => node.textContent)
      .filter((text) => text?.startsWith("×"));

    expect(copies).toEqual(["×2", "×2"]);
  });
});
