// asset(create_subobject) (#975).
//
// Some plugin data assets store their payload as named subobjects that live
// inside the asset's own package and are referenced from a struct array.
// Editing an existing entry already worked through set_property,
// bulk_set_properties and append_array_elements with dotted paths, but ADDING
// one needs a brand-new subobject and nothing native could make one:
// create_asset_by_class refuses component classes outright and otherwise puts
// the new object in a package of its own.
//
// Two things bit the reporter and both are handled here.
//
//  1. A freshly created object that nothing references is collected within
//     roughly one bridge call, so they had to create one object and reference
//     it in the very next call, and an intermediate read lost it. The new
//     object is created RF_Standalone, which is the engine's own "keep this
//     even though nothing points at it yet", and the owning package is written
//     in the same call by default. RF_Standalone is also what makes
//     UPackage::SavePackage harvest an otherwise unreferenced object as an
//     export, so the subobject is on disk and reachable by path from the next
//     call onwards whatever the collector decides to do in between.
//
//  2. Plugin classes are frequently absent from the Python `unreal` module, so
//     the class has to be named by object path. className goes through the
//     shared class resolver, which accepts /Script/Module.Class, the
//     Module.Class shorthand and the bare name alike.

#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/** Apply a properties object to one object, dotted paths and all. Returns
	 *  the number applied; every failure lands in OutErrors. */
	int32 MCPApplySubobjectProperties(
		UObject* Target,
		const TSharedPtr<FJsonObject>& Properties,
		TArray<FString>& OutApplied,
		TArray<FString>& OutErrors)
	{
		if (!Target || !Properties.IsValid()) return 0;

		int32 Applied = 0;
		for (const auto& Pair : Properties->Values)
		{
			// The key type is not FString on every engine the plugin builds
			// against, so materialise it before it is used as one.
			const FString PropertyPath(*Pair.Key);
			FString SetError;
			if (MCPJsonProperty::SetDottedPropertyFromJson(Target, PropertyPath, Pair.Value, SetError))
			{
				OutApplied.Add(PropertyPath);
				++Applied;
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("%s: %s"), *PropertyPath, *SetError));
			}
		}
		return Applied;
	}

	TArray<TSharedPtr<FJsonValue>> MCPSubobjectStringsToJson(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(Values.Num());
		for (const FString& Value : Values) Out.Add(MakeShared<FJsonValueString>(Value));
		return Out;
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::CreateSubobject(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	Name.TrimStartAndEndInline();
	if (Name.IsEmpty())
	{
		return MCPError(TEXT("'name' must not be empty: the subobject is addressed by name from the next call onwards."));
	}

	// This writes into an existing package, so the mount guardrail applies
	// before anything is resolved or created.
	if (MCPIsProtectedAssetPath(AssetPath))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is on a protected mount (/Engine/, /Script/, /Memory/, /Temp/), which the bridge never writes to."),
			*AssetPath));
	}

	TSharedPtr<FJsonValue> LoadError;
	UObject* OwnerAsset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!OwnerAsset) return LoadError;

	UPackage* OwnerPackage = OwnerAsset->GetOutermost();
	if (!OwnerPackage)
	{
		return MCPError(FString::Printf(TEXT("Asset '%s' has no package to own a subobject."), *AssetPath));
	}

	UClass* Class = MCPResolveClass(ClassName);
	if (auto Err = MCPCheckClassUsable(ClassName, Class)) return Err;

	// An Actor is created by spawning it into a level, and NewObject on one
	// outside a level is not a supported construction. Say so rather than
	// letting the engine decide how to fail. Components are deliberately NOT
	// refused: they are the case this action exists for.
	if (Class->IsChildOf(AActor::StaticClass()))
	{
		return MCPClassUnusableError(ClassName, Class, TEXT("actor_class"),
			TEXT("an Actor is spawned into a level, not created inside an asset package. Use level(spawn_actor)."));
	}
	if (Class->IsChildOf(UClass::StaticClass()))
	{
		return MCPClassUnusableError(ClassName, Class, TEXT("class_object"),
			TEXT("it is a class object rather than an instantiable type. Pass the class you want an INSTANCE of."));
	}

	// outer=asset gives "/Game/Foo/DA_Thing.DA_Thing:Name", the ordinary
	// subobject-of-the-asset form. outer=package gives
	// "/Game/Foo/DA_Thing.Name", which is what a plugin that keeps its payload
	// as siblings of the asset inside one package expects.
	const FString OuterSpec = OptionalString(Params, TEXT("outer"), TEXT("asset")).ToLower();
	if (OuterSpec != TEXT("asset") && OuterSpec != TEXT("package"))
	{
		return MCPError(FString::Printf(
			TEXT("'outer' must be 'asset' (default) or 'package', got '%s'."), *OuterSpec));
	}
	UObject* Outer = (OuterSpec == TEXT("package"))
		? static_cast<UObject*>(OwnerPackage)
		: OwnerAsset;

	// A class can declare the outer type it must live under. Checking it here
	// turns an engine assertion into an answer.
	if (Class->ClassWithin && !Outer->IsA(Class->ClassWithin))
	{
		return MCPClassUnusableError(ClassName, Class, TEXT("class_within"), FString::Printf(
			TEXT("it must be created inside a %s, and the chosen outer '%s' is a %s. Try the other 'outer' value."),
			*Class->ClassWithin->GetName(), *Outer->GetPathName(), *Outer->GetClass()->GetName()));
	}

	const FName SubobjectName(*Name);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("reuse")).ToLower();
	if (OnConflict != TEXT("reuse") && OnConflict != TEXT("error"))
	{
		return MCPError(FString::Printf(
			TEXT("'onConflict' must be 'reuse' (default) or 'error', got '%s'."), *OnConflict));
	}

	const TSharedPtr<FJsonObject>* PropertiesField = nullptr;
	TSharedPtr<FJsonObject> Properties;
	if (Params->TryGetObjectField(TEXT("properties"), PropertiesField) && PropertiesField && (*PropertiesField).IsValid())
	{
		Properties = *PropertiesField;
	}

	// Every property is applied to a throwaway instance first, so a bad path or
	// a bad value rejects the call before the target package is touched. This
	// is the same preflight bulk_upsert_data_assets uses.
	if (Properties.IsValid() && Properties->Values.Num() > 0)
	{
		UObject* Probe = NewObject<UObject>(GetTransientPackage(), Class, NAME_None, RF_Transient);
		if (!Probe)
		{
			return MCPError(FString::Printf(
				TEXT("Could not instantiate '%s' even in the transient package."), *Class->GetPathName()));
		}
		const FGCRootScope KeepProbeAlive(Probe);

		TArray<FString> ProbeApplied;
		TArray<FString> ProbeErrors;
		MCPApplySubobjectProperties(Probe, Properties, ProbeApplied, ProbeErrors);
		if (ProbeErrors.Num() > 0)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("success"), false);
			Obj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("%d of %d properties would not apply to a %s, so nothing was created: %s"),
				ProbeErrors.Num(), Properties->Values.Num(), *Class->GetName(),
				*FString::Join(ProbeErrors, TEXT("; "))));
			Obj->SetStringField(TEXT("reason"), TEXT("property_preflight_failed"));
			Obj->SetArrayField(TEXT("propertyErrors"), MCPSubobjectStringsToJson(ProbeErrors));
			return MakeShared<FJsonValueObject>(Obj);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("className"), Class->GetName());
	Result->SetStringField(TEXT("classPath"), Class->GetPathName());
	Result->SetStringField(TEXT("outer"), OuterSpec);
	Result->SetStringField(TEXT("outerPath"), Outer->GetPathName());
	Result->SetStringField(TEXT("packageName"), OwnerPackage->GetName());

	UObject* Subobject = StaticFindObjectFast(UObject::StaticClass(), Outer, SubobjectName);
	const bool bExisted = Subobject != nullptr;
	if (bExisted)
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(
				TEXT("A subobject named '%s' already exists at '%s' (a %s). Pass onConflict='reuse' to configure it in place."),
				*Name, *Subobject->GetPathName(), *Subobject->GetClass()->GetName()));
		}
		if (Subobject->GetClass() != Class)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' already exists as a %s, not a %s. Pick another name, or address the existing object directly."),
				*Subobject->GetPathName(), *Subobject->GetClass()->GetName(), *Class->GetName()));
		}
		MCPSetExisted(Result);
	}
	else
	{
		OwnerAsset->Modify();
		// #975: RF_Standalone is what keeps the object alive with nothing
		// referencing it yet, and what makes the save below harvest it as a
		// package export. RF_Public lets the struct array that will reference
		// it hold an ordinary object reference rather than an instanced one.
		Subobject = NewObject<UObject>(
			Outer, Class, SubobjectName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Subobject)
		{
			return MCPError(FString::Printf(
				TEXT("Could not create a '%s' named '%s' inside '%s'."),
				*Class->GetPathName(), *Name, *Outer->GetPathName()));
		}
		MCPSetCreated(Result);
	}

	// Belt and braces for the window between this call and the next one: the
	// object is rooted for the rest of this handler, and standalone plus saved
	// after it.
	const FGCRootScope KeepSubobjectAlive(Subobject);

	TArray<FString> Applied;
	TArray<FString> PropertyErrors;
	if (Properties.IsValid())
	{
		Subobject->Modify();
		MCPApplySubobjectProperties(Subobject, Properties, Applied, PropertyErrors);
		Subobject->PostEditChange();
	}

	Result->SetStringField(TEXT("objectPath"), Subobject->GetPathName());
	Result->SetArrayField(TEXT("propertiesSet"), MCPSubobjectStringsToJson(Applied));
	if (PropertyErrors.Num() > 0)
	{
		Result->SetArrayField(TEXT("propertyErrors"), MCPSubobjectStringsToJson(PropertyErrors));
	}

	OwnerAsset->PostEditChange();
	OwnerPackage->MarkPackageDirty();

	// The new object lives inside a package that existed before this call, and
	// nothing in this surface takes one back out: delete_asset would destroy
	// the owning asset along with everything else in it, which is a larger loss
	// than the thing being undone. Undoing this needs a remove_subobject that
	// does not exist, so the property values overwritten on a reused subobject
	// are not recoverable through the bridge either.
	MCPSetNoRollback(Result, FString::Printf(
		TEXT("Created or reconfigured the subobject '%s' inside the existing package '%s'. No action removes a named ")
		TEXT("subobject from a package or restores its previous property values; delete_asset would destroy the whole ")
		TEXT("owning asset rather than undo this."),
		*Name, *OwnerPackage->GetName()));

	// Saving in the same call is what closes the garbage-collection window the
	// issue is about: after this the subobject is an export on disk, so a later
	// call resolves it by path even if the in-memory copy is collected.
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	bool bPersisted = false;
	FString PersistReason;
	if (bSave)
	{
		bPersisted = SaveAssetPackage(OwnerAsset);
		if (!bPersisted)
		{
			PersistReason = FString::Printf(
				TEXT("The editor refused to write '%s'. The subobject exists in memory and is kept alive, but it is not on disk yet."),
				*OwnerPackage->GetName());
		}
	}
	else
	{
		PersistReason = FString::Printf(
			TEXT("save=false was requested, so '%s' is dirty in memory only. The subobject survives garbage collection while the editor runs, but it is not on disk until the package is saved."),
			*OwnerPackage->GetName());
	}
	Result->SetBoolField(TEXT("persisted"), bPersisted);
	Result->SetBoolField(TEXT("saved"), bPersisted);
	Result->SetBoolField(TEXT("packageDirty"), OwnerPackage->IsDirty());
	if (!bPersisted)
	{
		Result->SetStringField(TEXT("persistError"), PersistReason);
		if (bSave)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), PersistReason);
		}
	}

	if (PropertyErrors.Num() > 0)
	{
		// The preflight passed and the write did not, which means the object is
		// half configured. Loud, not a footnote.
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Created '%s' but %d propert%s would not apply: %s"),
			*Subobject->GetPathName(), PropertyErrors.Num(),
			PropertyErrors.Num() == 1 ? TEXT("y") : TEXT("ies"),
			*FString::Join(PropertyErrors, TEXT("; "))));
	}

	return MCPResult(Result);
}
