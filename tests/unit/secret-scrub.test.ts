import { describe, expect, it } from "vitest";
import { scrubSecrets } from "../../src/feedback/secret-scrub.js";

/**
 * Feedback posts to a public tracker. Each credential shape below must come
 * out redacted, and the text around it must survive.
 */
const r = (n: number, chars = "aB3dE5fG7hJ9kL1mN2pQ4rS6tU8vW0xYz") =>
  Array.from({ length: n }, (_, i) => chars[i % chars.length]).join("");

const SAMPLES: Array<[string, string, string]> = [
  ["quoted JSON key", `{"api_key": "${r(24)}"}`, r(24)],
  ["quoted JSON password", `{"password":"${r(12)}"}`, r(12)],
  ["single-quoted key", `{'client_secret': '${r(20)}'}`, r(20)],
  ["Google API key", `key AIza${r(35)} end`, `AIza${r(35)}`],
  ["GitLab token", `glpat-${r(20)}`, `glpat-${r(20)}`],
  ["npm token", `//registry.npmjs.org/:_authToken=npm_${r(36)}`, `npm_${r(36)}`],
  ["SendGrid key", `SG.${r(22)}.${r(43)}`, `SG.${r(22)}.${r(43)}`],
  ["URL credentials", `https://deploy:${r(16)}@git.example.com/repo.git`, r(16)],
  ["OpenAI project key", `sk-proj-${r(40)}_${r(20)}-x`, `sk-proj-${r(40)}`],
  ["Anthropic key", `sk-ant-api03-${r(40)}`, `sk-ant-api03-${r(40)}`],
  ["GitHub fine-grained token", `github_pat_${r(22)}_${r(59)}`, `github_pat_${r(22)}`],
  ["GitHub classic token", `ghp_${r(36)}`, `ghp_${r(36)}`],
  ["Slack token", `xoxb-${r(12)}-${r(12)}`, `xoxb-${r(12)}`],
  ["AWS access key", "AKIAIOSFODNN7EXAMPLE", "AKIAIOSFODNN7EXAMPLE"],
  ["PEM private key", `-----BEGIN PRIVATE KEY-----\n${r(64)}\n-----END PRIVATE KEY-----`, r(64)],
  ["PEM RSA key", `-----BEGIN RSA PRIVATE KEY-----\n${r(64)}\n-----END RSA PRIVATE KEY-----`, r(64)],
  ["JWT", `Bearer eyJ${r(20)}.eyJ${r(30)}.${r(30)}`, `eyJ${r(20)}`],
];

describe("scrubSecrets", () => {
  for (const [name, input, secret] of SAMPLES) {
    it(`redacts a ${name}`, () => {
      const { text, hits } = scrubSecrets(`before ${input} after`);
      expect(text).not.toContain(secret);
      expect(text).toContain("[REDACTED]");
      expect(text.startsWith("before ")).toBe(true);
      expect(text.endsWith(" after")).toBe(true);
      expect(hits.length).toBeGreaterThan(0);
    });
  }

  it("keeps the key name of a quoted JSON secret so a reader sees what was removed", () => {
    expect(scrubSecrets(`{"api_key": "${r(24)}"}`).text).toBe(`{"api_key": "[REDACTED]"}`);
  });

  it("keeps the user and host of a URL and removes only the password", () => {
    expect(scrubSecrets(`https://deploy:${r(16)}@git.example.com/x`).text).toBe("https://deploy:[REDACTED]@git.example.com/x");
  });

  it("leaves what feedback is meant to carry", () => {
    const plain = [
      "ue-mcp@1.4.12 on UE 5.7, blueprint(action=\"create\") failed",
      "https://github.com/db-lyon/ue-mcp/issues/16",
      "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe",
      "/Game/Maps/Main.Main, BP_Hero_C, SG_Main, task-key value",
      "http://127.0.0.1:4100/ui/",
    ].join("\n");
    const { text, hits } = scrubSecrets(plain);
    expect(text).toBe(plain);
    expect(hits).toEqual([]);
  });
});
