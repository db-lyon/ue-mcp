import type { TaskContext } from "@db-lyon/flowkit";
import type { ToolContext } from "../types.js";

/**
 * FlowContext is the runtime context passed to flowkit tasks. It is a
 * structural superset of ToolContext: every FlowContext is a valid
 * ToolContext, so the registry can hand it to category-tool handlers
 * without rebuilding the shape field-by-field. Extending ToolContext
 * here (rather than re-declaring fields) is what prevents new ctx
 * accessors from silently dropping out at the handler boundary — every
 * accessor must be declared once, on ToolContext, and propagates.
 */
export interface FlowContext extends TaskContext, ToolContext {}
