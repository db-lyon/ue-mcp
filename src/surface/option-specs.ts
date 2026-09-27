/**
 * An action's parameters as flowkit option specs.
 *
 * `actionSchema` is the one reading of what an action takes, whatever declared
 * it: a handler's options, a bridge method's recorded C++ spec, an Epic tool's
 * input schema. This restates that reading in the shape flowkit's
 * `options_schema`, `registry.describe` and `FlowRunner.describeTask` use, so a
 * task description and `describe_action` cannot disagree.
 */
import type { OptionSpec, OptionSpecs } from "@db-lyon/flowkit";
import type { ToolDef } from "../core/types.js";
import { actionSchema, ROUTING_PARAMS, type ValueSchema } from "./action-schema.js";

type JsonType = "string" | "number" | "integer" | "boolean" | "object" | "array" | "null";

/** One of actionSchema's type names as JSON types, or undefined when it says "anything". */
function jsonTypes(type: string): JsonType[] | undefined {
  const out = new Set<JsonType>();
  for (const part of type.split("|")) {
    if (part.endsWith("[]")) { out.add("array"); continue; }
    switch (part) {
      case "string": case "enum": out.add("string"); break;
      case "number": out.add("number"); break;
      case "integer": out.add("integer"); break;
      case "boolean": out.add("boolean"); break;
      case "object": out.add("object"); break;
      case "null": out.add("null"); break;
      // any, literal, unknown: no constraint worth stating.
      default: return undefined;
    }
  }
  return out.size > 0 ? [...out] : undefined;
}

/** The element spec of a single-typed array, when its elements have one type. */
function itemSpec(type: string): Record<string, unknown> | undefined {
  const arrays = type.split("|").filter((t) => t.endsWith("[]"));
  if (arrays.length !== 1) return undefined;
  const element = jsonTypes(arrays[0].slice(0, -2));
  return element?.length === 1 ? { type: element[0] } : undefined;
}

/** One parameter as an option spec. `required` is the caller's to decide. */
export function optionSpecOf(param: ValueSchema): OptionSpec {
  const spec: OptionSpec = {};
  const types = jsonTypes(param.type);
  if (types) spec.type = types.length === 1 ? types[0] : types;
  if (param.description) spec.description = param.description;
  if (param.required) spec.required = true;
  if (param.default !== undefined) spec.default = param.default;
  if (param.enumValues?.length && types?.length === 1 && types[0] === "string") spec.enum = [...param.enumValues];
  const items = itemSpec(param.type);
  if (items) spec.items = items;
  return spec;
}

/**
 * The option specs of one action. Routing parameters are left out: the
 * dispatcher consumes them, and flowkit lets undeclared options through.
 */
export function actionOptionSpecs(tool: ToolDef, action: string): OptionSpecs {
  const specs: OptionSpecs = {};
  for (const param of actionSchema(tool, action).params) {
    if (ROUTING_PARAMS.has(param.name)) continue;
    specs[param.name] = optionSpecOf(param);
  }
  return specs;
}
