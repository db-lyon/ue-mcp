// Engine-version gate audit.
//
// No shebang, for the same reason as audit-unity-collisions.mjs: vitest
// re-imports this file. Invoke as `node scripts/audit-engine-gates.mjs`
// (the `audit:engine-gates` script).
//
// Public/HandlerEngineVersion.h is the one owner of engine-version gating. A
// gate written anywhere else is how the plugin came to have five spellings of
// "is this 5.5 or newer", feature macros #defined in .cpp files that leaked
// across the unity blob, and `MAJOR == 5` tests that silently pick the old path
// on a 6.x engine. This reports each of those shapes:
//
//   raw-version     ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION,
//                   UE_VERSION_OLDER_THAN or UE_VERSION_NEWER_THAN read outside
//                   the header. Use a UE_MCP_HAS_* gate.
//   cpp-define      A UE_MCP_* / MCP_HAS_* / WITH_* macro #defined in a .cpp.
//                   It leaks into the files after it in the unity blob.
//   floor-gate      UE_MCP_HAS_5_4_API: the oldest supported engine needs no gate.
//   major-equals    A Build.cs version test of the form `MajorVersion == N &&
//                   MinorVersion >= M`, which is false on a later major version.
//   editor-guard    A preprocessor test of WITH_EDITOR or WITH_EDITORONLY_DATA
//                   in an Editor-type module, where both are always 1 and the
//                   #else arm is dead code. Runtime-capable modules are listed
//                   in RUNTIME_MODULES and keep their guards.
//
// A file that genuinely cannot include the header is listed in EXEMPT with the
// reason, so the exception is visible rather than a silent hole.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const DEFAULT_SOURCE = path.join(REPO_ROOT, 'plugin', 'ue_mcp_bridge', 'Source');

/** The header that owns every gate, relative to the Source root. */
export const OWNER = 'UE_MCP_Bridge/Public/HandlerEngineVersion.h';

/** Files allowed to read the engine version directly, with the reason. */
export const EXEMPT = new Map([
  [
    'UE_MCP_BridgeStatus/Private/MCPEngineStatus.cpp',
    'loads at PostConfigInit and cannot depend on the bridge module; its one copy of UE_MCP_HAS_5_8_API is held to the header by tests/unit/engine-gates.test.ts',
  ],
]);

/** Modules that also build for runtime targets, where WITH_EDITOR is a real switch. */
export const RUNTIME_MODULES = new Set(['UE_MCP_BridgeRuntime']);

const EDITOR_GUARD = /^\s*#\s*(?:el)?if(?:n?def)?\b.*\bWITH_EDITOR(?:ONLY_DATA)?\b/;
const RAW = /\b(ENGINE_MAJOR_VERSION|ENGINE_MINOR_VERSION|UE_VERSION_OLDER_THAN|UE_VERSION_NEWER_THAN)\b/;
const CPP_DEFINE = /^\s*#\s*define\s+((?:UE_MCP_|MCP_HAS_|WITH_)\w*)/;
const FLOOR = /\bUE_MCP_HAS_5_4_API\b/;
const MAJOR_EQUALS = /MajorVersion\s*==\s*(\d+)\s*&&\s*Target\.Version\.MinorVersion\s*>=/;

function walk(dir, out = []) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full, out);
    else if (/\.(h|cpp|cs)$/.test(entry.name)) out.push(full);
  }
  return out;
}

/** Strip // comments so a comment that names a macro is not a use of it. */
function code(line) {
  const at = line.indexOf('//');
  return at >= 0 ? line.slice(0, at) : line;
}

/**
 * Every gate written outside the owner header, as
 * { kind, file, line, text }, file relative to the Source root.
 */
export function findEngineGateViolations(sourceRoot = DEFAULT_SOURCE) {
  const violations = [];
  for (const file of walk(sourceRoot)) {
    const rel = path.relative(sourceRoot, file).split(path.sep).join('/');
    if (rel === OWNER) continue;
    const exempt = EXEMPT.has(rel);
    const isCs = file.endsWith('.cs');
    fs.readFileSync(file, 'utf8')
      .split(/\r?\n/)
      .forEach((raw, index) => {
        const line = code(raw);
        const report = (kind) => violations.push({ kind, file: rel, line: index + 1, text: raw.trim() });
        if (isCs) {
          const m = MAJOR_EQUALS.exec(line);
          if (m && !new RegExp(`MajorVersion\\s*>\\s*${m[1]}\\s*\\|\\|`).test(line)) report('major-equals');
          return;
        }
        if (FLOOR.test(line)) report('floor-gate');
        if (!RUNTIME_MODULES.has(rel.split('/')[0]) && EDITOR_GUARD.test(line)) report('editor-guard');
        if (exempt) return;
        if (RAW.test(line)) report('raw-version');
        if (file.endsWith('.cpp') && CPP_DEFINE.test(line)) report('cpp-define');
      });
  }
  return violations.sort((a, b) => a.file.localeCompare(b.file) || a.line - b.line);
}

const ADVICE = {
  'raw-version': 'Gate on a UE_MCP_HAS_* macro from HandlerEngineVersion.h instead.',
  'cpp-define': 'Define the macro in HandlerEngineVersion.h; a #define in a .cpp leaks across the unity blob.',
  'floor-gate': 'The oldest supported engine needs no gate: drop the condition and keep the code.',
  'editor-guard': 'This module only builds for the editor, where the macro is always 1: drop the guard and any #else arm.',
  'major-equals':'Test "at least" the version, `MajorVersion > N || (MajorVersion == N && MinorVersion >= M)`, so a later major keeps the newer path.',
};

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const violations = findEngineGateViolations();
  if (violations.length === 0) {
    console.log('audit:engine-gates - every engine-version gate goes through HandlerEngineVersion.h');
    process.exit(0);
  }
  console.error(`audit:engine-gates - ${violations.length} gate(s) outside HandlerEngineVersion.h:\n`);
  for (const v of violations) {
    console.error(`  ${v.file}:${v.line} [${v.kind}] ${v.text}`);
    console.error(`      ${ADVICE[v.kind]}`);
  }
  process.exit(1);
}
