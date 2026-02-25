"""Sequencer / Cinematics handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def create_level_sequence(params: dict) -> dict:
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    package_path = "/".join(asset_path.split("/")[:-1])
    asset_name = asset_path.split("/")[-1]

    factory = unreal.LevelSequenceFactoryNew()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    seq = asset_tools.create_asset(asset_name, package_path, unreal.LevelSequence, factory)
    if seq is None:
        raise RuntimeError(f"Failed to create LevelSequence at {asset_path}")

    return {"path": asset_path, "success": True}


def get_sequence_info(params: dict) -> dict:
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    seq = unreal.EditorAssetLibrary.load_asset(asset_path)
    if seq is None:
        raise ValueError(f"Sequence not found: {asset_path}")

    movie_scene = seq.get_movie_scene() if hasattr(seq, "get_movie_scene") else None
    info = {"path": asset_path, "name": seq.get_name()}

    if movie_scene:
        display_rate = movie_scene.get_display_rate() if hasattr(movie_scene, "get_display_rate") else None
        info["displayRate"] = str(display_rate) if display_rate else None

        bindings = []
        if hasattr(movie_scene, "get_bindings"):
            for binding in movie_scene.get_bindings():
                tracks = []
                if hasattr(binding, "get_tracks"):
                    for track in binding.get_tracks():
                        tracks.append({"name": track.get_name(), "type": track.get_class().get_name()})
                bindings.append({
                    "name": binding.get_name() if hasattr(binding, "get_name") else str(binding),
                    "tracks": tracks,
                })
        info["bindings"] = bindings

        master_tracks = []
        if hasattr(movie_scene, "get_master_tracks"):
            for track in movie_scene.get_master_tracks():
                master_tracks.append({"name": track.get_name(), "type": track.get_class().get_name()})
        info["masterTracks"] = master_tracks

    return info


def add_sequence_track(params: dict) -> dict:
    asset_path = params.get("path", "")
    track_type = params.get("trackType", "")
    actor_label = params.get("actorLabel", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    seq = unreal.EditorAssetLibrary.load_asset(asset_path)
    if seq is None:
        raise ValueError(f"Sequence not found: {asset_path}")

    movie_scene = seq.get_movie_scene()

    if actor_label:
        world = unreal.EditorLevelLibrary.get_editor_world()
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target = None
        for a in actors:
            if a.get_actor_label() == actor_label or a.get_name() == actor_label:
                target = a
                break
        if target is None:
            raise ValueError(f"Actor not found: {actor_label}")

        binding = seq.add_possessable(target) if hasattr(seq, "add_possessable") else None
        if binding:
            return {"path": asset_path, "actorLabel": actor_label, "binding": str(binding), "success": True}

    if hasattr(movie_scene, "add_master_track"):
        track_class = getattr(unreal, track_type, None)
        if track_class:
            track = movie_scene.add_master_track(track_class)
            return {"path": asset_path, "trackType": track_type, "success": True}

    return {"path": asset_path, "trackType": track_type, "success": False, "note": "Track type not recognized"}


def play_sequence(params: dict) -> dict:
    action = params.get("action", "play")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()
    if action == "play":
        unreal.SystemLibrary.execute_console_command(world, "Sequencer.Play")
    elif action == "stop":
        unreal.SystemLibrary.execute_console_command(world, "Sequencer.Stop")
    elif action == "pause":
        unreal.SystemLibrary.execute_console_command(world, "Sequencer.Pause")

    return {"action": action, "success": True}


HANDLERS = {
    "create_level_sequence": create_level_sequence,
    "get_sequence_info": get_sequence_info,
    "add_sequence_track": add_sequence_track,
    "play_sequence": play_sequence,
}
