#if WITH_DEV_AUTOMATION_TESTS

#include "BridgeSessionToken.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <sys/stat.h>
#endif

/**
 * The bridge's session token, without a socket: the upgrade decision is a pure
 * function of the Authorization header and the expected token, and the file
 * writer is exercised in a throwaway directory, never the user's real one.
 * The client half is covered by tests/unit/bridge-token.test.ts.
 */

namespace
{
	/** A fresh directory under the project's Intermediate folder, removed by the caller. */
	FString MCPSessionTokenTest_ScratchDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectIntermediateDir(), TEXT("UEMCPSessionTokenTest"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	}

	FString MCPSessionTokenTest_Repeat(TCHAR Char, int32 Count)
	{
		FString Out;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out.AppendChar(Char);
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeSessionTokenDecisionTest,
	"UE.MCP.Bridge.SessionToken.Decision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeSessionTokenDecisionTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPBridgeSessionToken;

	const FString Expected = MCPSessionTokenTest_Repeat(TEXT('a'), 63) + TEXT("b");

	TestTrue(TEXT("the right token is accepted"),
		EvaluateAuthorization(true, TEXT("Bearer ") + Expected, Expected) == EMCPBridgeAuthDecision::Accepted);
	TestTrue(TEXT("the scheme is case-insensitive and surrounding space is ignored"),
		EvaluateAuthorization(true, TEXT("  bearer   ") + Expected + TEXT("  "), Expected) == EMCPBridgeAuthDecision::Accepted);

	TestTrue(TEXT("no header is refused as missing"),
		EvaluateAuthorization(false, FString(), Expected) == EMCPBridgeAuthDecision::Missing);
	TestTrue(TEXT("a different token of the same size is refused"),
		EvaluateAuthorization(true, TEXT("Bearer ") + MCPSessionTokenTest_Repeat(TEXT('a'), 64), Expected) == EMCPBridgeAuthDecision::Mismatch);
	TestTrue(TEXT("a shorter token is refused"),
		EvaluateAuthorization(true, TEXT("Bearer ") + Expected.LeftChop(1), Expected) == EMCPBridgeAuthDecision::Mismatch);
	TestTrue(TEXT("a longer token is refused"),
		EvaluateAuthorization(true, TEXT("Bearer ") + Expected + TEXT("0"), Expected) == EMCPBridgeAuthDecision::Mismatch);
	TestTrue(TEXT("a bare token with no scheme is malformed"),
		EvaluateAuthorization(true, Expected, Expected) == EMCPBridgeAuthDecision::Malformed);
	TestTrue(TEXT("another scheme is malformed"),
		EvaluateAuthorization(true, TEXT("Basic ") + Expected, Expected) == EMCPBridgeAuthDecision::Malformed);
	TestTrue(TEXT("an empty bearer is malformed"),
		EvaluateAuthorization(true, TEXT("Bearer   "), Expected) == EMCPBridgeAuthDecision::Malformed);

	// Fail closed: a bridge that could not publish a token accepts nothing,
	// including an empty bearer that would otherwise equal an empty token.
	TestTrue(TEXT("no configured token refuses a present header"),
		EvaluateAuthorization(true, TEXT("Bearer ") + Expected, FString()) == EMCPBridgeAuthDecision::NoTokenConfigured);
	TestTrue(TEXT("no configured token refuses a missing header"),
		EvaluateAuthorization(false, FString(), FString()) == EMCPBridgeAuthDecision::NoTokenConfigured);

	const FString MissingBody = DescribeRefusal(EMCPBridgeAuthDecision::Missing, 51234);
	TestTrue(TEXT("the missing-token refusal tells the user to update ue-mcp"), MissingBody.Contains(TEXT("Update ue-mcp")));
	TestTrue(TEXT("and names the port"), MissingBody.Contains(TEXT("51234")));
	TestTrue(TEXT("an accepted decision has no refusal text"), DescribeRefusal(EMCPBridgeAuthDecision::Accepted, 1).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeSessionTokenCompareTest,
	"UE.MCP.Bridge.SessionToken.ConstantTimeEquals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeSessionTokenCompareTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPBridgeSessionToken;

	TestTrue(TEXT("equal strings are equal"), ConstantTimeEquals(TEXT("0123abcd"), TEXT("0123abcd")));
	TestFalse(TEXT("a difference in the first character is seen"), ConstantTimeEquals(TEXT("x123abcd"), TEXT("0123abcd")));
	TestFalse(TEXT("a difference in the last character is seen"), ConstantTimeEquals(TEXT("0123abcx"), TEXT("0123abcd")));
	TestFalse(TEXT("a prefix is not equal"), ConstantTimeEquals(TEXT("0123"), TEXT("0123abcd")));
	TestFalse(TEXT("a longer string is not equal"), ConstantTimeEquals(TEXT("0123abcd0"), TEXT("0123abcd")));
	TestFalse(TEXT("case matters"), ConstantTimeEquals(TEXT("ABCD"), TEXT("abcd")));
	TestTrue(TEXT("two empty strings compare equal; EvaluateAuthorization refuses an empty token before this"),
		ConstantTimeEquals(FString(), FString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeSessionTokenGenerateTest,
	"UE.MCP.Bridge.SessionToken.Generate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeSessionTokenGenerateTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPBridgeSessionToken;

	FString First;
	FString Second;
	TestTrue(TEXT("the OS random source answers"), GenerateToken(First));
	TestTrue(TEXT("and answers again"), GenerateToken(Second));
	TestEqual(TEXT("a token is 64 hex digits"), First.Len(), TokenBytes * 2);

	bool bAllLowerHex = true;
	for (const TCHAR Char : First)
	{
		bAllLowerHex &= (Char >= TEXT('0') && Char <= TEXT('9')) || (Char >= TEXT('a') && Char <= TEXT('f'));
	}
	TestTrue(TEXT("every digit is lowercase hex"), bAllLowerHex);
	TestNotEqual(TEXT("two tokens differ"), First, Second);

	const uint8 Bytes[] = { 0x00, 0x0f, 0xa5, 0xff };
	TestEqual(TEXT("hex encoding is lowercase, two digits per byte"), BytesToLowerHex(Bytes, 4), FString(TEXT("000fa5ff")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeSessionTokenFileTest,
	"UE.MCP.Bridge.SessionToken.File",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeSessionTokenFileTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPBridgeSessionToken;

	const FString Root = MCPSessionTokenTest_ScratchDir();
	const FString Dir = FPaths::Combine(Root, TEXT("bridge"));
	const int32 Port = 54321;
	const FString Path = TokenFilePath(Dir, Port);
	TestTrue(TEXT("the file is named <port>.token"), Path.EndsWith(TEXT("/54321.token")));

	FString TokenA;
	FString TokenB;
	GenerateToken(TokenA);
	GenerateToken(TokenB);

	FString Error;
	TestTrue(TEXT("the token file is written"), WriteTokenFile(Dir, Port, TokenA, Error));
	TestEqual(TEXT("with no error"), Error, FString());

	FString ReadBack;
	TestTrue(TEXT("it reads back"), ReadTokenFile(Path, ReadBack));
	TestEqual(TEXT("holding exactly the token"), ReadBack, TokenA);

	TArray<FString> Leftovers;
	IFileManager::Get().FindFiles(Leftovers, *FPaths::Combine(Dir, TEXT("*.tmp")), /*Files*/ true, /*Directories*/ false);
	TestEqual(TEXT("no temporary is left behind"), Leftovers.Num(), 0);

#if PLATFORM_WINDOWS
	{
		// A protected DACL with one entry: the current user, nothing inherited.
		DWORD Needed = 0;
		::GetFileSecurityW(*Path, DACL_SECURITY_INFORMATION, nullptr, 0, &Needed);
		TArray<uint8> Buffer;
		Buffer.SetNumZeroed((int32)Needed);
		const bool bGot = Needed > 0 && ::GetFileSecurityW(*Path, DACL_SECURITY_INFORMATION, Buffer.GetData(), Needed, &Needed);
		TestTrue(TEXT("the file access list is readable"), bGot);
		if (bGot)
		{
			SECURITY_DESCRIPTOR_CONTROL Control = 0;
			DWORD Revision = 0;
			::GetSecurityDescriptorControl(Buffer.GetData(), &Control, &Revision);
			TestTrue(TEXT("the DACL is protected from inheritance"), (Control & SE_DACL_PROTECTED) != 0);

			BOOL bPresent = 0;
			BOOL bDefaulted = 0;
			PACL Acl = nullptr;
			::GetSecurityDescriptorDacl(Buffer.GetData(), &bPresent, &Acl, &bDefaulted);
			TestTrue(TEXT("a DACL is present"), bPresent && Acl != nullptr);
			if (Acl)
			{
				TestEqual(TEXT("it grants exactly one account"), (int32)Acl->AceCount, 1);
			}
		}
	}
#else
	{
		const FTCHARToUTF8 PathUtf8(*Path);
		const FTCHARToUTF8 DirUtf8(*Dir);
		struct stat FileInfo;
		struct stat DirInfo;
		TestTrue(TEXT("the file stats"), stat(PathUtf8.Get(), &FileInfo) == 0);
		TestTrue(TEXT("the directory stats"), stat(DirUtf8.Get(), &DirInfo) == 0);
		TestEqual(TEXT("the file is 0600"), (int32)(FileInfo.st_mode & 0777), 0600);
		TestEqual(TEXT("the directory is 0700"), (int32)(DirInfo.st_mode & 0777), 0700);
	}
#endif

	TestTrue(TEXT("a later session on the same port overwrites it"), WriteTokenFile(Dir, Port, TokenB, Error));
	ReadTokenFile(Path, ReadBack);
	TestEqual(TEXT("with its own token"), ReadBack, TokenB);

	TestFalse(TEXT("the earlier session does not delete the later one's file"), DeleteTokenFileIfOwned(Dir, Port, TokenA));
	TestTrue(TEXT("so it is still there"), FPaths::FileExists(Path));
	TestTrue(TEXT("the owner deletes its own file"), DeleteTokenFileIfOwned(Dir, Port, TokenB));
	TestFalse(TEXT("and it is gone"), FPaths::FileExists(Path));

	TestFalse(TEXT("an empty directory is refused"), WriteTokenFile(FString(), Port, TokenA, Error));
	TestFalse(TEXT("an empty token is refused"), WriteTokenFile(Dir, Port, FString(), Error));

	IFileManager::Get().DeleteDirectory(*Root, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeSessionTokenDirectoryTest,
	"UE.MCP.Bridge.SessionToken.Directory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeSessionTokenDirectoryTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPBridgeSessionToken;

	const FString Saved = FPlatformMisc::GetEnvironmentVariable(DirectoryEnvVar());

	FPlatformMisc::SetEnvironmentVar(DirectoryEnvVar(), TEXT("C:/somewhere/tokens/"));
	TestEqual(TEXT("UE_MCP_BRIDGE_TOKEN_DIR wins, trailing separator dropped"), TokenDirectory(), FString(TEXT("C:/somewhere/tokens")));

	FPlatformMisc::SetEnvironmentVar(DirectoryEnvVar(), TEXT(""));
	TestTrue(TEXT("unset, it is <home>/.ue-mcp/bridge"), TokenDirectory().EndsWith(TEXT("/.ue-mcp/bridge")));

	FPlatformMisc::SetEnvironmentVar(DirectoryEnvVar(), *Saved);
	return true;
}

#endif
