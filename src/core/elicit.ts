// @session independent: The MCP elicitation shape, as type declarations. No runtime code.
/**
 * Elicit a deterministic, user-mediated form response via the MCP client.
 * The server blocks until the client returns one of accept / decline / cancel.
 * Returns null when the connected client did not advertise the `elicitation`
 * capability - handlers that rely on this gate must refuse to proceed in
 * that case rather than fall back to an agent-mediated approval.
 */
export interface ElicitFn {
  (params: ElicitParams): Promise<ElicitResult>;
  /**
   * Whether the CONNECTED client advertised the `elicitation` capability.
   *
   * The presence of the function is NOT that answer. The server builds the
   * gate at startup, before any client has connected, so it always has a
   * function to hand over and the capability is only knowable once a client is
   * on the other end. Callers that branch on "can this user actually be asked"
   * must call this rather than test the function for undefined, which is true
   * of every client and would silently promote a client that advertised
   * nothing into the interactive path.
   *
   * Absent on a gate built outside the server (tests, embedders), where the
   * function was handed over deliberately and is taken at face value.
   */
  clientAdvertisesElicitation?: () => boolean;

  /**
   * Who is on the other end, as they named themselves at initialize.
   *
   * Read to decide how much the client can be trusted to RENDER, which is a
   * different question from what it advertised support for. Absent on a gate
   * built outside the server, where nothing knows.
   */
  client?: () => { name: string; version?: string } | undefined;
}

export interface ElicitParams {
  message: string;
  /**
   * Machine-readable context for a client that renders the form as its own
   * card, keyed by a namespaced name such as `ue-mcp/guard`. Optional; a
   * client that ignores it still has the message and the form.
   */
  _meta?: Record<string, unknown>;
  requestedSchema: {
    type: "object";
    properties: Record<string, ElicitPrimitiveSchema>;
    required?: string[];
  };
}

export type ElicitPrimitiveSchema =
  | { type: "string"; title?: string; description?: string; enum?: string[]; enumNames?: string[]; default?: string }
  | { type: "number" | "integer"; title?: string; description?: string; default?: number }
  | { type: "boolean"; title?: string; description?: string; default?: boolean }
  | (ElicitMultiSelectBase & { items: { type: "string"; enum: string[] } })
  | (ElicitMultiSelectBase & { items: { anyOf: Array<{ const: string; title: string }> } });

/** A checkbox group: the answer is the list of ticked values. */
interface ElicitMultiSelectBase {
  type: "array";
  title?: string;
  description?: string;
  minItems?: number;
  maxItems?: number;
  default?: string[];
}

export interface ElicitResult {
  action: "accept" | "decline" | "cancel";
  content?: Record<string, string | number | boolean | string[]>;
}
