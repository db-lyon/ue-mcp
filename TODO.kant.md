# Ontology — Evolution Roadmap

## Missing Concept Domains

### C++ Reflection Model
The ontology has no representation of C++ concepts. Blueprints don't exist in isolation — they inherit from C++ classes, call C++ functions, and use C++ structs/enums. The AI needs the conceptual skeleton of UE's reflection system.

- [ ] UCLASS specifiers: Blueprintable, BlueprintType, Abstract, NotBlueprintable, MinimalAPI, meta=(BlueprintSpawnableComponent)
- [ ] UPROPERTY specifiers: EditAnywhere, VisibleAnywhere, BlueprintReadWrite, BlueprintReadOnly, Category, meta=(ClampMin, ClampMax, AllowPrivateAccess), Replicated, ReplicatedUsing
- [ ] UFUNCTION specifiers: BlueprintCallable, BlueprintPure, BlueprintImplementableEvent, BlueprintNativeEvent, Server, Client, NetMulticast, Reliable/Unreliable
- [ ] USTRUCT / UENUM and their relationship to Blueprint-visible types
- [ ] The C++↔Blueprint contract: what specifiers make something visible, callable, or overridable from Blueprint
- [ ] Module structure: Runtime, Editor, Developer, UncookedOnly module types and when each is available

### Networking / Replication
Replication flags appear on every variable and function. The AI must understand the authority model.

- [ ] Authority model: Server, Client, AutonomousProxy, SimulatedProxy
- [ ] Property replication conditions: COND_None, COND_OwnerOnly, COND_SkipOwner, COND_SimulatedOnly, COND_AutonomousOnly, COND_InitialOnly
- [ ] RPC types: Server (client→server), Client (server→owning client), NetMulticast (server→all)
- [ ] Reliable vs Unreliable semantics
- [ ] Net relevancy and dormancy
- [ ] Relationship between replication and PropertyFlags in the Blueprint ontology

### Level / World Composition
- [ ] Actor-in-world placement model: transforms, attachment, component overrides
- [ ] Sub-level streaming: streaming volumes, level instances, level transforms
- [ ] World Partition: cells, data layers, runtime/editor grids, HLOD
- [ ] World settings: GameMode, default pawn, physics, gravity

### Animation System
- [ ] AnimBlueprint state machine: states, transitions, transition rules, blend logic
- [ ] AnimMontage structure: sections, notifies (AnimNotify vs AnimNotifyState), branching points, slot groups
- [ ] Blend spaces: 1D/2D, axes, sample points
- [ ] Anim slots and slot groups
- [ ] Root motion: enable/disable, how it interacts with movement component
- [ ] Linked anim layers

### Input System (Enhanced Input)
- [ ] InputMappingContext → InputAction binding pipeline
- [ ] InputAction: value type (bool, Axis1D, Axis2D, Axis3D)
- [ ] Triggers: Down, Pressed, Released, Hold, HoldAndRelease, Tap, Combo
- [ ] Modifiers: DeadZone, Scalar, Negate, Swizzle, FOVScaling
- [ ] Priority and context stacking

### Build System
- [ ] `.Build.cs` module definition
- [ ] `.Target.cs` build targets (Game, Editor, Server, Client)
- [ ] Module dependency types: Public, Private, DynamicallyLoaded
- [ ] Plugin `.uplugin` structure
- [ ] Cooking and packaging: cooked vs uncooked, platform targets, pak files

---

## Structural Issues in Existing Ontology

### No Workflow Patterns
The ontology describes *what things are* but not *how they compose into tasks*. The essay emphasizes causal and dependency relationships — we have some via PointBonds, but we're missing operational sequences.

- [ ] Add `WorkflowPattern` concept to the boundary space
- [ ] Encode common multi-step workflows as PointBond chains:
  - "Add a new variable to a Blueprint" → add_blueprint_variable → compile_blueprint → verify
  - "Create a new actor type" → create_blueprint → add variables → add components → compile → place in level
  - "Wire player input" → create InputAction → add to MappingContext → bind in Blueprint/C++ → test in PIE
  - "Debug a runtime issue" → get_status → read_blueprint → play_in_editor start → get_runtime_value → analyze
- [ ] These should be stacked as a separate context, not embedded in the type definitions

### Tool Surface Ontology Is a Flat Catalog
`McpSurface.kant` lists tools with descriptions. It should encode *when to use which tool* — decision logic, not just a menu.

- [ ] Add decision tree structure: "If you need to understand an asset and you know its type, use the specialized reader. If you don't know the type, use read_asset first."
- [ ] Encode tool composition patterns: "read_blueprint often needs to be followed by read_blueprint_graph for the EventGraph to understand behavior"
- [ ] Link tools to the concept domains they operate on — currently the PointBonds link to modes, but not to AssetTaxonomy types

### No Project-Specific Extension Points
The ontology is generic UE. Real projects need to layer conventions, module structure, and domain concepts on top. The Vale examples show this pattern but we haven't articulated it for ue-mcp.

- [ ] Document the stacking pattern: project-specific `.kant` files compose on top of the UE foundation
- [ ] Add a `ProjectConventions` SpaceBond template in the boundary space for projects to fill in (prefixes, directory layout, coding standards)
- [ ] Add a `ProjectModules` SpaceBond template for declaring the project's module dependency graph (like ValeModules.kant)

### Cross-Cutting Concerns Are Missing
Properties that cut across the type hierarchy aren't represented.

- [ ] "Has a visual editor graph" — applies to Blueprint, AnimBlueprint, Material, Niagara, but not DataTable, Texture, Mesh
- [ ] "Placeable in levels" — applies to Actor Blueprints and some asset types, but not others
- [ ] "Has a CDO / default object" — applies to anything with a UClass
- [ ] "Can be cooked" — platform-specific packaging concern that applies to all assets differently
- [ ] These should be Meanings or traits that compose with the AssetTaxonomy rather than duplicated per-type

### Signals Need More Dimensions
Currently we have three signals: mode, fidelity, mutability. These are good for the MCP's operational model but don't cover the domain.

- [ ] `complexity` signal — How structurally complex is this concept to work with (a simple variable change vs rewiring a state machine)
- [ ] `stability` signal — How settled is this area of UE (Enhanced Input is stable; Niagara API changes every release)
- [ ] `risk` signal — How dangerous is this operation (changing a DataTable row vs reparenting a Blueprint vs modifying replication settings)
