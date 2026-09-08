// Duplicate file-local definition audit.
//
// No shebang: this file is run by `node` from npm scripts and re-imported by
// vitest, whose loader chokes on the shebang line as "Invalid or unexpected
// token" and takes the whole test file down with it. Invoke as
// `node scripts/audit-unity-collisions.mjs` (already the `audit:unity` shape).
//
// UBT compiles a module as a unity build: several .cpp files are concatenated
// into one translation unit. An anonymous namespace is per translation unit, so
// two files can each define a file-local helper of the same name and both
// compile in isolation - right up until the grouping puts them in the same
// blob, and the two anonymous namespaces merge into one. Then the second
// definition is a redefinition: error C2084, "function already has a body".
//
// The grouping is not stable. It shifts with file count, file order, and the
// adaptive-unity working set, which UBT derives from `git status`. That is why
// a duplicate can sit in a release for weeks, build clean on the machine that
// wrote it, and break on a user's first build of the same source.
//
// Two shapes of hazard are reported, and both are things the compiler accepts
// one file at a time and rejects once the blob merges them:
//
//   redefinition  Two internal-linkage definitions that land in the same
//                 declarative region of the merged unit. An anonymous namespace
//                 in each of two files is one region once merged; so is the
//                 global namespace for two file-scope `static` functions.
//                 C2084.
//
//   ambiguity     Two definitions that stay separate entities but that
//                 unqualified lookup finds together. A file-scope `static` in
//                 one file and a same-signature function in a named namespace
//                 that another file opens with `using namespace N;` both land
//                 in the global namespace for lookup, and the call in the
//                 second file no longer resolves. C2668.
//
// Overloads are fine (that is what C++ does with them), so the signature, not
// just the name, is the key.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, '..');

