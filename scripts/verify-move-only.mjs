#!/usr/bin/env node
/*
    Move-only guard for the monolith decomposition phase.

    Proves that a refactor step RELOCATED function bodies instead of editing
    them. The baseline is read from git, so the check still works after the
    source file it came from has been gutted.

    Two modes:

      move       every function body that existed at --base still exists today,
                 byte-identical once whitespace is removed, somewhere under
                 app/Source/. This is the check for a pure translation-unit
                 split.

      decompose  every statement of one function that existed at --base appears
                 today, in the same relative order, across the new files. This
                 is the check for a function that was legitimately cut into
                 smaller ones (renderBlock).

    Usage:
      node scripts/verify-move-only.mjs --base <ref>
      node scripts/verify-move-only.mjs --base <ref> --paths app/Source/ArrangeView.cpp
      node scripts/verify-move-only.mjs --base <ref> --preserve-order renderBlock
*/

import { execFileSync } from 'node:child_process';
import { readdirSync, readFileSync } from 'node:fs';

const DEFAULT_PATHS = [
    'app/Source/AudioEngine.cpp',
    'app/Source/ArrangeView.cpp',
    'app/Source/MainComponent.cpp',
];

const argv = process.argv.slice(2);

function valueAfter (name)
{
    const i = argv.indexOf(name);
    return i >= 0 ? argv[i + 1] : null;
}

const base = valueAfter('--base');
const preserve = valueAfter('--preserve-order');

if (! base)
{
    console.error('usage: verify-move-only.mjs --base <ref> [--paths a b ...] [--preserve-order Name]');
    process.exit(2);
}

const paths = (() =>
{
    const i = argv.indexOf('--paths');
    if (i < 0)
        return DEFAULT_PATHS;

    const out = [];
    for (let j = i + 1; j < argv.length && ! argv[j].startsWith('--'); ++j)
        out.push(argv[j]);

    return out.length > 0 ? out : DEFAULT_PATHS;
})();

const normalise = (s) => s.replace(/\s+/g, '');

/* ---------------------------------------------------------------- parsing */

/* A scanner that reports every brace block whose preceding text looks like a
   function header. Blocks are tracked with a stack rather than a single depth
   counter because these files wrap everything in `namespace nc {`: functions
   live at depth 1 and 2, never at depth 0. */
function extractDefinitions (text)
{
    const definitions = [];
    const frames = [];
    const boundary = [0];        // index just past the last ; or block at each depth
    let depth = 0;
    let inString = false;
    let inChar = false;
    let inLineComment = false;
    let inBlockComment = false;

    for (let i = 0; i < text.length; ++i)
    {
        const c = text[i];
        const next = text[i + 1];

        if (inLineComment)
        {
            if (c === '\n') inLineComment = false;
            continue;
        }
        if (inBlockComment)
        {
            if (c === '*' && next === '/') { inBlockComment = false; ++i; }
            continue;
        }
        if (inString)
        {
            if (c === '\\') { ++i; continue; }
            if (c === '"') inString = false;
            continue;
        }
        if (inChar)
        {
            if (c === '\\') { ++i; continue; }
            if (c === '\'') inChar = false;
            continue;
        }

        if (c === '/' && next === '/') { inLineComment = true; ++i; continue; }
        if (c === '/' && next === '*') { inBlockComment = true; ++i; continue; }
        if (c === '"') { inString = true; continue; }
        if (c === '\'') { inChar = true; continue; }

        if (c === '{')
        {
            frames.push({ sigStart: boundary[depth] ?? 0, bodyStart: i });
            ++depth;
            boundary[depth] = i + 1;
            continue;
        }

        if (c === '}')
        {
            const frame = frames.pop();
            --depth;

            if (frame !== undefined)
            {
                const name = signatureOf(text.slice(frame.sigStart, frame.bodyStart));

                if (name !== null)
                    definitions.push({ name, body: text.slice(frame.bodyStart, i + 1) });
            }

            boundary[depth] = i + 1;
            continue;
        }

        if (c === ';')
            boundary[depth] = i + 1;
    }

    return definitions;
}

/** Returns the collapsed signature when the text before a `{` is a function
    header, or null for namespaces, types, control flow, aggregates and
    lambdas. */
/** Comments carry prose like "persistence (JUCE ApplicationProperties)" which
    would otherwise be parsed as a function name. No signature here contains a
    string literal, so a plain text strip is safe. */
function stripComments (s)
{
    return s.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ');
}

