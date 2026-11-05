/**
 * Edit a .uproject's Plugins array as text, so the file keeps its line
 * endings, indentation and key order. Unreal and source control both diff
 * this file, and a JSON round trip rewrites every line of it.
 */

interface Span {
  start: number;
  end: number;
}

/** Index just past the JSON value that starts at `i` (string, object, array or scalar). */
function skipValue(text: string, i: number): number {
  const c = text[i];
  if (c === '"') {
    for (let j = i + 1; j < text.length; j++) {
      if (text[j] === "\\") j++;
      else if (text[j] === '"') return j + 1;
    }
    throw new Error("unterminated string");
  }
  if (c === "{" || c === "[") {
    let depth = 0;
    for (let j = i; j < text.length; j++) {
      const d = text[j];
      if (d === '"') {
        j = skipValue(text, j) - 1;
      } else if (d === "{" || d === "[") {
        depth++;
      } else if (d === "}" || d === "]") {
        depth--;
        if (depth === 0) return j + 1;
      }
    }
    throw new Error("unbalanced brackets");
  }
  let j = i;
  while (j < text.length && !/[\s,}\]]/.test(text[j])) j++;
  return j;
}

const skipWs = (text: string, i: number): number => {
  while (i < text.length && /\s/.test(text[i])) i++;
  return i;
};

/** The span of the top-level "Plugins" array's brackets, or null when the file has none. */
function pluginsArray(text: string): Span | null {
  let i = skipWs(text, 0);
  if (text[i] !== "{") return null;
  i++;
  while (i < text.length) {
    i = skipWs(text, i);
    if (text[i] === "}") return null;
    if (text[i] === ",") {
      i++;
      continue;
    }
    const keyEnd = skipValue(text, i);
    const key = JSON.parse(text.slice(i, keyEnd)) as string;
    i = skipWs(text, keyEnd);
    if (text[i] !== ":") return null;
    i = skipWs(text, i + 1);
    const valueEnd = skipValue(text, i);
    if (key === "Plugins" && text[i] === "[") return { start: i, end: valueEnd };
    i = valueEnd;
  }
  return null;
}

/** The spans of an array's elements, given the span of its brackets. */
function elements(text: string, array: Span): Span[] {
  const out: Span[] = [];
  let i = array.start + 1;
  while (i < array.end - 1) {
    i = skipWs(text, i);
    if (text[i] === "]") break;
    if (text[i] === ",") {
      i++;
      continue;
    }
    const end = skipValue(text, i);
    out.push({ start: i, end });
    i = end;
  }
  return out;
}

/** Whether the .uproject text lists a plugin by this name. */
export function uprojectListsPlugin(text: string, name: string): boolean {
  const parsed = JSON.parse(text) as { Plugins?: Array<{ Name?: string }> };
  return Array.isArray(parsed.Plugins) && parsed.Plugins.some((p) => p?.Name === name);
}

/**
 * Remove every Plugins entry named `name`. Returns the new text and how many
 * entries went; the text is unchanged byte for byte when none matched.
 */
export function removeUProjectPluginEntry(text: string, name: string): { text: string; removed: number } {
  JSON.parse(text); // refuse to edit a file that is not valid JSON
  let out = text;
  let removed = 0;
  for (;;) {
    const array = pluginsArray(out);
    if (!array) break;
    const items = elements(out, array);
    const at = items.findIndex((e) => {
      try {
        return (JSON.parse(out.slice(e.start, e.end)) as { Name?: unknown })?.Name === name;
      } catch {
        return false;
      }
    });
    if (at < 0) break;
    let cut: Span;
    if (items.length === 1) cut = { start: array.start + 1, end: array.end - 1 };
    else if (at > 0) cut = { start: items[at - 1].end, end: items[at].end };
    else cut = { start: items[0].start, end: items[1].start };
    out = out.slice(0, cut.start) + out.slice(cut.end);
    removed++;
  }
  if (removed > 0) JSON.parse(out);
  return { text: out, removed };
}
