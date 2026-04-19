import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import {
  OntologyRegistry,
  createHandlerRegistryProjector,
  parse,
  parseFile,
  compose,
  select,
  parseSelector,
} from "../../src/ontology/index.js";
import { serializeFragment } from "../../src/ontology/index.js";

function fakeTools(): ToolDef[] {
  const blueprint = categoryTool("blueprint", "BP authoring.", {
    list_variables: bp("List variables", "bp_list"),
    create_variable: bp("Create a variable", "bp_create"),
    delete_variable: bp("Delete a variable", "bp_delete"),
  });
  const editor = categoryTool("editor", "Editor control.", {
    get_status: bp("Editor status", "editor_get_status"),
  });
  return [blueprint, editor];
}

describe("ontology parser: strict .kant subset", () => {
  it("round-trips an emitted fragment back to an equivalent tree", () => {
    const tools = fakeTools();
    const proj = createHandlerRegistryProjector(tools);
    const frag = proj.project(undefined as never);
    const text = serializeFragment(frag);
    const parsed = parse(text, "test");

    expect(parsed.blocks.UE).toBeDefined();
    const ue = parsed.blocks.UE;
    const tools_ = ue.children!.Mediation.children!.Registry.children!.Tools;
    expect(Object.keys(tools_.children!).sort()).toEqual(["blueprint", "editor"]);
    const bpActions = tools_.children!.blueprint.children!.Actions.children!;
    expect(Object.keys(bpActions).sort()).toEqual(["create_variable", "delete_variable", "list_variables"]);
    const del = bpActions.delete_variable;
    expect(del.purpose).toBe("Delete a variable");
    const clFlag = del.fields!.classification as { kind?: string; value?: number; marker?: string };
    expect(clFlag.kind).toBe("signal");
    expect(clFlag.marker).toBe("destructive");
    expect(clFlag.value).toBe(1);
  });

  it("rejects booleans, nulls, and inline arrays", () => {
    expect(() => parse("root:\n  enabled: true\n", "x")).toThrow(/booleans/);
    expect(() => parse("root:\n  value: null\n", "x")).toThrow(/nulls/);
    expect(() => parse("root:\n  items: [1,2]\n", "x")).toThrow(/inline/);
  });

  it("parses the committed kernel files", () => {
    const repoRoot = path.resolve(__dirname, "..", "..");
    const root = parseFile(path.join(repoRoot, "ontology", "kernel", "root.cairn"));
    const mediation = parseFile(path.join(repoRoot, "ontology", "kernel", "mediation.cairn"));
    expect(root.meanings.UE.meaning).toBe("Unreal Engine");
    expect(root.blocks.UE.children!.Mediation).toBeDefined();
    expect(mediation.meanings.Registry.category).toBe("/UE/Mediation");
  });
});

describe("ontology composer: priority + deep merge", () => {
  it("later layers override earlier at matching paths; non-overridden children fall through", () => {
    const base = parse(
      ["root@UE:", "  Mediation:", "    meaning: \"Mediation\"", "    Registry:", "      meaning: \"Registry\""].join("\n"),
      "base",
    );
    const overlay = parse(
      ["root@UE:", "  Mediation:", "    Registry:", "      meaning: \"Overridden Registry\"", "      note: \"added by overlay\""].join("\n"),
      "overlay",
    );

    const view = compose([
      { priority: 0, fragment: base },
      { priority: 1, fragment: overlay },
    ]);
    const reg = view.root.children!.Mediation.children!.Registry;
    expect(reg.meaning).toBe("Overridden Registry");
    expect(reg.fields!.note).toBe("added by overlay");
    // The parent Mediation keeps its meaning from the base layer.
    expect(view.root.children!.Mediation.meaning).toBe("Mediation");
  });

  it("composes kernel + projected fragment into a walkable tree", () => {
    const repoRoot = path.resolve(__dirname, "..", "..");
    const kernelRoot = parseFile(path.join(repoRoot, "ontology", "kernel", "root.cairn"));
    const kernelMed = parseFile(path.join(repoRoot, "ontology", "kernel", "mediation.cairn"));

    const proj = createHandlerRegistryProjector(fakeTools());
    const projText = serializeFragment(proj.project(undefined as never));
    const projected = parse(projText, "projected");

    const view = compose([
      { priority: 0, fragment: kernelRoot },
      { priority: 0, fragment: kernelMed },
      { priority: 1, fragment: projected },
    ]);
    expect(view.root.children!.Mediation.children!.Registry.children!.Tools).toBeDefined();
  });
});

