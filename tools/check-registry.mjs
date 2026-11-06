// Validates plugins/registry.json. Run by ctest and by CI; no dependencies.
import { readFileSync } from "node:fs";

const path = process.argv[2];

if (!path) {
  console.error("usage: check-registry.mjs <registry.json>");
  process.exit(1);
}

const kinds = new Set(["instrument", "effect", "utility"]);
const statuses = new Set(["native", "external", "port", "archived"]);
const errors = [];

let registry;
try {
  registry = JSON.parse(readFileSync(path, "utf8"));
} catch (error) {
  console.error(`registry: cannot parse ${path}: ${error.message}`);
  process.exit(1);
}

if (registry.schema !== 1) errors.push(`schema must be 1, found ${registry.schema}`);
if (!Array.isArray(registry.plugins) || registry.plugins.length === 0)
  errors.push("plugins must be a non-empty array");

const seen = new Set();

for (const [index, plugin] of (registry.plugins ?? []).entries()) {
  const where = `plugins[${index}]`;

  for (const key of ["id", "name", "source"]) {
    if (typeof plugin?.[key] !== "string" || plugin[key].trim() === "")
      errors.push(`${where}.${key} must be a non-empty string`);
  }

  if (typeof plugin?.id === "string") {
    if (!/^[a-z0-9-]+$/.test(plugin.id)) errors.push(`${where}.id must be lowercase kebab-case`);
    if (seen.has(plugin.id)) errors.push(`${where}.id "${plugin.id}" is duplicated`);
    seen.add(plugin.id);
  }

  if (!kinds.has(plugin?.kind)) errors.push(`${where}.kind "${plugin?.kind}" is not one of ${[...kinds].join(", ")}`);
  if (!statuses.has(plugin?.status)) errors.push(`${where}.status "${plugin?.status}" is not one of ${[...statuses].join(", ")}`);
}

const count = (status) => (registry.plugins ?? []).filter((p) => p.status === status).length;

if (errors.length > 0) {
  for (const error of errors) console.error(`registry: ${error}`);
  process.exit(1);
}

console.log(
  `registry ok: ${registry.plugins.length} plugins ` +
    `(${count("native")} native, ${count("external")} external, ${count("port")} to port, ${count("archived")} archived)`
);
