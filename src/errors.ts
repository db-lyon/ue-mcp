/**
 * Structured error codes for the MCP bridge.
 * Allows callers to distinguish error types without string-matching.
 */
export enum ErrorCode {
  NOT_CONNECTED = "NOT_CONNECTED",
  BRIDGE_TIMEOUT = "BRIDGE_TIMEOUT",
  BRIDGE_ERROR = "BRIDGE_ERROR",
  CONNECTION_LOST = "CONNECTION_LOST",
  UNKNOWN_ACTION = "UNKNOWN_ACTION",
  NO_HANDLER = "NO_HANDLER",
  PROJECT_NOT_LOADED = "PROJECT_NOT_LOADED",
  NOT_FOUND = "NOT_FOUND",
  INVALID_PARAMS = "INVALID_PARAMS",
  /** A source-control guard refused a mutating bridge call before it ran
   *  (e.g. the target is checked out by another user, or checkout failed). */
  WRITE_BLOCKED = "WRITE_BLOCKED",
}

export class McpError extends Error {
  constructor(
    public readonly code: ErrorCode,
    message: string,
  ) {
    super(message);
    this.name = "McpError";
  }
}
