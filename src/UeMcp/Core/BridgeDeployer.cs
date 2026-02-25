using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Extensions.Logging;
using Microsoft.Win32;

namespace UeMcp.Core;

public class BridgeDeployer
{
    private readonly ILogger<BridgeDeployer> _logger;
    private const string BridgeResourcePrefix = "bridge.";
    private const string BridgeDirName = "ue_mcp_bridge";
    private const string PythonPluginName = "PythonScriptPlugin";
    private const string StartupIniSection = "[/Script/PythonScriptPlugin.PythonScriptPluginSettings]";
    private const string StartupScriptLine = "+StartupScripts=ue_mcp_bridge/startup_script.py";

    public BridgeDeployer(ILogger<BridgeDeployer> logger)
    {
        _logger = logger;
    }

    public BridgeDeployResult Deploy(ProjectContext context)
    {
        var result = new BridgeDeployResult();

        try
        {
            result.PluginEnabled = EnsurePythonPlugin(context.ProjectPath!);
            result.BridgeDeployed = DeployBridgeFiles(context.ContentDir!);
            result.StartupConfigured = EnsureStartupConfig(context.ProjectPath!);
            result.WebsocketsInstalled = EnsureWebsockets(context.EngineAssociation, context.ProjectPath!);
        }
        catch (Exception ex)
        {
            _logger.LogWarning("Bridge deployment failed: {Error}", ex.Message);
            result.Error = ex.Message;
        }

        return result;
    }

    private bool EnsurePythonPlugin(string uprojectPath)
    {
        var json = File.ReadAllText(uprojectPath);
        var root = JsonNode.Parse(json)!.AsObject();

        var plugins = root["Plugins"]?.AsArray();
        if (plugins == null)
        {
            plugins = new JsonArray();
            root["Plugins"] = plugins;
        }

        bool alreadyEnabled = plugins.Any(p =>
            p?["Name"]?.GetValue<string>()?.Equals(PythonPluginName, StringComparison.OrdinalIgnoreCase) == true);

        if (alreadyEnabled)
        {
            _logger.LogDebug("PythonScriptPlugin already enabled");
            return false;
        }

        plugins.Insert(0, new JsonObject
        {
            ["Name"] = PythonPluginName,
            ["Enabled"] = true
        });

        File.WriteAllText(uprojectPath, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        _logger.LogInformation("Enabled PythonScriptPlugin in .uproject");
        return true;
    }

    private bool DeployBridgeFiles(string contentDir)
    {
        var targetDir = Path.Combine(contentDir, "Python", BridgeDirName);
        var assembly = Assembly.GetExecutingAssembly();
        var resourceNames = assembly.GetManifestResourceNames()
            .Where(n => n.StartsWith(BridgeResourcePrefix, StringComparison.OrdinalIgnoreCase))
            .ToList();

        if (resourceNames.Count == 0)
        {
            _logger.LogWarning("No embedded bridge resources found");
            return false;
        }

        bool anyDeployed = false;
        foreach (var resourceName in resourceNames)
        {
            var relativePath = resourceName[BridgeResourcePrefix.Length..]
                .Replace("\\", Path.DirectorySeparatorChar.ToString())
                .Replace("/", Path.DirectorySeparatorChar.ToString());
            var targetPath = Path.Combine(targetDir, relativePath);

            Directory.CreateDirectory(Path.GetDirectoryName(targetPath)!);

            using var stream = assembly.GetManifestResourceStream(resourceName);
            if (stream == null) continue;

            var newBytes = new byte[stream.Length];
            stream.ReadExactly(newBytes);

            bool shouldWrite = true;
            if (File.Exists(targetPath))
            {
                var existingBytes = File.ReadAllBytes(targetPath);
                shouldWrite = !existingBytes.AsSpan().SequenceEqual(newBytes);
            }

            if (shouldWrite)
            {
                File.WriteAllBytes(targetPath, newBytes);
                anyDeployed = true;
                _logger.LogDebug("Deployed: {Path}", relativePath);
            }
        }

        if (anyDeployed)
            _logger.LogInformation("Bridge plugin deployed to {Dir}", targetDir);
        else
            _logger.LogDebug("Bridge plugin already up to date");

        return anyDeployed;
    }

    private bool EnsureWebsockets(string? engineAssociation, string uprojectPath)
    {
        var pythonExe = FindUePython(engineAssociation, uprojectPath);
        if (pythonExe == null)
        {
            _logger.LogDebug("Could not locate UE Python — skipping websockets install");
            return false;
        }

        var checkResult = RunProcess(pythonExe, "-c \"import websockets\"");
        if (checkResult.exitCode == 0)
        {
            _logger.LogDebug("websockets already installed in UE Python");
            return false;
        }

        _logger.LogInformation("Installing websockets in UE Python at {Path}", pythonExe);
        var installResult = RunProcess(pythonExe, "-m pip install websockets --quiet --disable-pip-version-check");
        if (installResult.exitCode == 0)
        {
            _logger.LogInformation("websockets installed successfully");
            return true;
        }

        _logger.LogWarning("Failed to install websockets: {Output}", installResult.output);
        return false;
    }

    private string? FindUePython(string? engineAssociation, string uprojectPath)
    {
        var engineRoot = FindEngineInstall(engineAssociation);
        if (engineRoot == null) return null;

        var pythonExe = Path.Combine(engineRoot, @"Engine\Binaries\ThirdParty\Python3\Win64\python.exe");
        if (File.Exists(pythonExe))
        {
            _logger.LogDebug("Found UE Python at {Path}", pythonExe);
            return pythonExe;
        }

        _logger.LogDebug("UE Python not found under {Root}", engineRoot);
        return null;
    }

    private string? FindEngineInstall(string? engineAssociation)
    {
        if (string.IsNullOrEmpty(engineAssociation))
            return null;

        if (Guid.TryParse(engineAssociation, out _))
            return FindEngineByGuid(engineAssociation);

        return FindLauncherEngine(engineAssociation);
    }

    private string? FindEngineByGuid(string guid)
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Epic Games\Unreal Engine\Builds");
            var path = key?.GetValue(guid) as string;
            if (path != null && Directory.Exists(path))
                return path;
        }
        catch { }

