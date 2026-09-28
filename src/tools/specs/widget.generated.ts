// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd widget handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_to_viewport": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      },
      {
        "name": "zOrder",
        "type": "number",
        "required": false,
        "description": "Viewport Z-order (#602)"
      }
    ]
  },
  "add_widget": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "widgetClass",
        "type": "string",
        "required": true,
        "description": "Widget class: a short name (TextBlock, CanvasPanel), a full path, or a Widget Blueprint path",
        "aliases": [
          "typeName"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": false,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "name",
          "widgetDisplayName"
        ]
      },
      {
        "name": "parentWidgetName",
        "type": "string",
        "required": false,
        "description": "Name of the parent panel widget (#798)",
        "aliases": [
          "parentWidget"
        ]
      }
    ]
  },
  "add_widget_animation_event_key": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint function the event key calls"
      },
      {
        "name": "time",
        "type": "number",
        "required": false,
        "description": "Key time in SECONDS, converted to frames on the animation's tick resolution"
      },
      {
        "name": "trackName",
        "type": "string",
        "required": false,
        "description": "Event track display name (default Events)"
      }
    ]
  },
  "add_widget_animation_key": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Reflected property name on the widget"
      },
      {
        "name": "time",
        "type": "number",
        "required": true,
        "description": "Key time in SECONDS, converted to frames on the animation's tick resolution"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "The keyed number"
      },
      {
        "name": "channel",
        "type": "string",
        "required": false,
        "description": "Channel by name (R/G/B/A, Left/Top/Right/Bottom, Translation.X); a miss lists the section's real channels"
      },
      {
        "name": "channelIndex",
        "type": "integer",
        "required": false,
        "description": "Channel by index, used when channel is not given (default 0)"
      },
      {
        "name": "interpolation",
        "type": "string",
        "required": false,
        "description": "cubic (default), linear or constant"
      }
    ]
  },
  "add_widget_animation_track": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Reflected property name on the widget"
      }
    ]
  },
  "audit_commonui": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Widget Blueprint whose CommonUI wiring to check as well",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      }
    ]
  },
  "audit_widget_accessibility": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      },
      {
        "name": "minFontSize",
        "type": "number",
        "required": false,
        "description": "Smallest acceptable font size in points (default 12)"
      },
      {
        "name": "minHitSize",
        "type": "number",
        "required": false,
        "description": "Smallest acceptable interactive hit area in slate units (default 40)"
      }
    ]
  },
  "audit_widget_focus_chain": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      }
    ]
  },
  "bind_widget_animation_event": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      },
      {
        "name": "event",
        "type": "string",
        "required": false,
        "description": "Animation lifecycle event: Finished (default) or Started"
      },
      {
        "name": "userTag",
        "type": "string",
        "required": false,
        "description": "User tag scoping a Started binding"
      }
    ]
  },
  "bulk_set_widget_properties": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "properties",
        "type": "array",
        "required": true,
        "description": "[{widgetName, propertyName, value}] (#563)",
        "items": "object"
      }
    ]
  },
  "clear_widget_binding": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": false,
        "description": "Reflected property name on the widget"
      }
    ]
  },
  "clear_widget_navigation": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "direction",
        "type": "string",
        "required": false,
        "description": "Up, Down, Left, Right, Next or Previous; omit to clear all six"
      }
    ]
  },
  "create_editor_utility_blueprint": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Full destination, e.g. /Game/UI/WBP_Example; wins over name + packagePath",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Bare asset name, placed in packagePath"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for name (default /Game/EditorUtilities)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "assetPath"
          ],
          [
            "name"
          ]
        ]
      }
    ],
    "contractExempt": "Creates and saves an Editor Utility Blueprint under the contract values; nothing it reads fails first"
  },
  "create_editor_utility_widget": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Full destination, e.g. /Game/UI/WBP_Example; wins over name + packagePath",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Bare asset name, placed in packagePath"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for name (default /Game/EditorUtilities)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "assetPath"
          ],
          [
            "name"
          ]
        ]
      }
    ],
    "contractExempt": "Creates and saves an Editor Utility Widget under the contract values; nothing it reads fails first"
  },
  "create_widget_animation": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      },
      {
        "name": "durationSeconds",
        "type": "number",
        "required": false,
        "description": "Playback range length in seconds (default 1)"
      },
      {
        "name": "displayRate",
        "type": "number",
        "required": false,
        "description": "Timeline display rate in fps (default 60)"
      },
      {
        "name": "displayLabel",
        "type": "string",
        "required": false,
        "description": "Designer-facing label (defaults to animationName)"
      }
    ]
  },
  "create_widget_blueprint": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Full destination, e.g. /Game/UI/WBP_Example; wins over name + packagePath",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Bare asset name, placed in packagePath"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for name (default /Game/UI/Widgets)"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "UUserWidget subclass: a short name or a class path (default UserWidget)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "assetPath"
          ],
          [
            "name"
          ]
        ]
      }
    ],
    "contractExempt": "Creates and saves a Widget Blueprint under the contract values; nothing it reads fails first"
  },
  "delete_widget_animation": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      }
    ]
  },
  "extract_widget_subtree": {
    "category": "widget",
    "params": [
      {
        "name": "sourceAssetPath",
        "type": "string",
        "required": true,
        "description": "WidgetBlueprint the subtree is read from",
        "aliases": [
          "sourcePath"
        ]
      },
      {
        "name": "sourceWidgetName",
        "type": "string",
        "required": true,
        "description": "Widget in the source that becomes the extracted root",
        "aliases": [
          "widgetName",
          "widgetDisplayName"
        ]
      },
      {
        "name": "destinationAssetPath",
        "type": "string",
        "required": true,
        "description": "Destination package path, including the new asset name",
        "aliases": [
          "destinationPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "destinationParentClass",
        "type": "string",
        "required": false,
        "description": "UUserWidget subclass for the destination (default UserWidget)"
      },
      {
        "name": "destinationRootName",
        "type": "string",
        "required": false,
        "description": "Name override for the extracted root; descendants keep their names"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Plan only, no asset is created or saved (default true)"
      }
    ]
  },
  "get_bind_widget_contract": {
    "category": "widget",
    "params": [
      {
        "name": "className",
        "type": "string",
        "required": false,
        "description": "Native UserWidget parent whose contract to read: a short name, a class path, or a Widget Blueprint path"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Widget Blueprint whose parent's contract to read and whose tree to check against it",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "className"
          ],
          [
            "assetPath"
          ]
        ]
      }
    ]
  },
  "get_runtime_delegates": {
    "category": "widget",
    "params": [
      {
        "name": "widgetName",
        "type": "string",
        "required": false,
        "description": "Exact live instance name. Provide this or className",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "className",
        "type": "string",
        "required": false,
        "description": "Widget class name that locates the live host widget"
      }
    ]
  },
  "get_runtime_focus_path": {
    "category": "widget",
    "params": [
      {
        "name": "userIndex",
        "type": "integer",
        "required": false,
        "description": "Local player user index (default 0)"
      }
    ]
  },
  "get_runtime_widget": {
    "category": "widget",
    "params": [
      {
        "name": "widgetName",
        "type": "string",
        "required": false,
        "description": "Exact live instance name",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "className",
        "type": "string",
        "required": false,
        "description": "Widget class name that locates the live host widget"
      },
      {
        "name": "childName",
        "type": "string",
        "required": false,
        "description": "Named child inside the UserWidget (#559)"
      },
      {
        "name": "maxDepth",
        "type": "integer",
        "required": false,
        "description": "Max widget-tree depth to walk (default 6)"
      },
      {
        "name": "includeLayout",
        "type": "boolean",
        "required": false,
        "description": "Add read-only layout diagnostics (geometry, slot, clipping, viewport, per-node deltas) to every node and report the host UserWidget under host (#775)"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "widgetName"
          ],
          [
            "className"
          ]
        ]
      }
    ]
  },
  "get_widget_animation": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      }
    ]
  },
  "get_widget_details": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      }
    ]
  },
  "get_widget_properties": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "includeSubtree",
        "type": "boolean",
        "required": false,
        "description": "Also dump descendant widgets (#547)"
      }
    ]
  },
  "inspect_runtime_instances": {
    "category": "widget",
    "params": [
      {
        "name": "widgetName",
        "type": "string",
        "required": false,
        "description": "Exact live instance name. Provide this or classFilter",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "classFilter",
        "type": "string",
        "required": false,
        "description": "Class name substring filter"
      },
      {
        "name": "propertyNames",
        "type": "array",
        "required": false,
        "description": "Exact reflected property names to serialize",
        "items": "string"
      },
      {
        "name": "includeSubtree",
        "type": "boolean",
        "required": false,
        "description": "Also dump descendant widgets (#547)"
      },
      {
        "name": "childName",
        "type": "string",
        "required": false,
        "description": "Named child inside the UserWidget (#559)"
      },
      {
        "name": "childClassFilter",
        "type": "string",
        "required": false,
        "description": "Class substring filter for subtree nodes (implies includeSubtree)"
      },
      {
        "name": "viewportOnly",
        "type": "boolean",
        "required": false,
        "description": "Only widgets currently added to the viewport"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: pie (default) | game | auto. The editor world is never a valid target"
      },
      {
        "name": "pieInstance",
        "type": "integer",
        "required": false,
        "description": "PIE instance id for multi-client sessions"
      },
      {
        "name": "maxInstances",
        "type": "integer",
        "required": false,
        "description": "Maximum matching widget instances returned (1 to 500, default 100)",
        "min": 1,
        "max": 500
      },
      {
        "name": "maxNodesPerInstance",
        "type": "integer",
        "required": false,
        "description": "Maximum root/subtree nodes per instance (1 to 2000, default 250)",
        "min": 1,
        "max": 2000
      }
    ]
  },
  "invoke_runtime_function": {
    "category": "widget",
    "params": [
      {
        "name": "widgetName",
        "type": "string",
        "required": false,
        "description": "Exact live instance name",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "className",
        "type": "string",
        "required": false,
        "description": "Widget class name that locates the live host widget"
      },
      {
        "name": "functionName",
        "type": "string",
        "required": false,
        "description": "Parameterless UFUNCTION to call on the live widget (#559), or with childName the child delegate to fire (#812)"
      },
      {
        "name": "childName",
        "type": "string",
        "required": false,
        "description": "Named child inside the UserWidget (#559)"
      },
      {
        "name": "value",
        "type": "any",
        "required": false,
        "description": "Value for the child interaction: true, false or toggle for a CheckBox, a number for a Slider or SpinBox, text for a text box, an option or index for a ComboBoxString"
      },
      {
        "name": "commitMethod",
        "type": "string",
        "required": false,
        "description": "Text and spin box commit type: OnEnter (default), OnUserMovedFocus, OnCleared, Default (#812)"
      }
    ],
    "choices": [
      {
        "mode": "atLeastOne",
        "branches": [
          [
            "widgetName"
          ],
          [
            "className"
          ]
        ]
      }
    ]
  },
  "list_runtime_widgets": {
    "category": "widget",
    "params": [
      {
        "name": "classFilter",
        "type": "string",
        "required": false,
        "description": "Class name substring filter"
      },
      {
        "name": "namePrefix",
        "type": "string",
        "required": false,
        "description": "Instance name prefix filter"
      },
      {
        "name": "viewportOnly",
        "type": "boolean",
        "required": false,
        "description": "Only widgets currently added to the viewport"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 200, max 2000)"
      }
    ]
  },
  "list_widget_bindings": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      },
      {
        "name": "filterWidgetName",
        "type": "string",
        "required": false,
        "description": "Only bindings on this widget (#530)"
      },
      {
        "name": "filterProperty",
        "type": "string",
        "required": false,
        "description": "Only bindings of this property (#530)"
      }
    ]
  },
  "list_widget_blueprints": {
    "category": "widget",
    "params": [
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Include sub-paths (default true)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 200, max 2000)"
      }
    ]
  },
  "list_widget_classes": {
    "category": "widget",
    "params": [
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the class name"
      },
      {
        "name": "module",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the defining module, e.g. UMG or CommonUI"
      },
      {
        "name": "includeAbstract",
        "type": "boolean",
        "required": false,
        "description": "Include abstract base classes, which cannot be added to a tree (default false)"
      },
      {
        "name": "includeBlueprint",
        "type": "boolean",
        "required": false,
        "description": "Include loaded Widget Blueprint generated classes as well as native ones (default false)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 300, max 5000)"
      }
    ]
  },
  "move_widget": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "newParentWidgetName",
        "type": "string",
        "required": true,
        "description": "Panel widget to reparent into",
        "aliases": [
          "parentWidgetName",
          "parentWidget"
        ]
      }
    ]
  },
  "read_widget_animations": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      }
    ]
  },
  "read_widget_tree": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      }
    ]
  },
  "remove_widget": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      }
    ]
  },
  "remove_widget_animation_event_key": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      },
      {
        "name": "time",
        "type": "number",
        "required": false,
        "description": "Key time in SECONDS, converted to frames on the animation's tick resolution"
      },
      {
        "name": "trackName",
        "type": "string",
        "required": false,
        "description": "Event track display name (default Events)"
      }
    ]
  },
  "remove_widget_animation_key": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Reflected property name on the widget"
      },
      {
        "name": "time",
        "type": "number",
        "required": true,
        "description": "Key time in SECONDS, converted to frames on the animation's tick resolution"
      },
      {
        "name": "channel",
        "type": "string",
        "required": false,
        "description": "Channel by name (R/G/B/A, Left/Top/Right/Bottom, Translation.X); a miss lists the section's real channels"
      },
      {
        "name": "channelIndex",
        "type": "integer",
        "required": false,
        "description": "Channel by index, used when channel is not given (default 0)"
      }
    ]
  },
  "remove_widget_animation_track": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Reflected property name on the widget"
      }
    ]
  },
  "reorder_child": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "index",
        "type": "number",
        "required": true,
        "description": "Target sibling index within the parent panel (#635)"
      }
    ]
  },
  "restore_widget_navigation": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "previous",
        "type": "array",
        "required": true,
        "description": "The captured navigation snapshot set_navigation / clear_navigation return in their rollback payload",
        "items": "object"
      }
    ]
  },
  "run_editor_utility_blueprint": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      }
    ]
  },
  "run_editor_utility_widget": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ]
      }
    ]
  },
  "set_root_widget": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      }
    ]
  },
  "set_runtime_focus": {
    "category": "widget",
    "params": [
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Named child of a live PIE widget, or the live UserWidget's own name",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "userIndex",
        "type": "integer",
        "required": false,
        "description": "Local player user index (default 0)"
      },
      {
        "name": "className",
        "type": "string",
        "required": false,
        "description": "Widget class name that locates the live host widget"
      }
    ]
  },
  "set_widget_navigation": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "rules",
        "type": "array",
        "required": false,
        "description": "Navigation writes applied as one validated batch",
        "items": "object",
        "fields": [
          {
            "name": "widgetName",
            "type": "string",
            "required": true,
            "description": "Widget whose navigation is written"
          },
          {
            "name": "direction",
            "type": "string",
            "required": true,
            "description": "Up, Down, Left, Right, Next or Previous"
          },
          {
            "name": "rule",
            "type": "string",
            "required": false,
            "description": "Escape, Explicit (default), Wrap, Stop, Custom or CustomBoundary"
          },
          {
            "name": "widgetToFocus",
            "type": "string",
            "required": false,
            "description": "Target widget name; required for Explicit"
          }
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": false,
        "description": "Widget whose navigation a single write sets",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "direction",
        "type": "string",
        "required": false,
        "description": "Direction of a single write, which it needs: Up, Down, Left, Right, Next or Previous"
      },
      {
        "name": "rule",
        "type": "string",
        "required": false,
        "description": "Rule of a single write: Escape, Explicit (default), Wrap, Stop, Custom or CustomBoundary"
      },
      {
        "name": "widgetToFocus",
        "type": "string",
        "required": false,
        "description": "Target widget of a single Explicit write"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "rules"
          ],
          [
            "widgetName"
          ]
        ]
      }
    ]
  },
  "set_widget_property": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Reflected property name on the widget"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "New value as UE export text, e.g. (Left=8,Top=8,Right=8,Bottom=8)",
        "aliases": [
          "propertyValue"
        ]
      }
    ]
  },
  "set_widget_style": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)",
        "aliases": [
          "widgetDisplayName"
        ]
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": true,
        "description": "Reflected property name on the widget"
      },
      {
        "name": "value",
        "type": "any",
        "required": true,
        "description": "JSON object mirroring the style struct, or a scalar"
      }
    ]
  },
  "unbind_widget_animation_event": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "animationName",
        "type": "string",
        "required": true,
        "description": "The UWidgetAnimation's object name or display label"
      },
      {
        "name": "event",
        "type": "string",
        "required": false,
        "description": "Animation lifecycle event: Finished (default) or Started"
      },
      {
        "name": "userTag",
        "type": "string",
        "required": false,
        "description": "User tag scoping a Started binding"
      }
    ]
  },
  "wrap_root_widget": {
    "category": "widget",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798)",
        "aliases": [
          "path",
          "widgetBlueprintPath"
        ],
        "role": "editTarget"
      },
      {
        "name": "wrapperClass",
        "type": "string",
        "required": true,
        "description": "Panel widget class (CanvasPanel, VerticalBox, Overlay, etc.); must be a UPanelWidget subclass",
        "aliases": [
          "widgetClass"
        ]
      },
      {
        "name": "wrapperName",
        "type": "string",
        "required": false,
        "description": "Name for the new wrapper widget"
      }
    ]
  }
};

/** Every key the spec'd widget handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
