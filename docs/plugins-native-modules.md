# Native C++ modules

A plugin that needs engine APIs the bridge does not expose ships a UE C++ module in its npm package. `ue-mcp plugin install` copies the module into the project, the project build compiles it, and its `StartupModule` registers handlers on the bridge with `UEMCP::RegisterExternalHandler`.

A manifest for a fictional `sample-recorder` plugin:

```yaml
actionPrefix: rec                    # used only when injecting into a built-in category

nativeModule:
  uePluginName: Sample_Recorder      # the .uplugin that gets deployed
  minBridgeApi: 2                    # minimum UEMCP_BRIDGE_API_VERSION; 2 = typed contracts
  source: ue/Plugins/Sample_Recorder # path inside the package
  supportedEngineVersions: ["5.6", "5.7"]   # optional; shown on the registry listing
  category: recorder                 # surface the handlers under recorder(...)
  categoryDescription: "Record and replay gameplay input"
  specs: handler-specs.json          # recorded C++ contracts (ue-mcp plugin record-specs)
  handlers:
    record_arm:   { description: "Arm the input recorder" }
    replay_arm:   { description: "Arm the input replayer" }
    inject_input:
      description: "Single-frame Enhanced Input inject"
      timeoutSeconds: 5
```

## How handlers become actions

With `category` set, every handler listed under `handlers:` becomes an action that dispatches to the bridge method of the same name. No TypeScript task is involved.

- **A new category** (`recorder` above) becomes its own top-level tool, with unprefixed actions: `recorder(action="record_arm")`. `categoryDescription` is the tool's summary. Between plugins, the first to claim a name wins.
- **A built-in category** (for example `gameplay`) gets the handlers injected with the `actionPrefix`: `gameplay(action="rec_record_arm")`.

Without `category`, the handlers are registered on the bridge but are not actions. A task can still reach them with `this.bridge.call("record_arm", params)`.

Per handler:

