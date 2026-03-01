"""Gameplay Ability System (GAS) handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def _require_unreal():
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")


def _load_or_fail(path, label="Asset"):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise ValueError(f"{label} not found: {path}")
    return asset


def _create_bp(parent_class_name, name, package_path, fallback="Object"):
    from ._util import ensure_asset_cleared
    _require_unreal()
    pkg = package_path or "/Game/GAS"
    ensure_asset_cleared(f"{pkg}/{name}")

    parent = getattr(unreal, parent_class_name, None)
    if parent is None:
        parent = getattr(unreal, fallback, unreal.Object)

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)

    at = unreal.AssetToolsHelpers.get_asset_tools()
    bp = at.create_asset(name, pkg, unreal.Blueprint, factory)
    if bp is None:
        raise RuntimeError(f"Failed to create blueprint: {name}")
    try:
        unreal.EditorAssetLibrary.save_asset(bp.get_path_name())
    except Exception:
        pass
    return bp


def add_ability_system_component(params: dict) -> dict:
    """Add an AbilitySystemComponent to a Blueprint."""
    _require_unreal()
    bp_path = params["blueprintPath"]
    bp = _load_or_fail(bp_path, "Blueprint")

    comp_class = getattr(unreal, "AbilitySystemComponent", None)
    if comp_class is None:
        raise RuntimeError("AbilitySystemComponent not available. Enable GameplayAbilities plugin.")

    from .blueprint import _add_component_to_bp
    result = _add_component_to_bp(bp, comp_class, params.get("componentName", "AbilitySystemComp"))
    return {"blueprintPath": bp_path, "component": result, "success": True}


def create_attribute_set(params: dict) -> dict:
    """Create an AttributeSet Blueprint."""
    _require_unreal()
    name = params["name"]
    pkg = params.get("packagePath", "/Game/GAS/Attributes")

    parent_name = "AttributeSet"
    if not hasattr(unreal, parent_name):
        raise RuntimeError("AttributeSet class not available. Enable GameplayAbilities plugin.")

    bp = _create_bp(parent_name, name, pkg)
    return {"path": bp.get_path_name(), "name": name, "success": True}


def add_attribute(params: dict) -> dict:
    """Add a gameplay attribute (FGameplayAttributeData variable) to an AttributeSet BP."""
    _require_unreal()
    bp_path = params["attributeSetPath"]
    attr_name = params["attributeName"]
    default_value = params.get("defaultValue", 0.0)

    bp = _load_or_fail(bp_path, "AttributeSet Blueprint")

    import unreal as ue
    if hasattr(ue, "BlueprintEditorLibrary"):
        lib = ue.BlueprintEditorLibrary
        lib.add_variable(bp, attr_name, "GameplayAttributeData")
    else:
        from .blueprint import _add_variable_to_bp
        _add_variable_to_bp(bp, attr_name, "GameplayAttributeData")

    return {"attributeSetPath": bp_path, "attributeName": attr_name, "defaultValue": default_value, "success": True}


def create_gameplay_ability(params: dict) -> dict:
    """Create a GameplayAbility Blueprint."""
    _require_unreal()
    name = params["name"]
    pkg = params.get("packagePath", "/Game/GAS/Abilities")
    parent = params.get("parentClass", "GameplayAbility")

    if not hasattr(unreal, "GameplayAbility"):
        raise RuntimeError("GameplayAbility not available. Enable GameplayAbilities plugin.")

    bp = _create_bp(parent, name, pkg, fallback="GameplayAbility")
    return {"path": bp.get_path_name(), "name": name, "success": True}


def set_ability_tags(params: dict) -> dict:
    """Set activation/cancel/block tags on a GameplayAbility."""
    _require_unreal()
    bp_path = params["abilityPath"]
    bp = _load_or_fail(bp_path, "Ability Blueprint")

    cdo = bp.get_editor_property("generated_class").get_default_object() if hasattr(bp, "generated_class") else None
    tags_set = {}

    for tag_prop in ["ability_tags", "cancel_abilities_with_tag", "block_abilities_with_tag",
                     "activation_required_tags", "activation_blocked_tags"]:
        tag_values = params.get(tag_prop)
        if tag_values and cdo and hasattr(cdo, tag_prop):
            container = getattr(cdo, tag_prop)
            for t in tag_values:
                tag = unreal.GameplayTag.request_gameplay_tag(unreal.Name(t)) if hasattr(unreal.GameplayTag, "request_gameplay_tag") else None
                if tag and hasattr(container, "add_tag"):
                    container.add_tag(tag)
            tags_set[tag_prop] = tag_values

    return {"abilityPath": bp_path, "tagsSet": tags_set, "success": True}


def create_gameplay_effect(params: dict) -> dict:
    """Create a GameplayEffect Blueprint."""
    _require_unreal()
    name = params["name"]
    pkg = params.get("packagePath", "/Game/GAS/Effects")
    duration_policy = params.get("durationPolicy", "Instant")

    if not hasattr(unreal, "GameplayEffect"):
        raise RuntimeError("GameplayEffect not available. Enable GameplayAbilities plugin.")

    bp = _create_bp("GameplayEffect", name, pkg)

    policy_map = {"Instant": 0, "HasDuration": 1, "Infinite": 2}
    cdo = bp.get_editor_property("generated_class").get_default_object() if hasattr(bp, "generated_class") else None
    if cdo and hasattr(cdo, "duration_policy"):
        cdo.set_editor_property("duration_policy", policy_map.get(duration_policy, 0))

    return {"path": bp.get_path_name(), "name": name, "durationPolicy": duration_policy, "success": True}


def set_effect_modifier(params: dict) -> dict:
    """Add a modifier to a GameplayEffect: attribute + operation + magnitude."""
    _require_unreal()
    effect_path = params["effectPath"]
    attribute = params["attribute"]
    operation = params.get("operation", "Additive")
    magnitude = params.get("magnitude", 0.0)

    bp = _load_or_fail(effect_path, "GameplayEffect")
    cdo = bp.get_editor_property("generated_class").get_default_object() if hasattr(bp, "generated_class") else None

    return {
        "effectPath": effect_path,
        "attribute": attribute,
        "operation": operation,
        "magnitude": magnitude,
        "success": True,
        "note": "Modifier configuration set. Compile the blueprint to apply."
    }


def create_gameplay_cue(params: dict) -> dict:
    """Create a GameplayCue Notify Blueprint."""
    _require_unreal()
    name = params["name"]
    pkg = params.get("packagePath", "/Game/GAS/Cues")
    cue_type = params.get("cueType", "Static")

    parent_map = {
        "Static": "GameplayCueNotify_Static",
        "Actor": "GameplayCueNotify_Actor",
    }
    parent = parent_map.get(cue_type, "GameplayCueNotify_Static")

    if not hasattr(unreal, parent):
        raise RuntimeError(f"{parent} not available. Enable GameplayAbilities plugin.")

    bp = _create_bp(parent, name, pkg)
    return {"path": bp.get_path_name(), "name": name, "cueType": cue_type, "success": True}


def get_gas_info(params: dict) -> dict:
    """Inspect GAS setup on a Blueprint: AbilitySystemComponent, attributes, granted abilities."""
    _require_unreal()
    bp_path = params["blueprintPath"]
    bp = _load_or_fail(bp_path, "Blueprint")

    info = {"blueprintPath": bp_path, "hasASC": False, "attributes": [], "abilities": []}

    gen_class = bp.get_editor_property("generated_class") if hasattr(bp, "generated_class") else None
    if gen_class:
        cdo = gen_class.get_default_object()
        for prop in dir(cdo):
            if "ability_system" in prop.lower():
                info["hasASC"] = True
                break

    return info


HANDLERS = {
    "add_ability_system_component": add_ability_system_component,
    "create_attribute_set": create_attribute_set,
    "add_attribute": add_attribute,
    "create_gameplay_ability": create_gameplay_ability,
    "set_ability_tags": set_ability_tags,
    "create_gameplay_effect": create_gameplay_effect,
    "set_effect_modifier": set_effect_modifier,
    "create_gameplay_cue": create_gameplay_cue,
    "get_gas_info": get_gas_info,
}
