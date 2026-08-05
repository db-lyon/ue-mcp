import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("level — read", () => {
  it("get_world_outliner", async () => {
    const r = await callBridge(bridge, "get_world_outliner");
    expect(r.ok, r.error).toBe(true);
    expect(r.result).toBeDefined();
  });

  it("get_world_outliner with classFilter", async () => {
    const r = await callBridge(bridge, "get_world_outliner", { classFilter: "StaticMeshActor" });
    expect(r.ok, r.error).toBe(true);
  });

  it("get_world_outliner with nameFilter", async () => {
    const r = await callBridge(bridge, "get_world_outliner", { nameFilter: "Light" });
    expect(r.ok, r.error).toBe(true);
  });

  it("get_current_level", async () => {
    const r = await callBridge(bridge, "get_current_level");
    expect(r.ok, r.error).toBe(true);
  });

  it("list_levels", async () => {
    const r = await callBridge(bridge, "list_levels");
    expect(r.ok, r.error).toBe(true);
  });

  it("get_selected_actors", async () => {
    const r = await callBridge(bridge, "get_selected_actors");
    expect(r.ok, r.error).toBe(true);
  });

  it("list_volumes", async () => {
    const r = await callBridge(bridge, "list_volumes");
    expect(r.ok, r.error).toBe(true);
  });

  it("list_volumes with type filter", async () => {
    const r = await callBridge(bridge, "list_volumes", { volumeType: "BlockingVolume" });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("level — actor details (dynamic)", () => {
  let firstActor: string | undefined;

  beforeAll(async () => {
    const r = await callBridge(bridge, "get_world_outliner");
    if (r.ok) {
      const actors = resultArray(r.result, "actors", "outliner");
      if (actors && actors.length > 0) {
        const first = actors[0] as Record<string, unknown>;
        firstActor = (first.label ?? first.name ?? first.actorLabel) as string | undefined;
      }
    }
  });

  it("get_actor_details", async ({ skip }) => {
    if (!firstActor) skip();
    const r = await callBridge(bridge, "get_actor_details", { actorLabel: firstActor });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("level — write (with cleanup)", () => {
  const placed: string[] = [];
  const createdAssets: string[] = [];

  afterAll(async () => {
    for (const label of [...placed].reverse()) {
      await callBridge(bridge, "delete_actor", { actorLabel: label });
    }
    for (const assetPath of createdAssets) {
      await callBridge(bridge, "delete_asset", { assetPath, force: true });
    }
  });

  it("place_actor (Cube)", async () => {
    const r = await callBridge(bridge, "place_actor", {
      actorClass: "/Script/Engine.StaticMeshActor",
      label: "MCPTest_Cube",
      location: { x: 0, y: 0, z: 500 },
    });
    expect(r.ok, r.error).toBe(true);
    placed.push("MCPTest_Cube");
  });

  it("move_actor", async () => {
    const r = await callBridge(bridge, "move_actor", {
      actorLabel: "MCPTest_Cube",
      location: { x: 100, y: 100, z: 500 },
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("select_actors", async () => {
    const r = await callBridge(bridge, "select_actors", { actorLabels: ["MCPTest_Cube"] });
    expect(r.ok, r.error).toBe(true);
  });

  it("spawn_light (point)", async () => {
    const r = await callBridge(bridge, "spawn_light", {
      lightType: "point",
      location: { x: 200, y: 0, z: 500 },
      intensity: 5000,
      label: "MCPTest_Light",
    });
    expect(r.ok, r.error).toBe(true);
    placed.push("MCPTest_Light");
  });

  it("set_light_properties", async () => {
    const r = await callBridge(bridge, "set_light_properties", {
      actorLabel: "MCPTest_Light",
      intensity: 8000,
      color: { r: 255, g: 128, b: 0 },
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("spawn_volume (BlockingVolume)", async () => {
    const r = await callBridge(bridge, "spawn_volume", {
      volumeType: "BlockingVolume",
      location: { x: 300, y: 0, z: 500 },
      extent: { x: 100, y: 100, z: 100 },
      label: "MCPTest_Volume",
    });
    expect(r.ok, r.error).toBe(true);
    placed.push("MCPTest_Volume");
  });

  it("add_component_to_actor", async () => {
    const r = await callBridge(bridge, "add_component_to_actor", {
      actorLabel: "MCPTest_Cube",
      componentClass: "PointLightComponent",
      componentName: "TestLight",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("attach_component targets named SceneComponents and sockets", async () => {
    const suffix = Date.now();
    const assetName = `AttachSocketMesh_${suffix}`;
    const assetPath = `${TEST_PREFIX}/${assetName}`;
    const assetObjectPath = `${assetPath}.${assetName}`;
    const parentLabel = `MCPTest_AttachParent_${suffix}`;
    const childLabel = `MCPTest_AttachChild_${suffix}`;
    const socketName = "MCPTestSocket";

    const duplicated = await callBridge(bridge, "duplicate_asset", {
      sourcePath: "/Engine/BasicShapes/Cube.Cube",
      destinationPath: assetPath,
    });
    expect(duplicated.ok, duplicated.error).toBe(true);
    expect((duplicated.result as Record<string, unknown>).success).not.toBe(false);
    createdAssets.push(assetPath);

    const socket = await callBridge(bridge, "add_socket", {
      assetPath: assetObjectPath,
      socketName,
      relativeLocation: { x: 0, y: 0, z: 100 },
    });
    expect(socket.ok, socket.error).toBe(true);
    expect((socket.result as Record<string, unknown>).success).not.toBe(false);

    const parent = await callBridge(bridge, "place_actor", {
      actorClass: "/Script/Engine.StaticMeshActor",
      label: parentLabel,
      staticMesh: assetObjectPath,
      location: { x: 500, y: 0, z: 500 },
    });
    expect(parent.ok, parent.error).toBe(true);
    placed.push(parentLabel);

    const child = await callBridge(bridge, "place_actor", {
      actorClass: "/Script/Engine.StaticMeshActor",
      label: childLabel,
      staticMesh: "/Engine/BasicShapes/Cube.Cube",
    });
    expect(child.ok, child.error).toBe(true);
    placed.push(childLabel);

    const parentTree = await callBridge(bridge, "get_component_tree", { actorLabel: parentLabel });
    expect(parentTree.ok, parentTree.error).toBe(true);
    const parentComponents = ((parentTree.result as Record<string, unknown>).components ?? []) as Array<Record<string, unknown>>;
    const parentMesh = parentComponents.find((component) =>
      String(component.class).includes("StaticMeshComponent"),
    );
    expect(parentMesh?.name).toBeTruthy();
    const parentComponentName = String(parentMesh!.name);

    const attachedRoot = await callBridge(bridge, "attach_component", {
      childLabel,
      parentLabel,
      parentComponentName: parentComponentName.toLowerCase(),
      socketName,
      attachRule: "SnapToTarget",
    });
    expect(attachedRoot.ok, attachedRoot.error).toBe(true);
    const rootResult = attachedRoot.result as Record<string, unknown>;
    expect(rootResult.success).not.toBe(false);
    expect(rootResult.attached).toBe(true);
    expect(rootResult.parentComponentName).toBe(parentComponentName);
    expect(rootResult.parentComponentClass).toBe("StaticMeshComponent");
    expect(rootResult.socketName).toBe(socketName);
    expect(rootResult.attachRule).toBe("SnapToTarget");
    expect(rootResult.weldSimulatedBodies).toBe(false);
    expect(rootResult.attachmentChanged).toBe(true);

    const alreadyAttached = await callBridge(bridge, "attach_component", {
      childLabel,
      parentLabel,
      parentComponentName,
      socketName,
      attachRule: "KeepWorld",
    });
    expect(alreadyAttached.ok, alreadyAttached.error).toBe(true);
    const alreadyAttachedResult = alreadyAttached.result as Record<string, unknown>;
    expect(alreadyAttachedResult.success).not.toBe(false);
    expect(alreadyAttachedResult.alreadyAttached).toBe(true);
    expect(alreadyAttachedResult.attachmentChanged).toBe(false);
    expect(alreadyAttachedResult.attachmentRulesApplied).toBe(false);

    const attachedTree = await callBridge(bridge, "get_component_tree", { actorLabel: childLabel });
    const attachedComponents = ((attachedTree.result as Record<string, unknown>).components ?? []) as Array<Record<string, unknown>>;
    const childRoot = attachedComponents.find((component) => component.name === rootResult.childComponentName);
    expect(childRoot?.attachParent).toBe(parentComponentName);
    expect(childRoot?.attachSocket).toBe(socketName);

    const detachedRoot = await callBridge(bridge, "detach_component", { childLabel });
    expect(detachedRoot.ok, detachedRoot.error).toBe(true);
    const detachedRootResult = detachedRoot.result as Record<string, unknown>;
    expect(detachedRootResult.detached).toBe(true);
    expect(detachedRootResult.alreadyDetached).toBe(false);
    expect(detachedRootResult.detachmentChanged).toBe(true);
    expect(detachedRootResult.previousParentLabel).toBe(parentLabel);

    const childComponentName = "SocketChildComponent";
    const addedComponent = await callBridge(bridge, "add_component_to_actor", {
      actorLabel: childLabel,
      componentClass: "SceneComponent",
      componentName: childComponentName,
    });
    expect(addedComponent.ok, addedComponent.error).toBe(true);
    expect((addedComponent.result as Record<string, unknown>).success).not.toBe(false);

    const attachedComponent = await callBridge(bridge, "attach_component", {
      childLabel,
      parentLabel,
      childComponentName: childComponentName.toLowerCase(),
      parentComponentName,
      socketName,
      attachRule: "KeepRelative",
    });
    expect(attachedComponent.ok, attachedComponent.error).toBe(true);
    const componentResult = attachedComponent.result as Record<string, unknown>;
    expect(componentResult.success).not.toBe(false);
    expect(componentResult.attached).toBe(true);
    expect(componentResult.childComponentName).toBe(childComponentName);
    expect(componentResult.childIsRoot).toBe(false);

    const invalidRule = await callBridge(bridge, "attach_component", {
      childLabel,
      parentLabel,
      childComponentName,
      parentComponentName,
      attachRule: "Teleport",
    });
    expect(invalidRule.ok, invalidRule.error).toBe(true);
    const invalidRuleResult = invalidRule.result as Record<string, unknown>;
    expect(invalidRuleResult.success).toBe(false);
    expect(String(invalidRuleResult.error)).toContain("Invalid attachRule");

    const missingSocket = await callBridge(bridge, "attach_component", {
      childLabel,
      parentLabel,
      childComponentName,
      parentComponentName,
      socketName: "MissingSocket",
    });
    expect(missingSocket.ok, missingSocket.error).toBe(true);
    const missingSocketResult = missingSocket.result as Record<string, unknown>;
    expect(missingSocketResult.success).toBe(false);
    expect(String(missingSocketResult.error)).toContain("does not exist");

    const stillAttachedTree = await callBridge(bridge, "get_component_tree", { actorLabel: childLabel });
    const stillAttachedComponents = ((stillAttachedTree.result as Record<string, unknown>).components ?? []) as Array<Record<string, unknown>>;
    const namedChild = stillAttachedComponents.find((component) => component.name === childComponentName);
    expect(namedChild?.attachParent).toBe(parentComponentName);
    expect(namedChild?.attachSocket).toBe(socketName);

    const detachedComponent = await callBridge(bridge, "detach_component", {
      childLabel,
      childComponentName,
    });
    expect(detachedComponent.ok, detachedComponent.error).toBe(true);
    const detachedComponentResult = detachedComponent.result as Record<string, unknown>;
    expect(detachedComponentResult.success).not.toBe(false);
    expect(detachedComponentResult.detached).toBe(true);
    expect(detachedComponentResult.alreadyDetached).toBe(false);
    expect(detachedComponentResult.detachmentChanged).toBe(true);
    expect(detachedComponentResult.previousParentLabel).toBe(parentLabel);
    expect(detachedComponentResult.previousParentComponentName).toBe(parentComponentName);
    expect(detachedComponentResult.previousParentComponentClass).toBe("StaticMeshComponent");
    expect(detachedComponentResult.previousSocketName).toBe(socketName);

    const attachedKeepWorld = await callBridge(bridge, "attach_component", {
      childLabel,
      parentLabel,
      childComponentName,
      parentComponentName,
      socketName,
    });
    expect(attachedKeepWorld.ok, attachedKeepWorld.error).toBe(true);
    const keepWorldResult = attachedKeepWorld.result as Record<string, unknown>;
    expect(keepWorldResult.success).not.toBe(false);
    expect(keepWorldResult.attachRule).toBe("KeepWorld");
    expect(keepWorldResult.attachmentChanged).toBe(true);

    const finalDetach = await callBridge(bridge, "detach_component", {
      childLabel,
      childComponentName,
    });
    expect(finalDetach.ok, finalDetach.error).toBe(true);
    const finalDetachResult = finalDetach.result as Record<string, unknown>;
    expect(finalDetachResult.detached).toBe(true);
    expect(finalDetachResult.alreadyDetached).toBe(false);
    expect(finalDetachResult.detachmentChanged).toBe(true);

    const alreadyDetached = await callBridge(bridge, "detach_component", {
      childLabel,
      childComponentName,
    });
    expect(alreadyDetached.ok, alreadyDetached.error).toBe(true);
    const alreadyDetachedResult = alreadyDetached.result as Record<string, unknown>;
    expect(alreadyDetachedResult.success).not.toBe(false);
    expect(alreadyDetachedResult.detached).toBe(true);
    expect(alreadyDetachedResult.alreadyDetached).toBe(true);
    expect(alreadyDetachedResult.detachmentChanged).toBe(false);
    expect(alreadyDetachedResult.previousParentLabel).toBe("");
    expect(alreadyDetachedResult.previousParentComponentName).toBe("");
    expect(alreadyDetachedResult.previousSocketName).toBe("");

    const sameActorParentComponentName = "SocketParentComponent";
    const addedSameActorParent = await callBridge(bridge, "add_component_to_actor", {
      actorLabel: childLabel,
      componentClass: "SceneComponent",
      componentName: sameActorParentComponentName,
    });
    expect(addedSameActorParent.ok, addedSameActorParent.error).toBe(true);
    expect((addedSameActorParent.result as Record<string, unknown>).success).not.toBe(false);

    const sameActorAttach = await callBridge(bridge, "attach_component", {
      childLabel,
      parentLabel: childLabel,
      childComponentName,
      parentComponentName: sameActorParentComponentName,
    });
    expect(sameActorAttach.ok, sameActorAttach.error).toBe(true);
    expect((sameActorAttach.result as Record<string, unknown>).success).not.toBe(false);

    const selfAttach = await callBridge(bridge, "attach_component", {
      childLabel,
      parentLabel: childLabel,
      childComponentName,
      parentComponentName: childComponentName,
    });
    expect(selfAttach.ok, selfAttach.error).toBe(true);
    const selfAttachResult = selfAttach.result as Record<string, unknown>;
    expect(selfAttachResult.success).toBe(false);
    expect(String(selfAttachResult.error)).toContain("to itself");

    const cycleAttach = await callBridge(bridge, "attach_component", {
      childLabel,
      parentLabel: childLabel,
      childComponentName: sameActorParentComponentName,
      parentComponentName: childComponentName,
    });
    expect(cycleAttach.ok, cycleAttach.error).toBe(true);
    const cycleAttachResult = cycleAttach.result as Record<string, unknown>;
    expect(cycleAttachResult.success).toBe(false);
    expect(String(cycleAttachResult.error)).toContain("descendant");

    const afterRejectedTopology = await callBridge(bridge, "get_component_tree", { actorLabel: childLabel });
    expect(afterRejectedTopology.ok, afterRejectedTopology.error).toBe(true);
    const afterRejectedComponents = ((afterRejectedTopology.result as Record<string, unknown>).components ?? []) as Array<Record<string, unknown>>;
    const afterRejectedChild = afterRejectedComponents.find((component) => component.name === childComponentName);
    expect(afterRejectedChild?.attachParent).toBe(sameActorParentComponentName);
  });

  it("attach_component rejects cross-level named-component references", async () => {
    const suffix = Date.now();
    const parentLabel = `MCPTest_CrossLevelParent_${suffix}`;
    const childLabel = `MCPTest_CrossLevelChild_${suffix}`;
    const childComponentName = "CrossLevelChildComponent";
    const sublevelPath = "/Engine/Maps/Entry";
    const sublevelName = "Entry";

    const current = await callBridge(bridge, "get_current_edit_level");
    expect(current.ok, current.error).toBe(true);
    const currentResult = current.result as Record<string, unknown>;
    const originalLevelName = String(currentResult.levelName);
    let sublevelAdded = false;

    try {
      const addedSublevel = await callBridge(bridge, "add_streaming_sublevel", {
        levelPath: sublevelPath,
        initiallyLoaded: true,
        initiallyVisible: true,
      });
      expect(addedSublevel.ok, addedSublevel.error).toBe(true);
      expect((addedSublevel.result as Record<string, unknown>).success).not.toBe(false);
      sublevelAdded = true;

      const selectedSublevel = await callBridge(bridge, "set_current_edit_level", {
        levelName: sublevelName,
      });
      expect(selectedSublevel.ok, selectedSublevel.error).toBe(true);
      expect((selectedSublevel.result as Record<string, unknown>).success).not.toBe(false);

      const parent = await callBridge(bridge, "place_actor", {
        actorClass: "/Script/Engine.StaticMeshActor",
        label: parentLabel,
        staticMesh: "/Engine/BasicShapes/Cube.Cube",
      });
      expect(parent.ok, parent.error).toBe(true);
      expect((parent.result as Record<string, unknown>).success).not.toBe(false);

      const restoredPersistent = await callBridge(bridge, "set_current_edit_level", {
        levelName: originalLevelName,
      });
      expect(restoredPersistent.ok, restoredPersistent.error).toBe(true);
      expect((restoredPersistent.result as Record<string, unknown>).success).not.toBe(false);

      const child = await callBridge(bridge, "place_actor", {
        actorClass: "/Script/Engine.StaticMeshActor",
        label: childLabel,
        staticMesh: "/Engine/BasicShapes/Cube.Cube",
      });
      expect(child.ok, child.error).toBe(true);
      expect((child.result as Record<string, unknown>).success).not.toBe(false);

      const addedChildComponent = await callBridge(bridge, "add_component_to_actor", {
        actorLabel: childLabel,
        componentClass: "SceneComponent",
        componentName: childComponentName,
      });
      expect(addedChildComponent.ok, addedChildComponent.error).toBe(true);
      expect((addedChildComponent.result as Record<string, unknown>).success).not.toBe(false);

      const beforeTree = await callBridge(bridge, "get_component_tree", { actorLabel: childLabel });
      expect(beforeTree.ok, beforeTree.error).toBe(true);
      const beforeComponents = ((beforeTree.result as Record<string, unknown>).components ?? []) as Array<Record<string, unknown>>;
      const beforeChild = beforeComponents.find((component) => component.name === childComponentName);
      const previousAttachParent = beforeChild?.attachParent;

      const rejected = await callBridge(bridge, "attach_component", {
        childLabel,
        parentLabel,
        childComponentName,
      });
      expect(rejected.ok, rejected.error).toBe(true);
      const rejectedResult = rejected.result as Record<string, unknown>;
      expect(rejectedResult.success).toBe(false);
      expect(String(rejectedResult.error)).toContain("same level");

      const afterTree = await callBridge(bridge, "get_component_tree", { actorLabel: childLabel });
      expect(afterTree.ok, afterTree.error).toBe(true);
      const afterComponents = ((afterTree.result as Record<string, unknown>).components ?? []) as Array<Record<string, unknown>>;
      const afterChild = afterComponents.find((component) => component.name === childComponentName);
      expect(afterChild?.attachParent).toBe(previousAttachParent);
    } finally {
      await callBridge(bridge, "set_current_edit_level", { levelName: originalLevelName });
      await callBridge(bridge, "delete_actor", { actorLabel: childLabel });
      await callBridge(bridge, "delete_actor", { actorLabel: parentLabel });
      if (sublevelAdded) {
        await callBridge(bridge, "remove_streaming_sublevel", { levelName: sublevelName });
      }
    }
  });

  it("delete_actor", async () => {
    const r = await callBridge(bridge, "place_actor", {
      actorClass: "/Script/Engine.StaticMeshActor",
      label: "MCPTest_DeleteMe",
      location: { x: 0, y: 0, z: 999 },
    });
    expect(r.ok, r.error).toBe(true);
    const d = await callBridge(bridge, "delete_actor", { actorLabel: "MCPTest_DeleteMe" });
    expect(d.ok, d.error).toBe(true);
  });
});
