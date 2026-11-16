import type { TaskConstructor } from "@db-lyon/flowkit";
import type { z } from "zod";
import { McpError, ErrorCode } from "../core/errors.js";
import type { ActionSpec } from "../surface/tool-types.js";
import { paramsClause, type ParamSpec } from "../surface/handler-spec.js";
import type { ManifestSchemaField } from "./manifest.js";

type Fields = Record<string, ManifestSchemaField> | undefined;

/** A manifest `schema:` block as the per-action parameter spec describe_action and signatures read. */
export function manifestParamSpecs(fields: Fields): ParamSpec[] {
  return Object.entries(fields ?? {}).map(([name, f]) => {
    const types = f.type === undefined ? ["any" as const] : Array.isArray(f.type) ? f.type : [f.type];
    const spec: ParamSpec = { name, type: types[0], required: f.required === true, description: f.description ?? "" };
    if (types.length > 1) spec.orTypes = types.slice(1);
    return spec;
  });
}

/** The names a manifest `schema:` block marks required. */
export function requiredFieldNames(fields: Fields): string[] {
  return Object.entries(fields ?? {}).filter(([, f]) => f.required === true).map(([name]) => name);
}

/**
 * The action fields a manifest `schema:` declares: its spec, and a Params
 * clause on the description so the requiredness is per action. One flat zod
 * shape backs every action in a category, so it cannot carry it.
 */
export function manifestActionFields(description: string, fields: Fields): Partial<ActionSpec> {
  if (!fields || Object.keys(fields).length === 0) return { description };
  const paramSpec = manifestParamSpecs(fields);
  const clause = /\bParams:/.test(description) ? "" : paramsClause({ params: paramSpec });
  return { description: [description, clause].filter(Boolean).join(" "), paramSpec };
}

/** A lifted category key: always optional, since other actions share it. */
export function liftOptional(schema: z.ZodType): z.ZodType {
  return schema.isOptional() ? schema : schema.optional();
}

/**
 * A plugin task that refuses a call missing a required parameter before it
 * runs, on every route that reaches the registry (live, gateway, flow, batch).
 */
export function requiringParams(ctor: TaskConstructor, action: string, required: readonly string[]): TaskConstructor {
  if (required.length === 0) return ctor;
  const Base = ctor as unknown as new (...args: unknown[]) => { run(): Promise<unknown> };
  class RequiringParams extends Base {
    private readonly given: Record<string, unknown>;
    constructor(...args: unknown[]) {
      super(...args);
      this.given = (args[1] ?? {}) as Record<string, unknown>;
    }
    override async run(): Promise<unknown> {
      const missing = required.filter((k) => this.given[k] === undefined);
      if (missing.length > 0) {
        throw new McpError(
          ErrorCode.INVALID_PARAMS,
          `${action} needs ${missing.join(", ")}, and ${missing.length > 1 ? "they were" : "it was"} not given`,
        );
      }
      return super.run();
    }
  }
  return RequiringParams as unknown as TaskConstructor;
}

/**
 * A declared parameter whose name the dispatcher consumes (`action`,
 * `timeoutMs`, ...), so no call could ever deliver it to the plugin. Returns
 * the first such name, or undefined.
 */
export function reservedParamName(names: Iterable<string>, reserved: readonly string[]): string | undefined {
  for (const n of names) if (reserved.includes(n)) return n;
  return undefined;
}

/** Why a declared parameter can never arrive, worded for the plugin author. */
export function reservedParamReason(param: string): string {
  return `declares a parameter named '${param}', which every category's dispatcher consumes before a handler runs, `
    + `so no call can deliver it. Rename it (for example '${param}_path' or '${param}Name')`;
}
