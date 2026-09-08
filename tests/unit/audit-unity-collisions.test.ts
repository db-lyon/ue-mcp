import { describe, it, expect } from "vitest";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { findUnityCollisions } from "../../scripts/audit-unity-collisions.mjs";

const REPO_ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const PLUGIN_ROOT = path.join(REPO_ROOT, "plugin");

/**
 * Lays out .cpp files the way UBT expects, so the audit resolves the module
 * name from the path exactly as it does in the real tree.
 */
function moduleTree(files: Record<string, string>): string {
  const root = mkdtempSync(path.join(tmpdir(), "unity-collisions-"));
  const dir = path.join(root, "Source", "TestModule", "Private");
  mkdirSync(dir, { recursive: true });
  for (const [name, body] of Object.entries(files)) {
    writeFileSync(path.join(dir, name), body);
  }
  return root;
}

describe("the shipped plugin sources", () => {
  it("define no file-local helper twice within one module", () => {
    // A duplicate here compiles on the machine that wrote it and fails on a
    // user's build the moment unity groups the two files together. v1.2.0
    // shipped exactly that: IsProtectedAssetPath in both AssetHandlers.cpp and
    // AssetHandlers_BulkUpsert.cpp.
    const collisions = findUnityCollisions(PLUGIN_ROOT);
    const rendered = collisions
      .map((c) => `[${c.module}] ${c.signature}\n    ${c.files.join("\n    ")}`)
      .join("\n");
    expect(rendered).toBe("");
  });
});

describe("findUnityCollisions", () => {
  it("reports a helper defined identically in two files of one module", () => {
    const root = moduleTree({
      "A.cpp": "namespace\n{\n\tbool IsProtected(const FString& Path)\n\t{\n\t\treturn false;\n\t}\n}\n",
      "B.cpp": "namespace\n{\n\tbool IsProtected(const FString& Path)\n\t{\n\t\treturn true;\n\t}\n}\n",
    });
    const collisions = findUnityCollisions(root);
    expect(collisions).toHaveLength(1);
    expect(collisions[0].signature).toBe("IsProtected(const FString &)");
    expect(collisions[0].module).toBe("TestModule");
    expect(collisions[0].files).toHaveLength(2);
  });

  it("ignores the parameter names, which do not participate in overloading", () => {
    const root = moduleTree({
      "A.cpp": "namespace\n{\n\tbool Check(const FString& Path)\n\t{\n\t\treturn false;\n\t}\n}\n",
      "B.cpp": "namespace\n{\n\tbool Check(const FString& Other)\n\t{\n\t\treturn true;\n\t}\n}\n",
    });
    expect(findUnityCollisions(root)).toHaveLength(1);
  });

  it("accepts genuine overloads, which share a unity blob legally", () => {
    // FindNodeByName and PinToJson really are spelled this way in two handler
    // files each, and really do compile: the parameter types differ.
    const root = moduleTree({
      "A.cpp": "namespace\n{\n\tUSoundNode* FindNodeByName(USoundCue* Cue, const FString& Name)\n\t{\n\t\treturn nullptr;\n\t}\n}\n",
      "B.cpp": "namespace\n{\n\tUPCGNode* FindNodeByName(UPCGGraph* Graph, const FString& Name)\n\t{\n\t\treturn nullptr;\n\t}\n}\n",
    });
    expect(findUnityCollisions(root)).toEqual([]);
  });

  it("accepts the same helper in two different modules", () => {
    const root = mkdtempSync(path.join(tmpdir(), "unity-collisions-"));
    for (const mod of ["ModuleOne", "ModuleTwo"]) {
      const dir = path.join(root, "Source", mod, "Private");
      mkdirSync(dir, { recursive: true });
      writeFileSync(
        path.join(dir, "Handler.cpp"),
        "namespace\n{\n\tbool Check(const FString& Path)\n\t{\n\t\treturn false;\n\t}\n}\n"
      );
    }
    expect(findUnityCollisions(root)).toEqual([]);
  });

  it("does not mistake a call inside a function body for a definition", () => {
    const root = moduleTree({
      "A.cpp": "namespace\n{\n\tvoid Run()\n\t{\n\t\tif (Check(Path))\n\t\t{\n\t\t\tDoThing();\n\t\t}\n\t}\n}\n",
      "B.cpp": "namespace\n{\n\tvoid Other()\n\t{\n\t\tif (Check(Path))\n\t\t{\n\t\t\tDoThing();\n\t\t}\n\t}\n}\n",
    });
    expect(findUnityCollisions(root)).toEqual([]);
  });

  it("does not read braces inside comments or string literals as scope", () => {
    const root = moduleTree({
      "A.cpp":
        'namespace\n{\n\t// bool Check(const FString& Path) {\n\tconst TCHAR* Brace = TEXT("{");\n\tbool Real(int32 Value)\n\t{\n\t\treturn Value > 0;\n\t}\n}\n',
      "B.cpp": "namespace\n{\n\tbool Real(int32 Value)\n\t{\n\t\treturn Value < 0;\n\t}\n}\n",
    });
    const collisions = findUnityCollisions(root);
    expect(collisions).toHaveLength(1);
    expect(collisions[0].signature).toBe("Real(int32)");
  });
});

