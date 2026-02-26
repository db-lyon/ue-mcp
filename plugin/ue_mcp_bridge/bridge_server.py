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
        self._game_thread_queue: queue.Queue = queue.Queue()
        self._tick_handle = None
        self._register_handlers()

    def _register_handlers(self):
        from .handlers import asset as asset_h
        from .handlers import blueprint as bp_h
        from .handlers import editor as editor_h
        from .handlers import pie as pie_h
        from .handlers import reflection as reflect_h
        from .handlers import nodes as nodes_h
        from .handlers import level as level_h
        from .handlers import material as material_h
        from .handlers import animation as anim_h
        from .handlers import umg as umg_h
        from .handlers import pcg as pcg_h
        from .handlers import landscape as landscape_h
        from .handlers import foliage as foliage_h
        from .handlers import sequencer as sequencer_h
        from .handlers import niagara as niagara_h
        from .handlers import lighting as lighting_h
        from .handlers import audio as audio_h
        from .handlers import navigation as nav_h
        from .handlers import spline as spline_h
        from .handlers import volume as volume_h
        from .handlers import material_authoring as mat_auth_h
        from .handlers import texture as texture_h
        from .handlers import level_management as level_mgmt_h
        from .handlers import input_system as input_h
        from .handlers import behavior_tree as bt_h
        from .handlers import skeleton as skeleton_h
        from .handlers import widget as widget_h
        from .handlers import performance as perf_h
        from .handlers import demo as demo_h
        from .handlers import asset_management as asset_mgmt_h
        from .handlers import mesh_import as mesh_import_h
        from .handlers import physics as physics_h

        self._handlers.update(asset_h.HANDLERS)
        self._handlers.update(bp_h.HANDLERS)
        self._handlers.update(editor_h.HANDLERS)
        self._handlers.update(pie_h.HANDLERS)
        self._handlers.update(reflect_h.HANDLERS)
        self._handlers.update(nodes_h.HANDLERS)
        self._handlers.update(level_h.HANDLERS)
        self._handlers.update(material_h.HANDLERS)
        self._handlers.update(anim_h.HANDLERS)
        self._handlers.update(umg_h.HANDLERS)
        self._handlers.update(pcg_h.HANDLERS)
        self._handlers.update(landscape_h.HANDLERS)
        self._handlers.update(foliage_h.HANDLERS)
        self._handlers.update(sequencer_h.HANDLERS)
        self._handlers.update(niagara_h.HANDLERS)
        self._handlers.update(lighting_h.HANDLERS)
        self._handlers.update(audio_h.HANDLERS)
        self._handlers.update(nav_h.HANDLERS)
        self._handlers.update(spline_h.HANDLERS)
        self._handlers.update(volume_h.HANDLERS)
        self._handlers.update(mat_auth_h.HANDLERS)
        self._handlers.update(texture_h.HANDLERS)
        self._handlers.update(level_mgmt_h.HANDLERS)
        self._handlers.update(input_h.HANDLERS)
        self._handlers.update(bt_h.HANDLERS)
        self._handlers.update(skeleton_h.HANDLERS)
        self._handlers.update(widget_h.HANDLERS)
        self._handlers.update(perf_h.HANDLERS)
        self._handlers.update(demo_h.HANDLERS)
        self._handlers.update(asset_mgmt_h.HANDLERS)
        self._handlers.update(mesh_import_h.HANDLERS)
        self._handlers.update(physics_h.HANDLERS)

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
