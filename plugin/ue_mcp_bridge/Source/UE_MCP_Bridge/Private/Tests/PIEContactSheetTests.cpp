// Automation coverage for ComposeContactSheet (roadmap item 1b).
// Writes a few solid-colour PNG frames, composes a contact sheet, and asserts a
// decodable JPEG lands on disk. Cannot run on this workstation (no ue-mcp host);
// exists so the behaviour is verified on the user's build.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEContactSheet.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEContactSheetComposeTest,
	"UE.MCP.PIE.ContactSheet.ComposesDecodableJpeg",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEContactSheetComposeTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;

	const FString Dir = FPaths::ProjectSavedDir() / TEXT("MCPContactSheetTest") /
		FGuid::NewGuid().ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*Dir, true);

	const int32 W = 64, H = 48;
	const FColor Colors[3] = { FColor(200, 40, 40), FColor(40, 200, 40), FColor(40, 40, 200) };
	TArray<FString> Frames;
	for (int32 i = 0; i < 3; ++i)
	{
		TArray<FColor> Pixels;
		Pixels.Init(Colors[i], W * H);
		TArray64<uint8> PNG;
		FImageUtils::PNGCompressImageArray(W, H, Pixels, PNG);
		const FString Path = Dir / FString::Printf(TEXT("frame_%05d.png"), i);
		TestTrue(TEXT("frame written"), FFileHelper::SaveArrayToFile(PNG, *Path));
		Frames.Add(Path);
	}

	const TArray<FString> Labels = { TEXT("F0"), TEXT("F1"), TEXT("F2") };
	const FString OutPath = Dir / TEXT("contact.jpg");

	FContactSheetParams CP;
	CP.CellWidth = 120;
	const bool bOk = ComposeContactSheet(Frames, OutPath, CP, &Labels);
	TestTrue(TEXT("ComposeContactSheet succeeded"), bOk);

	// The output must be a decodable JPEG with positive dimensions.
	TArray<uint8> Raw;
	TestTrue(TEXT("contact sheet file present"), FFileHelper::LoadFileToArray(Raw, *OutPath));
	if (Raw.Num() > 0)
	{
		IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const EImageFormat Fmt = IWM.DetectImageFormat(Raw.GetData(), Raw.Num());
		TestEqual(TEXT("output is JPEG"), (int32)Fmt, (int32)EImageFormat::JPEG);
		TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(Fmt);
		TestTrue(TEXT("decodes"), Wrapper.IsValid() && Wrapper->SetCompressed(Raw.GetData(), Raw.Num()));
		if (Wrapper.IsValid())
		{
			TestTrue(TEXT("has width"), Wrapper->GetWidth() > 0);
			TestTrue(TEXT("has height"), Wrapper->GetHeight() > 0);
		}
	}

	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
