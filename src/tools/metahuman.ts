// @session per-session: Every category tool is cloned per session and dispatches against the addressed editor.
import type { ToolDef } from "../surface/tool-types.js";
import { categoryTool } from "../surface/category-tool.js";
import { specBp, schema as specSchema } from "./specs/metahuman.generated.js";

// MetaHuman Creator (#1354) over UMetaHumanCharacterEditorSubsystem. The
// MetaHumanCharacter plugin is optional and off by default; every action
// answers "MetaHuman plugin not available" with the enable step when it is off.
export const metahumanTool: ToolDef = categoryTool(
  "metahuman",
  "MetaHuman Creator characters: create a MetaHumanCharacter, read its body constraints, wardrobe and texture state, set parametric body constraints, dress wardrobe slots, request the cloud auto-rig and texture sources, and assemble it into skeletal meshes and a blueprint.",
  {
    status: specBp("read", "Report whether the MetaHumanCharacter plugin is enabled and its editor subsystem loaded, and list the MetaHumanCharacter assets under /Game. The Epic cloud login state is reported as unknown with the reason: no reflected API exposes it. Call this first.", "metahuman_status"),
    create_character: specBp("mutate", "Create a MetaHumanCharacter asset through the plugin's own factory. Idempotent by name (onConflict skip). Rollback deletes the asset.", "metahuman_create_character"),
    read_character: specBp("read", "Read a MetaHumanCharacter: template type, texture state (hasHighResolutionTextures, synthesized face and body texture counts, hasFaceDNABlendshapes), and with includeBody (default) the body constraints (name, active, target, min, max), wardrobe slot selections and canBuild. Opens the character for editing to read those and closes it again if it was not open. The rig state is not reflected and is reported as unknown with the reason.", "metahuman_read_character"),
    set_body: specBp("mutate", "Set parametric body constraints by name, {Height: 180, Muscularity: 0.9} or {Height: {target: 180, active: true}}, and commit the body state. Writes the whole constraint array back, so edits are not lost to the copy the engine hands out, refuses unknown names with the list of real ones, and returns each constraint before and after. Rollback restores the previous targets and active flags.", "metahuman_set_body"),
    set_wardrobe: specBp("mutate", "Dress wardrobe slots: {\"Top Garment\": wardrobeItemPath}. Each item is added to the character's collection and selected in its slot, then the preview is rebuilt. Returns per-item applied/error and the slot selections before and after. No inverse.", "metahuman_set_wardrobe"),
    request_rig: {
      ...specBp("mutate", "Request auto-rigging from the MetaHuman cloud service (rigType JointsOnly or JointsAndBlendshapes), blocking by default. This is the step that opens the Epic login, so run it before request_textures. A refusal for want of a login is an error with loginState not-logged-in. Returns the texture/rig state and canBuild afterwards plus the MetaHuman log warnings. No inverse.", "metahuman_request_rig"),
      timeoutMs: 900_000,
    },
    request_textures: {
      ...specBp("mutate", "Request high-resolution texture sources from the MetaHuman cloud service, blocking by default. Needs the Epic login request_rig opens; a refusal for want of it is an error with loginState not-logged-in. Returns texture state before and after. No inverse.", "metahuman_request_textures"),
      timeoutMs: 900_000,
    },
    assemble: {
      ...specBp("mutate", "Assemble the character (pipelineType Cinematic, Optimized or UEFN; quality such as Medium) as one tracked long call of up to 15 minutes. Checks CanBuildMetaHuman first and returns its logged reason when it refuses. Returns the created skeletal meshes, blueprints and other asset paths, the elapsed seconds and the MetaHuman log warnings. No inverse.", "metahuman_assemble"),
      timeoutMs: 900_000,
    },
  },
  {
    ...specSchema,
  },
);
