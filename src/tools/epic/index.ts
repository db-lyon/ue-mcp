// GENERATED FILE - do not edit. See scripts/generate-epic-actions.mjs.
import * as animation from "./animation.generated.js";
import * as asset from "./asset.generated.js";
import * as audio from "./audio.generated.js";
import * as blueprint from "./blueprint.generated.js";
import * as conversation from "./conversation.generated.js";
import * as dataflow from "./dataflow.generated.js";
import * as editor from "./editor.generated.js";
import * as epic from "./epic.generated.js";
import * as fab from "./fab.generated.js";
import * as gameplay from "./gameplay.generated.js";
import * as gas from "./gas.generated.js";
import * as level from "./level.generated.js";
import * as material from "./material.generated.js";
import * as niagara from "./niagara.generated.js";
import * as pcg from "./pcg.generated.js";
import * as plugins from "./plugins.generated.js";
import * as project from "./project.generated.js";
import * as reflection from "./reflection.generated.js";
import * as source_control from "./source_control.generated.js";
import * as statetree from "./statetree.generated.js";
import * as widget from "./widget.generated.js";

/** Every category that Unreal's toolset registry contributes actions to. */
export const EPIC_CATEGORIES = {
  animation: animation,
  asset: asset,
  audio: audio,
  blueprint: blueprint,
  conversation: conversation,
  dataflow: dataflow,
  editor: editor,
  epic: epic,
  fab: fab,
  gameplay: gameplay,
  gas: gas,
  level: level,
  material: material,
  niagara: niagara,
  pcg: pcg,
  plugins: plugins,
  project: project,
  reflection: reflection,
  source_control: source_control,
  statetree: statetree,
  widget: widget,
} as const;

export type EpicCategoryName = keyof typeof EPIC_CATEGORIES;
