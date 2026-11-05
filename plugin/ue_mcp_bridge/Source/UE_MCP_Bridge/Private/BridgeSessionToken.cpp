#include "BridgeSessionToken.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include <sddl.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#else
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Helpers are file-local but carry a distinctive prefix: this module is a unity
// build, and two blobs merging anonymous namespaces must not collide.
namespace
{
	FString MCPSessionToken_TrimDir(const FString& Dir)
	{
		FString Out = Dir;
		Out.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Out.Len() > 1 && Out.EndsWith(TEXT("/")))
		{
			Out.LeftChopInline(1);
		}
		return Out;
	}

#if PLATFORM_WINDOWS
	/** The current process user's SID in string form, or empty. */
	FString MCPSessionToken_CurrentUserSid()
	{
		HANDLE Token = nullptr;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &Token))
		{
			return FString();
		}

		FString Result;
		DWORD Size = 0;
		::GetTokenInformation(Token, TokenUser, nullptr, 0, &Size);
		if (Size > 0)
		{
			TArray<uint8> Buffer;
			Buffer.SetNumZeroed((int32)Size);
			if (::GetTokenInformation(Token, TokenUser, Buffer.GetData(), Size, &Size))
			{
				LPWSTR SidString = nullptr;
				if (::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(Buffer.GetData())->User.Sid, &SidString))
				{
					Result = SidString;
					::LocalFree(SidString);
				}
			}
		}
		::CloseHandle(Token);
		return Result;
	}

	/**
	 * A protected DACL granting the current user alone full control. Protected
	 * means nothing is inherited from the parent, so a project or profile folder
	 * that other accounts can read does not leak into it. Free with LocalFree.
	 */
	PSECURITY_DESCRIPTOR MCPSessionToken_OwnerOnlyDescriptor(bool bDirectory)
	{
		const FString Sid = MCPSessionToken_CurrentUserSid();
		if (Sid.IsEmpty())
		{
			return nullptr;
		}
		const FString Sddl = FString::Printf(bDirectory ? TEXT("D:P(A;OICI;FA;;;%s)") : TEXT("D:P(A;;FA;;;%s)"), *Sid);
		PSECURITY_DESCRIPTOR Descriptor = nullptr;
		if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(*Sddl, SDDL_REVISION_1, &Descriptor, nullptr))
		{
			return nullptr;
		}
		return Descriptor;
	}

	bool MCPSessionToken_EnsurePrivateDir(const FString& Dir, FString& OutError)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Dir), /*Tree*/ true);

		PSECURITY_DESCRIPTOR Descriptor = MCPSessionToken_OwnerOnlyDescriptor(/*bDirectory*/ true);
		if (!Descriptor)
		{
			OutError = FString::Printf(TEXT("could not build an owner-only access list (error %u)"), (uint32)::GetLastError());
			return false;
		}

		SECURITY_ATTRIBUTES Attributes;
		Attributes.nLength = sizeof(Attributes);
		Attributes.lpSecurityDescriptor = Descriptor;
		Attributes.bInheritHandle = 0;

		bool bOk = true;
		if (!::CreateDirectoryW(*Dir, &Attributes))
		{
			const DWORD Error = ::GetLastError();
			if (Error == ERROR_ALREADY_EXISTS)
			{
				// Tighten a directory an older run or another tool created. The
				// file carries its own access list, so a failure here is not fatal.
				::SetFileSecurityW(*Dir, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, Descriptor);
			}
			else
			{
				OutError = FString::Printf(TEXT("could not create %s (error %u)"), *Dir, (uint32)Error);
				bOk = false;
			}
		}
		::LocalFree(Descriptor);
		return bOk;
	}

	bool MCPSessionToken_WritePrivateFile(const FString& FinalPath, const FString& Contents, FString& OutError)
	{
		const FString TempPath = FString::Printf(TEXT("%s.%u.tmp"), *FinalPath, FPlatformProcess::GetCurrentProcessId());
		::DeleteFileW(*TempPath);

		PSECURITY_DESCRIPTOR Descriptor = MCPSessionToken_OwnerOnlyDescriptor(/*bDirectory*/ false);
		if (!Descriptor)
		{
			OutError = FString::Printf(TEXT("could not build an owner-only access list (error %u)"), (uint32)::GetLastError());
			return false;
		}

		SECURITY_ATTRIBUTES Attributes;
		Attributes.nLength = sizeof(Attributes);
		Attributes.lpSecurityDescriptor = Descriptor;
		Attributes.bInheritHandle = 0;

		// CREATE_NEW: the access list is applied only to a file the call
		// creates, never to one it opens.
		HANDLE File = ::CreateFileW(*TempPath, GENERIC_WRITE, 0, &Attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		::LocalFree(Descriptor);
		if (File == INVALID_HANDLE_VALUE)
		{
			OutError = FString::Printf(TEXT("could not create %s (error %u)"), *TempPath, (uint32)::GetLastError());
			return false;
		}

		const FTCHARToUTF8 Utf8(*Contents);
		DWORD Written = 0;
		const bool bWrote = ::WriteFile(File, Utf8.Get(), (DWORD)Utf8.Length(), &Written, nullptr) && Written == (DWORD)Utf8.Length();
		::CloseHandle(File);
		if (!bWrote)
		{
			::DeleteFileW(*TempPath);
			OutError = FString::Printf(TEXT("could not write %s"), *TempPath);
			return false;
		}

		// The rename keeps the temporary's access list on the final file.
		if (!::MoveFileExW(*TempPath, *FinalPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			const DWORD Error = ::GetLastError();
			::DeleteFileW(*TempPath);
			OutError = FString::Printf(TEXT("could not move the token into %s (error %u)"), *FinalPath, (uint32)Error);
			return false;
		}
		return true;
	}
#else
	bool MCPSessionToken_EnsurePrivateDir(const FString& Dir, FString& OutError)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Dir), /*Tree*/ true);

		const FTCHARToUTF8 DirUtf8(*Dir);
		if (mkdir(DirUtf8.Get(), 0700) != 0 && errno != EEXIST)
		{
			OutError = FString::Printf(TEXT("could not create %s (errno %d)"), *Dir, errno);
			return false;
		}

		struct stat Info;
		if (lstat(DirUtf8.Get(), &Info) != 0)
		{
			OutError = FString::Printf(TEXT("could not stat %s (errno %d)"), *Dir, errno);
			return false;
		}
		if (!S_ISDIR(Info.st_mode))
		{
			OutError = FString::Printf(TEXT("%s is not a directory"), *Dir);
			return false;
		}
		if (Info.st_uid != geteuid())
		{
			OutError = FString::Printf(TEXT("%s is owned by uid %u, not this user (%u)"), *Dir, (uint32)Info.st_uid, (uint32)geteuid());
			return false;
		}
		if ((Info.st_mode & 0077) != 0 && chmod(DirUtf8.Get(), 0700) != 0)
		{
			OutError = FString::Printf(TEXT("could not restrict %s to its owner (errno %d)"), *Dir, errno);
			return false;
		}
		return true;
	}

	bool MCPSessionToken_WritePrivateFile(const FString& FinalPath, const FString& Contents, FString& OutError)
	{
		const FString TempPath = FString::Printf(TEXT("%s.%u.tmp"), *FinalPath, FPlatformProcess::GetCurrentProcessId());
		const FTCHARToUTF8 TempUtf8(*TempPath);
		const FTCHARToUTF8 FinalUtf8(*FinalPath);
		unlink(TempUtf8.Get());

		const int Fd = open(TempUtf8.Get(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (Fd < 0)
		{
			OutError = FString::Printf(TEXT("could not create %s (errno %d)"), *TempPath, errno);
			return false;
		}
		// The umask can only remove bits, but say 0600 explicitly anyway.
		fchmod(Fd, 0600);

		const FTCHARToUTF8 Utf8(*Contents);
		const char* Cursor = Utf8.Get();
		int32 Remaining = Utf8.Length();
		bool bWrote = true;
		while (Remaining > 0)
		{
			const ssize_t Wrote = write(Fd, Cursor, (size_t)Remaining);
			if (Wrote < 0 && errno == EINTR)
			{
				continue;
			}
			if (Wrote <= 0)
			{
				bWrote = false;
				break;
			}
			Cursor += Wrote;
			Remaining -= (int32)Wrote;
		}
		close(Fd);
		if (!bWrote)
		{
			unlink(TempUtf8.Get());
			OutError = FString::Printf(TEXT("could not write %s (errno %d)"), *TempPath, errno);
			return false;
		}
		if (rename(TempUtf8.Get(), FinalUtf8.Get()) != 0)
		{
			const int Error = errno;
			unlink(TempUtf8.Get());
			OutError = FString::Printf(TEXT("could not move the token into %s (errno %d)"), *FinalPath, Error);
			return false;
		}
		return true;
	}
#endif
}

bool UEMCPBridgeSessionToken::FillRandomBytes(uint8* Out, int32 Num)
{
	if (!Out || Num <= 0)
	{
		return false;
	}
#if PLATFORM_WINDOWS
	return BCRYPT_SUCCESS(::BCryptGenRandom(nullptr, Out, (ULONG)Num, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
	const int Fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (Fd < 0)
	{
		return false;
	}
	int32 Filled = 0;
	while (Filled < Num)
	{
		const ssize_t Got = read(Fd, Out + Filled, (size_t)(Num - Filled));
		if (Got < 0 && errno == EINTR)
		{
			continue;
		}
		if (Got <= 0)
		{
			break;
		}
		Filled += (int32)Got;
	}
	close(Fd);
	return Filled == Num;
#endif
}

FString UEMCPBridgeSessionToken::BytesToLowerHex(const uint8* Bytes, int32 Num)
{
	static const TCHAR Digits[] = TEXT("0123456789abcdef");
	FString Out;
	Out.Reserve(Num * 2);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		Out.AppendChar(Digits[Bytes[Index] >> 4]);
		Out.AppendChar(Digits[Bytes[Index] & 0x0F]);
	}
	return Out;
}

bool UEMCPBridgeSessionToken::GenerateToken(FString& OutToken)
{
	OutToken.Reset();
	uint8 Bytes[TokenBytes];
	if (!FillRandomBytes(Bytes, TokenBytes))
	{
		return false;
	}
	OutToken = BytesToLowerHex(Bytes, TokenBytes);
	FMemory::Memzero(Bytes, sizeof(Bytes));
	return true;
}

bool UEMCPBridgeSessionToken::ConstantTimeEquals(const FString& A, const FString& B)
{
	const int32 Len = A.Len();
	if (Len != B.Len())
	{
		return false;
	}
	const TCHAR* PA = *A;
	const TCHAR* PB = *B;
	uint32 Diff = 0;
	for (int32 Index = 0; Index < Len; ++Index)
	{
		Diff |= (uint32)PA[Index] ^ (uint32)PB[Index];
	}
	return Diff == 0;
}

EMCPBridgeAuthDecision UEMCPBridgeSessionToken::EvaluateAuthorization(bool bHeaderPresent, const FString& HeaderValue, const FString& ExpectedToken)
{
	if (ExpectedToken.IsEmpty())
	{
		return EMCPBridgeAuthDecision::NoTokenConfigured;
	}
	if (!bHeaderPresent)
	{
		return EMCPBridgeAuthDecision::Missing;
	}

	const FString Value = HeaderValue.TrimStartAndEnd();
	static const FString Scheme = TEXT("Bearer ");
	if (!Value.StartsWith(Scheme, ESearchCase::IgnoreCase))
	{
		return EMCPBridgeAuthDecision::Malformed;
	}
	const FString Presented = Value.Mid(Scheme.Len()).TrimStartAndEnd();
	if (Presented.IsEmpty())
	{
		return EMCPBridgeAuthDecision::Malformed;
	}
	return ConstantTimeEquals(Presented, ExpectedToken) ? EMCPBridgeAuthDecision::Accepted : EMCPBridgeAuthDecision::Mismatch;
}

FString UEMCPBridgeSessionToken::DescribeRefusal(EMCPBridgeAuthDecision Decision, int32 Port)
{
	switch (Decision)
	{
	case EMCPBridgeAuthDecision::Missing:
		return FString::Printf(
			TEXT("This UE-MCP bridge requires a session token and the client sent none. ")
			TEXT("Update ue-mcp (npx ue-mcp@latest, or run 'ue-mcp update' in the project) so it reads the token the editor publishes for port %d."),
			Port);
	case EMCPBridgeAuthDecision::Malformed:
		return TEXT("The Authorization header must be 'Bearer <session token>'.");
	case EMCPBridgeAuthDecision::Mismatch:
		return FString::Printf(
			TEXT("The session token does not match this editor's. Reconnect so the client rereads the token file for port %d, ")
			TEXT("and check that the client runs as the same user as the editor."),
			Port);
	case EMCPBridgeAuthDecision::NoTokenConfigured:
		return TEXT("This editor could not publish a session token (the editor log says why), so its UE-MCP bridge accepts no connections. Restart the editor.");
	default:
		return FString();
	}
}

FString UEMCPBridgeSessionToken::TokenDirectory()
{
	const FString Override = FPlatformMisc::GetEnvironmentVariable(DirectoryEnvVar());
	if (!Override.IsEmpty())
	{
		return MCPSessionToken_TrimDir(Override);
	}

	// os.homedir() reads USERPROFILE on Windows and HOME elsewhere before it
	// asks the OS. Read the same variable first so both sides land on one path.
#if PLATFORM_WINDOWS
	FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
#else
	FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
#endif
	if (Home.IsEmpty())
	{
		Home = FPlatformProcess::UserHomeDir();
	}
	if (Home.IsEmpty())
	{
		return FString();
	}
	return MCPSessionToken_TrimDir(FPaths::Combine(Home, TEXT(".ue-mcp"), TEXT("bridge")));
}

FString UEMCPBridgeSessionToken::TokenFilePath(const FString& Dir, int32 Port)
{
	return FPaths::Combine(MCPSessionToken_TrimDir(Dir), FString::Printf(TEXT("%d.token"), Port));
}

bool UEMCPBridgeSessionToken::WriteTokenFile(const FString& Dir, int32 Port, const FString& Token, FString& OutError)
{
	OutError.Reset();
	if (Dir.IsEmpty())
	{
		OutError = TEXT("no token directory (no home directory and no UE_MCP_BRIDGE_TOKEN_DIR)");
		return false;
	}
	if (Token.IsEmpty())
	{
		OutError = TEXT("empty token");
		return false;
	}
	const FString CleanDir = MCPSessionToken_TrimDir(Dir);
	if (!MCPSessionToken_EnsurePrivateDir(CleanDir, OutError))
	{
		return false;
	}
	return MCPSessionToken_WritePrivateFile(TokenFilePath(CleanDir, Port), Token, OutError);
}

bool UEMCPBridgeSessionToken::ReadTokenFile(const FString& FilePath, FString& OutToken)
{
	OutToken.Reset();
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *FilePath))
	{
		return false;
	}
	OutToken = Raw.TrimStartAndEnd();
	return true;
}

bool UEMCPBridgeSessionToken::DeleteTokenFileIfOwned(const FString& Dir, int32 Port, const FString& Token)
{
	if (Dir.IsEmpty() || Token.IsEmpty())
	{
		return false;
	}
	const FString FilePath = TokenFilePath(Dir, Port);
	FString OnDisk;
	if (!ReadTokenFile(FilePath, OnDisk) || !ConstantTimeEquals(OnDisk, Token))
	{
		// Gone already, or a later editor on the same port wrote its own.
		return false;
	}
	return IFileManager::Get().Delete(*FilePath, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
}
