"""
WebSocket server that receives JSON-RPC requests from the MCP server
and dispatches them to the appropriate handler on the game thread.
"""

import asyncio
import json
import queue
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
        self._handler_modules: list = []
        self._game_thread_queue: queue.Queue = queue.Queue()
        self._tick_handle = None
        self._register_handlers()

    def _get_handler_modules(self) -> list:
        from .handlers import (
            asset, blueprint, editor, pie, reflection, nodes, level,
            material, animation, umg, pcg, landscape, foliage, sequencer,
            niagara, lighting, audio, navigation, spline, volume,
            material_authoring, texture, level_management, input_system,
            behavior_tree, skeleton, widget, performance, demo,
            asset_management, mesh_import, physics, gas, networking,
            ai_systems, material_graph, niagara_authoring, game_framework,
            pipeline, logs,
        )
        return [
            asset, blueprint, editor, pie, reflection, nodes, level,
            material, animation, umg, pcg, landscape, foliage, sequencer,
            niagara, lighting, audio, navigation, spline, volume,
            material_authoring, texture, level_management, input_system,
            behavior_tree, skeleton, widget, performance, demo,
            asset_management, mesh_import, physics, gas, networking,
            ai_systems, material_graph, niagara_authoring, game_framework,
            pipeline, logs,
        ]

    def _register_handlers(self):
        self._handler_modules = self._get_handler_modules()
        self._handlers.clear()
        for mod in self._handler_modules:
            self._handlers.update(mod.HANDLERS)
        self._handlers["reload_handlers"] = lambda params: self._reload_handlers()

    def _reload_handlers(self) -> dict:
        """Reload all handler modules from disk and re-register them."""
        import importlib
        old_count = len(self._handlers)
        for mod in self._handler_modules:
            importlib.reload(mod)
        self._handler_modules = self._get_handler_modules()
        self._handlers.clear()
        for mod in self._handler_modules:
            self._handlers.update(mod.HANDLERS)
        self._handlers["reload_handlers"] = lambda params: self._reload_handlers()
        new_count = len(self._handlers)
        msg = f"Reloaded {len(self._handler_modules)} handler modules ({new_count} methods registered)"
        if HAS_UNREAL:
            unreal.log(f"[UE-MCP] {msg}")
        return {"success": True, "message": msg, "handlerCount": new_count}

    def start(self):
        if websockets is None:
            raise RuntimeError(
                "websockets package not found. Install it in your UE Python environment:\n"
                "  <UE_INSTALL>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe -m pip install websockets"
            )

        if HAS_UNREAL:
            self._tick_handle = unreal.register_slate_post_tick_callback(self._process_game_thread_queue)

        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()

    def stop(self):
        if self._tick_handle is not None and HAS_UNREAL:
            unreal.unregister_slate_post_tick_callback(self._tick_handle)
            self._tick_handle = None
        if self._loop is not None:
            self._loop.call_soon_threadsafe(self._loop.stop)
        if self._thread is not None:
            self._thread.join(timeout=5)
            self._thread = None

    def _process_game_thread_queue(self, delta_time):
        """Called every Slate tick on the game thread. Drains pending handler calls."""
        while not self._game_thread_queue.empty():
            try:
                handler, params, future, loop = self._game_thread_queue.get_nowait()
                try:
                    result = handler(params)
                    loop.call_soon_threadsafe(future.set_result, result)
                except Exception as e:
                    loop.call_soon_threadsafe(future.set_exception, e)
            except queue.Empty:
                break

    async def _run_on_game_thread(self, handler, params):
        """Queue a handler for game-thread execution and await the result."""
        future = self._loop.create_future()
        self._game_thread_queue.put((handler, params, future, self._loop))
        return await asyncio.wait_for(future, timeout=30.0)

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
            if HAS_UNREAL:
                result = await self._run_on_game_thread(handler, params)
            else:
                result = handler(params)

            return {"id": req_id, "result": result}
        except Exception as e:
            traceback.print_exc()
            return {
                "id": req_id,
                "error": {"code": -32000, "message": str(e)}
            }
