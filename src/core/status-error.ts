// @session independent: An error type carrying an HTTP status. No state.
/**
 * An error a daemon route answers with its own status and `{ error: message }`
 * instead of 500: a refusal the caller can act on, such as a missing parameter
 * (400), an unknown session (404) or a state that forbids the change (409).
 */
export class StatusError extends Error {
  constructor(readonly status: number, message: string) {
    super(message);
  }
}
