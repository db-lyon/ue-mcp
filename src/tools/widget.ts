import { z } from "zod";
import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../category-tool.js";
import { normalizeWidgetParams, WIDGET_PARAM_GROUPS } from "./widget-params.js";
import { actions as epicActions, schema as epicSchema } from "./epic/widget.generated.js";
import { specBp, schema as specSchema } from "./specs/widget.generated.js";

export const widgetTool: ToolDef = categoryTool(
  "widget",
  "UMG Widget Blueprints, Editor Utility Widgets, and Editor Utility Blueprints. One parameter contract across every action (#798): the asset is always `assetPath`, an Unreal package path such as `/Game/UI/WBP_Example`; a widget inside the tree is always `widgetName`; its parent panel is always `parentWidgetName`; arguments for any wrapped `epic_*` action go in `input`, and a top-level `assetPath` is folded into the asset reference of the wrapped tool for you. A `.uasset` suffix, an object suffix (`.WBP_Example`), backslashes, and the legacy `path` / `widgetBlueprintPath` / `widgetBlueprint` spellings are accepted and normalized. Create actions take `assetPath` too; `name` plus `packagePath` remains valid and is composed into it. See [Widget parameter contract](widget-parameters.md).",
  {
    read_tree:         specBp("read", "Read widget hierarchy.", "read_widget_tree"),
    get_details:       specBp("read", "Inspect widget (curated subset).", "get_widget_details"),
    get_properties:    specBp("read", "Full reflected property dump for a widget - every UPROPERTY (RenderOpacity, Visibility, ColorAndOpacity, Border padding/colors, Image brush TintColor/ImageSize, fonts, etc.) plus the slot block, for diagnosing visual bugs get_details omits. Pass includeSubtree to also dump children (#547).", "get_widget_properties"),
    list_bindings:     specBp("read", "List designer property bindings on a WidgetBlueprint (the UE 5.7 Python API keeps them protected). Returns {widgetName, propertyName, functionName, bindingType}. Optional filterWidgetName/filterProperty (#530).", "list_widget_bindings"),
    clear_binding:     specBp("mutate", "Remove designer binding(s) matching widgetName (and optional propertyName) from a WidgetBlueprint without opening the editor. Idempotent (#530).", "clear_widget_binding"),
    set_property:      specBp("mutate", "Set widget property. Slot struct props take UE struct text that persists every field - `Slot.Size`=`(Value=1.0,SizeRule=Fill)`, `Slot.Padding`=`(Left=8,Top=8,Right=8,Bottom=8)` - or a nested field path like `Slot.Size.Value` / `Slot.Padding.Left`; an invalid value errors instead of silently writing 0 (#532).", "set_widget_property"),
    set_style:         specBp("mutate", "Set a full/nested style struct on a widget from JSON (FButtonStyle, FEditableTextBoxStyle, FSlateFontInfo, FSlateColor and their nested brushes) - what set_property's scalar path can't express. value is a JSON object mirroring the struct; propertyName is the struct property, e.g. WidgetStyle (#563).", "set_widget_style"),
    reorder_child:     specBp("mutate", "Reorder a widget among its parent panel's children (move to a sibling index) - e.g. insert a new row BETWEEN two existing children. move_widget only reparents (#635).", "reorder_child"),
    bulk_set_properties: specBp("mutate", "Apply many {widgetName, propertyName, value} style/property writes to a WidgetBlueprint in one call (single compile+save) - font/color/style stylesheet across many widgets (#563).", "bulk_set_widget_properties"),
    list:              specBp("read", "List Widget BPs, sorted by object path.", "list_widget_blueprints"),
    read_animations:   specBp("read", "Read UMG animations.", "read_widget_animations"),

    // ── UMG animation authoring (T7) ────────────────────────────────────
    // A UWidgetAnimation, its UMovieScene tracks and its sections are objects
    // that have to be constructed and registered before any property on them
    // exists, so set_property cannot reach them. Once a track exists, its
    // ordinary UPROPERTYs stay editor(set_property) territory.
    create_animation: specBp("mutate", "Create a UMG animation on a Widget Blueprint, with its MovieScene, display rate and playback range. Idempotent by animationName: a second call reports existed and leaves the timing alone.", "create_widget_animation"),
    delete_animation: specBp("mutate", "Delete a UMG animation from a Widget Blueprint, including its bindings. The rollback recreates an empty animation of the same name and rate, so the tracks and keys are NOT restored.", "delete_widget_animation"),
    get_animation: specBp("read", "Read one UMG animation in full: display rate, tick resolution, playback range, and per bound widget every track, section, channel and key time/value in SECONDS, plus the event tracks. read_animations reports the shape, this reports the values, which is what verifies a key actually landed.", "get_widget_animation"),
    add_animation_track: specBp("mutate", "Add a property track to a UMG animation, binding the widget into the animation first if it is not bound yet. The track class is chosen from the property's own reflected type; a property Sequencer cannot key is refused by name with the keyable types listed.", "add_widget_animation_track"),
    remove_animation_track: specBp("mutate", "Remove a property track from a UMG animation. The rollback re-adds the empty track, so its keys are NOT restored.", "remove_widget_animation_track"),
    add_animation_key: specBp("mutate", "Set a key on an animation track channel at a time in SECONDS, creating the track and the section if needed. Pick the channel by name (R/G/B/A, Left/Top/Right/Bottom, Translation.X) or by channelIndex; a miss lists the channels the section actually has.", "add_widget_animation_key"),
    remove_animation_key: specBp("mutate", "Remove the key at a time in SECONDS from an animation track channel. Removing a key that is not there reports unchanged rather than failing.", "remove_widget_animation_key"),
    add_animation_event_key: specBp("mutate", "Add an event key that calls a Widget Blueprint function at a time in SECONDS, creating the event track and its trigger section if needed. trackName defaults to Events.", "add_widget_animation_event_key"),
    remove_animation_event_key: specBp("mutate", "Remove the event key at a time in SECONDS from an animation event track.", "remove_widget_animation_event_key"),
    bind_animation_event: specBp("mutate", "Bind an animation lifecycle event (Finished or Started) on a Widget Blueprint to a handler graph, creating the K2Node_WidgetAnimationEvent if it is not there. userTag scopes a Started binding. Idempotent: an existing binding reports existed.", "bind_widget_animation_event"),
    unbind_animation_event: specBp("mutate", "Remove an animation lifecycle event binding from a Widget Blueprint.", "unbind_widget_animation_event"),

    // ── Navigation, focus and accessibility (T8) ────────────────────────
    // UWidget::Navigation is an Instanced UPROPERTY that is null on every
    // widget until something makes one, so the dotted-path setter stops at
    // "object reference is null". Creating the subobject is the gap; the
    // per-direction fields stay reachable by widget(set_style) afterwards.
    set_navigation: specBp("mutate", "Write UMG navigation rules on a widget: rule is Escape, Explicit, Wrap, Stop, Custom or CustomBoundary, direction is Up, Down, Left, Right, Next or Previous, and widgetToFocus names the target for Explicit. Creates the UWidgetNavigation subobject that set_property cannot make. Pass one write as widgetName + direction + rule, or many as rules[] of {widgetName, direction, rule, widgetToFocus}; the whole batch is validated before anything is written.", "set_widget_navigation"),
    clear_navigation: specBp("mutate", "Reset navigation rules on a widget back to Escape. Omit direction to clear all six. Clearing what is already clear reports unchanged.", "clear_widget_navigation"),
    restore_navigation: specBp("mutate", "Restore navigation rules from a captured snapshot - the `previous` array that set_navigation and clear_navigation put in their rollback payload, so a failed flow can be undone by hand as well as by the runner.", "restore_widget_navigation"),
    audit_focus_chain: specBp("read", "Read-only report over the whole widget tree: which widgets can take focus, their explicit navigation edges, widgets reachable from none of them, edges pointing at a missing/invisible/unfocusable target, and directions whose opposite does not lead back.", "audit_widget_focus_chain"),
    audit_accessibility: specBp("read", "Read-only accessibility report over the whole widget tree: font sizes under minFontSize, interactive widgets whose authored hit area is under minHitSize, and what could not be checked from authored values alone.", "audit_widget_accessibility"),
    get_runtime_focus_path: specBp("read", "Read the live Slate focus path for a user index in PIE - which widget holds focus and the chain of widgets above it. Requires a running PIE or Game world; the editor's own focus is never reported.", "get_runtime_focus_path"),
    set_runtime_focus: specBp("mutate", "Give keyboard focus to a named child of a live PIE widget, so a navigation chain can be walked and verified rather than predicted. Locate the host with className when more than one instance is up. Setting focus where it already is reports unchanged.", "set_runtime_focus"),

    create:            specBp("mutate", "Create Widget BP. assetPath is the full destination, e.g. /Game/UI/WBP_Example; name + packagePath is the older spelling of the same thing. Idempotent by path.", "create_widget_blueprint"),
    create_utility_widget:    specBp("mutate", "Create editor utility widget. assetPath is the full destination; name + packagePath is the older spelling. Idempotent by path.", "create_editor_utility_widget"),
    run_utility_widget:       specBp("mutate", "Open editor utility widget.", "run_editor_utility_widget"),
    create_utility_blueprint: specBp("mutate", "Create editor utility blueprint. assetPath is the full destination; name + packagePath is the older spelling. Idempotent by path.", "create_editor_utility_blueprint"),
    run_utility_blueprint:    specBp("mutate", "Run editor utility blueprint.", "run_editor_utility_blueprint"),
    // #799: both compile and save the Widget Blueprint before they answer, and
    // that can run well past the 30s default when the asset is open in the
    // designer. The mutation is on disk by the time the default cap expires, so
    // giving up early only turns a completed call into an ambiguous one. Both
    // are idempotent by assetPath + widgetName, so a retry is safe.
    add_widget: {
      ...specBp("mutate", "Add widget to widget tree. Idempotent by assetPath + widgetName: passing widgetName makes a retry safe, and the result carries requestedWidgetName/persistedWidgetName/renamed plus compileStatus.", "add_widget"),
      timeoutMs: 120_000,
    },
    remove_widget: {
      ...specBp("mutate", "Remove widget from tree. Idempotent, and clears the widget's Widget Blueprint GUID metadata so later compiles stop reporting a deleted variable (#799).", "remove_widget"),
      timeoutMs: 120_000,
    },
    move_widget:              specBp("mutate", "Reparent widget.", "move_widget"),
    set_root:                 specBp("mutate", "Replace WBP root with an existing widget by name (#365).", "set_root_widget"),
    wrap_root:                specBp("mutate", "Wrap the current root in a new panel widget (UMG 'Wrap With'). wrapperClass must be a UPanelWidget subclass (#365).", "wrap_root_widget"),
    list_classes:             specBp("read", "List the UWidget classes this editor has loaded, grouped by the module that defines them, each with its full path, parent class, whether it is a panel that accepts children, and the slot properties its children take. This is how a CommonUI or project-C++ widget is discovered: pass a row's `name` to add_widget as widgetClass, or its `path` when two modules share a name. Loaded classes only, so a Widget Blueprint nothing has opened is absent (use list) and a class from a disabled plugin does not exist until project(enable_plugin) and a restart.", "list_widget_classes"),
    get_bind_widget_contract: specBp("read","Report the BindWidget contract a native UserWidget parent imposes: every UPROPERTY marked BindWidget, BindWidgetOptional, BindWidgetAnim or BindWidgetAnimOptional, with the exact widget name it demands, the class that name must be, whether it is optional, and which ancestor declares it. Metadata is not a property value and reflect_class does not report these keys, so this is the only way to learn the contract short of a failed compile. Pass className for the contract alone, or assetPath to also check one Widget Blueprint's own tree against its parent and get back satisfied/missing/wrongType.", "get_bind_widget_contract"),
    audit_commonui:           specBp("read", "Read-only CommonUI wiring report. Checks the rules that fail silently at runtime rather than at compile time: the plugin being enabled at all, GameViewportClientClass being a CommonGameViewportClient (without it gamepad navigation and Back do nothing), CommonInputSettings.InputData being set (without it no input action resolves and bound action bars render empty) and, when assetPath names a Widget Blueprint, an activatable widget with no DesiredFocusWidget, a CommonBoundActionBar with no ActionButtonClass, and CommonUI widgets with no Style. Every problem carries the exact call that fixes it.", "audit_commonui"),
    extract_subtree:          specBp("mutate", "Lift an authored designer subtree out of one WidgetBlueprint into a standalone one, using UMG's own clipboard serializer so hierarchy, child order, editable properties, panel slot data and named-slot content survive. The selected widget becomes the destination root. dryRun defaults to true and only returns the name mapping - pass dryRun=false to actually write the asset. The destination must be absent or empty; an exact-shape replay returns existed. The source is never compiled or saved.", "extract_widget_subtree"),
    list_runtime:             specBp("read", "(#160) List live UUserWidget instances in the PIE world, sorted by object path.", "list_runtime_widgets"),
    get_runtime:              specBp("read","(#160) Inspect a live PIE widget tree with text/visibility/brush/percent plus style values: renderOpacity (all), colorAndOpacity (TextBlock/Image), Border brushColor/contentColorAndOpacity (#592). includeLayout adds read-only layout diagnostics to every node - geometry (desired/local/absolute size, layout and render bounds), render transform, effective opacity, reflected slot properties including structured Canvas anchors/offsets/alignment, derived clipping, parent and viewport overlap, and a diagnostics array - plus the host UserWidget node under `host`, a `layoutCapture` summary, and per-node `deltaSincePreviousCapture` against the previous includeLayout call on that instance, so capture / reproduce / capture again isolates position-dependent sizing. Off by default: it is a much larger payload. A given widgetName and className must both match.", "get_runtime_widget"),
    inspect_runtime_instances: specBp("read", "Inspect every matching live widget instance (never an implicit first match), with stable identity/owning-player metadata and selected reflected properties on the widget or subtree. Requires a running PIE/Game world and errors instead of falling back to the editor world. Passing childName or childClassFilter implies includeSubtree. Provide widgetName or classFilter.", "inspect_runtime_instances"),
    get_runtime_delegates:    specBp("read", "(#161) Read delegate binding state on a live PIE widget, located by widgetName or className. Returns array of {delegateName, isBound, numBindings}.", "get_runtime_delegates"),
    add_to_viewport:          specBp("mutate", "(#602) Instantiate a WidgetBlueprint and add it to the live PIE viewport for visual verification. Requires PIE running.", "add_to_viewport"),
    invoke_runtime_function:  specBp("unknown","(#559/#812) Fire a UI interaction on a live PIE widget: a parameterless UFUNCTION (functionName) on the located UserWidget, OR drive an interactive child via childName - Button (click), CheckBox (value true/false/toggle), Slider and SpinBox (numeric value), EditableText/EditableTextBox/MultiLineEditableText/MultiLineEditableTextBox (string value), ComboBoxString (option string or index). The matching delegate is broadcast so bound Blueprint logic runs. functionName alongside childName picks the delegate (e.g. OnPressed, OnTextChanged). Locate the widget with widgetName or className.", "invoke_runtime_function"),
    ...epicActions,
  },
  {
    ...epicSchema,
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration. A key listed again below is shared with hand-written
    // actions or read by the normalizer, and tests/unit/handler-specs.test.ts
    // holds the two to one type.
    //
    // The normalizer and the specs coexist: normalizeWidgetParams runs in
    // prepareCall before any mapping, and a specBp action has none, so the
    // folded bag reaches the registry whole. The handler reads assetPath,
    // widgetName and parentWidgetName; the mirrored `path` and the legacy
    // spellings left in the bag come back as paramsNotRead from C++ and are
    // dropped by WIDGET_PARAM_GROUPS. The spec declares only the aliases the
    // handler itself accepts (path, name, typeName, propertyValue,
    // widgetBlueprintPath and so on), so a direct bridge caller keeps them.
    ...specSchema,
    assetPath: z.string().optional().describe("Canonical Widget Blueprint / Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)"),
    widgetName: z.string().optional().describe("Canonical name of a widget inside the tree (#798)"),
    parentWidgetName: z.string().optional().describe("Canonical name of the parent panel widget (#798)"),
    // The spec cannot carry bounds; these keep them, with the type the spec declares.
    maxInstances: z.number().int().min(1).max(500).optional().describe("inspect_runtime_instances: maximum matching widget instances returned"),
    maxNodesPerInstance: z.number().int().min(1).max(2000).optional().describe("inspect_runtime_instances: maximum root/subtree nodes per instance"),
    // Accepted spellings of the canonical names above. Declared so the
    // transport does not strip them before the normalizer can fold them in.
    path: z.string().optional().describe("Legacy alias for assetPath. Use assetPath (#798)"),
    widgetBlueprintPath: z.string().optional().describe("Alias for assetPath. Use assetPath (#798)"),
    widgetBlueprint: z.union([z.string(), z.record(z.unknown())]).optional().describe("Alias for assetPath, also accepted as the engine's {refPath} object reference. Use assetPath (#798)"),
    widgetDisplayName: z.string().optional().describe("Alias for widgetName. Use widgetName (#798)"),
    parentWidget: z.string().optional().describe("Alias for parentWidgetName. Use parentWidgetName (#798)"),
  },
  { normalizeParams: normalizeWidgetParams, paramGroups: WIDGET_PARAM_GROUPS },
);
