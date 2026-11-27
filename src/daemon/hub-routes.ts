// @session per-session: Serves the one hub of the project this daemon serves; nothing in a request names an editor.
/**
 * The Playtest Hub over the daemon's HTTP API: the hub.* entries of
 * DAEMON_ROUTES. A bad body answers 400 (with `findings` when a file's
 * content does not read), an unknown id 404 and a request that cannot be met
 * now 409, each with { error } (HubError is a StatusError). Every change
 * publishes hub.changed (src/hub/hub.ts).
 */
import type { Hub } from "../hub/hub.js";
import { FindingError } from "../hub/util.js";
import { ok, type RouteHandlers, type RouteResult } from "./router.js";

type HubRouteName = Extract<keyof RouteHandlers, `hub.${string}`>;

/** An id a route handler needs from its path. */
const param = (params: Record<string, string>, name: string): string => params[name] ?? "";

/** An answer whose 400 carries every finding. */
function findings<T>(run: () => T | Promise<T>): Promise<RouteResult> {
  return Promise.resolve()
    .then(run)
    .then(
      (v) => ok(v),
      (e: unknown) => {
        if (e instanceof FindingError) return { status: 400, body: { error: e.message, findings: e.findings } };
        throw e;
      },
    );
}

export function hubRoutes(hub: () => Hub): Pick<RouteHandlers, HubRouteName> {
  const noContent = { status: 204 } as const;
  const id = (p: Record<string, string>) => param(p, "id");
  return {
    "hub.overview": () => ok(hub().overview()),
    "hub.clips": () => ok({ clips: hub().clips() }),
    "hub.clip": ({ params }) => findings(() => ({ clip: hub().clip(id(params)) })),
    "hub.clip.create": async (r) => findings(async () => ({ clip: hub().createClip(await r.body()) })),
    "hub.clip.put": async (r) => findings(async () => ({ clip: hub().putClip(id(r.params), await r.body()) })),
    "hub.clip.delete": ({ params }) => (hub().deleteClip(id(params)), noContent),
    "hub.record.start": async (r) => ok(await hub().recordStart(await r.body())),
    "hub.record.stop": async (r) => findings(async () => ({ clip: await hub().recordStop(await r.body()) })),
    "hub.record.state": () => ok(hub().readRecording()),
    "hub.arrangements": () => ok({ arrangements: hub().arrangements() }),
    "hub.arrangement": ({ params }) => findings(() => hub().arrangement(id(params))),
    "hub.arrangement.create": async (r) => findings(async () => hub().createArrangement(await r.body())),
    "hub.arrangement.put": async (r) => findings(async () => hub().putArrangement(id(r.params), await r.body())),
    "hub.arrangement.delete": ({ params }) => (hub().deleteArrangement(id(params)), noContent),
    "hub.performers": () => ok(hub().performers()),
    "hub.play": async (r) => ok(await hub().play(await r.body())),
    "hub.play.state": async () => ok(await hub().readPlay()),
    "hub.play.stop": async () => ok(await hub().stopPlay()),
    "hub.tests": () => ok({ tests: hub().tests() }),
    "hub.test": ({ params, query }) => ok(hub().test(id(params), Number(query.get("limit")) || undefined)),
    "hub.suites": () => ok({ suites: hub().suites() }),
    "hub.suite": ({ params }) => findings(() => hub().suite(id(params))),
    "hub.suite.create": async (r) => findings(async () => hub().createSuite(await r.body())),
    "hub.suite.put": async (r) => findings(async () => hub().putSuite(id(r.params), await r.body())),
    "hub.suite.delete": ({ params }) => (hub().deleteSuite(id(params)), noContent),
    "hub.suite.quarantine": async (r) => ok(hub().quarantine(id(r.params), await r.body())),
    "hub.suite.flaky": async (r) => ok(hub().markFlaky(id(r.params), await r.body())),
    "hub.runs": ({ query }) => ok(hub().runs(Object.fromEntries(query))),
    "hub.run": ({ params }) => ok(hub().run(param(params, "runId"))),
    "hub.run.verdict": async (r) => ok(await hub().verdict(param(r.params, "runId"), await r.body(), "person")),
    "hub.run.stop": async ({ params }) => ok(await hub().stopRun(param(params, "runId"))),
    "hub.queue": () => ok(hub().queueView()),
    "hub.queue.add": async (r) => ok(hub().enqueue(await r.body(), "person")),
    "hub.queue.remove": ({ params }) => ok(hub().removeQueued(id(params))),
    "hub.queue.pause": async (r) => ok(hub().setPaused(await r.body())),
    "hub.lease": () => ok(hub().leaseView()),
    "hub.release": async () => ok(await hub().release()),
    "hub.schedules": () => ok({ schedules: hub().schedules() }),
    "hub.schedule.create": async (r) => ok({ schedule: hub().createSchedule(await r.body()) }),
    "hub.schedule.update": async (r) => ok({ schedule: hub().updateSchedule(id(r.params), await r.body()) }),
    "hub.schedule.delete": ({ params }) => (hub().deleteSchedule(id(params)), noContent),
  };
}