describe("ontology selector: path walks and predicates", () => {
  function setupView() {
    const repoRoot = path.resolve(__dirname, "..", "..");
    const kernelRoot = parseFile(path.join(repoRoot, "ontology", "kernel", "root.cairn"));
    const kernelMed = parseFile(path.join(repoRoot, "ontology", "kernel", "mediation.cairn"));
    const proj = createHandlerRegistryProjector(fakeTools());
    const projected = parse(serializeFragment(proj.project(undefined as never)), "projected");
    return compose([
      { priority: 0, fragment: kernelRoot },
      { priority: 0, fragment: kernelMed },
      { priority: 1, fragment: projected },
    ]);
  }

  it("parses a selector with a predicate", () => {
    const s = parseSelector("/UE/Mediation/Registry/Tools/*/Actions/*@classification=destructive");
    expect(s.segments).toEqual(["UE", "Mediation", "Registry", "Tools", "*", "Actions", "*"]);
    expect(s.predicate).toEqual({ field: "classification", op: "=", value: "destructive" });
  });

  it("exact path match resolves one point", () => {
    const view = setupView();
    const r = select(view.root, "/UE/Mediation/Registry/Tools/blueprint");
    expect(r).toHaveLength(1);
    expect(r[0].path).toBe("/UE/Mediation/Registry/Tools/blueprint");
  });

  it("'*' matches one segment", () => {
    const view = setupView();
    const r = select(view.root, "/UE/Mediation/Registry/Tools/*");
    const names = r.map((m) => m.path.split("/").pop()).sort();
    expect(names).toEqual(["blueprint", "editor"]);
  });

  it("'**' matches recursively", () => {
    const view = setupView();
    const r = select(view.root, "/UE/Mediation/Registry/Tools/**");
    const names = r.map((m) => m.path.split("/").pop());
    expect(names).toContain("blueprint");
    expect(names).toContain("Actions");
    expect(names).toContain("delete_variable");
  });

  it("predicate filters by signal marker (=)", () => {
    const view = setupView();
    const r = select(
      view.root,
      "/UE/Mediation/Registry/Tools/*/Actions/*@classification=destructive",
    );
    expect(r).toHaveLength(1);
    expect(r[0].path.endsWith("/delete_variable")).toBe(true);
  });

  it("predicate filters by signal numeric value (>=)", () => {
    const view = setupView();
    const r = select(
      view.root,
      "/UE/Mediation/Registry/Tools/*/Actions/*@classification>=0.5",
    );
    const names = r.map((m) => m.path.split("/").pop()).sort();
    expect(names).toEqual(["create_variable", "delete_variable"]);
  });

  it("returns zero matches for a non-existent path", () => {
    const view = setupView();
    const r = select(view.root, "/UE/Does/Not/Exist");
    expect(r).toHaveLength(0);
  });
});

describe("ontology registry: composeView + query end-to-end", () => {
  let tmpDir: string;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ont-reg-"));
  });
  afterEach(() => {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  it("composes kernel + projected cache + repo-local in priority order", () => {
    const repoRoot = path.resolve(__dirname, "..", "..");
    const kernelDir = path.join(repoRoot, "ontology", "kernel");
    const projectedDir = path.join(tmpDir, "projected");
    const repoLocalDir = path.join(tmpDir, "local");
    fs.mkdirSync(projectedDir, { recursive: true });
    fs.mkdirSync(repoLocalDir, { recursive: true });

    const registry = new OntologyRegistry(
      () => projectedDir,
      () => ({
        kernel: { priority: 0, paths: [kernelDir] },
        projected: { priority: 1, paths: [projectedDir] },
        repoLocal: { priority: 2, paths: [repoLocalDir] },
      }),
    );
    registry.register(createHandlerRegistryProjector(fakeTools()), () => undefined);
    registry.projectAll();

    // A repo-local layer that overrides one action's purpose. All
    // layers root at UE@UE: so paths compose cleanly.
    fs.writeFileSync(
      path.join(repoLocalDir, "override.cairn"),
      [
        "UE@UE:",
        "  Mediation:",
        "    Registry:",
        "      Tools:",
        "        blueprint:",
        "          Actions:",
        "            delete_variable:",
        "              purpose: \"Override: audit requires explicit approval\"",
        "",
      ].join("\n"),
      "utf-8",
    );

    const q = registry.query("/UE/Mediation/Registry/Tools/blueprint/Actions/delete_variable");
    expect(q.matches).toHaveLength(1);
    expect(q.matches[0].point.purpose).toBe("Override: audit requires explicit approval");
  });
});
