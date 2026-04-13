#!/usr/bin/env tsx
/**
 * Generate the default ue-mcp.yml from code annotations.
 *
 * Walks ALL_TOOLS, reads each action's description + bridge/handler metadata,
 * and emits a YAML config with every built-in task declared.
 *
 * Run: tsx scripts/generate-default-config.ts
 * Output: dist/ue-mcp.default.yml
 */

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { projectTool } from "../src/tools/project.js";
import { assetTool } from "../src/tools/asset.js";
import { blueprintTool } from "../src/tools/blueprint.js";
import { levelTool } from "../src/tools/level.js";
import { materialTool } from "../src/tools/material.js";
import { animationTool } from "../src/tools/animation.js";
import { landscapeTool } from "../src/tools/landscape.js";
import { pcgTool } from "../src/tools/pcg.js";
import { foliageTool } from "../src/tools/foliage.js";
import { niagaraTool } from "../src/tools/niagara.js";
import { audioTool } from "../src/tools/audio.js";
import { widgetTool } from "../src/tools/widget.js";
import { editorTool } from "../src/tools/editor.js";
import { reflectionTool } from "../src/tools/reflection.js";
import { gameplayTool } from "../src/tools/gameplay.js";
import { gasTool } from "../src/tools/gas.js";
import { networkingTool } from "../src/tools/networking.js";
import { demoTool } from "../src/tools/demo.js";
import { feedbackTool } from "../src/tools/feedback.js";
import type { ToolDef } from "../src/types.js";
import { buildDefaults } from "../src/flow/loader.js";

const ALL_TOOLS: ToolDef[] = [
  projectTool, assetTool, blueprintTool, levelTool, materialTool,
  animationTool, landscapeTool, pcgTool, foliageTool, niagaraTool,
  audioTool, widgetTool, editorTool, reflectionTool, gameplayTool,
  gasTool, networkingTool, demoTool, feedbackTool,
];

function generate(): string {
  const lines: string[] = [];

  lines.push("# Auto-generated — do not edit by hand.");
  lines.push("# Source: scripts/generate-default-config.ts");
  lines.push("");
  lines.push("ue-mcp:");
  lines.push("  version: 1");
  lines.push("");
  lines.push("tasks:");

  for (const tool of ALL_TOOLS) {
    lines.push("");
    lines.push(`  # ── ${tool.name} ──`);

    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;
      const desc = spec.description;
      const isBridge = !!spec.bridge;
      // Handler tasks are registered by name; bridge tasks use the generic class_path
      const classPath = isBridge ? "ue-mcp.bridge" : taskName;

      lines.push(`  ${taskName}:`);
      lines.push(`    class_path: ${classPath}`);
      lines.push(`    group: ${tool.name}`);
      if (desc) {
        lines.push(`    description: ${yamlString(desc)}`);
      }
      if (isBridge) {
        lines.push(`    options:`);
        lines.push(`      method: ${spec.bridge}`);
      }
    }
  }

  // Emit flows from the same source of truth as the runtime defaults
  const defaults = buildDefaults(ALL_TOOLS);
  const flows = defaults.flows as Record<string, { description: string; steps: Record<string, { task?: string; flow?: string; options?: Record<string, unknown> }> }>;

  lines.push("");
  lines.push("flows:");

  for (const [flowName, flowDef] of Object.entries(flows)) {
    lines.push("");
    lines.push(`  ${flowName}:`);
    lines.push(`    description: ${yamlString(flowDef.description)}`);
    lines.push(`    steps:`);

    const sortedKeys = Object.keys(flowDef.steps)
      .map(Number)
      .sort((a, b) => a - b);

    for (const key of sortedKeys) {
      const step = flowDef.steps[String(key)];
      lines.push(`      ${key}:`);
      if (step.task) lines.push(`        task: ${step.task}`);
      if (step.flow) lines.push(`        flow: ${step.flow}`);
      if (step.options && Object.keys(step.options).length > 0) {
        lines.push(`        options:`);
        for (const [optKey, optVal] of Object.entries(step.options)) {
          if (typeof optVal === "object" && optVal !== null) {
            // Inline YAML object (e.g. { x: 0, y: 0, z: 0 })
            const inner = Object.entries(optVal)
              .map(([k, v]) => `${k}: ${v}`)
              .join(", ");
            lines.push(`          ${optKey}: { ${inner} }`);
          } else {
            lines.push(`          ${optKey}: ${optVal}`);
          }
        }
      }
    }
  }

  lines.push("");

  return lines.join("\n");
}

/** Escape a string for inline YAML (quote if it contains special chars). */
function yamlString(s: string): string {
  if (/[:#\[\]{}&*!|>'"%@`]/.test(s) || s.includes("\n")) {
    return `"${s.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
  }
  return s;
}

// ── Main ──────────────────────────────────────────────────────────

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");
const outDir = path.join(root, "dist");

if (!fs.existsSync(outDir)) {
  fs.mkdirSync(outDir, { recursive: true });
}

const yaml = generate();
const outPath = path.join(outDir, "ue-mcp.default.yml");
fs.writeFileSync(outPath, yaml, "utf-8");

const taskCount = (yaml.match(/^\s{2}\w+\.\w+:/gm) ?? []).length;
console.log(`[generate] ${outPath} — ${taskCount} tasks`);