        return null;
    }

    private string? FindLauncherEngine(string association)
    {
        // Epic Games Launcher manifest
        var launcherDat = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
            @"Epic\UnrealEngineLauncher\LauncherInstalled.dat");

        if (File.Exists(launcherDat))
        {
            try
            {
                var json = File.ReadAllText(launcherDat);
                var doc = JsonDocument.Parse(json);
                if (doc.RootElement.TryGetProperty("InstallationList", out var list))
                {
                    foreach (var entry in list.EnumerateArray())
                    {
                        var appName = entry.GetProperty("AppName").GetString() ?? "";
                        if (appName.Equals($"UE_{association}", StringComparison.OrdinalIgnoreCase))
                        {
                            var loc = entry.GetProperty("InstallLocation").GetString();
                            if (loc != null && Directory.Exists(loc))
                            {
                                _logger.LogDebug("Found engine via launcher manifest at {Path}", loc);
                                return loc;
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                _logger.LogDebug("Failed to read launcher manifest: {Error}", ex.Message);
            }
        }

        // Common install paths
        string[] candidates =
        [
            $@"C:\Program Files\Epic Games\UE_{association}",
            $@"D:\Program Files\Epic Games\UE_{association}",
            $@"C:\Epic Games\UE_{association}",
            $@"D:\Epic Games\UE_{association}",
        ];

        foreach (var path in candidates)
        {
            if (Directory.Exists(path))
            {
                _logger.LogDebug("Found engine at {Path}", path);
                return path;
            }
        }

        return null;
    }

    private bool EnsureStartupConfig(string uprojectPath)
    {
        var projectDir = Path.GetDirectoryName(uprojectPath)!;
        var iniPath = Path.Combine(projectDir, "Config", "DefaultEngine.ini");

        if (!File.Exists(iniPath))
        {
            _logger.LogWarning("DefaultEngine.ini not found at {Path}", iniPath);
            return false;
        }

        var content = File.ReadAllText(iniPath);

        bool hasOldEntry = content.Contains("/Game/Python/ue_mcp_bridge/startup_script.py", StringComparison.OrdinalIgnoreCase);
        if (hasOldEntry)
        {
            content = content.Replace(
                "+StartupScripts=/Game/Python/ue_mcp_bridge/startup_script.py",
                "+StartupScripts=ue_mcp_bridge/startup_script.py");
            File.WriteAllText(iniPath, content);
            _logger.LogInformation("Fixed startup script path in DefaultEngine.ini");
            return true;
        }

        if (content.Contains("ue_mcp_bridge/startup_script.py", StringComparison.OrdinalIgnoreCase))
        {
            _logger.LogDebug("Startup script already configured");
            return false;
        }

        var sectionIndex = content.IndexOf(StartupIniSection, StringComparison.OrdinalIgnoreCase);
        if (sectionIndex >= 0)
        {
            var insertAt = sectionIndex + StartupIniSection.Length;
            content = content.Insert(insertAt, Environment.NewLine + StartupScriptLine);
        }
        else
        {
            content += Environment.NewLine + Environment.NewLine
                + StartupIniSection + Environment.NewLine
                + StartupScriptLine + Environment.NewLine;
        }

        File.WriteAllText(iniPath, content);
        _logger.LogInformation("Added bridge startup script to DefaultEngine.ini");
        return true;
    }

    private static (int exitCode, string output) RunProcess(string exe, string args)
    {
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = exe,
                Arguments = args,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            using var proc = Process.Start(psi);
            if (proc == null) return (-1, "Failed to start process");

            var stdout = proc.StandardOutput.ReadToEnd();
            var stderr = proc.StandardError.ReadToEnd();
            proc.WaitForExit(30_000);

            return (proc.ExitCode, string.IsNullOrEmpty(stderr) ? stdout : stderr);
        }
        catch (Exception ex)
        {
            return (-1, ex.Message);
        }
    }
}

public class BridgeDeployResult
{
    public bool PluginEnabled { get; set; }
    public bool BridgeDeployed { get; set; }
    public bool StartupConfigured { get; set; }
    public bool WebsocketsInstalled { get; set; }
    public string? Error { get; set; }

    public bool AnyChanges => PluginEnabled || BridgeDeployed || StartupConfigured || WebsocketsInstalled;
    public string Summary
    {
        get
        {
            if (Error != null) return $"Bridge deployment failed: {Error}";
            if (!AnyChanges) return "Bridge already configured";

            var changes = new List<string>();
            if (PluginEnabled) changes.Add("enabled PythonScriptPlugin");
            if (BridgeDeployed) changes.Add("deployed bridge plugin");
            if (StartupConfigured) changes.Add("configured auto-start");
            if (WebsocketsInstalled) changes.Add("installed websockets");
            return "Bridge setup: " + string.Join(", ", changes);
        }
    }
}
