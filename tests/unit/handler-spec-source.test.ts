/**
 * A C++ spec edit that was never re-recorded fails here, without an editor.
 *
 * tests/golden/handler-specs.json can only be recorded from a running editor,
 * and CI has no Unreal install, so before this a changed spec reached the TS
 * surface only when somebody re-recorded by hand, and CI could not tell that
 * nobody had. The recorder stores a fingerprint of the spec source beside the
 * recording (scripts/lib/spec-source.mjs); this compares it with the source.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { describe, expect, it } from "vitest";
import {
  readSpecSource,
  specSourceDrift,
  specSourceFingerprint,
} from "../../scripts/lib/spec-source.mjs";

describe("the handler spec recording", () => {
  it("was recorded from the spec source as it stands", () => {
    const drift = specSourceDrift(readSpecSource(), specSourceFingerprint());
    const shown = drift.slice(0, 20);
    expect(
      drift,
      [
        "The C++ parameter specs changed since tests/golden/handler-specs.json was recorded:",
        ...shown.map((l) => `  ${l}`),
        ...(drift.length > shown.length ? [`  and ${drift.length - shown.length} more`] : []),
        "Re-record them: node scripts/deploy.mjs && npm run build, open the editor on tests/ue_mcp",
        "(npm run up), then npm run generate -- --editor, which records the specs and regenerates",
        "everything after them. CI has no Unreal install, so it cannot record them for you.",
      ].join("\n"),
    ).toEqual([]);
  });

  it("ignores whitespace, comments, timeouts and file moves, and sees a changed description", () => {
    const shared = `const FMCPParamSpec PathParam = MCPParam::Required(TEXT("path"), EType::String, TEXT("Asset path"));\n`;
    const reg = (desc: string) =>
      `Registry.RegisterHandler(TEXT("do_thing"), &DoThing, {\n\tPathParam,\n\tMCPParam::Optional(TEXT("n"), EType::Integer, TEXT("${desc}")),\n});\n`;
    const plain = `Registry.RegisterHandlerWithTimeout(TEXT("slow"), &Slow, 120.f);\n`;
    const tree = (files: Record<string, string>) => {
      const dir = fs.mkdtempSync(path.join(os.tmpdir(), "spec-source-"));
      for (const [rel, text] of Object.entries(files)) {
        fs.mkdirSync(path.dirname(path.join(dir, rel)), { recursive: true });
        fs.writeFileSync(path.join(dir, rel), text);
      }
      return specSourceFingerprint(dir);
    };

    const base = tree({ "A/A.cpp": shared + reg("Count") + plain });
    expect(Object.keys(base.handlers)).toEqual(["do_thing"]);
    const reformatted = tree({ "A/A.cpp": `// note\n${shared}\r\n` + reg("Count").replace(/\t/g, "    ") + plain });
    expect(specSourceDrift(base, reformatted)).toEqual([]);
    const moved = tree({ "B/B.cpp": reg("Count"), "A/Shared.h": shared });
    expect(specSourceDrift(base, moved)).toEqual([]);
    const edited = tree({ "A/A.cpp": shared + reg("How many") + plain });
    expect(specSourceDrift(base, edited)).toEqual(["do_thing: its spec arguments changed"]);
    const sharedEdited = tree({ "A/A.cpp": shared.replace("Asset path", "Path") + reg("Count") });
    expect(specSourceDrift(base, sharedEdited)).toEqual(["a shared parameter or spec helper line changed"]);
  });
});
