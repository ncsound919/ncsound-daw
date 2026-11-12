#!/usr/bin/env node
/**
 * Generate the C++ studio-FX catalog spec table from the vendored catalog.
 *
 *   node tools/gen-studiofx-catalog.mjs
 *
 * Reads tests/fixtures/studiofx/catalog.json (vendored from overlay-shared)
 * and rewrites engine/include/nc/engine/StudioFxCatalogData.hpp. The header
 * is committed: the C++ chain codec sanitises slots against this table, so a
 * catalog change can never reach one app without the others.
 *
 * Re-run after re-vendoring catalog.json, then rebuild and run the parity
 * tests. A diff here without a matching upstream catalog change is a bug.
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..");
const CATALOG = path.join(ROOT, "tests", "fixtures", "studiofx", "catalog.json");
const OUT = path.join(ROOT, "engine", "include", "nc", "engine", "StudioFxCatalogData.hpp");

function cppString(value) {
  return `"${value.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
}

function cppFloat(value) {
  // Shortest round-trip decimal, exactly what the JSON parser yields as a
  // double before the float cast — so the table matches the TS defaults bit
  // for bit after `static_cast<float>`. Integral values need “.0”: `2f`
  // is not a valid C++ floating literal.
  let text = JSON.stringify(value);
  if (!/[.eEn]/.test(text)) text += ".0";
  return `${text}f`;
}

const catalog = JSON.parse(fs.readFileSync(CATALOG, "utf8"));
if (catalog.schema !== "nc.studio-fx-catalog.v1" || !Array.isArray(catalog.kinds)) {
  console.error(`Unexpected catalog shape in ${CATALOG}`);
  process.exit(1);
}

const lines = [];
lines.push("#pragma once");
lines.push("");
lines.push("#include <cstddef>");
lines.push("");
lines.push("namespace nc::engine::studiofx");
lines.push("{");
lines.push("/** One selectable value of a choice parameter (ratio, mode, ...). */");
lines.push("struct FxParamChoice");
lines.push("{");
lines.push("    float value = 0.0f;");
lines.push("    const char* label = nullptr;");
lines.push("};");
lines.push("");
lines.push("/** Range and default of one module parameter. Mirrors FxParamSpec. */");
lines.push("struct FxParamSpec");
lines.push("{");
lines.push("    const char* key = nullptr;");
lines.push("    const char* label = nullptr;");
lines.push("    float minValue = 0.0f;");
lines.push("    float maxValue = 0.0f;");
lines.push("    float defaultValue = 0.0f;");
lines.push("    const char* unit = nullptr;");
lines.push("    const FxParamChoice* choices = nullptr;");
lines.push("    std::size_t numChoices = 0;");
lines.push("};");
lines.push("");
lines.push("/** One insert module kind. Mirrors FxKindSpec. */");
lines.push("struct FxKindSpec");
lines.push("{");
lines.push("    const char* id = nullptr;");
lines.push("    const char* name = nullptr;");
lines.push("    const char* category = nullptr;");
lines.push("    const FxParamSpec* params = nullptr;");
lines.push("    std::size_t numParams = 0;");
lines.push("};");
lines.push("");

const kindSymbols = [];
for (const kind of catalog.kinds) {
  const safe = kind.id.replace(/[^A-Za-z0-9_]/g, "_");
  for (const param of kind.params) {
    if (Array.isArray(param.choices) && param.choices.length > 0) {
      lines.push(`static constexpr FxParamChoice kChoices_${safe}_${param.key}[] = {`);
      for (const choice of param.choices) {
        lines.push(`    { ${cppFloat(choice.value)}, ${cppString(choice.label)} },`);
      }
      lines.push("};");
    }
  }
  const paramsSym = `kParams_${safe}`;
  lines.push(`static constexpr FxParamSpec ${paramsSym}[] = {`);
  for (const param of kind.params) {
    const hasChoices = Array.isArray(param.choices) && param.choices.length > 0;
    const choices = hasChoices
      ? `kChoices_${safe}_${param.key}, ${param.choices.length}`
      : "nullptr, 0";
    lines.push(
      `    { ${cppString(param.key)}, ${cppString(param.label)}, ` +
        `${cppFloat(param.min)}, ${cppFloat(param.max)}, ${cppFloat(param.default)}, ` +
        `${cppString(param.unit)}, ${choices} },`,
    );
  }
  lines.push("};");
  kindSymbols.push({ id: kind.id, name: kind.name, category: kind.category, paramsSym, count: kind.params.length });
}

lines.push("static constexpr FxKindSpec kFxKinds[] = {");
for (const kind of kindSymbols) {
  lines.push(
    `    { ${cppString(kind.id)}, ${cppString(kind.name)}, ${cppString(kind.category)}, ` +
      `${kind.paramsSym}, ${kind.count} },`,
  );
}
lines.push("};");
lines.push("");
lines.push("inline constexpr std::size_t kNumFxKinds = sizeof (kFxKinds) / sizeof (kFxKinds[0]);");
lines.push("");
lines.push("} // namespace nc::engine::studiofx");

fs.writeFileSync(OUT, lines.join("\n") + "\n");
console.log(`wrote ${OUT} (${catalog.kinds.length} kinds)`);
