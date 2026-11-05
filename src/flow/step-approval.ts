/**
 * Approving each flow step as it runs (feature "flows.stepApproval").
 *
 * An extension registers an approver under its own name. A run that names it
 * (POST /v1/flows/<name>/run with `stepApproval: { approver, token }`) asks it
 * before every task the run creates, with the task's resolved options, and a
 * denied task fails as that step's error without running. Rollback tasks are
 * never asked: they undo what already ran. A run that names an approver which
 * is not registered fails closed, so a caller that asked for approval never
 * gets a run without it.
 */
import type { TaskRegistry, TaskResult } from "@db-lyon/flowkit";

export interface FlowStepApprovalRequest {
  runId: string;
  flowName: string;
  /** The task the step runs, as the registry names it: `category.action` for an action. */
  task: string;
  /** flowkit's execution phase: "task" for a step, or on_success / on_failure work. */
  phase: string;
  /** The task's options after the run resolved them. */
  options: Record<string, unknown>;
  /** The caller's token from the run request, so the approver knows whose run it is. */
  token: string;
}

export type FlowStepDecision = { allow: true } | { allow: false; reason?: string };

export type FlowStepApprover = (request: FlowStepApprovalRequest) => Promise<FlowStepDecision>;

/** What a run asks for: the registered approver to ask, and the caller's token for it. */
export interface FlowStepApprovalSpec {
  approver: string;
  token: string;
}

const approvers = new Map<string, FlowStepApprover>();

/** Register `approver` under `owner`, replacing an earlier one. The function returned removes it. */
export function registerFlowStepApprover(owner: string, approver: FlowStepApprover): () => void {
  approvers.set(owner, approver);
  return () => {
    if (approvers.get(owner) === approver) approvers.delete(owner);
  };
}

/** The spec in a run request body, or null when it has none; a malformed one throws. */
export function parseStepApproval(raw: unknown): FlowStepApprovalSpec | null {
  if (raw === undefined || raw === null) return null;
  const r = raw as { approver?: unknown; token?: unknown };
  if (typeof raw !== "object" || typeof r.approver !== "string" || !r.approver || typeof r.token !== "string") {
    throw new Error("stepApproval must be { approver: string, token: string }");
  }
  return { approver: r.approver, token: r.token };
}

interface Runnable {
  run(): Promise<TaskResult>;
}

const refused = (message: string): TaskResult => ({ success: false, error: new Error(message) });

/** `registry` with every task it creates asking the spec's approver before it runs. */
export function withStepApproval(registry: TaskRegistry, spec: FlowStepApprovalSpec, runId: string, flowName: string): TaskRegistry {
  const create = async (name: string, ctx: unknown, options: Record<string, unknown>): Promise<Runnable> => {
    const task = (await registry.create(name, ctx as never, options)) as unknown as Runnable;
    const phase = String((ctx as { executionPhase?: string } | undefined)?.executionPhase ?? "task");
    if (phase === "rollback") return task;
    const original = task.run.bind(task);
    task.run = async () => {
      const approve = approvers.get(spec.approver);
      if (!approve) return refused(`Step ${name} needs approval from ${spec.approver}, which is not loaded, so it did not run.`);
      let decision: FlowStepDecision;
      try {
        decision = await approve({ runId, flowName, task: name, phase, options: { ...(options ?? {}) }, token: spec.token });
      } catch (e) {
        return refused(`Step ${name} could not be approved, so it did not run: ${e instanceof Error ? e.message : String(e)}`);
      }
      if (!decision.allow) return refused(decision.reason ?? `Step ${name} was declined, so it did not run.`);
      return original();
    };
    return task;
  };
  return new Proxy(registry, {
    get(target, prop, receiver) {
      if (prop === "create") return create;
      const value = Reflect.get(target, prop, receiver);
      return typeof value === "function" ? value.bind(target) : value;
    },
  });
}
