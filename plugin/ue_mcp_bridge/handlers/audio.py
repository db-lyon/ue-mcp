"""Audio handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def list_sound_assets(params: dict) -> dict:
    directory = params.get("directory", "/Game")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(directory, recursive=True)

    sound_types = {"SoundWave", "SoundCue", "MetaSoundSource", "SoundMix", "SoundClass"}
    sounds = []
    for ad in assets:
        class_name = str(ad.asset_class_path.asset_name)
        if class_name in sound_types:
            sounds.append({
                "path": str(ad.package_name),
                "name": str(ad.asset_name),
                "type": class_name,
            })

    return {"directory": directory, "count": len(sounds), "sounds": sounds}


def play_sound_at_location(params: dict) -> dict:
    from ._util import to_vec3
    sound_path = params.get("soundPath", "")
    location = to_vec3(params.get("location", [0, 0, 0]))
    volume = params.get("volume", 1.0)
    pitch = params.get("pitch", 1.0)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    sound = unreal.EditorAssetLibrary.load_asset(sound_path)
    if sound is None:
        raise ValueError(f"Sound not found: {sound_path}")

    world = unreal.EditorLevelLibrary.get_editor_world()
    loc = unreal.Vector(location[0], location[1], location[2])

    unreal.GameplayStatics.play_sound_at_location(
        world, sound, loc, volume_multiplier=volume, pitch_multiplier=pitch
    )

    return {"soundPath": sound_path, "success": True}


def spawn_ambient_sound(params: dict) -> dict:
    from ._util import to_vec3
    sound_path = params.get("soundPath", "")
    location = to_vec3(params.get("location", [0, 0, 0]))
    label = params.get("label", "")
    volume = params.get("volume", 1.0)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    loc = unreal.Vector(location[0], location[1], location[2])
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.AmbientSound, loc)

    if actor is None:
        raise RuntimeError("Failed to spawn AmbientSound")

    if label:
        actor.set_actor_label(label)

    sound = unreal.EditorAssetLibrary.load_asset(sound_path)
    if sound:
        comp = actor.get_component_by_class(unreal.AudioComponent)
        if comp:
            comp.set_editor_property("sound", sound)
            comp.set_editor_property("volume_multiplier", float(volume))

    return {"soundPath": sound_path, "label": label or actor.get_actor_label(), "success": True}


def create_sound_cue(params: dict) -> dict:
    """Create a new SoundCue asset, optionally wiring in a SoundWave."""
    from ._util import resolve_asset_path, ensure_asset_cleared
    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/Audio")
    if not asset_name:
        asset_name = params.get("name", "SC_NewCue")
        package_path = params.get("packagePath", "/Game/Audio")
        full_path = f"{package_path}/{asset_name}"
    sound_wave_path = params.get("soundWavePath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    ensure_asset_cleared(full_path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    factory = None
    if hasattr(unreal, "SoundCueFactoryNew"):
        factory = unreal.SoundCueFactoryNew()
    elif hasattr(unreal, "SoundCueFactory"):
        factory = unreal.SoundCueFactory()

    if factory and sound_wave_path:
        wave = unreal.EditorAssetLibrary.load_asset(sound_wave_path)
        if wave and hasattr(factory, "set_editor_property"):
            try:
                factory.set_editor_property("initial_sound_wave", wave)
            except Exception:
                pass

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        asset = tools.create_asset(asset_name, package_path, unreal.SoundCue, None)

    if asset is None:
        raise RuntimeError(f"Failed to create SoundCue at {package_path}/{asset_name}")

    unreal.EditorAssetLibrary.save_asset(f"{package_path}/{asset_name}")
    return {
        "path": f"{package_path}/{asset_name}",
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
    }


def create_metasound_source(params: dict) -> dict:
    """Create a new MetaSoundSource asset."""
    from ._util import resolve_asset_path, ensure_asset_cleared
    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/Audio")
    if not asset_name:
        asset_name = params.get("name", "MS_NewSource")
        package_path = params.get("packagePath", "/Game/Audio")
        full_path = f"{package_path}/{asset_name}"

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    ensure_asset_cleared(full_path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    factory = None
    if hasattr(unreal, "MetaSoundSourceFactory"):
        factory = unreal.MetaSoundSourceFactory()
    elif hasattr(unreal, "MetaSoundFactory"):
        factory = unreal.MetaSoundFactory()

    if factory is None:
        raise RuntimeError("MetaSound factory not available in this UE version")

    asset = tools.create_asset(asset_name, package_path, None, factory)
    if asset is None:
        raise RuntimeError(f"Failed to create MetaSoundSource at {package_path}/{asset_name}")

    unreal.EditorAssetLibrary.save_asset(f"{package_path}/{asset_name}")
    return {
        "path": f"{package_path}/{asset_name}",
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
    }


HANDLERS = {
    "list_sound_assets": list_sound_assets,
    "play_sound_at_location": play_sound_at_location,
    "spawn_ambient_sound": spawn_ambient_sound,
    "create_sound_cue": create_sound_cue,
    "create_metasound_source": create_metasound_source,
}
