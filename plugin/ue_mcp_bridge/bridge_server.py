"""
WebSocket server that receives JSON-RPC requests from the MCP server
and dispatches them to the appropriate handler.
"""

import asyncio
import json
import threading
import traceback
from typing import Any

try:
    import websockets
    import websockets.server
except ImportError:
    websockets = None

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


class BridgeServer:
    def __init__(self, host: str = "localhost", port: int = 9877):
        self.host = host
        self.port = port
        self._thread: threading.Thread | None = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._server = None
        self._handlers: dict[str, Any] = {}
        self._register_handlers()

    def _register_handlers(self):
        from .handlers import asset as asset_h
        from .handlers import blueprint as bp_h
        from .handlers import editor as editor_h
        from .handlers import pie as pie_h
        from .handlers import reflection as reflect_h

        self._handlers.update(asset_h.HANDLERS)
        self._handlers.update(bp_h.HANDLERS)
        self._handlers.update(editor_h.HANDLERS)
        self._handlers.update(pie_h.HANDLERS)
        self._handlers.update(reflect_h.HANDLERS)

    def start(self):
        if websockets is None:
            raise RuntimeError(
                "websockets package not found. Install it in your UE Python environment:\n"
                "  <UE_INSTALL>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe -m pip install websockets"
            )

        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()

    def stop(self):
        if self._loop is not None:
            self._loop.call_soon_threadsafe(self._loop.stop)
        if self._thread is not None:
            self._thread.join(timeout=5)
            self._thread = None

    def _run_loop(self):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)

        async def serve():
            self._server = await websockets.server.serve(
                self._handle_connection,
                self.host,
                self.port,
            )
            if HAS_UNREAL:
                unreal.log(f"[UE-MCP] Bridge listening on ws://{self.host}:{self.port}")
            await self._server.wait_closed()

        try:
            self._loop.run_until_complete(serve())
        except Exception:
            pass

    async def _handle_connection(self, websocket):
        remote = websocket.remote_address
        if HAS_UNREAL:
            unreal.log(f"[UE-MCP] Client connected from {remote}")
        else:
            print(f"[UE-MCP] Client connected from {remote}")

        try:
            async for message in websocket:
                response = await self._dispatch(message)
                await websocket.send(json.dumps(response))
        except Exception as e:
            if HAS_UNREAL:
                unreal.log_warning(f"[UE-MCP] Connection error: {e}")
            else:
                print(f"[UE-MCP] Connection error: {e}")

    async def _dispatch(self, raw_message: str) -> dict:
        try:
            msg = json.loads(raw_message)
        except json.JSONDecodeError as e:
            return {"id": None, "error": {"code": -32700, "message": f"Parse error: {e}"}}

        req_id = msg.get("id")
        method = msg.get("method", "")
        params = msg.get("params", {})

        handler = self._handlers.get(method)
        if handler is None:
            return {
                "id": req_id,
                "error": {"code": -32601, "message": f"Unknown method: {method}"}
            }

        try:
            if asyncio.iscoroutinefunction(handler):
                result = await handler(params)
            else:
                result = handler(params)

            return {"id": req_id, "result": result}
        except Exception as e:
            traceback.print_exc()
            return {
                "id": req_id,
                "error": {"code": -32000, "message": str(e)}
            }
