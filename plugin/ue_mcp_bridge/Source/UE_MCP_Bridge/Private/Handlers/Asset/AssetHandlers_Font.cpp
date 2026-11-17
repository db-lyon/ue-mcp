// Font authoring (#1377): import_font turns a .ttf/.otf into a UFontFace, and
// create_font builds a UFont whose default typeface points at font faces.
// FTypefaceEntry and FFontData are not reachable from Python or a property write.

#include "AssetHandlers.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Factories/FontFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
	bool MCPParseFontHinting(const FString& Text, EFontHinting& Out)
	{
		static const TPair<const TCHAR*, EFontHinting> Names[] = {
			{ TEXT("Default"), EFontHinting::Default }, { TEXT("Auto"), EFontHinting::Auto },
			{ TEXT("AutoLight"), EFontHinting::AutoLight }, { TEXT("Monochrome"), EFontHinting::Monochrome },
			{ TEXT("None"), EFontHinting::None },
		};
		for (const auto& Name : Names)
		{
			if (Text.Equals(Name.Key, ESearchCase::IgnoreCase)) { Out = Name.Value; return true; }
		}
		return false;
	}

	bool MCPParseFontLoadingPolicy(const FString& Text, EFontLoadingPolicy& Out)
	{
		static const TPair<const TCHAR*, EFontLoadingPolicy> Names[] = {
			{ TEXT("LazyLoad"), EFontLoadingPolicy::LazyLoad }, { TEXT("Stream"), EFontLoadingPolicy::Stream },
			{ TEXT("Inline"), EFontLoadingPolicy::Inline },
		};
		for (const auto& Name : Names)
		{
			if (Text.Equals(Name.Key, ESearchCase::IgnoreCase)) { Out = Name.Value; return true; }
		}
		return false;
	}

	FCompositeFont& MCPMutableCompositeFont(UFont* Font)
	{
#if UE_MCP_HAS_5_7_API
		return Font->GetMutableInternalCompositeFont();
#else
		return Font->CompositeFont;
#endif
	}

	/** Replace Font's default typeface with one entry per face and save it. */
	TArray<TSharedPtr<FJsonValue>> MCPWriteDefaultTypeface(UFont* Font, const TArray<UFontFace*>& Faces, const TArray<FString>& Names)
	{
		Font->Modify();
		Font->FontCacheType = EFontCacheType::Runtime;
		FTypeface& Typeface = MCPMutableCompositeFont(Font).DefaultTypeface;
		Typeface.Fonts.Reset();
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < Faces.Num(); ++i)
		{
			FTypefaceEntry& Entry = Typeface.Fonts.AddDefaulted_GetRef();
			Entry.Name = FName(*Names[i]);
			Entry.Font = FFontData(Faces[i]);
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Names[i]);
			Row->SetStringField(TEXT("fontFace"), Faces[i]->GetPathName());
			Out.Add(MakeShared<FJsonValueObject>(Row));
		}
		Font->PostEditChange();
		Font->MarkPackageDirty();
		return Out;
	}

	/** A new UFont through the editor's own factory, cached at runtime. */
	UFont* MCPCreateFontAsset(const FString& Name, const FString& PackagePath)
	{
		UFontFactory* Factory = NewObject<UFontFactory>();
		Factory->bEditAfterNew = false;
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		return Cast<UFont>(AssetToolsModule.Get().CreateAsset(Name, PackagePath, UFont::StaticClass(), Factory));
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportFont(const TSharedPtr<FJsonObject>& Params)
{
	FString FilePath;
	if (auto Err = RequireString(Params, TEXT("filePath"), FilePath)) return Err;
	// Every parameter is read before anything can fail (#1057).
	FString Name = OptionalString(Params, TEXT("name"));
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Fonts"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const FString HintingText = OptionalString(Params, TEXT("hinting"));
	const FString LoadingText = OptionalString(Params, TEXT("loadingPolicy"));
	const bool bCreateFont = OptionalBool(Params, TEXT("createFont"), false);

	if (!FPaths::FileExists(FilePath))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FilePath));
	}
	const FString Extension = FPaths::GetExtension(FilePath).ToLower();
	if (Extension != TEXT("ttf") && Extension != TEXT("otf") && Extension != TEXT("ttc") && Extension != TEXT("otc"))
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a font file: expected .ttf, .otf, .ttc or .otc"), *FilePath));
	}
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error") && OnConflict != TEXT("replace"))
	{
		return MCPError(TEXT("onConflict must be skip, error or replace"));
	}
	EFontHinting Hinting = EFontHinting::Default;
	if (!HintingText.IsEmpty() && !MCPParseFontHinting(HintingText, Hinting))
	{
		return MCPError(TEXT("hinting must be Default, Auto, AutoLight, Monochrome or None"));
	}
	EFontLoadingPolicy LoadingPolicy = EFontLoadingPolicy::LazyLoad;
	if (!LoadingText.IsEmpty() && !MCPParseFontLoadingPolicy(LoadingText, LoadingPolicy))
	{
		return MCPError(TEXT("loadingPolicy must be LazyLoad, Stream or Inline"));
	}
	if (Name.IsEmpty())
	{
		Name = ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(FilePath));
	}
	if (MCPIsProtectedAssetPath(PackagePath))
	{
		return MCPError(FString::Printf(TEXT("'%s' is a protected mount, which cannot be overridden; import into /Game or a plugin content folder"), *PackagePath));
	}

	const FString PackageName = PackagePath / Name;
	UFontFace* Existing = LoadObject<UFontFace>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Existing && OnConflict != TEXT("replace"))
	{
		if (auto Early = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("Font face"))) return Early;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath) || Bytes.Num() == 0)
	{
		return MCPError(FString::Printf(TEXT("Could not read %s"), *FilePath));
	}

	UFontFace* Face = Existing;
	const bool bFaceCreated = Face == nullptr;
	if (!Face)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Face = NewObject<UFontFace>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	}
	if (!Face) return MCPError(FString::Printf(TEXT("Could not create a FontFace at %s"), *PackageName));

	Face->Modify();
	Face->SourceFilename = FilePath;
	Face->Hinting = Hinting;
	Face->LoadingPolicy = LoadingPolicy;
	Face->FontFaceData->SetData(MoveTemp(Bytes));
	Face->CacheSubFaces();
	Face->PostEditChange();
	Face->MarkPackageDirty();
	if (bFaceCreated) FAssetRegistryModule::AssetCreated(Face);
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Face, SaveError);

	auto Result = MCPSuccess();
	if (bFaceCreated) MCPSetCreated(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), Face->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("sourceFile"), FilePath);
	Result->SetNumberField(TEXT("bytes"), Face->FontFaceData->GetData().Num());
	TArray<TSharedPtr<FJsonValue>> SubFaces;
	for (const FString& SubFace : Face->SubFaces) SubFaces.Add(MakeShared<FJsonValueString>(SubFace));
	Result->SetArrayField(TEXT("subFaces"), SubFaces);
	MCPNoteSaveOutcome(Result, Face->GetPathName(), bSaved, SaveError);

	if (bCreateFont)
	{
		const FString FontName = Name + TEXT("_Font");
		UFont* Font = LoadObject<UFont>(nullptr, *(PackagePath / FontName + TEXT(".") + FontName), nullptr, LOAD_NoWarn | LOAD_Quiet);
		const bool bFontCreated = Font == nullptr;
		if (!Font) Font = MCPCreateFontAsset(FontName, PackagePath);
		if (!Font)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("The font face was imported, but creating the Font asset %s failed"), *FontName));
		}
		else
		{
			Result->SetArrayField(TEXT("typefaces"), MCPWriteDefaultTypeface(Font, { Face }, { TEXT("Default") }));
			FString FontSaveError;
			const bool bFontSaved = SaveAssetPackageChecked(Font, FontSaveError);
			Result->SetStringField(TEXT("fontPath"), Font->GetPathName());
			Result->SetBoolField(TEXT("fontCreated"), bFontCreated);
			Result->SetBoolField(TEXT("fontSaved"), bFontSaved);
			if (!bFontSaved) Result->SetStringField(TEXT("fontSaveError"), FontSaveError);
		}
	}

	if (bFaceCreated)
	{
		MCPSetDeleteAssetRollback(Result, Face->GetPathName());
		if (bCreateFont)
		{
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback deletes the font face only; delete fontPath with asset(delete) as well."));
		}
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("An existing font face was replaced with the file's bytes; the previous bytes were not kept."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::CreateCompositeFont(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	// Every parameter is read before anything can fail (#1057).
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Fonts"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	TArray<FString> FacePaths;
	TArray<FString> TypefaceNames;
	const TArray<TSharedPtr<FJsonValue>>* FacesJson = nullptr;
	if (TryGetArrayParam(Params, TEXT("fontFaces"), FacesJson) && FacesJson)
	{
		for (const TSharedPtr<FJsonValue>& V : *FacesJson)
		{
			FString S;
			FacePaths.Add(V.IsValid() && V->TryGetString(S) ? S : FString());
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* NamesJson = nullptr;
	if (TryGetArrayParam(Params, TEXT("typefaceNames"), NamesJson) && NamesJson)
	{
		for (const TSharedPtr<FJsonValue>& V : *NamesJson)
		{
			FString S;
			TypefaceNames.Add(V.IsValid() && V->TryGetString(S) ? S : FString());
		}
	}

	if (FacePaths.Num() == 0) return MCPError(TEXT("fontFaces is empty: pass the FontFace asset paths the font uses"));
	if (TypefaceNames.Num() > 0 && TypefaceNames.Num() != FacePaths.Num())
	{
		return MCPError(FString::Printf(TEXT("typefaceNames has %d entries for %d fontFaces; give one name per face or omit it"), TypefaceNames.Num(), FacePaths.Num()));
	}
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error") && OnConflict != TEXT("update"))
	{
		return MCPError(TEXT("onConflict must be skip, error or update"));
	}

	TArray<UFontFace*> Faces;
	for (int32 i = 0; i < FacePaths.Num(); ++i)
	{
		UFontFace* Face = LoadAssetByPath<UFontFace>(FacePaths[i]);
		if (!Face) return MCPError(FString::Printf(TEXT("fontFaces[%d]: no FontFace at '%s' (import one with asset(import_font))"), i, *FacePaths[i]));
		Faces.Add(Face);
	}
	// A face named Regular, Bold or Italic is the usual typeface name; the first face defaults to Default.
	if (TypefaceNames.Num() == 0)
	{
		for (int32 i = 0; i < Faces.Num(); ++i) TypefaceNames.Add(i == 0 ? FString(TEXT("Default")) : Faces[i]->GetName());
	}
	TSet<FString> Seen;
	for (const FString& TypefaceName : TypefaceNames)
	{
		if (TypefaceName.IsEmpty()) return MCPError(TEXT("typefaceNames entries must be non-empty"));
		if (Seen.Contains(TypefaceName)) return MCPError(FString::Printf(TEXT("typeface name '%s' is used twice"), *TypefaceName));
		Seen.Add(TypefaceName);
	}

	UFont* Font = LoadObject<UFont>(nullptr, *(PackagePath / Name + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Font && OnConflict != TEXT("update"))
	{
		if (auto Early = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("Font"))) return Early;
	}
	const bool bCreated = Font == nullptr;
	TArray<TSharedPtr<FJsonValue>> PreviousTypefaces;
	if (Font)
	{
		for (const FTypefaceEntry& Entry : MCPMutableCompositeFont(Font).DefaultTypeface.Fonts)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Entry.Name.ToString());
			const UObject* FaceObject = Entry.Font.GetFontFaceAsset();
			Row->SetStringField(TEXT("fontFace"), FaceObject ? FaceObject->GetPathName() : FString());
			PreviousTypefaces.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	if (!Font) Font = MCPCreateFontAsset(Name, PackagePath);
	if (!Font) return MCPError(FString::Printf(TEXT("Could not create a Font asset %s in %s"), *Name, *PackagePath));

	const TArray<TSharedPtr<FJsonValue>> Typefaces = MCPWriteDefaultTypeface(Font, Faces, TypefaceNames);
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Font, SaveError);

	auto Result = MCPSuccess();
	if (bCreated) MCPSetCreated(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), Font->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("fontCacheType"), TEXT("Runtime"));
	Result->SetArrayField(TEXT("typefaces"), Typefaces);
	MCPNoteSaveOutcome(Result, Font->GetPathName(), bSaved, SaveError);
	if (bCreated)
	{
		MCPSetDeleteAssetRollback(Result, Font->GetPathName());
	}
	else
	{
		Result->SetArrayField(TEXT("previousTypefaces"), PreviousTypefaces);
		MCPSetNoRollback(Result, TEXT("The default typeface of an existing font was rewritten; previousTypefaces lists what it held, and create_font with onConflict=update and those faces restores it."));
	}
	return MCPResult(Result);
}
