import { describe, it, expect, vi, afterEach } from "vitest";
import { CLI_COMMANDS, cliCommandNames, findCliCommand } from "../../src/cli-commands.js";

afterEach(() => {
  vi.restoreAllMocks();
});

describe("the CLI command table", () => {
  it.each(CLI_COMMANDS.map((c) => [c.name, c] as const))(
    "%s loads, exports run, and does nothing on import",
    async (_name, command) => {
      const exit = vi.spyOn(process, "exit").mockImplementation((() => undefined) as never);
      const log = vi.spyOn(console, "log").mockImplementation(() => undefined);
      const error = vi.spyOn(console, "error").mockImplementation(() => undefined);

      const loaded = await command.load();

      expect(typeof loaded.run).toBe("function");
      expect(exit).not.toHaveBeenCalled();
      expect(log).not.toHaveBeenCalled();
      expect(error).not.toHaveBeenCalled();
    },
  );

  it("names every command once", () => {
    const names = cliCommandNames();
    expect(new Set(names).size).toBe(names.length);
  });

  it("finds a command by name or alias and nothing else", () => {
    expect(findCliCommand("dialog")?.name).toBe("dialog");
    expect(findCliCommand("--version")?.name).toBe("version");
    expect(findCliCommand("C:/proj/Game.uproject")).toBeUndefined();
    expect(findCliCommand(undefined)).toBeUndefined();
  });
});
