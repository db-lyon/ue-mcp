import { spawn } from "node:child_process";

/**
 * The command that opens an http(s) URL in the system browser, as argv. On
 * Windows the URL handler runs through rundll32 rather than `cmd /c start`,
 * so no `&` in a query string is ever read by cmd.
 */
export function openUrlInvocation(url: string, platform: NodeJS.Platform = process.platform): [string, string[]] {
  let parsed: URL;
  try {
    parsed = new URL(url);
  } catch {
    throw new Error(`Not a URL: ${url}`);
  }
  if (parsed.protocol !== "http:" && parsed.protocol !== "https:") throw new Error(`Refusing to open a ${parsed.protocol} URL`);
  const href = parsed.href;
  if (platform === "win32") return ["rundll32.exe", ["url.dll,FileProtocolHandler", href]];
  return platform === "darwin" ? ["open", [href]] : ["xdg-open", [href]];
}

/** Open a URL in the system browser, detached. */
export function openUrl(url: string): void {
  const [cmd, args] = openUrlInvocation(url);
  spawn(cmd, args, { windowsHide: true, detached: true, stdio: "ignore" }).on("error", () => {}).unref();
}
