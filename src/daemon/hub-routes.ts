// @session per-session: Serves the one hub of the project this daemon serves; nothing in a request names an editor.
/**
 * The Playtest Hub over the daemon's HTTP API: the hub.* entries of
 * DAEMON_ROUTES, as HUB_ROUTES in the hub contract names their bodies. A bad
 * body answers 400, an unknown id 404 and a request that cannot be met now
 * 409, each with { error } (HubError is a StatusError). Every change publishes
 * hub.changed (src/hub/hub.ts).
 */
import * as fs from "node:fs";
import { DAEMON_HEADERS } from "../contract/daemon.js";
import type { Hub } from "../hub/hub.js";
import { runDir } from "../hub/evidence.js";
import { FindingError } from "../hub/set-service.js";
import { StatusError } from "../core/status-error.js";
import { ok, type RouteHandlers, type RouteResult } from "./router.js";

type HubRouteName = Extract<keyof RouteHandlers, `hub.${string}`>;

/** An id a route handler needs from its path. */
const param = (params: Record<string, string>, name: string): string => params[name] ?? "";

export function hubRoutes(hub: () => Hub, projectDir: string): Pick<RouteHandlers, HubRouteName> {
  const q = (query: URLSearchParams) => Object.fromEntries(query);
  const noContent = { status: 204 } as const;
  return {
    "hub.overview": async () => ok(await hub().overview()),
    "hub.goal": async ({ params }) => ok(await hub().goal(param(params, "id"))),
    "hub.goal.create": async (r) => ok({ goal: hub().goals.create(await r.body()) }),
    "hub.goal.update": async (r) => ok({ goal: hub().goals.update(param(r.params, "id"), await r.body()) }),
    "hub.goal.delete": ({ params }) => {
      hub().deleteGoal(param(params, "id"));
      return noContent;
    },
    "hub.goal.addChecks": async (r) => ok({ goal: hub().goals.addChecks(param(r.params, "id"), (await r.body() as { checks?: unknown } | undefined)?.checks) }),
    "hub.goal.orderChecks": async (r) => ok({ goal: hub().goals.orderChecks(param(r.params, "id"), await r.body()) }),
    "hub.goal.removeCheck": async ({ params }) => ok({ goal: hub().goals.removeCheck(param(params, "id"), await hub().goalCheckId(param(params, "id"), param(params, "checkId"))) }),
    "hub.goal.updateCheck": async (r) => ok({ goal: hub().goals.updateGoalCheck(param(r.params, "id"), await hub().goalCheckId(param(r.params, "id"), param(r.params, "checkId")), await r.body()) }),
    "hub.goal.addRequire": async (r) => ok({ goal: hub().goals.addRequire(param(r.params, "id"), await r.body()) }),
    "hub.goal.orderRequires": async (r) => ok({ goal: hub().goals.orderRequires(param(r.params, "id"), await r.body()) }),
    "hub.goal.removeRequire": ({ params }) => ok({ goal: hub().goals.removeRequire(param(params, "id"), param(params, "goalId")) }),
    "hub.goal.signOff": async (r) => ok({ signOff: await hub().signOff(param(r.params, "id"), await r.body(), signedVia(r.req.headers[DAEMON_HEADERS.via])) }),
    "hub.goal.clearSignOff": ({ params }) => {
      hub().clearSignOff(param(params, "id"));
      return noContent;
    },
    "hub.goal.writePlaytests": async (r) => ({ status: 202, body: hub().writePlaytests(param(r.params, "id"), await r.body()) }),
    "hub.check": async ({ params, query }) => ok(await hub().check(param(params, "checkId"), q(query))),
    "hub.check.update": async (r) => ok({ meta: hub().updateCheckMeta(param(r.params, "checkId"), await r.body()) }),
    "hub.check.run": async (r) => ok(await hub().runCheck(param(r.params, "checkId"), await r.body())),
    "hub.rules": async (r) => ok({ rules: hub().goals.setRules(await r.body()) }),
    "hub.store.export": () => ok(hub().goals.exportAll()),
    "hub.store.import": async (r) => ok(hub().goals.importAll(await r.body())),
    "hub.runs": ({ query }) => ok(hub().runsPage(q(query))),
    "hub.run": ({ params }) => ok(hub().run(param(params, "runId"))),
    "hub.run.results": ({ params }) => {
      const dir = runDir(projectDir, param(params, "runId"));
      const file = dir ? `${dir}/results.json` : null;
      if (!file || !fs.existsSync(file)) throw new StatusError(404, `no results for run ${param(params, "runId")}`);
      return { status: 200, headers: { "content-type": "application/json" }, file };
    },
    "hub.run.addLink": async (r) => ok({ record: hub().addRunLink(param(r.params, "runId"), await r.body()) }),
    "hub.run.removeLink": ({ params, query }) => ok({ record: hub().removeRunLink(param(params, "runId"), query.get("url")) }),
    "hub.queue": () => ok(hub().queueView()),
    "hub.queue.pause": async (r) => ok(hub().setPaused(await r.body())),
    "hub.lease": () => ok(hub().leaseView()),
    "hub.release": async () => ok(await hub().release()),
    "hub.plan": async (r) => ok(await hub().queuePlan(await r.body(), "person")),
    "hub.plan.cancel": async ({ params }) => ok({ plan: await hub().cancelPlan(param(params, "id")) }),
    "hub.plan.move": async (r) => ok(hub().movePlan(param(r.params, "id"), await r.body())),
    "hub.schedule.create": async (r) => ok({ schedule: hub().createSchedule(await r.body()) }),
    "hub.schedule.update": async (r) => ok({ schedule: hub().updateSchedule(param(r.params, "id"), await r.body()) }),
    "hub.schedule.delete": ({ params }) => {
      hub().deleteSchedule(param(params, "id"));
      return noContent;
    },
    "hub.fix": async (r) => ok(await hub().fix(await r.body())),
    "hub.set": async () => ok(await hub().set.view()),
    "hub.set.edit": async (r) => findings(async () => hub().set.edit(await r.body())),
    "hub.arrangement": async ({ params }) => ok(await hub().set.arrangement(param(params, "id"))),
    "hub.arrangement.put": async (r) => findings(async () => hub().set.putArrangement(param(r.params, "id"), await r.body())),
    "hub.arrangement.edit": async (r) => findings(async () => hub().set.editArrangement(param(r.params, "id"), await r.body())),
    "hub.clips": async ({ query }) => ok({ clips: await hub().set.clips(q(query)) }),
    "hub.set.run": async (r) => ok(await hub().runSet(await r.body(), "person")),
    "hub.run.verdict": async (r) => ok(await hub().runVerdict(param(r.params, "runId"), await r.body())),
  };
}

/** An answer whose 400 carries the finding and its candidates, as the set and arrangement routes give it. */
async function findings<T>(run: () => Promise<T>): Promise<RouteResult> {
  try {
    return ok(await run());
  } catch (e) {
    if (e instanceof FindingError) return { status: 400, body: { error: e.message, finding: e.finding, findings: e.findings } };
    throw e;
  }
}

/** The device a sign-off came through, when a client that relays remote requests says so; undefined at this computer. */
function signedVia(v: string | string[] | undefined): string | undefined {
  const s = Array.isArray(v) ? v[0] : v;
  if (!s) return undefined;
  try {
    return decodeURIComponent(s).slice(0, 120);
  } catch {
    return undefined;
  }
}
