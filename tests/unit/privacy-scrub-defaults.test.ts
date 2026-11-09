import { afterEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { privacyScrub } from "../../src/feedback/privacy-scrub.js";

/**
 * The privacy pass also covers what a caller cannot be relied on to pass:
 * email addresses, the machine name, and the project the process runs in.
 */
const base = { username: "__nobody_user__", homeDir: "/__nobody_home__" };

describe("privacyScrub without a full context", () => {
  const cwd = process.cwd();
  const temps: string[] = [];
  afterEach(() => {
    process.chdir(cwd);
    for (const d of temps.splice(0)) fs.rmSync(d, { recursive: true, force: true });
  });

  it("redacts email addresses", () => {
    const { text, hits } = privacyScrub("contact jane.doe+ue@studio-name.co.uk or ops@example.com", base);
    expect(text).toBe("contact REDACTED_EMAIL or REDACTED_EMAIL");
    expect(hits.find((h) => h.rule === "email")?.count).toBe(2);
  });

  it("keeps package versions and scoped names, which look like addresses but are not", () => {
    const input = "ue-mcp@1.4.12 with @db-lyon/flowkit@0.3.0 and david@host";
    expect(privacyScrub(input, base).text).toBe(input);
  });

  it("redacts the machine name, from os.hostname() when none is passed", () => {
    const host = os.hostname();
    const { text, hits } = privacyScrub(`crash on ${host} at boot`, base);
    if (host.length >= 3) {
      expect(text).toBe("crash on REDACTED_HOST at boot");
      expect(hits.find((h) => h.rule === "hostname")?.count).toBe(1);
    }
    expect(privacyScrub("built on render-box-07 today", { ...base, hostname: "render-box-07" }).text).toBe(
      "built on REDACTED_HOST today",
    );
  });

  it("derives the project root and name from the working directory when the caller passes none", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-privacy-"));
    temps.push(dir);
    const root = path.join(dir, "SecretGame");
    fs.mkdirSync(root);
    fs.writeFileSync(path.join(root, "SecretGame.uproject"), "{}");
    process.chdir(root);
    const input = `SecretGame failed loading ${path.join(root, "Content", "Hero.uasset")}`;
    const { text } = privacyScrub(input, base);
    expect(text).not.toContain("SecretGame");
    expect(text).toContain("REDACTED_PROJECT failed");
    expect(text).toContain("REDACTED_PROJECT_ROOT");
  });
});
