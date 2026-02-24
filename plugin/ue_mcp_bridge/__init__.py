"""
UE MCP Bridge Plugin

A lightweight WebSocket bridge server that runs inside Unreal Editor,
exposing the editor's Python API to the UE-MCP server for live operations.

Start the bridge by running in the UE Python console:
    import ue_mcp_bridge; ue_mcp_bridge.start()

Or add to your project's startup script for automatic launch.
"""

__version__ = "0.1.0"

_server = None

def start(host="localhost", port=9877):
    """Start the MCP bridge server."""
    global _server
    if _server is not None:
        print(f"[UE-MCP] Bridge already running on {host}:{port}")
        return

    from .bridge_server import BridgeServer
    _server = BridgeServer(host, port)
    _server.start()
    print(f"[UE-MCP] Bridge server started on ws://{host}:{port}")

def stop():
    """Stop the MCP bridge server."""
    global _server
    if _server is not None:
        _server.stop()
        _server = None
        print("[UE-MCP] Bridge server stopped")

def restart(host="localhost", port=9877):
    """Restart the MCP bridge server."""
    stop()
    start(host, port)
