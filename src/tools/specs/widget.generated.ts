// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

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
          "path"
        ]
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
          "name"
        ]
      },
      {
        "name": "parentWidgetName",
        "type": "string",
        "required": false,
        "description": "Name of the parent panel widget (#798)"
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
          "path"
        ]
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
          "path"
        ]
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
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
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
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
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
          "path"
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
          "path"
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
          "path"
        ]
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
          "path"
        ]
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
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
          "path"
        ]
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
          "path"
        ]
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
          "path"
        ]
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
          "path"
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
          "widgetName"
        ]
      },
      {
        "name": "destinationAssetPath",
        "type": "string",
        "required": true,
        "description": "Destination package path, including the new asset name",
        "aliases": [
          "destinationPath"
        ]
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
          "path"
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
        "description": "Exact live instance name. Provide this or className"
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
        "description": "Exact live instance name"
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
          "path"
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
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
        "description": "Exact live instance name. Provide this or classFilter"
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
        "description": "Maximum matching widget instances returned (1 to 500, default 100)"
      },
      {
        "name": "maxNodesPerInstance",
        "type": "integer",
        "required": false,
        "description": "Maximum root/subtree nodes per instance (1 to 2000, default 250)"
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
        "description": "Exact live instance name"
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
          "path"
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
      },
      {
        "name": "newParentWidgetName",
        "type": "string",
        "required": true,
        "description": "Panel widget to reparent into",
        "aliases": [
          "parentWidgetName"
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
          "path"
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
          "path"
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
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
          "path"
        ]
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
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
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
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
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
          "path"
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
          "path"
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
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
        "description": "Named child of a live PIE widget, or the live UserWidget's own name"
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
          "path"
        ]
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
        "description": "Widget whose navigation a single write sets"
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
      },
      {
        "name": "widgetName",
        "type": "string",
        "required": true,
        "description": "Name of a widget inside the tree (#798)"
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
          "path"
        ]
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
          "path"
        ]
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

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_to_viewport: "Params: assetPath (or path, or widgetBlueprintPath), zOrder?",
  add_widget: "Params: assetPath (or path), widgetClass (or typeName), widgetName? (or name), parentWidgetName?",
  add_widget_animation_event_key: "Params: assetPath (or path), animationName, functionName, time?, trackName?",
  add_widget_animation_key: "Params: assetPath (or path), animationName, widgetName, propertyName, time, value, channel?, channelIndex?, interpolation?",
  add_widget_animation_track: "Params: assetPath (or path), animationName, widgetName, propertyName",
  audit_commonui: "Params: assetPath? (or path)",
  audit_widget_accessibility: "Params: assetPath (or path), minFontSize?, minHitSize?",
  audit_widget_focus_chain: "Params: assetPath (or path)",
  bind_widget_animation_event: "Params: assetPath (or path), animationName, event?, userTag?",
  bulk_set_widget_properties: "Params: assetPath (or path), properties",
  clear_widget_binding: "Params: assetPath (or path), widgetName, propertyName?",
  clear_widget_navigation: "Params: assetPath (or path), widgetName, direction?",
  create_editor_utility_blueprint: "Params: at least one of assetPath (or path)/name, packagePath?, onConflict?",
  create_editor_utility_widget: "Params: at least one of assetPath (or path)/name, packagePath?, onConflict?",
  create_widget_animation: "Params: assetPath (or path), animationName, durationSeconds?, displayRate?, displayLabel?",
  create_widget_blueprint: "Params: at least one of assetPath (or path)/name, packagePath?, parentClass?, onConflict?",
  delete_widget_animation: "Params: assetPath (or path), animationName",
  extract_widget_subtree: "Params: sourceAssetPath (or sourcePath), sourceWidgetName (or widgetName), destinationAssetPath (or destinationPath), destinationParentClass?, destinationRootName?, dryRun?",
  get_bind_widget_contract: "Params: at least one of className/assetPath (or path)",
  get_runtime_delegates: "Params: widgetName?, className?",
  get_runtime_focus_path: "Params: userIndex?",
  get_runtime_widget: "Params: at least one of widgetName/className, childName?, maxDepth?, includeLayout?",
  get_widget_animation: "Params: assetPath (or path), animationName",
  get_widget_details: "Params: assetPath (or path), widgetName",
  get_widget_properties: "Params: assetPath (or path), widgetName, includeSubtree?",
  inspect_runtime_instances: "Params: widgetName?, classFilter?, propertyNames?, includeSubtree?, childName?, childClassFilter?, viewportOnly?, world?, pieInstance?, maxInstances?, maxNodesPerInstance?",
  invoke_runtime_function: "Params: at least one of widgetName/className, functionName?, childName?, value?, commitMethod?",
  list_runtime_widgets: "Params: classFilter?, namePrefix?, viewportOnly?, cursor?, limit?",
  list_widget_bindings: "Params: assetPath (or path), filterWidgetName?, filterProperty?",
  list_widget_blueprints: "Params: recursive?, cursor?, limit?",
  list_widget_classes: "Params: filter?, module?, includeAbstract?, includeBlueprint?, cursor?, limit?",
  move_widget: "Params: assetPath (or path), widgetName, newParentWidgetName (or parentWidgetName)",
  read_widget_animations: "Params: assetPath (or path)",
  read_widget_tree: "Params: assetPath (or path)",
  remove_widget: "Params: assetPath (or path), widgetName",
  remove_widget_animation_event_key: "Params: assetPath (or path), animationName, time?, trackName?",
  remove_widget_animation_key: "Params: assetPath (or path), animationName, widgetName, propertyName, time, channel?, channelIndex?",
  remove_widget_animation_track: "Params: assetPath (or path), animationName, widgetName, propertyName",
  reorder_child: "Params: assetPath (or path), widgetName, index",
  restore_widget_navigation: "Params: assetPath (or path), previous",
  run_editor_utility_blueprint: "Params: assetPath (or path)",
  run_editor_utility_widget: "Params: assetPath (or path)",
  set_root_widget: "Params: assetPath (or path), widgetName",
  set_runtime_focus: "Params: widgetName, userIndex?, className?",
  set_widget_navigation: "Params: assetPath (or path), rules OR widgetName, direction?, rule?, widgetToFocus?",
  set_widget_property: "Params: assetPath (or path), widgetName, propertyName, value (or propertyValue)",
  set_widget_style: "Params: assetPath (or path), widgetName, propertyName, value",
  unbind_widget_animation_event: "Params: assetPath (or path), animationName, event?, userTag?",
  wrap_root_widget: "Params: assetPath (or path), wrapperClass (or widgetClass), wrapperName?",
};

