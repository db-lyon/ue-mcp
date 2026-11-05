import { describe, expect, it } from "vitest";
import { openUrlInvocation } from "../../src/core/open-url.js";

/** `daemon open` and the daemon's first start open a URL with a token in its query string. */
describe("opening a URL in the browser", () => {
  it("hands the URL to the Windows URL handler as one argument, never to cmd", () => {
    const [cmd, args] = openUrlInvocation("http://127.0.0.1:4100/ui/?token=a&b=c", "win32");
    expect(cmd).toBe("rundll32.exe");
    expect(args).toEqual(["url.dll,FileProtocolHandler", "http://127.0.0.1:4100/ui/?token=a&b=c"]);
  });

  it("uses open and xdg-open elsewhere", () => {
    expect(openUrlInvocation("https://x.test/", "darwin")).toEqual(["open", ["https://x.test/"]]);
    expect(openUrlInvocation("https://x.test/", "linux")).toEqual(["xdg-open", ["https://x.test/"]]);
  });

  it("opens only http and https URLs", () => {
    for (const bad of ["file:///C:/Windows/System32/calc.exe", "javascript:alert(1)", "not a url", "C:/x.bat"]) {
      expect(() => openUrlInvocation(bad, "win32")).toThrow();
    }
  });
});
