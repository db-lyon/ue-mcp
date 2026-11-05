import { describe, it, expect } from "vitest";
import { citedShas, classify, closingComment, prereleaseComment, PRERELEASE_MARKER } from "../../scripts/release-closeout.mjs";

describe("release-closeout classify", () => {
  const inTag = (c: string) => c.startsWith("in");
  it("is released only when every fix commit is in the tag", () => {
    expect(classify(["in1", "in2"], inTag)).toBe("released");
    expect(classify(["in1", "out"], inTag)).toBe("partial");
    expect(classify(["out"], inTag)).toBe("pending");
    expect(classify([], inTag)).toBe("unknown");
  });
});

describe("release-closeout citedShas", () => {
  it("finds commit-like hex and skips words, numbers and issue refs", () => {
    expect(citedShas("Fixed in 070ed7e5 and b8af5fce (#1239), released in v1.3.10. deadbeef 12345678 #abc1234 org/abc1234")).toEqual([
      "070ed7e5",
      "b8af5fce",
    ]);
  });
});

describe("release-closeout comments", () => {
  it("cites short SHAs and the version", () => {
    expect(closingComment("v1.4.8", ["0123456789abcdef", "fedcba9876543210"])).toBe("Fixed in 01234567, fedcba98, released in v1.4.8.");
    expect(prereleaseComment("v1.4.8-beta.1", ["0123456789abcdef"])).toContain(PRERELEASE_MARKER);
  });
});
