import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { normalizeWidgetParams } from "./widget-params.js";
import { CURSOR_PARAM, paged } from "../pagination.js";
import { actions as epicActions, schema as epicSchema } from "./epic/widget.generated.js";

export const widgetTool: ToolDef = categoryTool(
  "widget",
  "UMG Widget Blueprints, Editor Utility Widgets, and Editor Utility Blueprints. One parameter contract across every action (#798): the asset is always `assetPath`, an Unreal package path such as `/Game/UI/WBP_Example`; a widget inside the tree is always `widgetName`; its parent panel is always `parentWidgetName`; arguments for the `epic_*` actions always go in `input`, and a top-level `assetPath` is folded into the asset reference of the wrapped tool for you. A `.uasset` suffix, an object suffix (`.WBP_Example`), backslashes, and the legacy `path` / `widgetBlueprintPath` / `widgetBlueprint` spellings are accepted and normalized. Create actions take `assetPath` too; `name` plus `packagePath` remains valid and is composed into it. See [Widget parameter contract](widget-parameters.md).",
  {
    read_tree:         bp("read", "Read widget hierarchy. Params: assetPath", "read_widget_tree"),
    get_details:       bp("read", "Inspect widget (curated subset). Params: assetPath, widgetName", "get_widget_details"),
    get_properties:    bp("read", "Full reflected property dump for a widget - every UPROPERTY (RenderOpacity, Visibility, ColorAndOpacity, Border padding/colors, Image brush TintColor/ImageSize, fonts, etc.) plus the slot block, for diagnosing visual bugs get_details omits. Pass includeSubtree to also dump children (#547). Params: assetPath, widgetName, includeSubtree?", "get_widget_properties", (p) => ({ assetPath: p.assetPath, widgetName: p.widgetName, includeSubtree: p.includeSubtree })),
    list_bindings:     bp("read", "List designer property bindings on a WidgetBlueprint (the UE 5.7 Python API keeps them protected). Returns {widgetName, propertyName, functionName, bindingType}. Optional filterWidgetName/filterProperty (#530). Params: assetPath, filterWidgetName?, filterProperty?", "list_widget_bindings", (p) => ({ assetPath: p.assetPath, filterWidgetName: p.filterWidgetName, filterProperty: p.filterProperty })),
    clear_binding:     bp("mutate", "Remove designer binding(s) matching widgetName (and optional propertyName) from a WidgetBlueprint without opening the editor. Idempotent (#530). Params: assetPath, widgetName, propertyName?", "clear_widget_binding", (p) => ({ assetPath: p.assetPath, widgetName: p.widgetName, propertyName: p.propertyName })),
    set_property:      bp("mutate", "Set widget property. Slot struct props take UE struct text that persists every field - `Slot.Size`=`(Value=1.0,SizeRule=Fill)`, `Slot.Padding`=`(Left=8,Top=8,Right=8,Bottom=8)` - or a nested field path like `Slot.Size.Value` / `Slot.Padding.Left`; an invalid value errors instead of silently writing 0 (#532). Params: assetPath, widgetName, propertyName, value", "set_widget_property"),
    set_style:         bp("mutate", "Set a full/nested style struct on a widget from JSON (FButtonStyle, FEditableTextBoxStyle, FSlateFontInfo, FSlateColor and their nested brushes) - what set_property's scalar path can't express. value is a JSON object mirroring the struct. Params: assetPath, widgetName, propertyName (e.g. WidgetStyle), value (#563)", "set_widget_style", (p) => ({ assetPath: p.assetPath, widgetName: p.widgetName, propertyName: p.propertyName, value: p.value })),
    reorder_child:     bp("mutate", "Reorder a widget among its parent panel's children (move to a sibling index) - e.g. insert a new row BETWEEN two existing children. move_widget only reparents. Params: assetPath, widgetName, index (#635)", "reorder_child", (p) => ({ assetPath: p.assetPath, widgetName: p.widgetName, index: p.index })),
    bulk_set_properties: bp("mutate", "Apply many {widgetName, propertyName, value} style/property writes to a WidgetBlueprint in one call (single compile+save) - font/color/style stylesheet across many widgets. Params: assetPath, properties[] (#563)", "bulk_set_widget_properties", (p) => ({ assetPath: p.assetPath, properties: p.properties })),
    list:              bp("read", paged("List Widget BPs, sorted by object path. Params: directory?, recursive?"), "list_widget_blueprints"),
    read_animations:   bp("read", "Read UMG animations. Params: assetPath", "read_widget_animations"),

    // ── UMG animation authoring (T7) ────────────────────────────────────
    // A UWidgetAnimation, its UMovieScene tracks and its sections are objects
    // that have to be constructed and registered before any property on them
    // exists, so set_property cannot reach them. Once a track exists, its
    // ordinary UPROPERTYs stay editor(set_property) territory.
    create_animation: bp("mutate", "Create a UMG animation on a Widget Blueprint, with its MovieScene, display rate and playback range. Idempotent by animationName: a second call reports existed and leaves the timing alone. Params: assetPath, animationName, durationSeconds?, displayRate?, displayLabel?", "create_widget_animation", (p) => ({ assetPath: p.assetPath, animationName: p.animationName, durationSeconds: p.durationSeconds, displayRate: p.displayRate, displayLabel: p.displayLabel })),
    delete_animation: bp("mutate", "Delete a UMG animation from a Widget Blueprint, including its bindings. The rollback recreates an empty animation of the same name and rate, so the tracks and keys are NOT restored. Params: assetPath, animationName", "delete_widget_animation", (p) => ({ assetPath: p.assetPath, animationName: p.animationName })),
    get_animation: bp("read", "Read one UMG animation in full: display rate, tick resolution, playback range, and per bound widget every track, section, channel and key time/value in SECONDS, plus the event tracks. read_animations reports the shape, this reports the values, which is what verifies a key actually landed. Params: assetPath, animationName", "get_widget_animation", (p) => ({ assetPath: p.assetPath, animationName: p.animationName })),
    add_animation_track: bp("mutate", "Add a property track to a UMG animation, binding the widget into the animation first if it is not bound yet. The track class is chosen from the property's own reflected type; a property Sequencer cannot key is refused by name with the keyable types listed. Params: assetPath, animationName, widgetName, propertyName", "add_widget_animation_track", (p) => ({ assetPath: p.assetPath, animationName: p.animationName, widgetName: p.widgetName, propertyName: p.propertyName })),
    remove_animation_track: bp("mutate", "Remove a property track from a UMG animation. The rollback re-adds the empty track, so its keys are NOT restored. Params: assetPath, animationName, widgetName, propertyName", "remove_widget_animation_track", (p) => ({ assetPath: p.assetPath, animationName: p.animationName, widgetName: p.widgetName, propertyName: p.propertyName })),
    add_animation_key: bp("mutate", "Set a key on an animation track channel at a time in SECONDS, creating the track and the section if needed. Pick the channel by name (R/G/B/A, Left/Top/Right/Bottom, Translation.X) or by channelIndex; a miss lists the channels the section actually has. Params: assetPath, animationName, widgetName, propertyName, time, value, channel?, channelIndex?, interpolation?", "add_widget_animation_key", (p) => ({ assetPath: p.assetPath, animationName: p.animationName, widgetName: p.widgetName, propertyName: p.propertyName, time: p.time, value: p.value, channel: p.channel, channelIndex: p.channelIndex, interpolation: p.interpolation })),
    remove_animation_key: bp("mutate", "Remove the key at a time in SECONDS from an animation track channel. Removing a key that is not there reports unchanged rather than failing. Params: assetPath, animationName, widgetName, propertyName, time, channel?, channelIndex?", "remove_widget_animation_key", (p) => ({ assetPath: p.assetPath, animationName: p.animationName, widgetName: p.widgetName, propertyName: p.propertyName, time: p.time, channel: p.channel, channelIndex: p.channelIndex })),
    add_animation_event_key: bp("mutate", "Add an event key that calls a Widget Blueprint function at a time in SECONDS, creating the event track and its trigger section if needed. trackName defaults to Events. Params: assetPath, animationName, functionName, time?, trackName?", "add_widget_animation_event_key", (p) => ({ assetPath: p.assetPath, animationName: p.animationName, functionName: p.functionName, time: p.time, trackName: p.trackName })),
    remove_animation_event_key: bp("mutate", "Remove the event key at a time in SECONDS from an animation event track. Params: assetPath, animationName, time?, trackName?", "remove_widget_animation_event_key", (p) => ({ assetPath: p.assetPath, animationName: p.animationName, time: p.time, trackName: p.trackName })),
    bind_animation_event: bp("mutate", "Bind an animation lifecycle event (Finished or Started) on a Widget Blueprint to a handler graph, creating the K2Node_WidgetAnimationEvent if it is not there. userTag scopes a Started binding. Idempotent: an existing binding reports existed. Params: assetPath, animationName, event?, userTag?", "bind_widget_animation_event", (p) => ({ assetPath: p.assetPath, animationName: p.animationName, event: p.event, userTag: p.userTag })),
    unbind_animation_event: bp("mutate", "Remove an animation lifecycle event binding from a Widget Blueprint. Params: assetPath, animationName, event?, userTag?", "unbind_widget_animation_event", (p) => ({ assetPath: p.assetPath, animationName: p.animationName, event: p.event, userTag: p.userTag })),

    // ── Navigation, focus and accessibility (T8) ────────────────────────
    // UWidget::Navigation is an Instanced UPROPERTY that is null on every
    // widget until something makes one, so the dotted-path setter stops at
    // "object reference is null". Creating the subobject is the gap; the
    // per-direction fields stay reachable by widget(set_style) afterwards.
    set_navigation: bp("mutate", "Write UMG navigation rules on a widget: rule is Escape, Explicit, Wrap, Stop, Custom or CustomBoundary, direction is Up, Down, Left, Right, Next or Previous, and widgetToFocus names the target for Explicit. Creates the UWidgetNavigation subobject that set_property cannot make. Pass one write as widgetName + direction + rule, or many as rules[] of {widgetName, direction, rule, widgetToFocus}; the whole batch is validated before anything is written. Params: assetPath, rules[]? OR widgetName?, direction?, rule?, widgetToFocus?", "set_widget_navigation", (p) => ({ assetPath: p.assetPath, rules: p.rules, widgetName: p.widgetName, direction: p.direction, rule: p.rule, widgetToFocus: p.widgetToFocus })),
    clear_navigation: bp("mutate", "Reset navigation rules on a widget back to Escape. Omit direction to clear all six. Clearing what is already clear reports unchanged. Params: assetPath, widgetName, direction?", "clear_widget_navigation", (p) => ({ assetPath: p.assetPath, widgetName: p.widgetName, direction: p.direction })),
    restore_navigation: bp("mutate", "Restore navigation rules from a captured snapshot - the `previous` array that set_navigation and clear_navigation put in their rollback payload, so a failed flow can be undone by hand as well as by the runner. Params: assetPath, previous[]", "restore_widget_navigation", (p) => ({ assetPath: p.assetPath, previous: p.previous })),
    audit_focus_chain: bp("read", "Read-only report over the whole widget tree: which widgets can take focus, their explicit navigation edges, widgets reachable from none of them, edges pointing at a missing/invisible/unfocusable target, and directions whose opposite does not lead back. Params: assetPath", "audit_widget_focus_chain", (p) => ({ assetPath: p.assetPath })),
    audit_accessibility: bp("read", "Read-only accessibility report over the whole widget tree: font sizes under minFontSize, interactive widgets whose authored hit area is under minHitSize, and what could not be checked from authored values alone. Params: assetPath, minFontSize?, minHitSize?", "audit_widget_accessibility", (p) => ({ assetPath: p.assetPath, minFontSize: p.minFontSize, minHitSize: p.minHitSize })),
    get_runtime_focus_path: bp("read", "Read the live Slate focus path for a user index in PIE - which widget holds focus and the chain of widgets above it. Requires a running PIE or Game world; the editor's own focus is never reported. Params: userIndex?", "get_runtime_focus_path", (p) => ({ userIndex: p.userIndex })),
    set_runtime_focus: bp("mutate", "Give keyboard focus to a named child of a live PIE widget, so a navigation chain can be walked and verified rather than predicted. Locate the host with className when more than one instance is up. Setting focus where it already is reports unchanged. Params: widgetName, userIndex?, className?", "set_runtime_focus", (p) => ({ widgetName: p.widgetName, userIndex: p.userIndex, className: p.className })),

    create:            bp("mutate", "Create Widget BP. assetPath is the full destination, e.g. /Game/UI/WBP_Example; name + packagePath is the older spelling of the same thing and is composed into it. Params: assetPath, name?, packagePath?, parentClass?", "create_widget_blueprint"),
    create_utility_widget:    bp("mutate", "Create editor utility widget. Params: assetPath, name?, packagePath?", "create_editor_utility_widget"),
    run_utility_widget:       bp("mutate", "Open editor utility widget. Params: assetPath", "run_editor_utility_widget"),
    create_utility_blueprint: bp("mutate", "Create editor utility blueprint. Params: assetPath, name?, packagePath?", "create_editor_utility_blueprint"),
    run_utility_blueprint:    bp("mutate", "Run editor utility blueprint. Params: assetPath", "run_editor_utility_blueprint"),
    // #799: both compile and save the Widget Blueprint before they answer, and
    // that can run well past the 30s default when the asset is open in the
    // designer. The mutation is on disk by the time the default cap expires, so
    // giving up early only turns a completed call into an ambiguous one. Both
    // are idempotent by assetPath + widgetName, so a retry is safe.
    add_widget: {
      ...bp("mutate", "Add widget to widget tree. Idempotent by assetPath + widgetName: passing widgetName makes a retry safe, and the result carries requestedWidgetName/persistedWidgetName/renamed plus compileStatus. Params: assetPath, widgetClass, widgetName?, parentWidgetName?", "add_widget"),
      timeoutMs: 120_000,
    },
    remove_widget: {
      ...bp("mutate", "Remove widget from tree. Idempotent, and clears the widget's Widget Blueprint GUID metadata so later compiles stop reporting a deleted variable (#799). Params: assetPath, widgetName", "remove_widget"),
      timeoutMs: 120_000,
    },
    move_widget:              bp("mutate", "Reparent widget. Params: assetPath, widgetName, newParentWidgetName", "move_widget"),
    set_root:                 bp("mutate", "Replace WBP root with an existing widget by name (#365). Params: assetPath, widgetName", "set_root_widget", (p) => ({ assetPath: p.assetPath, widgetName: p.widgetName })),
    wrap_root:                bp("mutate", "Wrap the current root in a new panel widget (UMG 'Wrap With'). Params: assetPath, wrapperClass (must be a UPanelWidget subclass), wrapperName? (#365)", "wrap_root_widget", (p) => ({ assetPath: p.assetPath, wrapperClass: p.wrapperClass, widgetClass: p.widgetClass, wrapperName: p.wrapperName })),
    list_classes:             bp("read", paged("List the UWidget classes this editor has loaded, grouped by the module that defines them, each with its full path, parent class, whether it is a panel that accepts children, and the slot properties its children take. This is how a CommonUI or project-C++ widget is discovered: pass a row's `name` to add_widget as widgetClass, or its `path` when two modules share a name. Loaded classes only, so a Widget Blueprint nothing has opened is absent (use list) and a class from a disabled plugin does not exist until project(enable_plugin) and a restart. Params: filter?, module?, includeAbstract?, includeBlueprint?, limit? (default 300, max 5000)"), "list_widget_classes", (p) => ({ filter: p.filter, module: p.module, includeAbstract: p.includeAbstract, includeBlueprint: p.includeBlueprint, cursor: p.cursor, limit: p.limit })),
    get_bind_widget_contract: bp("read", "Report the BindWidget contract a native UserWidget parent imposes: every UPROPERTY marked BindWidget, BindWidgetOptional, BindWidgetAnim or BindWidgetAnimOptional, with the exact widget name it demands, the class that name must be, whether it is optional, and which ancestor declares it. Metadata is not a property value and reflect_class does not report these keys, so this is the only way to learn the contract short of a failed compile. Pass className for the contract alone, or assetPath to also check one Widget Blueprint's own tree against its parent and get back satisfied/missing/wrongType. Params: className? OR assetPath?", "get_bind_widget_contract", (p) => ({ className: p.className, assetPath: p.assetPath })),
    audit_commonui:           bp("read", "Read-only CommonUI wiring report. Checks the rules that fail silently at runtime rather than at compile time: the plugin being enabled at all, GameViewportClientClass being a CommonGameViewportClient (without it gamepad navigation and Back do nothing), CommonInputSettings.InputData being set (without it no input action resolves and bound action bars render empty) and, when assetPath names a Widget Blueprint, an activatable widget with no DesiredFocusWidget, a CommonBoundActionBar with no ActionButtonClass, and CommonUI widgets with no Style. Every problem carries the exact call that fixes it. Params: assetPath?", "audit_commonui", (p) => ({ assetPath: p.assetPath })),
    extract_subtree:          bp("mutate", "Lift an authored designer subtree out of one WidgetBlueprint into a standalone one, using UMG's own clipboard serializer so hierarchy, child order, editable properties, panel slot data and named-slot content survive. The selected widget becomes the destination root. dryRun defaults to true and only returns the name mapping - pass dryRun=false to actually write the asset. The destination must be absent or empty; an exact-shape replay returns existed. The source is never compiled or saved. Params: sourceAssetPath, sourceWidgetName, destinationAssetPath, destinationParentClass?, destinationRootName?, dryRun?", "extract_widget_subtree", (p) => ({ sourceAssetPath: p.sourceAssetPath, sourceWidgetName: p.sourceWidgetName, destinationAssetPath: p.destinationAssetPath, destinationParentClass: p.destinationParentClass, destinationRootName: p.destinationRootName, dryRun: p.dryRun })),
    list_runtime:             bp("read", paged("(#160) List live UUserWidget instances in the PIE world, sorted by object path. Params: classFilter?, namePrefix?, viewportOnly?"), "list_runtime_widgets"),
    get_runtime:              bp("read", "(#160) Inspect a live PIE widget tree with text/visibility/brush/percent plus style values: renderOpacity (all), colorAndOpacity (TextBlock/Image), Border brushColor/contentColorAndOpacity (#592). includeLayout adds read-only layout diagnostics to every node - geometry (desired/local/absolute size, layout and render bounds), render transform, effective opacity, reflected slot properties including structured Canvas anchors/offsets/alignment, derived clipping, parent and viewport overlap, and a diagnostics array - plus the host UserWidget node under `host`, a `layoutCapture` summary, and per-node `deltaSincePreviousCapture` against the previous includeLayout call on that instance, so capture / reproduce / capture again isolates position-dependent sizing. Off by default: it is a much larger payload. Params: widgetName? | className?, childName?, maxDepth?, includeLayout?", "get_runtime_widget"),
    inspect_runtime_instances: bp("read", "Inspect every matching live widget instance (never an implicit first match), with stable identity/owning-player metadata and selected reflected properties on the widget or subtree. Requires a running PIE/Game world and errors instead of falling back to the editor world. Passing childName or childClassFilter implies includeSubtree. Provide widgetName or classFilter. Params: widgetName?, classFilter?, propertyNames[]?, includeSubtree?, childName?, childClassFilter?, viewportOnly?, world?, pieInstance?, maxInstances?, maxNodesPerInstance?", "inspect_runtime_instances", (p) => ({ widgetName: p.widgetName, classFilter: p.classFilter, propertyNames: p.propertyNames, includeSubtree: p.includeSubtree, childName: p.childName, childClassFilter: p.childClassFilter, viewportOnly: p.viewportOnly, world: p.world, pieInstance: p.pieInstance, maxInstances: p.maxInstances, maxNodesPerInstance: p.maxNodesPerInstance })),
    get_runtime_delegates:    bp("read", "(#161) Read delegate binding state on a live PIE widget. Params: widgetName, className?. Returns array of {delegateName, isBound, numBindings}", "get_runtime_delegates"),
    add_to_viewport:          bp("mutate", "(#602) Instantiate a WidgetBlueprint and add it to the live PIE viewport for visual verification. Requires PIE running. Params: assetPath (WidgetBlueprint path), zOrder?", "add_to_viewport", (p) => ({ assetPath: p.assetPath, zOrder: p.zOrder })),
    invoke_runtime_function:  bp("unknown", "(#559/#812) Fire a UI interaction on a live PIE widget: a parameterless UFUNCTION (functionName) on the located UserWidget, OR drive an interactive child via childName - Button (click), CheckBox (value true/false/toggle), Slider and SpinBox (numeric value), EditableText/EditableTextBox/MultiLineEditableText/MultiLineEditableTextBox (string value), ComboBoxString (option string or index). The matching delegate is broadcast so bound Blueprint logic runs. functionName alongside childName picks the delegate (e.g. OnPressed, OnTextChanged). Locate the widget with widgetName or className. Params: widgetName?|className?, functionName?, childName?, value?, commitMethod?", "invoke_runtime_function", (p) => ({ widgetName: p.widgetName, className: p.className, functionName: p.functionName, childName: p.childName, value: p.value, commitMethod: p.commitMethod })),
    ...epicActions,
  },
  undefined,
  {
    ...epicSchema,
    assetPath: z.string().optional().describe("Canonical Widget Blueprint / Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)"),
    widgetName: z.string().optional().describe("Canonical name of a widget inside the tree (#798)"),
    includeSubtree: z.boolean().optional().describe("get_properties/inspect_runtime_instances: also dump descendant widgets (#547)"),
    widgetClass: z.string().optional(),
    parentWidgetName: z.string().optional().describe("Canonical name of the parent panel widget (#798)"),
    newParentWidgetName: z.string().optional(),
    propertyName: z.string().optional(),
    filterWidgetName: z.string().optional().describe("list_bindings: only bindings on this widget (#530)"),
    filterProperty: z.string().optional().describe("list_bindings: only bindings of this property (#530)"),
    value: z.unknown().optional(),
    properties: z.array(z.record(z.unknown())).optional().describe("bulk_set_properties: [{widgetName, propertyName, value}] (#563)"),
    index: z.number().optional().describe("reorder_child: target sibling index within the parent panel (#635)"),
    directory: z.string().optional(),
    recursive: z.boolean().optional(),
    name: z.string().optional().describe("create actions: bare asset name, used with packagePath. assetPath is the canonical alternative (#798)"),
    packagePath: z.string().optional().describe("create actions: destination folder, used with name (#798)"),
    parentClass: z.string().optional(),
    classFilter: z.string().optional().describe("Class name substring filter for runtime widget queries"),
    propertyNames: z.array(z.string()).optional().describe("inspect_runtime_instances: exact reflected property names to serialize"),
    childClassFilter: z.string().optional().describe("inspect_runtime_instances: class substring filter for subtree nodes (implies includeSubtree)"),
    // Deliberately a string, not z.enum. The MCP SDK validates arguments BEFORE
    // the tool callback runs, so a strict enum makes a typo fail at the transport
    // with a schema error, and the handler's own message never reaches the
    // caller. An unrecognised scope resolves to the editor world, which this
    // action refuses by name along with the scopes it accepts.
    world: z.string().optional().describe("inspect_runtime_instances: runtime world scope: pie (default) | game | auto. The editor world is never a valid target"),
    pieInstance: z.number().int().optional().describe("inspect_runtime_instances: PIE instance id for multi-client sessions"),
    maxInstances: z.number().int().min(1).max(500).optional().describe("inspect_runtime_instances: maximum matching widget instances returned"),
    maxNodesPerInstance: z.number().int().min(1).max(2000).optional().describe("inspect_runtime_instances: maximum root/subtree nodes per instance"),
    className: z.string().optional().describe("Widget class name for get_runtime (first match wins)"),
    namePrefix: z.string().optional().describe("Instance name prefix filter for list_runtime"),
    viewportOnly: z.boolean().optional().describe("list_runtime/inspect_runtime_instances: only return widgets currently added to the viewport"),
    childName: z.string().optional().describe("get_runtime/invoke_runtime_function/inspect_runtime_instances: named child inside the UserWidget (#559)"),
    functionName: z.string().optional().describe("invoke_runtime_function: parameterless UFUNCTION to call on the live widget (#559), or with childName the child delegate to fire (#812)"),
    commitMethod: z.string().optional().describe("invoke_runtime_function: text/spin box commit type - OnEnter (default), OnUserMovedFocus, OnCleared, Default (#812)"),
    zOrder: z.number().optional().describe("add_to_viewport: viewport Z-order (#602)"),
    maxDepth: z.number().optional().describe("get_runtime: max widget-tree depth to walk (default 6)"),
    includeLayout: z.boolean().optional().describe("get_runtime: add read-only layout diagnostics (geometry, slot, clipping, viewport, per-node deltas) to every node and report the host UserWidget under `host` (#775)"),
    // ── Class discovery (T6) ────────────────────────────────────────────
    filter: z.string().optional().describe("list_classes: case-insensitive substring of the class name"),
    module: z.string().optional().describe("list_classes: case-insensitive substring of the defining module, e.g. UMG or CommonUI"),
    includeAbstract: z.boolean().optional().describe("list_classes: include abstract base classes, which cannot be added to a tree (default false)"),
    includeBlueprint: z.boolean().optional().describe("list_classes: include loaded Widget Blueprint generated classes as well as native ones (default false)"),
    limit: z.number().int().optional().describe("Rows on this page for the paged list actions: list_classes (default 300, max 5000), list / list_runtime (default 200, max 2000)"),
    // The paged list actions in this category resume on a cursor. `limit`
    // is already declared above.
    cursor: CURSOR_PARAM,
    // ── UMG animation authoring (T7) ────────────────────────────────────
    animationName: z.string().optional().describe("Animation actions: the UWidgetAnimation's object name or display label"),
    durationSeconds: z.number().optional().describe("create_animation: playback range length in seconds (default 1)"),
    displayRate: z.number().optional().describe("create_animation: timeline display rate in fps (default 60)"),
    displayLabel: z.string().optional().describe("create_animation: designer-facing label (defaults to animationName)"),
    time: z.number().optional().describe("Animation key actions: key time in SECONDS, converted to frames on the animation's tick resolution"),
    channel: z.string().optional().describe("Animation key actions: channel by name (R/G/B/A, Left/Top/Right/Bottom, Translation.X); a miss lists the section's real channels"),
    channelIndex: z.number().int().optional().describe("Animation key actions: channel by index, used when channel is not given (default 0)"),
    interpolation: z.string().optional().describe("add_animation_key: cubic (default), linear or constant"),
    trackName: z.string().optional().describe("Animation event key actions: event track display name (default Events)"),
    event: z.string().optional().describe("bind/unbind_animation_event: animation lifecycle event - Finished (default) or Started"),
    userTag: z.string().optional().describe("bind/unbind_animation_event: user tag scoping a Started binding"),
    // ── Navigation, focus and accessibility (T8) ────────────────────────
    rules: z.array(z.record(z.unknown())).optional().describe("set_navigation: [{widgetName, direction, rule, widgetToFocus?}] applied as one validated batch"),
    direction: z.string().optional().describe("set/clear_navigation: Up, Down, Left, Right, Next or Previous"),
    rule: z.string().optional().describe("set_navigation: Escape, Explicit, Wrap, Stop, Custom or CustomBoundary"),
    widgetToFocus: z.string().optional().describe("set_navigation: target widget name for an Explicit rule"),
    previous: z.array(z.record(z.unknown())).optional().describe("restore_navigation: the captured navigation snapshot set_navigation / clear_navigation return in their rollback payload"),
    minFontSize: z.number().optional().describe("audit_accessibility: smallest acceptable font size in points (default 12)"),
    minHitSize: z.number().optional().describe("audit_accessibility: smallest acceptable interactive hit area in slate units (default 40)"),
    userIndex: z.number().int().optional().describe("get_runtime_focus_path/set_runtime_focus: local player user index (default 0)"),
    wrapperClass: z.string().optional().describe("wrap_root: panel widget class (CanvasPanel, VerticalBox, Overlay, etc.)"),
    wrapperName: z.string().optional().describe("wrap_root: optional name for the new wrapper widget"),
    // Accepted spellings of the canonical names above. Declared so the
    // transport does not strip them before the normalizer can fold them in.
    path: z.string().optional().describe("Legacy alias for assetPath. Use assetPath (#798)"),
    widgetBlueprintPath: z.string().optional().describe("Alias for assetPath. Use assetPath (#798)"),
    widgetBlueprint: z.union([z.string(), z.record(z.unknown())]).optional().describe("Alias for assetPath, also accepted as the engine's {refPath} object reference. Use assetPath (#798)"),
    widgetDisplayName: z.string().optional().describe("Alias for widgetName. Use widgetName (#798)"),
    parentWidget: z.string().optional().describe("Alias for parentWidgetName. Use parentWidgetName (#798)"),
    sourceAssetPath: z.string().optional().describe("extract_subtree: WidgetBlueprint the subtree is read from"),
    sourceWidgetName: z.string().optional().describe("extract_subtree: widget in the source that becomes the extracted root"),
    destinationAssetPath: z.string().optional().describe("extract_subtree: destination package path, including the new asset name"),
    destinationParentClass: z.string().optional().describe("extract_subtree: UUserWidget subclass for the destination (default UserWidget)"),
    destinationRootName: z.string().optional().describe("extract_subtree: name override for the extracted root; descendants keep their names"),
    dryRun: z.boolean().optional().describe("extract_subtree: plan only, no asset is created or saved (default true)"),
  },
  { normalizeParams: normalizeWidgetParams },
);
