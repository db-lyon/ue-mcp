#pragma once

// F4 Create (plans/domain-design.md 3.3): make one new asset idempotently.
// The scope reads where the asset goes, answers a rerun with the asset already
// there (or refuses it under onConflict=error), creates it, deletes it again
// when a configure step refuses, and Finish saves it and writes the created
// shape: created/existed, path, name, saved and a delete_asset rollback. A
// handler supplies the class, its configure step and its own result keys.
//
//   FMCPCreate Create(Params, { TEXT("BehaviorTree"), TEXT("/Game/AI") });
//   if (auto Err = Create.Read()) return Err;
//   UBehaviorTree* Tree = nullptr;
//   if (auto Early = Create.Make(Tree, Class)) return Early;   // existed, refused or failed
//   ...configure; on a refusal: return Create.Abandon(Err);
//   TSharedPtr<FJsonObject> Result = Create.Finish();
//   return MCPResult(Result);

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"

namespace MCPCreateDestination
{
	inline const TCHAR* Help()
	{
		return TEXT("Expected an Unreal package path such as '/Game/Blueprints/BP_Example'. ")
			TEXT("A '.uasset' suffix, an object suffix such as '.BP_Example', and backslashes are accepted and normalized. ")
			TEXT("A filesystem path or a path that does not start with '/' is not.");
	}

	/** #798: fold the spellings a caller uses for a create destination (a .uasset
	 *  suffix, an object suffix, backslashes, a {refPath} reference serialized as
	 *  JSON) into a long package path, and refuse what cannot be repaired
	 *  without guessing. */
	inline TSharedPtr<FJsonValue> Normalize(const FString& Raw, const TCHAR* Field, FString& OutPath)
	{
		FString Value = Raw.TrimStartAndEnd();
		if (Value.StartsWith(TEXT("{")))
		{
			TSharedPtr<FJsonObject> Reference;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Value);
			if (FJsonSerializer::Deserialize(Reader, Reference) && Reference.IsValid())
			{
				for (const TCHAR* Key : { TEXT("refPath"), TEXT("assetPath"), TEXT("path") })
				{
					FString Inner;
					if (Reference->TryGetStringField(Key, Inner) && !Inner.TrimStartAndEnd().IsEmpty())
					{
						Value = Inner.TrimStartAndEnd();
						break;
					}
				}
			}
		}

		Value.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Value.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("%s must not be empty. %s"), Field, Help()));
		}
		const bool bDrivePath = Value.Len() >= 3 && FChar::IsAlpha(Value[0]) && Value[1] == TEXT(':') && Value[2] == TEXT('/');
		if (bDrivePath || Value.StartsWith(TEXT("file:")))
		{
			return MCPError(FString::Printf(TEXT("%s '%s' is a filesystem path. %s"), Field, *Raw, Help()));
		}

		while (Value.Contains(TEXT("//")))
		{
			Value.ReplaceInline(TEXT("//"), TEXT("/"));
		}
		while (Value.Len() > 1 && Value.EndsWith(TEXT("/")))
		{
			Value = Value.LeftChop(1);
		}
		if (!Value.StartsWith(TEXT("/")))
		{
			return MCPError(FString::Printf(TEXT("%s '%s' is not a mount-rooted path. %s"), Field, *Raw, Help()));
		}

		// Everything from the first dot of the last segment is an extension or an
		// object suffix; the bridge addresses the package.
		int32 Cut = INDEX_NONE;
		Value.FindLastChar(TEXT('/'), Cut);
		const FString Dir = Value.Left(Cut);
		FString Leaf = Value.Mid(Cut + 1);
		int32 Dot = INDEX_NONE;
		if (Leaf.FindChar(TEXT('.'), Dot))
		{
			Leaf = Leaf.Left(Dot);
		}
		if (Leaf.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("%s '%s' has no asset name. %s"), Field, *Raw, Help()));
		}

		const FString Normalized = Dir + TEXT("/") + Leaf;
		TArray<FString> Segments;
		Normalized.ParseIntoArray(Segments, TEXT("/"), true);
		if (Segments.Num() < 2)
		{
			return MCPError(FString::Printf(TEXT("%s '%s' is missing a mount point. %s"), Field, *Raw, Help()));
		}
		OutPath = Normalized;
		return nullptr;
	}
}