function signatureOf (raw)
{
    const signature = stripComments(raw).replace(/\s+/g, ' ').trim();

    if (signature.length === 0)
        return null;
    if (/\b(namespace|class|struct|union|enum|extern)\b/.test(signature))
        return null;
    if (/^(if|for|while|switch|catch|do|else|try)\b/.test(signature))
        return null;
    if (signature.includes('[') || signature.includes('='))
        return null;
    if (! signature.includes('('))
        return null;
    if (! /\)\s*(const)?\s*(noexcept)?\s*(override)?\s*$/.test(signature))
        return null;

    return signature;
}

/** Splits a function body into top-level statements: each ends at a `;` or a
    complete `{ ... }` block. Semicolons inside parentheses (for-headers) and
    inside comments or strings do not split. */
function statementsOf (body)
{
    const out = [];
    let depth = 0;
    let paren = 0;
    let start = 1;               // skip the opening brace
    let inString = false;
    let inChar = false;
    let inLineComment = false;
    let inBlockComment = false;

    for (let i = 1; i < body.length; ++i)
    {
        const c = body[i];
        const next = body[i + 1];

        if (inLineComment)
        {
            if (c === '\n') inLineComment = false;
            continue;
        }
        if (inBlockComment)
        {
            if (c === '*' && next === '/') { inBlockComment = false; ++i; }
            continue;
        }
        if (inString)
        {
            if (c === '\\') { ++i; continue; }
            if (c === '"') inString = false;
            continue;
        }
        if (inChar)
        {
            if (c === '\\') { ++i; continue; }
            if (c === '\'') inChar = false;
            continue;
        }

        if (c === '/' && next === '/') { inLineComment = true; ++i; continue; }
        if (c === '/' && next === '*') { inBlockComment = true; ++i; continue; }
        if (c === '"') { inString = true; continue; }
        if (c === '\'') { inChar = true; continue; }

        if (c === '(') { ++paren; continue; }
        if (c === ')') { --paren; continue; }

        if (c === '{') { ++depth; continue; }

        if (c === '}')
        {
            --depth;

            if (depth === 0)
            {
                const statement = normalise(body.slice(start, i + 1));
                if (statement.length > 0) out.push(statement);
                start = i + 1;
            }
            continue;
        }

        if (c === ';' && depth === 0 && paren === 0)
        {
            const statement = normalise(body.slice(start, i + 1));
            if (statement.length > 0) out.push(statement);
            start = i + 1;
        }
    }

    return out;
}

/* ------------------------------------------------------------------ input */

function readAt (ref, path)
{
    try
    {
        return execFileSync('git', ['show', `${ref}:${path}`], { encoding: 'utf8' });
    }
    catch
    {
        return null;
    }
}

function workingTreeText ()
{
    const dir = 'app/Source';
    return readdirSync(dir)
        .filter((name) => name.endsWith('.cpp') || name.endsWith('.h'))
        .map((name) => readFileSync(`${dir}/${name}`, 'utf8'))
        .join('\n');
}

let baseText = '';

for (const path of paths)
{
    const text = readAt(base, path);

    if (text === null)
    {
        console.error(`base ref ${base} has no ${path}`);
        process.exit(2);
    }

    baseText += text + '\n';
}

const current = normalise(workingTreeText());
const baseDefinitions = extractDefinitions(baseText);

if (baseDefinitions.length === 0)
{
    console.error(`move-only: parsed no definitions out of ${paths.join(', ')} at ${base}`);
    process.exit(2);
}

/* ------------------------------------------------------------------ modes */

if (preserve)
{
    const target = baseDefinitions.find((d) => d.name.includes(preserve));

    if (! target)
    {
        console.error(`decompose: no base definition matching ${preserve}`);
        process.exit(2);
    }

    const statements = statementsOf(target.body);
    let cursor = 0;
    let matched = 0;

    for (const statement of statements)
    {
        const at = current.indexOf(statement, cursor);

        if (at < 0)
        {
            console.error(`decompose: statement ${matched + 1}/${statements.length} missing or out of order:`);
            console.error(`  ${statement.slice(0, 160)}`);
            console.error(`decompose: ${matched}/${statements.length} statements in order`);
            process.exit(1);
        }

        cursor = at + statement.length;
        ++matched;
    }

    console.log(`decompose: ${preserve}, ${matched}/${statements.length} statements in order`);
    process.exit(0);
}

const missing = [];

for (const definition of baseDefinitions)
{
    if (! current.includes(normalise(definition.body)))
        missing.push(definition);
}

for (const definition of missing)
{
    console.error(`move: body changed or lost: ${definition.name}`);
    console.error(`  ${normalise(definition.body).slice(0, 160)}`);
}

console.log(`move-only: ${baseDefinitions.length} definitions checked, ${missing.length} missing`);
process.exit(missing.length === 0 ? 0 : 1);
