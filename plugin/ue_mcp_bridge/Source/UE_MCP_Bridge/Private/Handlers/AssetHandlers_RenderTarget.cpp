#include "AssetHandlers.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Factories/TextureRenderTargetFactoryNew.h"
#include "HandlerAssetCreate.h"
#include "HandlerUtils.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
	constexpr int32 DefaultRenderTargetSize = 512;
	// UTextureRenderTarget2D::PostEditChangeProperty clamps to 8192, so anything
	// larger would be silently shrunk the first time the asset is edited in the
	// editor. Reject it up front instead.
	constexpr int32 MaxRenderTargetSize = 8192;

	struct FRenderTargetFormatEntry
	{
		const TCHAR* Name;
		ETextureRenderTargetFormat Format;
	};

	// The subset of ETextureRenderTargetFormat the editor exposes on a
	// TextureRenderTarget2D, in enum order. Single source of truth for both
	// parsing the `format` parameter and reporting it back.
	const FRenderTargetFormatEntry RenderTargetFormats[] = {
		{TEXT("R8"), RTF_R8},
		{TEXT("RG8"), RTF_RG8},
		{TEXT("RGBA8"), RTF_RGBA8},
		{TEXT("RGBA8_SRGB"), RTF_RGBA8_SRGB},
		{TEXT("R16F"), RTF_R16f},
		{TEXT("RG16F"), RTF_RG16f},
		{TEXT("RGBA16F"), RTF_RGBA16f},
		{TEXT("R32F"), RTF_R32f},
		{TEXT("RG32F"), RTF_RG32f},
		{TEXT("RGBA32F"), RTF_RGBA32f},
		{TEXT("RGB10A2"), RTF_RGB10A2},
	};

	bool TryParseRenderTargetFormat(const FString& Value, ETextureRenderTargetFormat& OutFormat)
	{
		for (const FRenderTargetFormatEntry& Entry : RenderTargetFormats)
		{
			if (Value.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				OutFormat = Entry.Format;
				return true;
			}
		}
		return false;
	}

	FString RenderTargetFormatToString(ETextureRenderTargetFormat Format)
	{
		for (const FRenderTargetFormatEntry& Entry : RenderTargetFormats)
		{
			if (Entry.Format == Format) return Entry.Name;
		}
		return TEXT("UNKNOWN");
	}

	FString RenderTargetFormatList()
	{
		TArray<FString> Names;
		for (const FRenderTargetFormatEntry& Entry : RenderTargetFormats) Names.Add(Entry.Name);
		return FString::Join(Names, TEXT(", "));
	}

	/** Full settings readback so callers never need a second round trip to
	 *  learn what the asset actually ended up with. */
	TSharedPtr<FJsonValue> MakeRenderTargetResult(UTextureRenderTarget2D* RenderTarget, bool bCreated)
	{
		auto Result = MCPSuccess();
		if (bCreated)
		{
			MCPSetCreated(Result);
			MCPSetDeleteAssetRollback(Result, RenderTarget->GetPathName());
		}
		else
		{
			MCPSetExisted(Result);
		}
		Result->SetStringField(TEXT("assetPath"), RenderTarget->GetPathName());
		Result->SetStringField(TEXT("path"), RenderTarget->GetPathName());
		Result->SetStringField(TEXT("name"), RenderTarget->GetName());
		Result->SetStringField(TEXT("packagePath"), FPackageName::GetLongPackagePath(RenderTarget->GetOutermost()->GetName()));
		Result->SetNumberField(TEXT("width"), RenderTarget->SizeX);
		Result->SetNumberField(TEXT("height"), RenderTarget->SizeY);
		Result->SetStringField(TEXT("format"), RenderTargetFormatToString(RenderTarget->RenderTargetFormat));

		TSharedPtr<FJsonObject> ColorObject = MakeShared<FJsonObject>();
		ColorObject->SetNumberField(TEXT("r"), RenderTarget->ClearColor.R);
		ColorObject->SetNumberField(TEXT("g"), RenderTarget->ClearColor.G);
		ColorObject->SetNumberField(TEXT("b"), RenderTarget->ClearColor.B);
		ColorObject->SetNumberField(TEXT("a"), RenderTarget->ClearColor.A);
		Result->SetObjectField(TEXT("clearColor"), ColorObject);

		Result->SetBoolField(TEXT("generateMips"), RenderTarget->bAutoGenerateMips != 0);
		Result->SetNumberField(TEXT("targetGamma"), RenderTarget->TargetGamma);
		return MCPResult(Result);
	}

	bool TryReadColor(const TSharedPtr<FJsonObject>& Params, FLinearColor& OutColor, FString& OutError)
	{
		if (!Params->HasField(TEXT("clearColor")))
		{
			return true;
		}

		const TSharedPtr<FJsonObject>* ColorObject = nullptr;
		if (!Params->TryGetObjectField(TEXT("clearColor"), ColorObject) || !ColorObject || !ColorObject->IsValid())
		{
			OutError = TEXT("clearColor must be an object with numeric r, g, b, and a channels");
			return false;
		}

		double R = 0.0;
		double G = 0.0;
		double B = 0.0;
		double A = 0.0;
		(*ColorObject)->TryGetNumberField(TEXT("r"), R);
		(*ColorObject)->TryGetNumberField(TEXT("g"), G);
		(*ColorObject)->TryGetNumberField(TEXT("b"), B);
		(*ColorObject)->TryGetNumberField(TEXT("a"), A);
		if (!FMath::IsFinite(R) || !FMath::IsFinite(G) || !FMath::IsFinite(B) || !FMath::IsFinite(A))
		{
			OutError = TEXT("clearColor channels must be finite numbers");
			return false;
		}
		OutColor = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
		return true;
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::CreateRenderTarget2D(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Error = RequireString(Params, TEXT("name"), Name)) return Error;
	Name.TrimStartAndEndInline();
	if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT(".")))
	{
		return MCPError(TEXT("name must be a non-empty Unreal asset name without '/' or '.'"));
	}

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
	PackagePath.TrimStartAndEndInline();
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath.LeftChopInline(1);
	if (!FPackageName::IsValidLongPackageName(PackagePath, true))
	{
		return MCPError(FString::Printf(TEXT("Invalid packagePath: %s"), *PackagePath));
	}
	const FString LowerPackagePath = PackagePath.ToLower();
	if (LowerPackagePath == TEXT("/engine") || LowerPackagePath.StartsWith(TEXT("/engine/")) ||
		LowerPackagePath == TEXT("/script") || LowerPackagePath.StartsWith(TEXT("/script/")) ||
		LowerPackagePath == TEXT("/memory") || LowerPackagePath.StartsWith(TEXT("/memory/")) ||
		LowerPackagePath == TEXT("/temp") || LowerPackagePath.StartsWith(TEXT("/temp/")))
	{
		return MCPError(FString::Printf(TEXT("Refusing to create an asset in protected mount: %s"), *PackagePath));
	}

	const int32 Width = OptionalInt(Params, TEXT("width"), DefaultRenderTargetSize);
	const int32 Height = OptionalInt(Params, TEXT("height"), DefaultRenderTargetSize);
	if (Width < 1 || Width > MaxRenderTargetSize || Height < 1 || Height > MaxRenderTargetSize)
	{
		return MCPError(FString::Printf(TEXT("width and height must be between 1 and %d"), MaxRenderTargetSize));
	}

	FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	OnConflict.ToLowerInline();
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
	{
		return MCPError(TEXT("onConflict must be 'skip' or 'error'"));
	}

	ETextureRenderTargetFormat RenderTargetFormat = RTF_RGBA8_SRGB;
	const FString Format = OptionalString(Params, TEXT("format"), TEXT("RGBA8_SRGB"));
	if (!TryParseRenderTargetFormat(Format, RenderTargetFormat))
	{
		return MCPError(FString::Printf(TEXT("format must be one of %s"), *RenderTargetFormatList()));
	}

	FLinearColor ClearColor = FLinearColor::Transparent;
	FString ColorError;
	if (!TryReadColor(Params, ClearColor, ColorError)) return MCPError(ColorError);
	const bool bGenerateMips = OptionalBool(Params, TEXT("generateMips"), false);
	const double TargetGamma = OptionalNumber(Params, TEXT("targetGamma"), 0.0);
	if (!FMath::IsFinite(TargetGamma) || TargetGamma < 0.0)
	{
		return MCPError(TEXT("targetGamma must be a finite number greater than or equal to 0"));
	}

	const FString PackageName = PackagePath + TEXT("/") + Name;
	if (!FPackageName::IsValidLongPackageName(PackageName, true))
	{
		return MCPError(FString::Printf(TEXT("Invalid render target package name: %s"), *PackageName));
	}

	// Probe first so the idempotent hit can report the settings of the asset
	// that is already on disk instead of the lean "existed" record.
	const FString ObjectPath = PackageName + TEXT(".") + Name;
	if (UObject* ExistingObject = LoadObject<UObject>(nullptr, *ObjectPath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("TextureRenderTarget2D '%s' already exists"), *ObjectPath));
		}
		UTextureRenderTarget2D* ExistingRenderTarget = Cast<UTextureRenderTarget2D>(ExistingObject);
		if (!ExistingRenderTarget)
		{
			return MCPError(FString::Printf(TEXT("Asset '%s' exists but is not a TextureRenderTarget2D"), *ObjectPath));
		}
		return MakeRenderTargetResult(ExistingRenderTarget, false);
	}

	// Go through the editor factory rather than a bare NewObject so the asset
	// is registered, dirtied and finalised exactly the way the content browser
	// would do it.
	UTextureRenderTargetFactoryNew* Factory = NewObject<UTextureRenderTargetFactoryNew>();
	Factory->Width = Width;
	Factory->Height = Height;

	auto Created = MCPCreateAssetIdempotent<UTextureRenderTarget2D>(
		Name, PackagePath, OnConflict, TEXT("TextureRenderTarget2D"), Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UTextureRenderTarget2D* RenderTarget = Created.Asset;
	RenderTarget->RenderTargetFormat = RenderTargetFormat;
	RenderTarget->ClearColor = ClearColor;
	RenderTarget->bAutoGenerateMips = bGenerateMips;
	RenderTarget->TargetGamma = static_cast<float>(TargetGamma);
	// InitAutoFormat derives the pixel format from RenderTargetFormat and
	// recreates the resource, so every setting above has to be in place first.
	RenderTarget->InitAutoFormat(Width, Height);

	if (!SaveAssetPackage(RenderTarget))
	{
		return MCPError(FString::Printf(TEXT("Created TextureRenderTarget2D '%s' but failed to save its package"), *ObjectPath));
	}

	return MakeRenderTargetResult(RenderTarget, true);
}
