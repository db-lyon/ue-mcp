using UnrealBuildTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

public class ue_mcpEditorTarget : TargetRules
{
	public ue_mcpEditorTarget(TargetInfo Target) : base(Target)
	{
		EnforceTestEnginePolicy();

		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ue_mcp");
	}

	private static void EnforceTestEnginePolicy()
	{
		string? ConfiguredRoot = Environment.GetEnvironmentVariable("UE_MCP_TEST_ENGINE_ROOT");
		if (String.IsNullOrWhiteSpace(ConfiguredRoot) || !Path.IsPathRooted(ConfiguredRoot))
		{
			throw new BuildException("UE_MCP_TEST_ENGINE_ROOT must name an absolute, dedicated engine root.");
		}

		string SelectedRoot = Canonicalize(ConfiguredRoot);
		string ActualRoot = Canonicalize(Path.Combine(Unreal.EngineDirectory.FullName, ".."));
		string MarkerPath = Path.Combine(ActualRoot, ".ue-mcp-test-engine");
		if (!File.Exists(MarkerPath))
		{
			throw new BuildException($"UE_MCP_TEST_ENGINE_ROOT is not marked as a dedicated test engine. Missing '{MarkerPath}'.");
		}

		if (!String.Equals(SelectedRoot, ActualRoot, PathComparison))
		{
			throw new BuildException(
				$"ue_mcpEditor must be built by UE_MCP_TEST_ENGINE_ROOT. Selected '{SelectedRoot}', but UBT is running from '{ActualRoot}'.");
		}

		string ProtectedValue = Environment.GetEnvironmentVariable("UE_MCP_PROTECTED_ENGINE_ROOTS") ?? String.Empty;
		foreach (string ProtectedEntry in ProtectedValue.Split(new[] { Path.PathSeparator }, StringSplitOptions.RemoveEmptyEntries))
		{
			string ProtectedRoot = Canonicalize(ProtectedEntry);
			if (IsSameOrUnder(ActualRoot, ProtectedRoot))
			{
				throw new BuildException($"UE-MCP test builds are forbidden under protected engine root '{ProtectedRoot}'.");
			}
		}

		if (!EnvironmentFlag("UE_MCP_ALLOW_TEST_ENGINE_CHANGES") && !HasCommandLineSwitch("-NoEngineChanges"))
		{
			throw new BuildException(
				"ue_mcpEditor requires -NoEngineChanges. Set UE_MCP_ALLOW_TEST_ENGINE_CHANGES=true only while bootstrapping a dedicated test engine.");
		}
	}

	private static StringComparison PathComparison =>
		OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;

	private static string Canonicalize(string Value)
	{
		string FullPath = Path.GetFullPath(Value).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
		DirectoryInfo Directory = new DirectoryInfo(FullPath);
		if (Directory.Exists)
		{
			FileSystemInfo? Resolved = Directory.ResolveLinkTarget(true);
			if (Resolved != null)
			{
				FullPath = Resolved.FullName.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
			}
		}
		return FullPath;
	}

	private static bool IsSameOrUnder(string Candidate, string Parent) =>
		String.Equals(Candidate, Parent, PathComparison) ||
		Candidate.StartsWith(Parent + Path.DirectorySeparatorChar, PathComparison);

	private static bool EnvironmentFlag(string Name)
	{
		string Value = Environment.GetEnvironmentVariable(Name) ?? String.Empty;
		return Value.Equals("1", StringComparison.OrdinalIgnoreCase) ||
			Value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
			Value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
			Value.Equals("on", StringComparison.OrdinalIgnoreCase);
	}

	private static bool HasCommandLineSwitch(string Switch) =>
		Environment.GetCommandLineArgs().Any(Argument =>
			Argument.Split(new[] { ' ', '\t', '"' }, StringSplitOptions.RemoveEmptyEntries)
				.Any(Token => Token.Equals(Switch, StringComparison.OrdinalIgnoreCase)));
}
