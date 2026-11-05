/**
 * Extension-to-extension services (feature "extension.services"): one
 * provider per id, watchers told on provide and revoke, and a disposed
 * extension's services revoked and its watches stopped.
 */
import { describe, expect, it } from "vitest";
import { ExtensionServices } from "../../src/daemon/extension-services.js";

describe("ExtensionServices", () => {
  it("fires a watch made before the provide when the service arrives", () => {
    const s = new ExtensionServices();
    const seen: unknown[] = [];
    s.forOwner("consumer").watch("host", (impl) => seen.push(impl));
    expect(seen).toEqual([]);
    const impl = { hello: () => "hi" };
    s.forOwner("provider").provide("host", impl);
    expect(seen).toEqual([impl]);
  });

  it("fires a watch made after the provide at once", () => {
    const s = new ExtensionServices();
    s.forOwner("provider").provide("host", 1);
    const seen: unknown[] = [];
    s.forOwner("consumer").watch("host", (impl) => seen.push(impl));
    expect(seen).toEqual([1]);
  });

  it("revokes a disposed provider's services and tells watchers with undefined", () => {
    const s = new ExtensionServices();
    const seen: unknown[] = [];
    s.forOwner("consumer").watch("host", (impl) => seen.push(impl));
    s.forOwner("provider").provide("host", "v1");
    s.revokeOwner("provider");
    expect(seen).toEqual(["v1", undefined]);
    expect(s.forOwner("consumer").get("host")).toBeUndefined();
    // The id is free again.
    s.forOwner("other").provide("host", "v2");
    expect(seen).toEqual(["v1", undefined, "v2"]);
  });

  it("stops a disposed consumer's watches before revoking what it provided", () => {
    const s = new ExtensionServices();
    const seen: unknown[] = [];
    const both = s.forOwner("both");
    both.watch("host", (impl) => seen.push(impl));
    s.forOwner("provider").provide("host", "v1");
    both.provide("own", 1);
    s.revokeOwner("both");
    s.revokeOwner("provider");
    expect(seen).toEqual(["v1"]);
    expect(s.get("own")).toBeUndefined();
  });

  it("revokes with the function provide returns, once", () => {
    const s = new ExtensionServices();
    const seen: unknown[] = [];
    s.forOwner("consumer").watch("host", (impl) => seen.push(impl));
    const revoke = s.forOwner("provider").provide("host", "v1");
    revoke();
    revoke();
    expect(seen).toEqual(["v1", undefined]);
  });

  it("answers undefined for an id nobody provides", () => {
    expect(new ExtensionServices().forOwner("x").get("nothing")).toBeUndefined();
  });

  it("refuses a second provider for a held id", () => {
    const s = new ExtensionServices();
    s.forOwner("a").provide("host", 1);
    expect(() => s.forOwner("b").provide("host", 2)).toThrow(/already provided by a/);
    expect(s.get("host")).toBe(1);
  });

  it("stops calling a watch once unsubscribed, and keeps going past one that throws", () => {
    const s = new ExtensionServices();
    const seen: unknown[] = [];
    s.forOwner("bad").watch("host", () => { throw new Error("boom"); });
    const stop = s.forOwner("consumer").watch("host", (impl) => seen.push(impl));
    const revoke = s.forOwner("provider").provide("host", "v1");
    stop();
    revoke();
    expect(seen).toEqual(["v1"]);
  });
});