/** What one creator declares. */
struct FMCPCreateSpec
{
	/** The type as errors and the existed probe name it, such as "BehaviorTree". */
	const TCHAR* TypeLabel = TEXT("Asset");
	/** packagePath when the caller sends none. */
	const TCHAR* DefaultFolder = TEXT("/Game");
	/** Whether onConflict is read; without it a rerun always answers existed. */
	bool bOnConflict = true;
	/** Whether a full assetPath may name the destination instead of name and packagePath. */
	bool bAssetPath = false;
};

class FMCPCreate
{
public:
	FMCPCreate(const TSharedPtr<FJsonObject>& InParams, const FMCPCreateSpec& InSpec)
		: Params(InParams)
		, Spec(InSpec)
	{
	}

	/**
	 * Read the destination and onConflict. With bAssetPath, a full assetPath
	 * wins over name and packagePath. Returns the error, or nullptr.
	 */
	TSharedPtr<FJsonValue> Read()
	{
		OnConflict = Spec.bOnConflict ? OptionalString(Params, TEXT("onConflict"), TEXT("skip")) : FString(TEXT("skip"));
		if (Spec.bAssetPath)
		{
			// Everything is read before anything can fail (#1057).
			const FString RawAssetPath = OptionalString(Params, TEXT("assetPath")).TrimStartAndEnd();
			const FString RawName = OptionalString(Params, TEXT("name")).TrimStartAndEnd();
			const FString RawFolder = OptionalString(Params, TEXT("packagePath")).TrimStartAndEnd();
			FString Normalized;
			if (!RawAssetPath.IsEmpty())
			{
				if (auto Err = MCPCreateDestination::Normalize(RawAssetPath, TEXT("assetPath"), Normalized)) return Err;
			}
			else if (!RawName.IsEmpty())
			{
				const FString Base = RawFolder.IsEmpty() ? FString(Spec.DefaultFolder) : RawFolder;
				if (auto Err = MCPCreateDestination::Normalize(Base + TEXT("/") + RawName, TEXT("packagePath + name"), Normalized)) return Err;
			}
			else
			{
				return MCPError(FString::Printf(
					TEXT("Missing required parameter 'assetPath'. %s A 'name' plus 'packagePath' pair is accepted as the same thing."),
					MCPCreateDestination::Help()));
			}
			Normalized.Split(TEXT("/"), &Folder, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			return nullptr;
		}
		if (auto Err = RequireString(Params, TEXT("name"), AssetName)) return Err;
		Folder = OptionalString(Params, TEXT("packagePath"), Spec.DefaultFolder);
		return nullptr;
	}

	/** Set the destination directly, for a creator that names it another way. */
	void Target(const FString& InName, const FString& InFolder)
	{
		AssetName = InName;
		Folder = InFolder;
		if (OnConflict.IsEmpty())
		{
			OnConflict = Spec.bOnConflict ? OptionalString(Params, TEXT("onConflict"), TEXT("skip")) : FString(TEXT("skip"));
		}
	}

	const FString& Name() const { return AssetName; }
	const FString& PackagePath() const { return Folder; }
	FString PackageName() const { return Folder + TEXT("/") + AssetName; }

	/** The asset already at the destination as an existed result, the onConflict refusal, or nullptr. */
	TSharedPtr<FJsonValue> Existing() const
	{
		return MCPCheckAssetExists(Folder, AssetName, OnConflict, Spec.TypeLabel);
	}

	/**
	 * Create the asset through AssetTools with Class and Factory. Returns what
	 * the handler hands back instead (existed, refused, or a failed create), or
	 * nullptr with Out set.
	 */
	template <typename T>
	TSharedPtr<FJsonValue> Make(T*& Out, UClass* Class = T::StaticClass(), UFactory* Factory = nullptr)
	{
		Out = nullptr;
		auto Made = MCPCreateAssetIdempotent<T>(AssetName, Folder, OnConflict, Spec.TypeLabel, Class, Factory);
		if (Made.EarlyReturn) return Made.EarlyReturn;
		Out = Made.Asset;
		Asset = Out;
		return nullptr;
	}

	/** Create the asset with NewObject in a fresh package, for types whose factory does not fit. */
	template <typename T>
	TSharedPtr<FJsonValue> MakeNewObject(T*& Out)
	{
		Out = nullptr;
		auto Made = MCPCreateAssetIdempotentNewObject<T>(AssetName, Folder, OnConflict, Spec.TypeLabel);
		if (Made.EarlyReturn) return Made.EarlyReturn;
		Out = Made.Asset;
		Asset = Out;
		return nullptr;
	}

	/** Create the asset in a fresh package through Construct, for types an engine utility builds. */
	template <typename T>
	TSharedPtr<FJsonValue> Construct(T*& Out, TFunctionRef<T*(UPackage*, FName, EObjectFlags)> Build)
	{
		Out = nullptr;
		if (auto Existed = Existing()) return Existed;
		UPackage* Package = CreatePackage(*PackageName());
		if (!Package) return MCPError(FString::Printf(TEXT("Failed to create package for %s"), *AssetName));
		Out = Build(Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!Out) return MCPError(FString::Printf(TEXT("Failed to create %s"), Spec.TypeLabel));
		FAssetRegistryModule::AssetCreated(Out);
		Out->MarkPackageDirty();
		Asset = Out;
		return nullptr;
	}

	/**
	 * Run Describe on the asset an existed answer names, so a rerun carries the
	 * same type keys as the create. Other answers pass through untouched.
	 */
	template <typename T>
	static TSharedPtr<FJsonValue> DescribeExisting(const TSharedPtr<FJsonValue>& Early, TFunctionRef<void(const TSharedPtr<FJsonObject>&, T*)> Describe)
	{
		const TSharedPtr<FJsonObject> Obj = Early.IsValid() && Early->Type == EJson::Object ? Early->AsObject() : nullptr;
		bool bExisted = false;
		FString Path;
		if (Obj.IsValid() && Obj->TryGetBoolField(TEXT("existed"), bExisted) && bExisted && Obj->TryGetStringField(TEXT("path"), Path))
		{
			if (T* Existing = LoadObject<T>(nullptr, *Path))
			{
				Obj->SetStringField(TEXT("class"), Existing->GetClass()->GetName());
				Describe(Obj, Existing);
			}
		}
		return Early;
	}

	/** Adopt an asset the handler created itself, so Finish and Abandon cover it. */
	void Adopt(UObject* InAsset) { Asset = InAsset; }

	UObject* Created() const { return Asset; }

	/** Delete the half-made asset and hand back Err, so a refused configure leaves nothing behind. */
	TSharedPtr<FJsonValue> Abandon(const TSharedPtr<FJsonValue>& Err)
	{
		if (Asset)
		{
			const FString Path = Asset->GetPathName();
			Asset = nullptr;
			UEditorAssetLibrary::DeleteAsset(Path);
		}
		return Err;
	}

	/**
	 * Save the asset (and AlsoSave, such as a parent it now points at) unless
	 * bSave is false, then write the created shape: saved and saveError,
	 * created/existed, path, name and the delete_asset rollback. The handler
	 * adds its own keys to the returned object.
	 */
	TSharedPtr<FJsonObject> Finish(bool bSave = true, TArrayView<UObject* const> AlsoSave = {})
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		const FString ObjectPath = Asset ? Asset->GetPathName() : PackageName();
		if (bSave && Asset)
		{
			// Every package is attempted; the first that did not write is the one reported.
			FString FailedPath;
			FString FailedReason;
			auto SaveOne = [&](UObject* Object)
			{
				FString Reason;
				if (Object && !SaveAssetPackageChecked(Object, Reason) && FailedPath.IsEmpty())
				{
					FailedPath = Object->GetPathName();
					FailedReason = Reason;
				}
			};
			SaveOne(Asset);
			for (UObject* Other : AlsoSave) SaveOne(Other);
			MCPNoteSaveOutcome(Result, FailedPath.IsEmpty() ? ObjectPath : FailedPath, FailedPath.IsEmpty(), FailedReason);
		}
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("path"), ObjectPath);
		Result->SetStringField(TEXT("name"), AssetName);
		Result->SetStringField(TEXT("packagePath"), Folder);
		if (Asset) Result->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		MCPSetDeleteAssetRollback(Result, ObjectPath);
		return Result;
	}