/** Strip comments and string/char literals so braces inside them cannot fool the scan. */
export function stripNonCode(source) {
  return source
    .replace(/\\["']/g, '__')
    .replace(/"(?:[^"\\\n]|\\.)*"/g, '""')
    .replace(/'(?:[^'\\\n]|\\.)*'/g, "''")
    .replace(/\/\*[\s\S]*?\*\//g, ' ')
    .replace(/\/\/[^\n]*/g, ' ');
}

/** Split a parameter list on top-level commas, ignoring those inside <> ( ) [ ]. */
function splitParams(list) {
  const out = [];
  let depth = 0;
  let buf = '';
  for (const ch of list) {
    if (ch === '<' || ch === '(' || ch === '[') depth++;
    else if (ch === '>' || ch === ')' || ch === ']') depth--;
    if (ch === ',' && depth === 0) {
      out.push(buf);
      buf = '';
      continue;
    }
    buf += ch;
  }
  if (buf.trim()) out.push(buf);
  return out;
}

/** "const FString& Path = X" -> "const FString&". The parameter name never
 *  participates in overload resolution, so it must not participate here. */
function normalizeParamType(param) {
  let p = param.split('=')[0];
  p = p.replace(/([*&])/g, ' $1 ');
  const tokens = p.split(/\s+/).filter(Boolean);
  if (tokens.length > 1 && /^[A-Za-z_]\w*$/.test(tokens[tokens.length - 1])) {
    const prior = tokens[tokens.length - 2];
    // Drop the trailing token only when it is a name, not the type itself.
    if (!['const', 'volatile', 'struct', 'class', 'unsigned', 'signed', 'long', 'short'].includes(prior)) {
      tokens.pop();
    }
  }
  return tokens.join(' ');
}

const CONTROL_FLOW = ['if', 'for', 'while', 'switch', 'catch', 'return', 'else', 'do'];

/**
 * Read one namespace-scope declaration and decide whether it is a function
 * definition. `decl` is everything since the last `;` or `}`, whitespace
 * collapsed, with the opening `{` not yet consumed.
 */
function parseDefinition(decl) {
  // A definition, not a call: `Name(params)` optionally trailed by
  // const/noexcept/override, and not a control-flow keyword. A qualified name
  // (`FFoo::Bar`) never matches, which is what keeps out-of-line member
  // definitions - external linkage, no unity hazard - out of the report.
  const m = decl.match(/(?:^|[\s*&>])([A-Za-z_]\w*)\s*\(([^()]*)\)\s*(?:const\s*)?(?:noexcept\s*)?$/);
  if (!m || CONTROL_FLOW.includes(m[1])) return null;
  // A leading return type is what separates a definition from a
  // constructor-style expression at namespace scope.
  const head = decl.slice(0, decl.lastIndexOf(m[1])).trim();
  if (head.length === 0) return null;
  const params = splitParams(m[2]).map(normalizeParamType).filter((p) => p && p !== 'void');
  return {
    name: m[1],
    signature: `${m[1]}(${params.join(', ')})`,
    isStatic: /(?:^|\s)static(?:\s|$)/.test(head),
  };
}

/**
 * Every function definition at namespace scope, tagged with the namespace it
 * sits in. Bodies, classes, structs and initialiser lists are skipped whole:
 * only the outermost declarative regions of a file can collide under unity.
 */
export function namespaceScopeFunctions(source) {
  const found = [];
  const stack = [];
  let buf = '';
  const atNamespaceScope = () => stack.every((s) => s.kind === 'ns');

  for (let i = 0; i < source.length; i++) {
    const ch = source[i];
    if (ch === '{') {
      if (!atNamespaceScope()) {
        stack.push({ kind: 'other' });
        continue;
      }
      const decl = buf.replace(/\s+/g, ' ').trim();
      buf = '';
      const named = decl.match(/(?:^|\s)namespace\s+([A-Za-z_][\w:]*)$/);
      if (named) {
        stack.push({ kind: 'ns', name: named[1] });
        continue;
      }
      if (/(?:^|\s)?namespace$/.test(decl)) {
        stack.push({ kind: 'ns', name: null });
        continue;
      }
      const fn = parseDefinition(decl);
      if (fn) {
        const namespaces = stack.filter((s) => s.kind === 'ns').map((s) => s.name);
        found.push({ ...fn, namespaces });
      }
      stack.push({ kind: 'other' });
      continue;
    }
    if (ch === '}') {
      stack.pop();
      if (atNamespaceScope()) buf = '';
      continue;
    }
    if (atNamespaceScope()) {
      if (ch === ';') buf = '';
      else buf += ch;
    }
  }
  return found;
}

/** Every `using namespace N;` in a file, at any scope. A directive inside a
 *  function body still injects N into the global namespace for that body. */
function usingDirectives(source) {
  const out = new Set();
  const re = /\busing\s+namespace\s+([A-Za-z_][\w:]*)\s*;/g;
  let m;
  while ((m = re.exec(source)) !== null) out.add(m[1]);
  return out;
}

/** Modules are the unity boundary: `.../Source/<Module>/...`. */
function moduleOf(file) {
  const parts = file.split(/[\\/]/);
  const idx = parts.lastIndexOf('Source');
  return idx >= 0 && parts[idx + 1] ? parts[idx + 1] : path.dirname(file);
}

function collectSources(root) {
  const out = [];
  if (!fs.existsSync(root)) return out;
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const full = path.join(root, entry.name);
    if (entry.isDirectory()) out.push(...collectSources(full));
    else if (entry.isFile() && entry.name.endsWith('.cpp')) out.push(full);
  }
  return out;
}

/**
 * Where unqualified lookup finds the entity. An anonymous namespace member is
 * reachable from the namespace enclosing it, so the anonymous components drop
 * out; the surviving named components are the scope.
 */
function lookupScope(namespaces) {
  return namespaces.filter((n) => n !== null).join('::');
}

/** The declarative region the definition actually occupies, anonymous
 *  namespaces included. Two files that agree on this are redefining. */
function declarativeRegion(namespaces) {
  return namespaces.map((n) => n ?? '(anonymous)').join('::');
}

/**
 * Internal linkage is what makes a definition legal in two files at once, and
 * therefore what makes it a unity hazard: an anonymous-namespace member, or
 * anything declared `static` at namespace scope.
 */
function hasInternalLinkage(fn) {
  return fn.isStatic || fn.namespaces.includes(null);
}

