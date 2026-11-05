// MCPResolvePathInside: a caller-chosen path is accepted only inside the
// allowed folders. Pure path arithmetic, so nothing touches the disk.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"

#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPathGuardTest,
	"UE.MCP.PathGuard.KeepsPathsInsideAllowedRoots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPathGuardTest::RunTest(const FString& Parameters)
{
	const FString ConfigDir = MCPFullPath(FPaths::ProjectConfigDir(), FPaths::ProjectDir());
	const TArray<FString> Roots = { ConfigDir };
	FString Full, Error;

	TestTrue(TEXT("a file name resolves inside the root"),
		MCPResolvePathInside(TEXT("DefaultGame.ini"), Roots, Full, Error));
	TestTrue(TEXT("and lands in it"), FPaths::IsSamePath(Full, ConfigDir / TEXT("DefaultGame.ini")));

	TestTrue(TEXT("a subfolder resolves inside the root"),
		MCPResolvePathInside(TEXT("Windows/WindowsEngine.ini"), Roots, Full, Error));

	TestTrue(TEXT("a '..' that comes back in is accepted"),
		MCPResolvePathInside(TEXT("Sub/../DefaultGame.ini"), Roots, Full, Error));

	TestFalse(TEXT("'..' out of the root is refused"),
		MCPResolvePathInside(TEXT("../../outside.ini"), Roots, Full, Error));
	TestTrue(TEXT("and the error names the allowed root"), Error.Contains(ConfigDir));

	TestFalse(TEXT("a deep '..' past the drive root is refused"),
		MCPResolvePathInside(TEXT("../../../../../../../../../../../../../../x.ini"), Roots, Full, Error));

	const FString Outside = MCPFullPath(FString(FPlatformProcess::UserTempDir()) / TEXT("ue-mcp-outside.ini"), FPaths::ProjectDir());
	TestFalse(TEXT("an absolute path outside the root is refused"),
		MCPResolvePathInside(Outside, Roots, Full, Error));

	TestTrue(TEXT("an absolute path inside the root is accepted"),
		MCPResolvePathInside(ConfigDir / TEXT("DefaultEngine.ini"), Roots, Full, Error));

	TestFalse(TEXT("a sibling that shares the root's prefix is refused"),
		MCPResolvePathInside(ConfigDir + TEXT("Sibling/x.ini"), Roots, Full, Error));

	TestFalse(TEXT("an empty path is refused"),
		MCPResolvePathInside(FString(), Roots, Full, Error));

#if PLATFORM_WINDOWS
	TestTrue(TEXT("a case variant of the root is the same folder on Windows"),
		MCPResolvePathInside(ConfigDir.ToUpper() / TEXT("DefaultGame.ini"), Roots, Full, Error));
	TestFalse(TEXT("a case variant does not open a way out"),
		MCPResolvePathInside(ConfigDir.ToUpper() / TEXT("../../outside.ini"), Roots, Full, Error));
#endif

	TestTrue(TEXT("the temp folder is a default writable root"),
		MCPResolvePathInside(Outside, MCPDefaultWritableRoots(), Full, Error));
	TestTrue(TEXT("Saved is a default writable root"),
		MCPResolvePathInside(FPaths::ProjectSavedDir() / TEXT("x.json"), MCPDefaultWritableRoots(), Full, Error));

	TestTrue(TEXT("an FPaths directory resolves to the real project folder"),
		FPaths::IsSamePath(MCPFullPath(FPaths::ProjectSavedDir() / TEXT("x.json"), FPaths::ProjectDir()),
			FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("x.json"))));
	TestTrue(TEXT("a project-relative path still resolves against the project"),
		FPaths::IsSamePath(MCPFullPath(TEXT("Saved/x.json"), FPaths::ProjectDir()),
			FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) / TEXT("Saved/x.json")));

	const TSharedPtr<FJsonValue> Refused = MCPRequirePathInside(TEXT("../x.ini"), Roots, Full);
	TestTrue(TEXT("the handler form returns an error value"), Refused.IsValid());
	TestFalse(TEXT("the handler form returns nothing for an allowed path"),
		MCPRequirePathInside(TEXT("x.ini"), Roots, Full).IsValid());
	return true;
}

#endif