describe("collisions that a named namespace and a using-directive create", () => {
  // The shape that shipped on this branch: AnimationHandlers_StateMachine.cpp
  // held `static void CompileAndSave(UBlueprint*)` at global scope while
  // BlueprintHandlers_Depth.cpp held MCPBlueprintDepth::CompileAndSave with the
  // same parameter list and opened the namespace in fourteen handler bodies.
  // Each file compiles alone. Put them in one blob and every one of those
  // fourteen unqualified calls sees both and resolves to neither: C2668.
  it("reports a named-namespace helper that a using-directive drops beside a file-scope static", () => {
    const root = moduleTree({
      "Anim.cpp": "static void CompileAndSave(UBlueprint* BP)\n{\n\tCompile(BP);\n}\n",
      "Depth.cpp":
        "namespace MCPDepth\n{\n\tstatic void CompileAndSave(UBlueprint* Blueprint)\n\t{\n\t\tCompile(Blueprint);\n\t}\n}\n\nvoid Handler()\n{\n\tusing namespace MCPDepth;\n\tCompileAndSave(BP);\n}\n",
    });
    const collisions = findUnityCollisions(root);
    expect(collisions).toHaveLength(1);
    expect(collisions[0].kind).toBe("ambiguity");
    expect(collisions[0].signature).toBe("MCPDepth::CompileAndSave(UBlueprint *)");
    expect(collisions[0].files).toHaveLength(2);
  });

  it("leaves the namespace alone when nothing opens it", () => {
    // Qualified-only use is exactly how a named namespace is supposed to work.
    const root = moduleTree({
      "Anim.cpp": "static void CompileAndSave(UBlueprint* BP)\n{\n\tCompile(BP);\n}\n",
      "Depth.cpp":
        "namespace MCPDepth\n{\n\tstatic void CompileAndSave(UBlueprint* Blueprint)\n\t{\n\t\tCompile(Blueprint);\n\t}\n}\n\nvoid Handler()\n{\n\tMCPDepth::CompileAndSave(BP);\n}\n",
    });
    expect(findUnityCollisions(root)).toEqual([]);
  });

  it("leaves the namespace alone when the signatures differ", () => {
    const root = moduleTree({
      "Anim.cpp": "static void CompileAndSave(UAnimBlueprint* BP)\n{\n\tCompile(BP);\n}\n",
      "Depth.cpp":
        "namespace MCPDepth\n{\n\tstatic void CompileAndSave(UBlueprint* Blueprint)\n\t{\n\t\tCompile(Blueprint);\n\t}\n}\n\nvoid Handler()\n{\n\tusing namespace MCPDepth;\n\tCompileAndSave(BP);\n}\n",
    });
    expect(findUnityCollisions(root)).toEqual([]);
  });

  it("does not report a file that already fails to compile on its own", () => {
    // Both definitions in one file is a bug, but not this audit's bug: it
    // breaks on a solo build, so unity grouping has nothing to do with it.
    const root = moduleTree({
      "Both.cpp":
        "static void CompileAndSave(UBlueprint* BP)\n{\n\tCompile(BP);\n}\n\nnamespace MCPDepth\n{\n\tstatic void CompileAndSave(UBlueprint* Blueprint)\n\t{\n\t\tCompile(Blueprint);\n\t}\n}\n\nvoid Handler()\n{\n\tusing namespace MCPDepth;\n\tCompileAndSave(BP);\n}\n",
    });
    expect(findUnityCollisions(root)).toEqual([]);
  });
});

describe("file-scope statics, which the anonymous-namespace scan never saw", () => {
  it("reports the same static helper defined in two files", () => {
    // Internal linkage makes this legal in two translation units and illegal in
    // the one the blob merges them into: C2084.
    const root = moduleTree({
      "A.cpp": "static bool IsProtected(const FString& Path)\n{\n\treturn false;\n}\n",
      "B.cpp": "static bool IsProtected(const FString& Path)\n{\n\treturn true;\n}\n",
    });
    const collisions = findUnityCollisions(root);
    expect(collisions).toHaveLength(1);
    expect(collisions[0].kind).toBe("redefinition");
    expect(collisions[0].signature).toBe("IsProtected(const FString &)");
  });

  it("reports a file-scope static against an anonymous-namespace helper", () => {
    // Two distinct entities, ::IsProtected and (anonymous)::IsProtected, that
    // unqualified lookup finds together.
    const root = moduleTree({
      "A.cpp": "static bool IsProtected(const FString& Path)\n{\n\treturn false;\n}\n",
      "B.cpp": "namespace\n{\n\tbool IsProtected(const FString& Path)\n\t{\n\t\treturn true;\n\t}\n}\n",
    });
    const collisions = findUnityCollisions(root);
    expect(collisions).toHaveLength(1);
    expect(collisions[0].kind).toBe("ambiguity");
  });

  it("accepts a static that only one file defines", () => {
    const root = moduleTree({
      "A.cpp": "static bool IsProtected(const FString& Path)\n{\n\treturn false;\n}\n",
      "B.cpp": "static bool IsAllowed(const FString& Path)\n{\n\treturn true;\n}\n",
    });
    expect(findUnityCollisions(root)).toEqual([]);
  });

  it("does not report an out-of-line member definition, which has external linkage", () => {
    const root = moduleTree({
      "A.cpp": "void FHandlers::Run(const FString& Path)\n{\n\tDoThing();\n}\n",
      "B.cpp": "void FOthers::Run(const FString& Path)\n{\n\tDoThing();\n}\n",
    });
    expect(findUnityCollisions(root)).toEqual([]);
  });

  it("does not read a struct's member functions as namespace-scope helpers", () => {
    const root = moduleTree({
      "A.cpp": "struct FLocal\n{\n\tbool Check(const FString& Path)\n\t{\n\t\treturn false;\n\t}\n};\n",
      "B.cpp": "struct FOther\n{\n\tbool Check(const FString& Path)\n\t{\n\t\treturn true;\n\t}\n};\n",
    });
    expect(findUnityCollisions(root)).toEqual([]);
  });
});
