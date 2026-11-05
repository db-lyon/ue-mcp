// Coverage for asset(reimport) on a texture: a new source file replaces the
// pixels and nothing else.
//
// A texture tuned after import (a UI icon with its LOD group, compression,
// sRGB, streaming, size cap, mips, filter and addressing set by hand) was
// reimported from a new file, and whether those settings survived depended on
// the import path. The fixture imports a generated PNG under a private mount,
// sets each of those settings, then reimports from a second PNG of a different
// size. The assertions are about the contract: the pixels change, every
// setting reads back as it was, the response carries both snapshots and both
// sizes, and the package is left dirty and unsaved unless save is passed.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Tests/MCPScopedTestMount.h"

#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	bool MCPReimportTestWritePng(const FString& FilePath, int32 Width, int32 Height, uint8 Shade)
	{
		TArray<FColor> Pixels;
		Pixels.SetNum(Width * Height);
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			Pixels[Index] = FColor(Shade, static_cast<uint8>(Index * 7), 255 - Shade, 255);
		}
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> Png = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
		{
			return false;
		}
		const TArray64<uint8> Encoded = Png->GetCompressed(100);
		return FFileHelper::SaveArrayToFile(Encoded, *FilePath);
	}

	TSharedPtr<FJsonObject> MCPReimportTestRun(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	bool MCPReimportTestSucceeded(const TSharedPtr<FJsonObject>& Response)
	{
		bool bSuccess = false;
		return Response.IsValid() && Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	int32 MCPReimportTestSize(const TSharedPtr<FJsonObject>& Response, const TCHAR* Field, const TCHAR* Axis)
	{
		const TSharedPtr<FJsonObject>* Size = nullptr;
		if (!Response.IsValid() || !Response->TryGetObjectField(Field, Size) || !Size) return -1;
		return static_cast<int32>((*Size)->GetNumberField(Axis));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetReimportTextureSettingsTest,
	"UE.MCP.Asset.Reimport.TextureKeepsItsSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetReimportTextureSettingsTest::RunTest(const FString& Parameters)
{
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPReimportTexture_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPReimportTexture"));
	const FString SourceDir = FPaths::Combine(Mount.ContentPath, TEXT("SourceFiles"));
	const FString FirstSource = FPaths::Combine(SourceDir, TEXT("T_UEMCPReimport.png"));
	const FString SecondSource = FPaths::Combine(SourceDir, TEXT("T_UEMCPReimport_v2.png"));
	if (!TestTrue(TEXT("the first source PNG is written"), MCPReimportTestWritePng(FirstSource, 8, 4, 40))) return false;
	if (!TestTrue(TEXT("the second source PNG is written"), MCPReimportTestWritePng(SecondSource, 16, 8, 200))) return false;

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	FString AssetPath;
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("filePath"), FirstSource);
		Request->SetStringField(TEXT("packagePath"), Mount.RootPath.LeftChop(1));
		const TSharedPtr<FJsonObject> Response = MCPReimportTestRun(Registry, TEXT("import_texture"), Request);
		const TArray<TSharedPtr<FJsonValue>>* Imported = nullptr;
		if (!TestTrue(TEXT("the fixture texture imports"), MCPReimportTestSucceeded(Response)
				&& Response->TryGetArrayField(TEXT("importedAssets"), Imported) && Imported && Imported->Num() == 1))
		{
			return false;
		}
		AssetPath = (*Imported)[0]->AsString();
	}
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);
	if (!TestNotNull(TEXT("the fixture is a Texture2D"), Texture)) return false;
	TestEqual(TEXT("at the first source's width"), static_cast<int32>(Texture->Source.GetSizeX()), 8);

	// Every setting moved off its import default, so a reimport that applied
	// its own would show.
	Texture->PreEditChange(nullptr);
	Texture->CompressionSettings = TC_Grayscale;
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->SRGB = false;
	Texture->NeverStream = true;
	Texture->MaxTextureSize = 64;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->Filter = TF_Nearest;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Mirror;
	Texture->PostEditChange();

	TSharedPtr<FJsonObject> Response;
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), AssetPath);
		Request->SetStringField(TEXT("filePath"), SecondSource);
		Response = MCPReimportTestRun(Registry, TEXT("reimport_asset"), Request);
	}
	if (!TestTrue(TEXT("the reimport from a new file succeeds"), MCPReimportTestSucceeded(Response))) return false;

	TestEqual(TEXT("the pixels are the new file's: width"), static_cast<int32>(Texture->Source.GetSizeX()), 16);
	TestEqual(TEXT("and height"), static_cast<int32>(Texture->Source.GetSizeY()), 8);
	TestEqual(TEXT("compression is kept"), static_cast<int32>(Texture->CompressionSettings.GetValue()), static_cast<int32>(TC_Grayscale));
	TestEqual(TEXT("LOD group is kept"), static_cast<int32>(Texture->LODGroup.GetValue()), static_cast<int32>(TEXTUREGROUP_UI));
	TestFalse(TEXT("sRGB is kept"), static_cast<bool>(Texture->SRGB));
	TestTrue(TEXT("NeverStream is kept"), static_cast<bool>(Texture->NeverStream));
	TestEqual(TEXT("max size is kept"), Texture->MaxTextureSize, 64);
	TestEqual(TEXT("mip generation is kept"), static_cast<int32>(Texture->MipGenSettings.GetValue()), static_cast<int32>(TMGS_NoMipmaps));
	TestEqual(TEXT("filter is kept"), static_cast<int32>(Texture->Filter.GetValue()), static_cast<int32>(TF_Nearest));
	TestEqual(TEXT("address X is kept"), static_cast<int32>(Texture->AddressX.GetValue()), static_cast<int32>(TA_Clamp));
	TestEqual(TEXT("address Y is kept"), static_cast<int32>(Texture->AddressY.GetValue()), static_cast<int32>(TA_Mirror));

	TestEqual(TEXT("the response reports the size before"), MCPReimportTestSize(Response, TEXT("sourceSizeBefore"), TEXT("x")), 8);
	TestEqual(TEXT("and after"), MCPReimportTestSize(Response, TEXT("sourceSizeAfter"), TEXT("x")), 16);
	const TSharedPtr<FJsonObject>* Before = nullptr;
	const TSharedPtr<FJsonObject>* After = nullptr;
	if (TestTrue(TEXT("both setting snapshots are reported"),
			Response->TryGetObjectField(TEXT("settingsBefore"), Before) && Before
			&& Response->TryGetObjectField(TEXT("settingsAfter"), After) && After))
	{
		TestEqual(TEXT("the snapshots agree"), (*After)->Values.Num(), (*Before)->Values.Num());
		for (const auto& Pair : (*Before)->Values)
		{
			const FString Key(Pair.Key);
			TestEqual(FString::Printf(TEXT("%s reads the same after"), *Key),
				(*After)->GetStringField(Key), Pair.Value->AsString());
		}
		TestEqual(TEXT("LODGroup is in the snapshot"), (*After)->GetStringField(TEXT("LODGroup")), FString(TEXT("TEXTUREGROUP_UI")));
		TestEqual(TEXT("a zero setting is exported, not left blank"), (*Before)->GetStringField(TEXT("SRGB")), FString(TEXT("False")));
	}
	TestTrue(TEXT("restoredSettings is reported"), Response->HasField(TEXT("restoredSettings")));
	TestFalse(TEXT("nothing was saved"), Response->GetBoolField(TEXT("saved")));
	TestTrue(TEXT("the package is left dirty"), Response->GetBoolField(TEXT("packageDirty")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
