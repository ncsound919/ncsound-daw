import js from "@eslint/js";
import globals from "globals";
import reactHooks from "eslint-plugin-react-hooks";
import reactRefresh from "eslint-plugin-react-refresh";
import tseslint from "typescript-eslint";
import prettier from "eslint-config-prettier";

// Formatting is Prettier's job; eslint-config-prettier turns off every rule
// that would fight it, so lint here is about correctness only.
export default tseslint.config(
  { ignores: ["dist", "node_modules", "scripts", "coverage"] },
  {
    files: ["**/*.{ts,tsx}"],
    extends: [js.configs.recommended, ...tseslint.configs.recommended],
    languageOptions: {
      ecmaVersion: 2022,
      globals: { ...globals.browser, ...globals.node },
    },
    plugins: { "react-hooks": reactHooks, "react-refresh": reactRefresh },
    rules: {
      ...reactHooks.configs.recommended.rules,
      "react-refresh/only-export-components": ["warn", { allowConstantExport: true }],
      "@typescript-eslint/no-unused-vars": [
        "error",
        { argsIgnorePattern: "^_", varsIgnorePattern: "^_" },
      ],
    },
  },
  {
    // Canvas polyline idiom: `x ? lineTo(x, y) : moveTo(x, y)`. A ternary used
    // for its side effect is what this rule targets, but rewriting twelve dense
    // hot-loop lines into if/else costs more readability than it buys.
    files: ["src/console-ui/renderers/**/*.ts"],
    rules: { "@typescript-eslint/no-unused-expressions": "off" },
  },
  prettier,
);
