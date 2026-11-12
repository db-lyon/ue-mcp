// GENERATED FILE - do not edit. See scripts/generate-epic-actions.mjs.
//
// The routing the generator uses to place each Epic toolset in a ue-mcp
// category, emitted so the server applies the same rule at runtime.

/** Pattern to category, first match wins. A toolset matching none belongs to `epic`. */
export const EPIC_TOOLSET_ROUTES: ReadonlyArray<readonly [RegExp, string]> = [
  [/(?:^|[._])fab(?!ric)/i, "fab"],
  [/levelsequence|templatesequence|sequencevalidator|deformergraph|optimus/i, "animation"],
  [/pcgeditor/i, "pcg"],
  [/environmentquery|navmesh|inputtoolset/i, "gameplay"],
  [/mvvm|commonui|slatedebugger/i, "widget"],
  [/metasound|soundwave/i, "audio"],
  [/sourcecontrol/i, "source_control"],
  [/blueprint_diff/i, "blueprint"],
  [/curveatlas|userdefinedenum|userdefinedstruct|geometry_ops/i, "asset"],
  [/editortoolset\.leveltoolset|unrealcanvasspatialannotation/i, "level"],
  [/consoletoolset\.consoletoolset|cheatstoolset|python_scripting|undohistory|scenecapture|rewinddebugger|perftoolset|mediatoolset/i, "editor"],
  [/filesystemtoolset|livecoding/i, "project"],
  [/physicsasset|physicstoolset/i, "gameplay"],
  [/gas|abilitysystem|gameplaycue|attributeset/i, "gas"],
  [/niagara/i, "niagara"],
  [/\bpcg\b|pcgspatial|pcgtoolset/i, "pcg"],
  [/umg|\bwidget/i, "widget"],
  [/state_?tree/i, "statetree"],
  [/controlrig|sequencer|keyframing|\banimation/i, "animation"],
  [/gameplaytags/i, "gameplay"],
  [/material/i, "material"],
  [/landscape/i, "landscape"],
  [/foliage/i, "foliage"],
  [/\.blueprint\.|blueprinttools/i, "blueprint"],
  [/\.actor\.|actortools/i, "level"],
  [/\.asset\.|assettools|data_?asset|curve_?table|dataregistry|data_?registry/i, "asset"],
  [/skeletal_?mesh/i, "animation"],
  [/static_?mesh|\btexture|data_?table|string_?table|semanticsearch/i, "asset"],
  [/\.scene\.|scenetools|\.primitive\.|primitivetools/i, "level"],
  [/\.object\.|objecttools/i, "reflection"],
  [/behavior_?tree|worldcondition/i, "gameplay"],
  [/slateinspector/i, "widget"],
  [/plugintoolset|gamefeatures/i, "plugins"],
  [/editorapptoolset|logstoolset/i, "editor"],
  [/configsettings|automationtest/i, "project"],
  [/dataflow/i, "dataflow"],
  [/conversation/i, "conversation"],
];

/** The ue-mcp category an Epic toolset belongs in. */
export function routeEpicToolset(name: string): string {
  for (const [re, category] of EPIC_TOOLSET_ROUTES) if (re.test(name)) return category;
  return "epic";
}

