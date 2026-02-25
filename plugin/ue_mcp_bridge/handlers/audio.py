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
    sound_path = params.get("soundPath", "")
    location = params.get("location", [0, 0, 0])
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
    sound_path = params.get("soundPath", "")
    location = params.get("location", [0, 0, 0])
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


HANDLERS = {
    "list_sound_assets": list_sound_assets,
    "play_sound_at_location": play_sound_at_location,
    "spawn_ambient_sound": spawn_ambient_sound,
}