private:
	TSharedPtr<FJsonObject> Params;
	FMCPCreateSpec Spec;
	FString AssetName;
	FString Folder;
	FString OnConflict;
	UObject* Asset = nullptr;
};

namespace MCPCreatePreset
{
	/**
	 * The parent a subclass creator makes: parentClass when the caller names
	 * one (it must derive from Base), else Base. Null with OutError set when
	 * the named class is missing or unrelated.
	 */
	inline UClass* ResolveParent(const TSharedPtr<FJsonObject>& Params, UClass* Base, const TCHAR* FriendlyType, TSharedPtr<FJsonValue>& OutError)
	{
		const FString Requested = OptionalString(Params, TEXT("parentClass"));
		if (Requested.IsEmpty()) return Base;
		if (UClass* Resolved = MCPResolveClassOfType(Requested, Base)) return Resolved;
		UClass* Other = MCPResolveClass(Requested);
		if (!Other)
		{
			OutError = MCPError(FString::Printf(TEXT("parentClass not found: %s"), *Requested));
			return nullptr;
		}
		OutError = MCPError(FString::Printf(TEXT("parentClass '%s' does not derive from %s, so it cannot be used as a %s."),
			*Other->GetPathName(), Base ? *Base->GetName() : TEXT("(none)"), FriendlyType));
		return nullptr;
	}

