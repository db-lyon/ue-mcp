using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class PhysicsTools
{
    [McpServerTool, Description(
        "Set the collision profile on an actor's primitive component. " +
        "Common profiles: 'BlockAll', 'OverlapAll', 'NoCollision', 'Pawn', 'PhysicsActor', 'Vehicle'.")]
    public static async Task<string> set_collision_profile(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label in the level")] string actorLabel,
        [Description("Collision profile name (e.g. 'BlockAll', 'OverlapAll', 'Pawn')")] string profileName,
        [Description("Optional: specific component class to target")] string? componentClass = null)
    {
        router.EnsureLiveMode("set_collision_profile");
        return await bridge.SendAndSerializeAsync("set_collision_profile", new()
        {
            ["actorLabel"] = actorLabel,
            ["profileName"] = profileName,
            ["componentClass"] = componentClass ?? ""
        });
    }

    [McpServerTool, Description(
        "Enable or disable physics simulation on an actor's mesh component.")]
    public static async Task<string> set_simulate_physics(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label in the level")] string actorLabel,
        [Description("True to enable physics simulation, false to disable")] bool simulate = true)
    {
        router.EnsureLiveMode("set_simulate_physics");
        return await bridge.SendAndSerializeAsync("set_simulate_physics", new()
        {
            ["actorLabel"] = actorLabel,
            ["simulate"] = simulate
        });
    }

    [McpServerTool, Description(
        "Set the collision enabled state on an actor's component. " +
        "Options: 'NoCollision', 'QueryOnly', 'PhysicsOnly', 'QueryAndPhysics'.")]
    public static async Task<string> set_collision_enabled(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label in the level")] string actorLabel,
        [Description("Collision mode: 'NoCollision', 'QueryOnly', 'PhysicsOnly', or 'QueryAndPhysics'")] string collisionEnabled = "QueryAndPhysics")
    {
        router.EnsureLiveMode("set_collision_enabled");
        return await bridge.SendAndSerializeAsync("set_collision_enabled", new()
        {
            ["actorLabel"] = actorLabel,
            ["collisionEnabled"] = collisionEnabled
        });
    }

    [McpServerTool, Description(
        "Set physics properties (mass, damping, gravity) on an actor's mesh component. " +
        "Properties: 'mass_in_kg', 'linear_damping', 'angular_damping', 'enable_gravity_override', etc.")]
    public static async Task<string> set_physics_properties(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label in the level")] string actorLabel,
        [Description("Properties to set as JSON (e.g. '{\"mass_in_kg\": 50, \"linear_damping\": 0.1}')")] string properties)
    {
        router.EnsureLiveMode("set_physics_properties");
        return await bridge.SendAndSerializeAsync("set_physics_properties", new()
        {
            ["actorLabel"] = actorLabel,
            ["properties"] = JsonSerializer.Deserialize<object>(properties)
        });
    }
}
