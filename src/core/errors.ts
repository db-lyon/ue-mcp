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
  /** Another session holds the exclusive lock on an asset this call would
   *  mutate. Retryable: the holder's lease expires or it releases the lock. */
  ASSET_LOCKED = "ASSET_LOCKED",
}

/**
 * Machine-readable companion to an error message. `outcome` is what the caller
 * needs before deciding to retry: "failed" means the call never ran, "unknown"
 * means it may have run to completion and the client stopped waiting (#799).
 */
export interface McpErrorDetails {
  outcome?: "failed" | "unknown";
  /** Bridge request id the editor logs the late reply under. */
  operationId?: string;
  method?: string;
  [key: string]: unknown;
}

export class McpError extends Error {
  constructor(
    public readonly code: ErrorCode,
    message: string,
    public readonly details?: McpErrorDetails,
  ) {
    super(message);
    this.name = "McpError";
  }
}
