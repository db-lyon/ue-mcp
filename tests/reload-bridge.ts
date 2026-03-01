import { EditorBridge } from "../src/bridge.js";

async function main() {
  const bridge = new EditorBridge("localhost", 9877);
  await bridge.connect(5000);

  const reloadCode = `
import importlib
import sys
import gc

# Reload all handler modules from disk
handler_modules = [k for k in list(sys.modules.keys()) if 'ue_mcp_bridge.handlers' in k]
reloaded = []
for mod_name in handler_modules:
    try:
        importlib.reload(sys.modules[mod_name])
        reloaded.append(mod_name)
    except Exception as e:
        print(f"FAIL reload {mod_name}: {e}")

# Reload bridge_server itself
bs_mod = None
for key in list(sys.modules.keys()):
    if 'bridge_server' in key:
        bs_mod = sys.modules[key]
        importlib.reload(bs_mod)
        break

# Find the live BridgeServer instance and call _reload_handlers
found = False
for obj in gc.get_objects():
    cls_name = type(obj).__name__
    if cls_name == 'BridgeServer' and hasattr(obj, '_reload_handlers'):
        count_before = len(obj.HANDLERS) if hasattr(obj, 'HANDLERS') else -1
        obj._reload_handlers()
        count_after = len(obj.HANDLERS) if hasattr(obj, 'HANDLERS') else -1
        print(f"BridgeServer found. Handlers: {count_before} -> {count_after}")
        found = True
        break

if not found:
    print(f"BridgeServer NOT found via gc. Reloaded {len(reloaded)} handler modules.")
    print(f"bridge_server module: {bs_mod}")

print(f"Reloaded modules: {len(reloaded)}")
`;

  try {
    const result = await bridge.call("execute_python", { code: reloadCode });
    console.log("Reload result:", JSON.stringify(result, null, 2));
  } catch (e: any) {
    console.log("Reload error:", e.message);
  }

  bridge.disconnect();
}

main().catch(console.error);
