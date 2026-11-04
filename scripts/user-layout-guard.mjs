/**
 * Keeps a test run's tabs out of the developer's editor layout.
 *
 * Every editor on the machine reads and writes one per-user layout:
 * UnrealEngine/Editor/EditorLayout.json, shared by every project and engine
 * version, and UnrealEngine/<version>/Saved/Config/WindowsEditor/EditorLayout.ini.
 * A test that opens Project Settings, or an import that summons a Content
 * Browser, leaves that tab saved as open, and the developer's next editor
 * restores it at startup. The runner reads the layouts before its first editor
 * starts and, once its editors are gone, closes every tab they left open that
 * was closed before. Nothing else in the files is changed.
 */
import fs from "node:fs";
import path from "node:path";

const TAB = /("TabId":\s*"([^"]*)",\s*"TabState":\s*")OpenedTab"/g;

/** The per-user layout files that exist under LOCALAPPDATA/UnrealEngine. */
export function userLayoutFiles(root = process.env.LOCALAPPDATA ? path.join(process.env.LOCALAPPDATA, "UnrealEngine") : null) {
  if (!root || !fs.existsSync(root)) return [];
  const files = [path.join(root, "Editor", "EditorLayout.json")];
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    if (entry.isDirectory()) files.push(path.join(root, entry.name, "Saved", "Config", "WindowsEditor", "EditorLayout.ini"));
  }
  return files.filter((f) => fs.existsSync(f));
}

/** Layout name to the ids of its open tabs. Null when the text is not a layout file this reads. */
export function openTabs(file, text) {
  const out = new Map();
  const collect = (name, body) => out.set(name, new Set([...body.matchAll(TAB)].map((m) => m[2])));
  if (file.endsWith(".json")) {
    let doc;
    try {
      doc = JSON.parse(text.replace(/^﻿/, ""));
    } catch {
      return null;
    }
    if (!doc || typeof doc !== "object") return null;
    for (const [name, layout] of Object.entries(doc)) {
      if (layout && typeof layout === "object") collect(name, JSON.stringify(layout, null, 1));
    }
    return out;
  }
  for (const line of text.split(/\r?\n/)) {
    const eq = line.indexOf("=(");
    if (eq > 0) collect(line.slice(0, eq), line.slice(eq + 1));
  }
  return out;
}

/**
 * The text with every tab closed that is open now but was not open in the same
 * layout before. Layouts the snapshot did not have are left alone.
 */
export function closeTabsOpenedSince(file, before, now) {
  const was = openTabs(file, before);
  const closed = [];
  if (!was) return { text: now, closed };
  const flip = (name, body) => body.replace(TAB, (whole, head, id) => {
    const open = was.get(name);
    if (!open || open.has(id)) return whole;
    closed.push(`${name}:${id}`);
    return `${head}ClosedTab"`;
  });
  if (file.endsWith(".json")) {
    let doc;
    try {
      doc = JSON.parse(now.replace(/^﻿/, ""));
    } catch {
      return { text: now, closed };
    }
    if (!doc || typeof doc !== "object") return { text: now, closed };
    // The text is edited in place, so the editor's own formatting survives: the
    // n-th tab entry in the text is the n-th in document order.
    const owners = Object.entries(doc).flatMap(([name, layout]) =>
      layout && typeof layout === "object" ? [...JSON.stringify(layout).matchAll(/"TabId":/g)].map(() => name) : []);
    if (owners.length !== [...now.matchAll(/"TabId":/g)].length) return { text: now, closed };
    let n = 0;
    const text = now.replace(/"TabId":\s*"[^"]*",\s*"TabState":\s*"[A-Za-z]+"|"TabId":/g, (match) => flip(owners[n++], match));
    return { text, closed };
  }
  const text = now.split(/(\r?\n)/).map((line) => {
    const eq = line.indexOf("=(");
    return eq > 0 ? line.slice(0, eq + 1) + flip(line.slice(0, eq), line.slice(eq + 1)) : line;
  }).join("");
  return { text, closed };
}

/**
 * Read the layouts now; release() closes the tabs opened since in each file
 * and returns what it closed, one `<file>: <layout>:<tab>` entry per tab.
 */
export function guardUserLayouts(files = userLayoutFiles()) {
  const before = new Map();
  for (const f of files) {
    try {
      before.set(f, fs.readFileSync(f, "utf8"));
    } catch {
      // unreadable now: nothing to compare against later
    }
  }
  return {
    release() {
      const report = [];
      for (const [f, was] of before) {
        let now;
        try {
          now = fs.readFileSync(f, "utf8");
        } catch {
          continue;
        }
        if (now === was) continue;
        const { text, closed } = closeTabsOpenedSince(f, was, now);
        if (closed.length === 0) continue;
        fs.writeFileSync(f, text);
        report.push(...closed.map((c) => `${f}: ${c}`));
      }
      return report;
    },
  };
}
