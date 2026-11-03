/**
 * MCP resources from the active editor: whatever resource providers are
 * registered with Epic's Unreal MCP plugin, listed and read through the bridge
 * (epic_list_mcp_resources / epic_read_mcp_resource).
 *
 * resources/list never fails: no editor, no plugin, an older bridge without the
 * methods, or no provider all list as empty.
 */
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import {
  ErrorCode as SdkErrorCode,
  ListResourcesRequestSchema,
  McpError as SdkMcpError,
  ReadResourceRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";
import type { IBridge } from "../bridge/bridge.js";
import { debug } from "../core/log.js";

export interface EditorResource {
  uri: string;
  name: string;
  title?: string;
  description?: string;
  mimeType?: string;
}

export type EditorResourceContents = { uri: string; mimeType?: string } & ({ text: string } | { blob: string });

const LIST_METHOD = "epic_list_mcp_resources";
const READ_METHOD = "epic_read_mcp_resource";

/** Whether this bridge can be asked at all: connected, and not known to lack the method. */
function canAsk(bridge: IBridge, method: string): boolean {
  if (!bridge.isConnected) return false;
  const have = bridge.registeredActions;
  return !have || have.includes(method);
}

export async function listEditorResources(bridge: IBridge): Promise<EditorResource[]> {
  if (!canAsk(bridge, LIST_METHOD)) return [];
  let answer: unknown;
  try {
    answer = await bridge.call(LIST_METHOD, {});
  } catch (e) {
    debug("resources", `list failed: ${e instanceof Error ? e.message : String(e)}`);
    return [];
  }
  const listed = (answer as { resources?: unknown } | null)?.resources;
  if (!Array.isArray(listed)) return [];
  const out: EditorResource[] = [];
  for (const r of listed) {
    if (!r || typeof r !== "object") continue;
    const entry = r as Record<string, unknown>;
    if (typeof entry.uri !== "string" || entry.uri === "") continue;
    out.push({
      uri: entry.uri,
      // MCP requires a name; the uri stands in when the provider gave none.
      name: typeof entry.name === "string" && entry.name !== "" ? entry.name : entry.uri,
      ...(typeof entry.title === "string" ? { title: entry.title } : {}),
      ...(typeof entry.description === "string" ? { description: entry.description } : {}),
      ...(typeof entry.mimeType === "string" ? { mimeType: entry.mimeType } : {}),
    });
  }
  return out;
}

export async function readEditorResource(bridge: IBridge, uri: string): Promise<EditorResourceContents[]> {
  if (!canAsk(bridge, READ_METHOD)) {
    throw new SdkMcpError(SdkErrorCode.InvalidParams, `Resource ${uri} is not available: no editor with Epic's Unreal MCP plugin is connected.`);
  }
  const answer = (await bridge.call(READ_METHOD, { uri })) as { success?: boolean; error?: string; contents?: unknown } | null;
  if (!answer || answer.success === false || !Array.isArray(answer.contents)) {
    throw new SdkMcpError(SdkErrorCode.InvalidParams, answer?.error ?? `Resource ${uri} could not be read.`);
  }
  const out: EditorResourceContents[] = [];
  for (const c of answer.contents) {
    if (!c || typeof c !== "object") continue;
    const entry = c as Record<string, unknown>;
    const base = {
      uri: typeof entry.uri === "string" ? entry.uri : uri,
      ...(typeof entry.mimeType === "string" ? { mimeType: entry.mimeType } : {}),
    };
    if (typeof entry.text === "string") out.push({ ...base, text: entry.text });
    else if (typeof entry.blob === "string") out.push({ ...base, blob: entry.blob });
  }
  return out;
}

/** Tell a connected client the resource list may have changed. */
export function announceResourceChange(server: McpServer): void {
  if (!server.isConnected()) return;
  void server.server.sendResourceListChanged().catch(() => undefined);
}

/** Serve resources/list and resources/read from the bridge `bridgeFor` returns at call time. */
export function registerEditorResources(server: McpServer, bridgeFor: () => IBridge): void {
  // listChanged: the list is empty until an editor connects, so the server
  // tells the client to read it again when the surface changes.
  server.server.registerCapabilities({ resources: { listChanged: true } });
  server.server.setRequestHandler(ListResourcesRequestSchema, async () => ({
    resources: await listEditorResources(bridgeFor()),
  }));
  server.server.setRequestHandler(ReadResourceRequestSchema, async (request) => ({
    contents: await readEditorResource(bridgeFor(), request.params.uri),
  }));
}
