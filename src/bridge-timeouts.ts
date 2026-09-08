/**
 * How long the client waits for a bridge call to answer (#989).
 *
 * The client waited a flat 30 seconds for every method. Two things were wrong
 * with that, and both produce the same bad outcome: the editor completes the
 * operation, the client reports a failure, and a naive retry applies the
 * mutation a second time.
 *
 *  1. Some calls are simply long. A 190-item batch on a machine that is also
 *     compiling shaders does not finish inside 30 seconds. Callers can now pass
 *     a `timeoutMs` and be believed.
 *
 *  2. The two ends disagreed. The C++ side registers a per-handler timeout via
 *     FMCPHandlerRegistry::RegisterHandlerWithTimeout, and the game-thread
 *     executor waits that long before giving up. delete_exact_labeled_actors_in_levels
 *     is allowed 300 seconds there and was cut off by the client at 30, so the
 *     server's own limit could never be reached and its error could never be
 *     read. The client now waits at least as long as the server said it would.
 *
 * The registered timeouts are NOT advertised. get_bridge_capabilities publishes
 * the protocol version, the feature list and the registered action names, but
 * not their timeouts, so there is no way to read this off the wire from a
 * plugin that is already built. Inventing a field would mean a protocol change
 * on the C++ side. The table below therefore mirrors the registrations by hand,
 * and tests/unit/bridge-timeout-parity.test.ts parses the plugin sources and
 * fails when the two drift apart. A mirror nobody checks is worse than none.
 */

/** What a method with no registered timeout gets. Unchanged from before. */
export const DEFAULT_BRIDGE_TIMEOUT_MS = 30_000;

/**
 * Slack added on top of a server-registered timeout, so the client outlives the
 * server's own deadline and can report the server's error instead of its own
 * guess. Without it the two expire together and the race decides which message
 * the user sees.
 */
export const SERVER_TIMEOUT_MARGIN_MS = 5_000;

/** Upper bound on any single call, caller-supplied included. */
export const MAX_BRIDGE_TIMEOUT_MS = 3_600_000;

/** Environment override for the floor under every call, in milliseconds. */
export const TIMEOUT_ENV_VAR = "UE_MCP_BRIDGE_TIMEOUT_MS";

/**
 * Mirror of every FMCPHandlerRegistry::RegisterHandlerWithTimeout call in
 * plugin/, in seconds, keyed by bridge method name.
 *
 * Keep it in the order the parity test reads it: alphabetical by method.
 * External handlers registered at runtime through
 * RegisterExternalHandlerWithTimeout are not here and cannot be, since they
 * come from plugins this repo does not build.
 */
export const REGISTERED_HANDLER_TIMEOUT_SECONDS: Readonly<Record<string, number>> = {
  analyze_landscape_terrain: 120,
  apply_landscape_erosion: 600,
  apply_mesh_fracture: 600,
  apply_mesh_hole_fill: 300,
  apply_mesh_mirror: 300,
  apply_mesh_remesh: 300,
  apply_mesh_simplify: 300,
  audit_asset_hygiene: 300,
  batch_retarget_animations: 300,
  batch_set_actor_properties: 300,
  batch_set_foliage_settings_where: 300,
  bulk_read_asset_properties: 300,
  bulk_restore_data_assets: 120,
  bulk_set_component_property: 300,
  bulk_upsert_data_assets: 120,
  convert_brushes_to_static_mesh: 600,
  create_cpp_class: 300,
  delete_exact_labeled_actors_in_levels: 300,
  export_landscape_heightmap: 300,
  fix_asset_hygiene: 300,
  fixup_redirectors: 300,
  generate_mesh_collision: 300,
  get_landscape_height_region: 120,
  get_landscape_holes: 120,
  get_landscape_layer_weight_region: 120,
  get_landscape_slope_map: 120,
  import_landscape_heightmap: 600,
  invoke_object_functions: 300,
  live_coding_compile: 300,
  load_actor_descs: 300,
  mesh_boolean: 300,
  paint_landscape_layer: 120,
  plan_real_world_landscape: 120,
  query_components: 300,
  read_blueprint_graph: 180,
  recreate_physics_state: 300,
  refresh_landscape_physical_material_collision: 600,
  remove_components_by_class: 300,
  rerun_construction_scripts: 300,
  run_automation_tests: 300,
  sculpt_landscape: 120,
  sculpt_landscape_region: 300,
  search_blueprint_call_sites: 600,
  set_actor_hlod_layer: 300,
  set_component_materials: 300,
  set_landscape_height_region: 300,
  set_landscape_holes: 300,
  set_landscape_layer_weight_region: 300,
  simulate_procedural_foliage: 600,
  snap_instances_to_surface: 300,
  spawn_actors_batch: 300,
  summarize_static_mesh_usage: 300,
  trigger_hitch: 30,
};

/** What the editor will wait for this method, in ms, or undefined for the default. */
export function registeredTimeoutMs(method: string): number | undefined {
  const seconds = REGISTERED_HANDLER_TIMEOUT_SECONDS[method];
  return seconds === undefined ? undefined : seconds * 1000;
}

/** The floor under every call: the env override when it is a usable number. */
export function environmentTimeoutMs(env: NodeJS.ProcessEnv = process.env): number | undefined {
  const raw = env[TIMEOUT_ENV_VAR];
  if (raw === undefined || raw === "") return undefined;
  const parsed = Number(raw);
  if (!Number.isFinite(parsed) || parsed <= 0) return undefined;
  return Math.min(parsed, MAX_BRIDGE_TIMEOUT_MS);
}

/**
 * How long to wait for `method`.
 *
 * An explicit per-call value wins outright, including a shorter one: a caller
 * that asks for less has said so on purpose. Otherwise the answer is the
 * longest of the default, the environment floor, and the server's own
 * registered limit plus its margin, so the client never gives up on a call the
 * editor is still allowed to be working on.
 */
export function resolveBridgeTimeout(
  method: string,
  explicitMs?: number,
  env: NodeJS.ProcessEnv = process.env,
): number {
  if (typeof explicitMs === "number" && Number.isFinite(explicitMs) && explicitMs > 0) {
    return Math.min(explicitMs, MAX_BRIDGE_TIMEOUT_MS);
  }
  const registered = registeredTimeoutMs(method);
  const candidates = [
    DEFAULT_BRIDGE_TIMEOUT_MS,
    environmentTimeoutMs(env) ?? 0,
    registered === undefined ? 0 : registered + SERVER_TIMEOUT_MARGIN_MS,
  ];
  return Math.min(Math.max(...candidates), MAX_BRIDGE_TIMEOUT_MS);
}