/** Every key the spec'd widget handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  animationName: z.string().optional().describe("The UWidgetAnimation's object name or display label"),
  assetPath: z.string().optional().describe("Widget Blueprint or Editor Utility asset path, e.g. /Game/UI/WBP_Example (#798) (add_to_viewport, add_widget, add_widget_animation_event_key, add_widget_animation_key, add_widget_animation_track, audit_widget_accessibility, audit_widget_focus_chain, bind_widget_animation_event, bulk_set_widget_properties, clear_widget_binding, clear_widget_navigation, create_widget_animation, delete_widget_animation, get_widget_animation, get_widget_details, get_widget_properties, list_widget_bindings, move_widget, read_widget_animations, read_widget_tree, remove_widget, remove_widget_animation_event_key, remove_widget_animation_key, remove_widget_animation_track, reorder_child, restore_widget_navigation, run_editor_utility_blueprint, run_editor_utility_widget, set_root_widget, set_widget_navigation, set_widget_property, set_widget_style, unbind_widget_animation_event, wrap_root_widget). Widget Blueprint whose CommonUI wiring to check as well (audit_commonui). Full destination, e.g. /Game/UI/WBP_Example; wins over name + packagePath (create_editor_utility_blueprint, create_editor_utility_widget, create_widget_blueprint). Widget Blueprint whose parent's contract to read and whose tree to check against it (get_bind_widget_contract)"),
  channel: z.string().optional().describe("Channel by name (R/G/B/A, Left/Top/Right/Bottom, Translation.X); a miss lists the section's real channels"),
  channelIndex: z.number().int().optional().describe("Channel by index, used when channel is not given (default 0)"),
  childClassFilter: z.string().optional().describe("Class substring filter for subtree nodes (implies includeSubtree)"),
  childName: z.string().optional().describe("Named child inside the UserWidget (#559)"),
  classFilter: z.string().optional().describe("Class name substring filter"),
  className: z.string().optional().describe("Native UserWidget parent whose contract to read: a short name, a class path, or a Widget Blueprint path (get_bind_widget_contract). Widget class name that locates the live host widget (get_runtime_delegates, get_runtime_widget, invoke_runtime_function, set_runtime_focus)"),
  commitMethod: z.string().optional().describe("Text and spin box commit type: OnEnter (default), OnUserMovedFocus, OnCleared, Default (#812)"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"),
  destinationAssetPath: z.string().optional().describe("Destination package path, including the new asset name"),
  destinationParentClass: z.string().optional().describe("UUserWidget subclass for the destination (default UserWidget)"),
  destinationPath: z.string().optional().describe("Alias for destinationAssetPath"),
  destinationRootName: z.string().optional().describe("Name override for the extracted root; descendants keep their names"),
  direction: z.string().optional().describe("Up, Down, Left, Right, Next or Previous; omit to clear all six (clear_widget_navigation). Direction of a single write, which it needs: Up, Down, Left, Right, Next or Previous (set_widget_navigation)"),
  displayLabel: z.string().optional().describe("Designer-facing label (defaults to animationName)"),
  displayRate: z.number().optional().describe("Timeline display rate in fps (default 60)"),
  dryRun: z.boolean().optional().describe("Plan only, no asset is created or saved (default true)"),
  durationSeconds: z.number().optional().describe("Playback range length in seconds (default 1)"),
  event: z.string().optional().describe("Animation lifecycle event: Finished (default) or Started"),
  filter: z.string().optional().describe("Case-insensitive substring of the class name"),
  filterProperty: z.string().optional().describe("Only bindings of this property (#530)"),
  filterWidgetName: z.string().optional().describe("Only bindings on this widget (#530)"),
  functionName: z.string().optional().describe("Widget Blueprint function the event key calls (add_widget_animation_event_key). Parameterless UFUNCTION to call on the live widget (#559), or with childName the child delegate to fire (#812) (invoke_runtime_function)"),
  includeAbstract: z.boolean().optional().describe("Include abstract base classes, which cannot be added to a tree (default false)"),
  includeBlueprint: z.boolean().optional().describe("Include loaded Widget Blueprint generated classes as well as native ones (default false)"),
  includeLayout: z.boolean().optional().describe("Add read-only layout diagnostics (geometry, slot, clipping, viewport, per-node deltas) to every node and report the host UserWidget under host (#775)"),
  includeSubtree: z.boolean().optional().describe("Also dump descendant widgets (#547)"),
  index: z.number().optional().describe("Target sibling index within the parent panel (#635)"),
  interpolation: z.string().optional().describe("cubic (default), linear or constant"),
  limit: z.number().int().optional().describe("Rows to return on this page (default 200, max 2000) (list_runtime_widgets, list_widget_blueprints). Rows to return on this page (default 300, max 5000) (list_widget_classes)"),
  maxDepth: z.number().int().optional().describe("Max widget-tree depth to walk (default 6)"),
  maxInstances: z.number().int().optional().describe("Maximum matching widget instances returned (1 to 500, default 100)"),
  maxNodesPerInstance: z.number().int().optional().describe("Maximum root/subtree nodes per instance (1 to 2000, default 250)"),
  minFontSize: z.number().optional().describe("Smallest acceptable font size in points (default 12)"),
  minHitSize: z.number().optional().describe("Smallest acceptable interactive hit area in slate units (default 40)"),
  module: z.string().optional().describe("Case-insensitive substring of the defining module, e.g. UMG or CommonUI"),
  name: z.string().optional().describe("Alias for widgetName (add_widget). Bare asset name, placed in packagePath (create_editor_utility_blueprint, create_editor_utility_widget, create_widget_blueprint)"),
  namePrefix: z.string().optional().describe("Instance name prefix filter"),
  newParentWidgetName: z.string().optional().describe("Panel widget to reparent into"),
  onConflict: z.string().optional().describe("When the asset exists: skip (default, report it) | error"),
  packagePath: z.string().optional().describe("Folder for name (default /Game/EditorUtilities) (create_editor_utility_blueprint, create_editor_utility_widget). Folder for name (default /Game/UI/Widgets) (create_widget_blueprint)"),
  parentClass: z.string().optional().describe("UUserWidget subclass: a short name or a class path (default UserWidget)"),
  parentWidgetName: z.string().optional().describe("Name of the parent panel widget (#798) (add_widget). Alias for newParentWidgetName (move_widget)"),
  path: z.string().optional().describe("Alias for assetPath"),
  pieInstance: z.number().int().optional().describe("PIE instance id for multi-client sessions"),
  previous: z.array(z.record(z.unknown())).optional().describe("The captured navigation snapshot set_navigation / clear_navigation return in their rollback payload"),
  properties: z.array(z.record(z.unknown())).optional().describe("[{widgetName, propertyName, value}] (#563)"),
  propertyName: z.string().optional().describe("Reflected property name on the widget"),
  propertyNames: z.array(z.string()).optional().describe("Exact reflected property names to serialize"),
  propertyValue: z.unknown().optional().describe("Alias for value"),
  recursive: z.boolean().optional().describe("Include sub-paths (default true)"),
  rule: z.string().optional().describe("Rule of a single write: Escape, Explicit (default), Wrap, Stop, Custom or CustomBoundary"),
  rules: z.array(z.object({ widgetName: z.string().describe("Widget whose navigation is written"), direction: z.string().describe("Up, Down, Left, Right, Next or Previous"), rule: z.string().optional().describe("Escape, Explicit (default), Wrap, Stop, Custom or CustomBoundary"), widgetToFocus: z.string().optional().describe("Target widget name; required for Explicit") })).optional().describe("Navigation writes applied as one validated batch"),
  sourceAssetPath: z.string().optional().describe("WidgetBlueprint the subtree is read from"),
  sourcePath: z.string().optional().describe("Alias for sourceAssetPath"),
  sourceWidgetName: z.string().optional().describe("Widget in the source that becomes the extracted root"),
  time: z.number().optional().describe("Key time in SECONDS, converted to frames on the animation's tick resolution"),
  trackName: z.string().optional().describe("Event track display name (default Events)"),
  typeName: z.string().optional().describe("Alias for widgetClass"),
  userIndex: z.number().int().optional().describe("Local player user index (default 0)"),
  userTag: z.string().optional().describe("User tag scoping a Started binding"),
  value: z.unknown().optional().describe("The keyed number (add_widget_animation_key). Value for the child interaction: true, false or toggle for a CheckBox, a number for a Slider or SpinBox, text for a text box, an option or index for a ComboBoxString (invoke_runtime_function). New value as UE export text, e.g. (Left=8,Top=8,Right=8,Bottom=8) (set_widget_property). JSON object mirroring the style struct, or a scalar (set_widget_style)"),
  viewportOnly: z.boolean().optional().describe("Only widgets currently added to the viewport"),
  widgetBlueprintPath: z.string().optional().describe("Alias for assetPath"),
  widgetClass: z.string().optional().describe("Widget class: a short name (TextBlock, CanvasPanel), a full path, or a Widget Blueprint path (add_widget). Alias for wrapperClass (wrap_root_widget)"),
  widgetName: z.string().optional().describe("Name of a widget inside the tree (#798) (add_widget, add_widget_animation_key, add_widget_animation_track, clear_widget_binding, clear_widget_navigation, get_widget_details, get_widget_properties, move_widget, remove_widget, remove_widget_animation_key, remove_widget_animation_track, reorder_child, set_root_widget, set_widget_property, set_widget_style). Alias for sourceWidgetName (extract_widget_subtree). Exact live instance name. Provide this or className (get_runtime_delegates). Exact live instance name (get_runtime_widget, invoke_runtime_function). Exact live instance name. Provide this or classFilter (inspect_runtime_instances). Named child of a live PIE widget, or the live UserWidget's own name (set_runtime_focus). Widget whose navigation a single write sets (set_widget_navigation)"),
  widgetToFocus: z.string().optional().describe("Target widget of a single Explicit write"),
  world: z.string().optional().describe("Runtime world scope: pie (default) | game | auto. The editor world is never a valid target"),
  wrapperClass: z.string().optional().describe("Panel widget class (CanvasPanel, VerticalBox, Overlay, etc.); must be a UPanelWidget subclass"),
  wrapperName: z.string().optional().describe("Name for the new wrapper widget"),
  zOrder: z.number().optional().describe("Viewport Z-order (#602)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