export function findUnityCollisions(root = path.join(repoRoot, 'plugin')) {
  const byModule = new Map();
  for (const file of collectSources(root)) {
    const source = stripNonCode(fs.readFileSync(file, 'utf8'));
    const mod = moduleOf(file);
    if (!byModule.has(mod)) byModule.set(mod, { functions: [], usings: new Map() });
    const bucket = byModule.get(mod);
    const rel = path.relative(repoRoot, file);
    for (const fn of namespaceScopeFunctions(source)) bucket.functions.push({ ...fn, file: rel });
    for (const ns of usingDirectives(source)) {
      if (!bucket.usings.has(ns)) bucket.usings.set(ns, new Set());
      bucket.usings.get(ns).add(rel);
    }
  }

  const collisions = [];
  for (const [mod, bucket] of byModule) {
    const local = bucket.functions.filter(hasInternalLinkage);

    // Hazard 1: two internal-linkage definitions that unqualified lookup finds
    // in the same scope of the merged unit. Same declarative region means the
    // second is a redefinition; different regions (a file-scope static against
    // an anonymous-namespace member) means every unqualified call is ambiguous.
    const byScope = new Map();
    for (const fn of local) {
      const key = `${lookupScope(fn.namespaces)}|${fn.signature}`;
      if (!byScope.has(key)) byScope.set(key, []);
      byScope.get(key).push(fn);
    }
    for (const group of byScope.values()) {
      const files = [...new Set(group.map((f) => f.file))].sort();
      if (files.length < 2) continue;
      const regions = new Set(group.map((f) => declarativeRegion(f.namespaces)));
      const scope = lookupScope(group[0].namespaces);
      collisions.push({
        module: mod,
        kind: regions.size === 1 ? 'redefinition' : 'ambiguity',
        signature: scope ? `${scope}::${group[0].signature}` : group[0].signature,
        files,
        detail:
          regions.size === 1
            ? `defined twice in ${declarativeRegion(group[0].namespaces) || 'the global namespace'} once the blob merges`
            : `${[...regions].map((r) => r || 'the global namespace').join(' and ')} both reach unqualified lookup`,
      });
    }

    // Hazard 2: a named namespace opened with `using namespace N;` injects its
    // names into the global namespace at the call site, where they meet the
    // file-scope statics and anonymous-namespace helpers of every other file in
    // the blob. Same signature there is C2668, and no file fails on its own.
    const globalLocals = new Map();
    for (const fn of local) {
      if (lookupScope(fn.namespaces) !== '') continue;
      if (!globalLocals.has(fn.signature)) globalLocals.set(fn.signature, new Set());
      globalLocals.get(fn.signature).add(fn.file);
    }
    for (const fn of bucket.functions) {
      const scope = lookupScope(fn.namespaces);
      if (scope === '') continue;
      const openers = bucket.usings.get(scope);
      if (!openers) continue;
      const clashing = globalLocals.get(fn.signature);
      if (!clashing) continue;
      for (const globalFile of clashing) {
        // Same file already fails to compile alone, so it is not this audit's
        // class. Only the pair that survives a solo build is reported.
        if (globalFile === fn.file) continue;
        const users = [...openers].filter((u) => u !== globalFile);
        if (users.length === 0) continue;
        collisions.push({
          module: mod,
          kind: 'ambiguity',
          signature: `${scope}::${fn.signature}`,
          files: [...new Set([fn.file, globalFile, ...users])].sort(),
          detail: `\`using namespace ${scope};\` in ${users.join(', ')} puts it beside the global-scope ${fn.signature} from ${globalFile}`,
        });
      }
    }
  }
  return collisions.sort(
    (a, b) => a.signature.localeCompare(b.signature) || a.kind.localeCompare(b.kind)
  );
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const collisions = findUnityCollisions();
  if (collisions.length === 0) {
    console.log('audit:unity-collisions - no duplicate file-local definitions');
    process.exit(0);
  }
  console.error(`audit:unity-collisions - ${collisions.length} unity hazard(s):\n`);
  for (const c of collisions) {
    console.error(`  [${c.module}] ${c.kind}: ${c.signature}`);
    console.error(`      ${c.detail}`);
    for (const f of c.files) console.error(`      ${f}`);
    console.error('');
  }
  console.error('Each of these compiles alone and fails when unity puts the files in one blob.');
  console.error('Hoist the helper into a shared header, or give the copies distinct signatures.');
  process.exit(1);
}