/** Every recorded toolset, by the category it routes to. */
export const EPIC_TOOLSETS_BY_CATEGORY: Readonly<Record<string, readonly string[]>> = {
  animation: ["AnimationAssistantToolset.SequencerDirectorToolset","ControlRigEditor.ControlRigSequencerEditorLibrary","EditorToolset.SkeletalMeshToolset","LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary","LevelSequenceEditor.LevelSequenceEditorSubsystem","OptimusScripting.DeformerGraphScriptingSubsystem","SequenceValidator.SequenceValidatorSubsystem","SequencerAnimTools.SequencerAnimToolsEditorLibrary","SequencerScripting.MovieSceneBindingExtensions","SequencerScripting.MovieSceneBindingTagExtensions","SequencerScripting.MovieSceneConditionExtensions","SequencerScripting.MovieSceneDoubleVectorTrackExtensions","SequencerScripting.MovieSceneEventTrackExtensions","SequencerScripting.MovieSceneFloatVectorTrackExtensions","SequencerScripting.MovieSceneFolderExtensions","SequencerScripting.MovieScenePropertyTrackExtensions","SequencerScripting.MovieSceneSectionEasingExtensions","SequencerScripting.MovieSceneSectionExtensions","SequencerScripting.MovieSceneSequenceExtensions","SequencerScripting.MovieSceneTimeWarpExtensions","SequencerScripting.MovieSceneTrackExtensions","SequencerScriptingEditor.SequencerToolsFunctionLibrary","TemplateSequenceEditor.TemplateSequenceEditorSubsystem","animation_toolset.toolsets.conditions.SequencerConditionTools","animation_toolset.toolsets.controlrig.ControlRigTools","animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools","animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools","animation_toolset.toolsets.import_export.SequencerImportExportTools","animation_toolset.toolsets.keyframing.SequencerKeyframingTools","animation_toolset.toolsets.outliner.SequencerOutlinerTools","animation_toolset.toolsets.sequencer.SequencerTools","editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools"],
  asset: ["ChaosClothAssetToolset.ChaosClothAssetToolset","DataRegistryToolset.DataRegistryTools","EditorToolset.AssetToolset","EditorToolset.CurveAtlasToolset","EditorToolset.CurveTableToolset","EditorToolset.DataAssetToolset","EditorToolset.DataTableToolset","EditorToolset.StaticMeshToolset","EditorToolset.StringTableToolset","EditorToolset.TextureToolset","EditorToolset.UserDefinedEnumToolset","EditorToolset.UserDefinedStructToolset","SemanticSearchToolset.SemanticSearchToolset","TextureGraphToolset.TextureGraphTools","editor_toolset.toolsets.asset.AssetTools","editor_toolset.toolsets.curve_table.CurveTableTools","editor_toolset.toolsets.data_asset.DataAssetTools","editor_toolset.toolsets.data_table.DataTableTools","editor_toolset.toolsets.static_mesh.StaticMeshTools","editor_toolset.toolsets.string_table.StringTableTools","editor_toolset.toolsets.texture.TextureTools","geometry_toolset.geometry_ops.GeometryTools"],
  audio: ["EditorToolset.SoundWaveToolset","MetaSoundToolset.MetaSoundAuthoringToolset","MetaSoundToolset.MetaSoundDiscoveryToolset","MetaSoundToolset.MetaSoundInspectionToolset"],
  blueprint: ["BlueprintToolsets.BlueprintDslToolset","BlueprintToolsets.BlueprintToolset","editor_toolset.toolsets.blueprint.BlueprintDslTools","editor_toolset.toolsets.blueprint.BlueprintTools","editor_toolset.toolsets.blueprint_diff.BlueprintDiffTools"],
  conversation: ["conversation_toolset.toolsets.conversation.ConversationTools"],
  dataflow: ["DataflowAgent.DataflowAgentToolset","DataflowEditor.DataflowScriptingSubsystem"],
  editor: ["CheatsToolset.CheatsToolset","ConsoleToolset.ConsoleToolset","ConsoleToolset.LogsToolset","EditorToolset.EditorAppToolset","EditorToolset.LogsToolset","EditorToolset.UndoHistoryToolset","MediaToolset.MediaToolset","PerfToolset.CsvProfilerToolset","PerfToolset.GpuProfilerToolset","PerfToolset.StatsToolset","RewindDebuggerToolset.RewindDebuggerToolset","SceneCaptureToolset.SceneCaptureToolset","editor_toolset.toolsets.python_scripting.PythonScriptingTools"],
  epic: ["AIAssistantEditor.AIAssistantToolset","KnowledgeBaseToolset.KnowledgeBaseTools","ToolsetRegistry.AgentSkillToolset","editor_toolset.toolsets.programmatic.ProgrammaticToolset"],
  fab: ["FabToolset.FabToolset"],
  gameplay: ["AIModuleToolset.BehaviorTreeToolset","AIModuleToolset.EnvironmentQueryToolset","GameplayTagsToolset.GameplayTagsToolset","InputToolset.InputToolset","NavMeshToolset.NavMeshToolset","PhysicsToolsets.PhysicsAssetToolset","WorldConditionsToolset.WorldConditionTools","aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools"],
  gas: ["GASToolsets.AbilitySystemInspectorToolset","GASToolsets.AttributeSetToolset","GASToolsets.GameplayAbilityToolset","GASToolsets.GameplayCueToolset"],
  level: ["EditorToolset.ActorToolset","EditorToolset.LevelToolset","EditorToolset.PrimitiveToolset","UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationEditorSubsystem","UnrealCanvasAnnotationsEditor.UnrealCanvasSpatialAnnotationToolset","editor_toolset.toolsets.actor.ActorTools","editor_toolset.toolsets.primitive.PrimitiveTools","editor_toolset.toolsets.scene.SceneTools"],
  material: ["EditorToolset.MaterialInstanceToolset","EditorToolset.MaterialToolset","editor_toolset.toolsets.material.MaterialTools","editor_toolset.toolsets.material_instance.MaterialInstanceTools"],
  niagara: ["NiagaraToolsets.NiagaraToolset_Assets","NiagaraToolsets.NiagaraToolset_Blueprint","NiagaraToolsets.NiagaraToolset_Component","NiagaraToolsets.NiagaraToolset_Info","NiagaraToolsets.NiagaraToolset_System"],
  pcg: ["PCGEditor.PCGEditorSubsystem","PCGToolset.PCGSpatialToolset","PCGToolset.PCGToolset"],
  plugins: ["AgentPluginToolset.AgentPluginToolset","GameFeaturesToolset.GameFeaturesToolset","PluginToolset.PluginToolset"],
  project: ["AutomationTestToolset.AutomationTestToolset","ConfigSettingsToolset.ConfigSettingsToolset","FileSystemToolset.FileSystemToolset","LiveCodingToolset.LiveCodingToolset"],
  reflection: ["EditorToolset.ObjectToolset","editor_toolset.toolsets.object.ObjectTools"],
  source_control: ["SourceControl.SourceControlHelpers"],
  statetree: ["state_tree_toolset.state_tree.StateTreeToolset","state_tree_toolset.toolsets.state_tree.StateTreeTools"],
  widget: ["CommonUIToolset.CommonUIToolset","MVVMToolset.MVVMToolset","SlateDebugger.SlateDebuggerSubsystem","SlateInspectorToolset.SlateInspectorToolset","UMGEditor.UMGDebuggerSubsystem","UMGEditor.WidgetBlueprintEditorSubsystem","UMGToolSet.UMGToolSet","WidgetAnimationToolset.WidgetAnimationToolset","WidgetFinder.WidgetFinderSubsystem","WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset"],
};