- `description` is the action's description.
- `timeoutSeconds` is the bridge-call timeout (default 30).
- `schema` declares loose parameters (`string`, `number`, `boolean`, `object`, `array`) for a handler with no recorded contract. It cannot express enums, ranges, integers or nested fields, and `required` is reported but left to the handler to enforce. Prefer a [typed contract](#typed-contracts).

With `specs:` set, a handler listed under `handlers:` that has no recorded contract is not surfaced, and the reason appears under `degraded`.

## Typed contracts

Bridge ABI 2 lets a native handler register with the same parameter contract a core handler declares (`MCPHandlerSpec.h` in the bridge's `Public/`). ue-mcp builds the action's signature and `describe_action` output from it and checks every call before sending it:

- each parameter's type, including `Integer`, `Vec3`, `Rotator`, `Color` and `Any`;
- `Enum({...})` on a string or an array of strings, case-sensitive;
- `Min`, `Max` and `Range` on a number or integer, or on each element of an array of them;
- `WithFields({...})` on an object or an array of objects, to any depth;
- required parameters, aliases, unions, literals, tagged variants and choices (`MCPSpec::ExactlyOne`, `MCPSpec::AtLeastOne`);
- undeclared keys, which are refused.

```cpp
UEMCP::RegisterExternalHandler(TEXT("stamp_set"), &FStampHandlers::Set, {
    MCPParam::Required(TEXT("actorPath"), EMCPParamType::String, TEXT("The stamp actor")).Alias(TEXT("path")),
    MCPParam::Optional(TEXT("blendMode"), EMCPParamType::String, TEXT("How heights combine"))
        .Enum({ TEXT("Max"), TEXT("Min"), TEXT("Override") }),
    MCPParam::Optional(TEXT("priority"), EMCPParamType::Integer, TEXT("Evaluation order")).Min(0.0),
    MCPParam::Optional(TEXT("quality"), EMCPParamType::Object, TEXT("Per-platform quality")).WithFields({
        MCPParam::RequiredField(TEXT("lod"), EMCPParamType::Integer, TEXT("LOD index")).Range(0.0, 7.0),
    }),
});
```

An invalid contract (an enum on a number, a minimum above its maximum, a field declared twice) is logged and dropped, and the call returns `false`. The handler stays registered.

The bridge enforces the contract on every call, whoever sent it, and refuses a bad one with `Invalid parameters for <handler>: <reason>`. It renames aliases to the declared name before the handler runs, so read only the declared name. `UEMCP::ContractViolation` (`MCPContract.h`) runs the same check for a nested call. For a name-to-scalar map, declare `Any` with `.OneOfForms({ EMCPValueForm::ScalarMap })`.

The bridge publishes the contracts in `get_bridge_capabilities.pluginHandlerSpecs`. Record them into the file `nativeModule.specs` names, with an editor running that has your module loaded:

```bash
ue-mcp plugin record-specs [dir] --project path/to/Project.uproject
ue-mcp plugin record-specs [dir] --project path/to/Project.uproject --check   # fail when the file is stale
```

Ship the file in the package. The server builds the surface from it, so the surface is the same with or without an editor connected. `project(action="get_status")` reports `deployedPlugin.handlerSpecDrift` when the running module registered something different. A call to a drifted handler is checked against what the running module registered; `describe_action` shows the recording until you record again and the client reconnects.

## Package layout

```
sample-recorder/
  ue-mcp.plugin.yml
  handler-specs.json
  dist/                              # TypeScript tasks, if any
  ue/
    Plugins/
      Sample_Recorder/
        Sample_Recorder.uplugin
        Source/
          Sample_Recorder/
            Sample_Recorder.Build.cs
            Private/
```

Add `ue` and the specs file to `files` in `package.json`:

```json
"files": ["dist", "ue", "handler-specs.json", "ue-mcp.plugin.yml", "knowledge", "skills", "README.md"]
```

## The module

Depend on `UE_MCP_Bridge` in the `.Build.cs`:

```csharp
public class Sample_Recorder : ModuleRules
{
    public Sample_Recorder(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json" });
        PrivateDependencyModuleNames.AddRange(new string[] { "UE_MCP_Bridge" });
    }
}
```

Register handlers in `StartupModule` and remove them in `ShutdownModule`:

```cpp
#include "MCPHandlerRegistration.h"

void FSample_RecorderModule::StartupModule()
{
    UEMCP::RegisterExternalHandler(
        TEXT("inject_input"),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonValue>
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("success"), true);
            return MakeShared<FJsonValueObject>(Result);
        },
        {
            MCPParam::Required(TEXT("action_path"), EMCPParamType::String, TEXT("InputAction asset path")),
            MCPParam::Optional(TEXT("value_x"), EMCPParamType::Number, TEXT("X axis value")).Range(-1.0, 1.0),
        });
}

void FSample_RecorderModule::ShutdownModule()
{
    UEMCP::UnregisterExternalHandler(TEXT("inject_input"));
}
```

Register the bare method name (`inject_input`). ue-mcp adds the `actionPrefix` when it injects the action into a built-in category. `RegisterExternalHandlerWithTimeout` and the contract overload's `TimeoutSeconds` argument set a longer game-thread timeout.

## Install and uninstall

For a plugin with `nativeModule:`, `ue-mcp plugin install` also:

1. Reads `UEMCP_BRIDGE_API_VERSION` from the project's deployed `MCPHandlerRegistration.h` and fails when it is below `minBridgeApi`, pointing at `ue-mcp deploy`. With no bridge deployed it warns and continues.
2. Copies `<package>/<source>` to `<project>/Plugins/<uePluginName>/`, removing files a previous version deployed and this one does not ship.
3. Records the copied files in `<project>/.ue-mcp/native-modules.json`.
4. Prints `REBUILD REQUIRED`. Build the project (`ue-mcp build`) before launching the editor.

`ue-mcp plugin uninstall` deletes `Plugins/<uePluginName>/` (source, Binaries and Intermediate) and the plugin's entry in the `.uproject`. It refuses while an editor has the project open.

## Bridge ABI

`UEMCP_BRIDGE_API_VERSION` is the C++ contract native plugins compile against. It changes only when the `FExternalHandlerFn` signature or the registration contract changes. Version 2 added typed contracts, so a module that registers one needs `minBridgeApi: 2`. A plugin whose `minBridgeApi` is above the deployed version is not loaded. `project(action="get_status")` reports the deployed version as `bridgeApiVersion`.
