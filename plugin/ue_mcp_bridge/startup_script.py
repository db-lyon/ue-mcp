"""
UE Editor startup script for the MCP bridge.

To auto-start the bridge when the editor launches, add this to your project's
DefaultEngine.ini under [/Script/PythonScriptPlugin.PythonScriptPluginSettings]:

    StartupScripts=/Path/To/ue_mcp_bridge/startup_script.py

Or place this file in your project's Content/Python directory and configure
the Python plugin to run it on startup.
"""

import sys
import os

plugin_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if plugin_dir not in sys.path:
    sys.path.insert(0, plugin_dir)

try:
    import ue_mcp_bridge
    ue_mcp_bridge.start()
except Exception as e:
    try:
        import unreal
        unreal.log_error(f"[UE-MCP] Failed to start bridge: {e}")
    except ImportError:
        print(f"[UE-MCP] Failed to start bridge: {e}")