	/** Add the Blueprint shape to a created or existed result: parentClass and generatedClass. */
	inline void DescribeBlueprint(const TSharedPtr<FJsonObject>& Result, const UBlueprint* Blueprint)
	{
		if (!Result.IsValid() || !Blueprint) return;
		if (Blueprint->ParentClass) Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass->GetPathName());
		if (Blueprint->GeneratedClass) Result->SetStringField(TEXT("generatedClass"), Blueprint->GeneratedClass->GetPathName());
	}

	/** DescribeBlueprint on an existed answer, so a rerun reads like the create. */
	inline TSharedPtr<FJsonValue> DescribeExisting(const TSharedPtr<FJsonValue>& Early)
	{
		return FMCPCreate::DescribeExisting<UBlueprint>(Early,
			[](const TSharedPtr<FJsonObject>& Obj, UBlueprint* Blueprint) { DescribeBlueprint(Obj, Blueprint); });
	}

	/**
	 * BlueprintSubclass: a Blueprint of Parent through Factory (a plain
	 * UBlueprintFactory when null), compiled, then Configure (a CDO edit, say),
	 * saved, with the Blueprint shape. The handler adds its own keys.
	 */
	inline TSharedPtr<FJsonObject> BlueprintSubclass(
		FMCPCreate& Create,
		UClass* Parent,
		TSharedPtr<FJsonValue>& OutEarly,
		TFunctionRef<void(UBlueprint*)> Configure,
		UBlueprintFactory* Factory = nullptr)
	{
		if (!Factory) Factory = NewObject<UBlueprintFactory>();
		Factory->ParentClass = Parent;
		UBlueprint* Blueprint = nullptr;
		if (auto Early = Create.Make(Blueprint, UBlueprint::StaticClass(), Factory))
		{
			OutEarly = DescribeExisting(Early);
			return nullptr;
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Configure(Blueprint);
		TSharedPtr<FJsonObject> Result = Create.Finish();
		DescribeBlueprint(Result, Blueprint);
		return Result;
	}

	inline TSharedPtr<FJsonValue> BlueprintSubclass(FMCPCreate& Create, UClass* Parent)
	{
		TSharedPtr<FJsonValue> Early;
		const TSharedPtr<FJsonObject> Result = BlueprintSubclass(Create, Parent, Early, [](UBlueprint*) {});
		return Result.IsValid() ? MCPResult(Result) : Early;
	}
}
