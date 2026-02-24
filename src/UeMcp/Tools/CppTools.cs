using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Offline;

namespace UeMcp.Tools;

[McpServerToolType]
public static class CppTools
{
    [McpServerTool, Description(
        "Parse a C++ header file for UE reflection macros: UCLASS, USTRUCT, UENUM declarations " +
        "with their specifiers, UPROPERTY fields with types and specifiers, UFUNCTION signatures. " +
        "This is the offline equivalent of reflect_class — use when the editor isn't running.")]
    public static string read_cpp_header(
        ModeRouter router,
        CppHeaderParser parser,
        [Description("Path to .h file, relative to project root or absolute")] string headerPath)
    {
        router.EnsureProjectLoaded();
        return parser.ReadHeader(headerPath);
    }

    [McpServerTool, Description(
        "Read a C++ module's structure: Build.cs dependencies, headers, source files. " +
        "Understanding module structure is essential for knowing what code is available where.")]
    public static string read_module(
        ModeRouter router,
        CppHeaderParser parser,
        [Description("Module name (e.g. 'MyGame', 'MyGameEditor')")] string moduleName)
    {
        router.EnsureProjectLoaded();
        return parser.ReadModule(moduleName);
    }

    [McpServerTool, Description(
        "List all C++ modules in the project's Source directory with their types, " +
        "file counts, and dependency lists.")]
    public static string list_modules(
        ModeRouter router,
        CppHeaderParser parser)
    {
        router.EnsureProjectLoaded();
        return parser.ListModules();
    }

    [McpServerTool, Description(
        "Search C++ source files for a symbol, UE macro, or text pattern. " +
        "Returns file paths, line numbers, and surrounding context.")]
    public static string search_cpp(
        ModeRouter router,
        CppHeaderParser parser,
        [Description("Search query — symbol name, macro, or text")] string query,
        [Description("Optional: limit search to a specific directory")] string? directory = null)
    {
        router.EnsureProjectLoaded();
        return parser.SearchCpp(query, directory);
    }
}
