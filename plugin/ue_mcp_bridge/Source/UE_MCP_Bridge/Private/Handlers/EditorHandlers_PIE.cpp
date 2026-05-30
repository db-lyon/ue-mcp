// Split from EditorHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FEditorHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in EditorHandlers.cpp::RegisterHandlers.

#include "EditorHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace
{
	static ULevelEditorPlaySettings* GetPlaySettingsForRW()
	{
		return GetMutableDefault<ULevelEditorPlaySettings>();
	}

	static bool SetIntPropOn(UObject* Obj, const TCHAR* Name, int32 NewVal)
	{
		FIntProperty* P = CastField<FIntProperty>(Obj->GetClass()->FindPropertyByName(FName(Name)));
		if (!P) return false;
		P->SetPropertyValue_InContainer(Obj, NewVal);
		return true;
	}
	static bool SetBoolPropOn(UObject* Obj, const TCHAR* Name, bool NewVal)
	{
		FBoolProperty* P = CastField<FBoolProperty>(Obj->GetClass()->FindPropertyByName(FName(Name)));
		if (!P) return false;
		P->SetPropertyValue_InContainer(Obj, NewVal);
		return true;
	}
	static bool SetEnumPropOn(UObject* Obj, const TCHAR* Name, int64 NewVal)
	{
		FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(Name));
		if (!Prop) return false;
		if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Obj), NewVal);
			return true;
		}
		if (FByteProperty* BP = CastField<FByteProperty>(Prop))
		{
			BP->SetPropertyValue_InContainer(Obj, static_cast<uint8>(NewVal));
			return true;
		}
		return false;
	}
	static int32 GetIntPropOn(const UObject* Obj, const TCHAR* Name, int32 Default = 0)
	{
		if (FIntProperty* P = CastField<FIntProperty>(Obj->GetClass()->FindPropertyByName(FName(Name))))
		{
			return P->GetPropertyValue_InContainer(Obj);
		}
		return Default;
	}
	static bool GetBoolPropOn(const UObject* Obj, const TCHAR* Name, bool Default = false)
	{
		if (FBoolProperty* P = CastField<FBoolProperty>(Obj->GetClass()->FindPropertyByName(FName(Name))))
		{
			return P->GetPropertyValue_InContainer(Obj);
		}
		return Default;
	}
	static int64 GetEnumPropOn(const UObject* Obj, const TCHAR* Name)
	{
		FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(Name));
		if (!Prop) return 0;
		if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			return EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Obj));
		}
		if (FByteProperty* BP = CastField<FByteProperty>(Prop))
		{
			return BP->GetPropertyValue_InContainer(Obj);
		}
		return 0;
	}

	static const TCHAR* NetModeNameFromValue(int64 V)
	{
		switch (V)
		{
		case static_cast<int64>(EPlayNetMode::PIE_ListenServer): return TEXT("ListenServer");
		case static_cast<int64>(EPlayNetMode::PIE_Client):       return TEXT("Client");
		default: return TEXT("Standalone");
		}
	}
}


TSharedPtr<FJsonValue> FEditorHandlers::PieControl(const TSharedPtr<FJsonObject>& Params)
{
	FString Action;
	if (auto Err = RequireString(Params, TEXT("action"), Action)) return Err;

	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	auto Result = MCPSuccess();

	if (Action == TEXT("status"))
	{
		bool bIsPlaying = (GEditor->PlayWorld != nullptr);
		Result->SetBoolField(TEXT("isPlaying"), bIsPlaying);
		Result->SetStringField(TEXT("action"), Action);
	}
	else if (Action == TEXT("start"))
	{
		if (GEditor->PlayWorld != nullptr)
		{
			return MCPError(TEXT("PIE session already active"));
		}

		// AssetRegistry must be done with its initial scan before PIE can start.
		// Cold editor starts spend 30-90s scanning; during that window
		// RequestPlaySession silently no-ops and the caller sees isPlaying=false
		// with no diagnostic (#406). Either wait it out (default) or return a
		// structured error if waitForAssetRegistry=false.
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Reg = ARM.Get();
		if (Reg.IsLoadingAssets())
		{
			bool bWait = true;
			Params->TryGetBoolField(TEXT("waitForAssetRegistry"), bWait);
			double TimeoutSec = 180.0;
			if (double Override; Params->TryGetNumberField(TEXT("assetRegistryTimeoutSeconds"), Override))
			{
				TimeoutSec = Override;
			}
			if (!bWait)
			{
				auto Err = MCPError(TEXT("AssetRegistry initial scan still in progress; PIE cannot start yet. Retry shortly or pass waitForAssetRegistry=true (default) to block until ready."));
				return Err;
			}
			const double Deadline = FPlatformTime::Seconds() + TimeoutSec;
			while (Reg.IsLoadingAssets() && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::Sleep(0.25);
			}
			if (Reg.IsLoadingAssets())
			{
				return MCPError(FString::Printf(TEXT("AssetRegistry still loading after %.1fs; PIE start aborted. Pass assetRegistryTimeoutSeconds to extend the wait, or retry later."), TimeoutSec));
			}
			Result->SetBoolField(TEXT("waitedForAssetRegistry"), true);
		}

		FRequestPlaySessionParams SessionParams;
		GEditor->RequestPlaySession(SessionParams);
		Result->SetStringField(TEXT("action"), Action);
	}
	else if (Action == TEXT("stop"))
	{
		if (GEditor->PlayWorld == nullptr)
		{
			return MCPError(TEXT("No PIE session active"));
		}

		GEditor->RequestEndPlayMap();
		Result->SetStringField(TEXT("action"), Action);
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown action: %s. Expected 'status', 'start', or 'stop'"), *Action));
	}

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FEditorHandlers::PieGetRuntimeValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	// Check if PIE is active
	if (GEditor->PlayWorld == nullptr)
	{
		return MCPError(TEXT("PIE is not active. Start a PIE session first."));
	}

	FString ActorPath;
	if (!Params->TryGetStringField(TEXT("actorPath"), ActorPath))
	{
		// Also accept actorLabel as a fallback
		if (!Params->TryGetStringField(TEXT("actorLabel"), ActorPath))
		{
			return MCPError(TEXT("Missing 'actorPath' parameter"));
		}
	}

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// Search for the actor in the PIE world (accept label, name, or full path)
	UWorld* PIEWorld = GEditor->PlayWorld;
	AActor* TargetActor = FindActorByLabelNameOrPath(PIEWorld, ActorPath);

	if (!TargetActor)
	{
		// Collect available actor names for the error message
		TArray<TSharedPtr<FJsonValue>> AvailableActors;
		int32 Count = 0;
		for (TActorIterator<AActor> It(PIEWorld); It && Count < 20; ++It, ++Count)
		{
			AvailableActors.Add(MakeShared<FJsonValueString>((*It)->GetActorLabel()));
		}
		TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
		ErrResult->SetBoolField(TEXT("success"), false);
		ErrResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found in PIE world"), *ActorPath));
		ErrResult->SetArrayField(TEXT("availableActors"), AvailableActors);
		return MCPResult(ErrResult);
	}

	// #344/#381: dotted-path resolution. propertyName "Inventory.Slots" or
	// "StatsComponent.CurrentHP" descends through component subobjects /
	// struct fields. Previously only flat property names worked, forcing
	// execute_python for every nested read on a UActorComponent subobject.
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = TargetActor->GetClass();
	const void* CurrentContainer = TargetActor;
	FProperty* Property = nullptr;

	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FProperty* Seg = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));

		// Bare component name at the head of the path resolves against the
		// actor's component list when no property of that name matches - so
		// "StatsComponent.CurrentHP" reaches the component subobject without
		// the caller having to know the C++ UPROPERTY name.
		if (!Seg && i == 0)
		{
			UActorComponent* MatchedComp = nullptr;
			for (UActorComponent* Comp : TargetActor->GetComponents())
			{
				if (Comp && Comp->GetName() == PathParts[i]) { MatchedComp = Comp; break; }
			}
			if (MatchedComp)
			{
				CurrentContainer = MatchedComp;
				CurrentStruct = MatchedComp->GetClass();
				continue;
			}
		}

		if (!Seg)
		{
			return MCPError(FString::Printf(
				TEXT("Property '%s' not found at '%s' (segment %d)"),
				*PathParts[i], *PropertyName, i));
		}
		if (i < PathParts.Num() - 1)
		{
			if (FStructProperty* SP = CastField<FStructProperty>(Seg))
			{
				CurrentContainer = SP->ContainerPtrToValuePtr<void>(const_cast<void*>(CurrentContainer));
				CurrentStruct = SP->Struct;
			}
			else if (FObjectProperty* OP = CastField<FObjectProperty>(Seg))
			{
				UObject* SubObject = OP->GetObjectPropertyValue(
					OP->ContainerPtrToValuePtr<void>(const_cast<void*>(CurrentContainer)));
				if (!SubObject)
				{
					return MCPError(FString::Printf(
						TEXT("Cannot descend into '%s' - reference is null"), *PathParts[i]));
				}
				CurrentContainer = SubObject;
				CurrentStruct = SubObject->GetClass();
			}
			else
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not a struct or sub-object - cannot descend"), *PathParts[i]));
			}
		}
		else
		{
			Property = Seg;
		}
	}

	if (!Property)
	{
		return MCPError(FString::Printf(TEXT("Property path '%s' did not resolve to a leaf property"), *PropertyName));
	}

	// Read property value and serialize based on type
	auto Result = MCPSuccess();
	const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(const_cast<void*>(CurrentContainer));

	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString Value = StrProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("String"));
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool Value = BoolProp->GetPropertyValue(PropertyValue);
		Result->SetBoolField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Bool"));
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		int32 Value = IntProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Int"));
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		float Value = FloatProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Float"));
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		double Value = DoubleProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Double"));
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FName Value = NameProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value.ToString());
		Result->SetStringField(TEXT("type"), TEXT("Name"));
	}
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FText Value = TextProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value.ToString());
		Result->SetStringField(TEXT("type"), TEXT("Text"));
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		// Handle common struct types: FVector, FRotator, FLinearColor
		if (StructProp->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector* Vec = reinterpret_cast<const FVector*>(PropertyValue);
			TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>();
			VecObj->SetNumberField(TEXT("x"), Vec->X);
			VecObj->SetNumberField(TEXT("y"), Vec->Y);
			VecObj->SetNumberField(TEXT("z"), Vec->Z);
			Result->SetObjectField(TEXT("value"), VecObj);
			Result->SetStringField(TEXT("type"), TEXT("Vector"));
		}
		else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator* Rot = reinterpret_cast<const FRotator*>(PropertyValue);
			TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
			RotObj->SetNumberField(TEXT("pitch"), Rot->Pitch);
			RotObj->SetNumberField(TEXT("yaw"), Rot->Yaw);
			RotObj->SetNumberField(TEXT("roll"), Rot->Roll);
			Result->SetObjectField(TEXT("value"), RotObj);
			Result->SetStringField(TEXT("type"), TEXT("Rotator"));
		}
		else if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		{
			const FLinearColor* Color = reinterpret_cast<const FLinearColor*>(PropertyValue);
			TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
			ColorObj->SetNumberField(TEXT("r"), Color->R);
			ColorObj->SetNumberField(TEXT("g"), Color->G);
			ColorObj->SetNumberField(TEXT("b"), Color->B);
			ColorObj->SetNumberField(TEXT("a"), Color->A);
			Result->SetObjectField(TEXT("value"), ColorObj);
			Result->SetStringField(TEXT("type"), TEXT("LinearColor"));
		}
		else
		{
			// Generic struct: export to string
			FString ExportedValue;
			Property->ExportTextItem_Direct(ExportedValue, PropertyValue, nullptr, nullptr, PPF_None);
			Result->SetStringField(TEXT("value"), ExportedValue);
			Result->SetStringField(TEXT("type"), StructProp->Struct->GetName());
		}
	}
	else
	{
		// Fallback: export property value as string
		FString ExportedValue;
		Property->ExportTextItem_Direct(ExportedValue, PropertyValue, nullptr, nullptr, PPF_None);
		Result->SetStringField(TEXT("value"), ExportedValue);
		Result->SetStringField(TEXT("type"), Property->GetCPPType());
	}

	Result->SetStringField(TEXT("actorPath"), ActorPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_runtime_values -- bulk read across the active world. classFilter +
// paths array, with each path supporting UObject-pointer hops AND zero-arg
// BlueprintCallable getter dispatch (#414).
//
// Closes the recurring "walk every actor with class X, dump GetRequired() /
// GetSupplied() / IsPowered() off a sub-object" Python escape hatch.
// ---------------------------------------------------------------------------
namespace
{
	// Serialize a property value into a JSON value field. Returns false if the
	// property type is unsupported.
	static bool WritePropertyValue(TSharedPtr<FJsonObject> Out, const TCHAR* Field, FProperty* Prop, const void* ValuePtr)
	{
		if (!Prop || !ValuePtr) { Out->SetField(Field, MakeShared<FJsonValueNull>()); return true; }
		if (FStrProperty* SP = CastField<FStrProperty>(Prop))     { Out->SetStringField(Field, SP->GetPropertyValue(ValuePtr)); return true; }
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))   { Out->SetBoolField(Field, BP->GetPropertyValue(ValuePtr)); return true; }
		if (FIntProperty* IP = CastField<FIntProperty>(Prop))     { Out->SetNumberField(Field, IP->GetPropertyValue(ValuePtr)); return true; }
		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop)) { Out->SetNumberField(Field, FP->GetPropertyValue(ValuePtr)); return true; }
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop)) { Out->SetNumberField(Field, DP->GetPropertyValue(ValuePtr)); return true; }
		if (FNameProperty* NP = CastField<FNameProperty>(Prop))   { Out->SetStringField(Field, NP->GetPropertyValue(ValuePtr).ToString()); return true; }
		if (FTextProperty* TP = CastField<FTextProperty>(Prop))   { Out->SetStringField(Field, TP->GetPropertyValue(ValuePtr).ToString()); return true; }
		if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* Obj = OP->GetObjectPropertyValue(ValuePtr);
			if (!Obj) { Out->SetField(Field, MakeShared<FJsonValueNull>()); return true; }
			Out->SetStringField(Field, Obj->GetPathName());
			return true;
		}
		if (FStructProperty* StP = CastField<FStructProperty>(Prop))
		{
			if (StP->Struct == TBaseStructure<FVector>::Get())
			{
				const FVector* V = reinterpret_cast<const FVector*>(ValuePtr);
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("x"), V->X); O->SetNumberField(TEXT("y"), V->Y); O->SetNumberField(TEXT("z"), V->Z);
				Out->SetObjectField(Field, O); return true;
			}
		}
		FString Exported;
		Prop->ExportTextItem_Direct(Exported, ValuePtr, nullptr, nullptr, PPF_None);
		Out->SetStringField(Field, Exported);
		return true;
	}

	// Walk one dotted path starting at Root. Per segment: property hop, sub-object
	// hop, or - at the leaf - a zero-arg UFUNCTION call. Writes the result onto Out.
	static void ResolvePath(UObject* Root, const FString& Path, TSharedPtr<FJsonObject> Out, const TCHAR* FieldKey, FString& OutErr)
	{
		if (!Root) { OutErr = TEXT("null root"); return; }
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 0) { OutErr = TEXT("empty path"); return; }

		UStruct* CurStruct = Root->GetClass();
		void* CurContainer = Root;
		UObject* CurObject = Root;

		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const FString& Seg = Parts[i];
			const bool bLast = (i == Parts.Num() - 1);
			FProperty* Prop = CurStruct->FindPropertyByName(FName(*Seg));

			if (bLast && Prop)
			{
				void* Val = Prop->ContainerPtrToValuePtr<void>(CurContainer);
				WritePropertyValue(Out, FieldKey, Prop, Val);
				return;
			}

			if (Prop)
			{
				if (FStructProperty* SP = CastField<FStructProperty>(Prop))
				{
					CurContainer = SP->ContainerPtrToValuePtr<void>(CurContainer);
					CurStruct = SP->Struct;
					continue;
				}
				if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
				{
					UObject* Sub = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CurContainer));
					if (!Sub) { OutErr = FString::Printf(TEXT("'%s' is null"), *Seg); return; }
					CurContainer = Sub;
					CurStruct = Sub->GetClass();
					CurObject = Sub;
					continue;
				}
				OutErr = FString::Printf(TEXT("'%s' is not a struct/sub-object - cannot descend"), *Seg);
				return;
			}

			// No property by that name. At the head of the path, try matching an
			// actor component by name (mirrors get_runtime_value behavior). At
			// any later segment OR the leaf, try a zero-arg UFUNCTION call - that
			// covers GetRequired() / IsPowered() etc.
			if (i == 0)
			{
				if (AActor* AsActor = Cast<AActor>(CurObject))
				{
					for (UActorComponent* Comp : AsActor->GetComponents())
					{
						if (Comp && Comp->GetName() == Seg)
						{
							CurContainer = Comp;
							CurStruct = Comp->GetClass();
							CurObject = Comp;
							goto NextSegment;
						}
					}
				}
			}

			// UFUNCTION zero-arg getter at this segment.
			if (UFunction* Fn = CurObject ? CurObject->FindFunction(FName(*Seg)) : nullptr)
			{
				if (Fn->NumParms == 1 && Fn->ReturnValueOffset != MAX_uint16)
				{
					uint8* Frame = (uint8*)FMemory_Alloca(Fn->ParmsSize);
					FMemory::Memzero(Frame, Fn->ParmsSize);
					for (TFieldIterator<FProperty> It(Fn); It; ++It)
					{
						It->InitializeValue_InContainer(Frame);
					}
					CurObject->ProcessEvent(Fn, Frame);
					FProperty* RetProp = Fn->GetReturnProperty();
					if (RetProp)
					{
						if (bLast)
						{
							void* RetVal = RetProp->ContainerPtrToValuePtr<void>(Frame);
							WritePropertyValue(Out, FieldKey, RetProp, RetVal);
						}
						else
						{
							OutErr = FString::Printf(TEXT("UFUNCTION '%s' must be the leaf segment - cannot descend into its return"), *Seg);
						}
					}
					else
					{
						OutErr = FString::Printf(TEXT("UFUNCTION '%s' has no return value"), *Seg);
					}
					for (TFieldIterator<FProperty> It(Fn); It; ++It)
					{
						It->DestroyValue_InContainer(Frame);
					}
					return;
				}
				OutErr = FString::Printf(TEXT("UFUNCTION '%s' must be zero-arg with a return"), *Seg);
				return;
			}

			OutErr = FString::Printf(TEXT("Segment '%s' is neither a property, component, nor a zero-arg UFUNCTION on %s"),
				*Seg, *CurStruct->GetName());
			return;

		NextSegment:;
		}
	}
}


TSharedPtr<FJsonValue> FEditorHandlers::GetRuntimeValues(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor) return MCPError(TEXT("Editor not available"));

	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("paths"), PathsArr) || !PathsArr || PathsArr->Num() == 0)
	{
		return MCPError(TEXT("Missing 'paths' (array of dotted property/function paths)"));
	}

	TArray<FString> Paths;
	for (const TSharedPtr<FJsonValue>& V : *PathsArr)
	{
		FString P;
		if (V->TryGetString(P) && !P.IsEmpty()) Paths.Add(P);
	}
	if (Paths.Num() == 0) return MCPError(TEXT("'paths' must contain at least one non-empty string"));

	const FString WorldHint = OptionalString(Params, TEXT("world"));
	UWorld* World = nullptr;
	if (WorldHint == TEXT("editor") || !GEditor->PlayWorld)
	{
		World = (UWorld*)GEditor->GetEditorWorldContext().World();
	}
	else
	{
		World = (UWorld*)GEditor->PlayWorld;
	}
	if (!World) return MCPError(TEXT("No world available"));

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Matched = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		// classFilter empty => every actor. Match against actor class OR any
		// component class so callers can target component types directly.
		bool bActorMatch = ClassFilter.IsEmpty() || Actor->GetClass()->GetName() == ClassFilter;
		UActorComponent* ComponentMatch = nullptr;
		if (!bActorMatch)
		{
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (Comp && Comp->GetClass()->GetName() == ClassFilter)
				{
					ComponentMatch = Comp;
					bActorMatch = true;
					break;
				}
			}
		}
		if (!bActorMatch) continue;

		++Matched;
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Row->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
		if (ComponentMatch)
		{
			Row->SetStringField(TEXT("componentName"), ComponentMatch->GetName());
			Row->SetStringField(TEXT("componentClass"), ComponentMatch->GetClass()->GetName());
		}

		TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Errors = MakeShared<FJsonObject>();
		UObject* ResolveRoot = ComponentMatch ? (UObject*)ComponentMatch : (UObject*)Actor;
		for (const FString& Path : Paths)
		{
			FString Err;
			ResolvePath(ResolveRoot, Path, Values, *Path, Err);
			if (!Err.IsEmpty()) Errors->SetStringField(Path, Err);
		}
		Row->SetObjectField(TEXT("values"), Values);
		if (Errors->Values.Num() > 0) Row->SetObjectField(TEXT("errors"), Errors);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), World == (UWorld*)GEditor->PlayWorld ? TEXT("pie") : TEXT("editor"));
	Result->SetStringField(TEXT("classFilter"), ClassFilter);
	Result->SetNumberField(TEXT("matched"), Matched);
	Result->SetArrayField(TEXT("rows"), Rows);
	return MCPResult(Result);
}


// #126: Fast-forward PIE game time. Raises WorldSettings dilation caps and calls SetGlobalTimeDilation.
TSharedPtr<FJsonValue> FEditorHandlers::SetPieTimeScale(const TSharedPtr<FJsonObject>& Params)
{
	double Factor = 1.0;
	if (!Params->TryGetNumberField(TEXT("factor"), Factor))
	{
		return MCPError(TEXT("Missing 'factor' (number) parameter"));
	}
	if (Factor <= 0.0)
	{
		return MCPError(TEXT("'factor' must be > 0"));
	}

	UWorld* World = GetPIEWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE/Game world active — start PIE first"));
	}

	AWorldSettings* WS = World->GetWorldSettings();
	if (!WS)
	{
		return MCPError(TEXT("WorldSettings not available on PIE world"));
	}

	// Raise dilation caps so Factor isn't clamped.
	const float CapHigh = FMath::Max(1000.0f, (float)Factor * 2.0f);
	WS->MaxGlobalTimeDilation = FMath::Max(WS->MaxGlobalTimeDilation, CapHigh);
	WS->MinGlobalTimeDilation = FMath::Min(WS->MinGlobalTimeDilation, 0.0001f);

	UGameplayStatics::SetGlobalTimeDilation(World, (float)Factor);

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("factor"), Factor);
	Result->SetNumberField(TEXT("maxCap"), WS->MaxGlobalTimeDilation);
	Result->SetNumberField(TEXT("minCap"), WS->MinGlobalTimeDilation);
	Result->SetStringField(TEXT("world"), World->GetName());
	return MCPResult(Result);
}

// ─── #148 capture_scene_png ────────────────────────────────────────
// Headless PNG screenshot via a reusable hidden SceneCapture2D actor.
// Works when the editor window is not focused (unlike viewport
// screenshots) and guarantees RGBA8 LDR PNG output suitable for image
// readers that reject HDR/EXR.


// #228/#229: PIE pawn lookup. Resolves the controlled pawn for a given
// player index against the live PIE world; PIE pawns spawn with runtime
// labels not addressable through the editor outliner.
TSharedPtr<FJsonValue> FEditorHandlers::GetPiePawn(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor) return MCPError(TEXT("Editor not available"));

	FWorldContext* PieCtx = GEditor->GetPIEWorldContext();
	UWorld* PieWorld = PieCtx ? PieCtx->World() : nullptr;
	if (!PieWorld)
	{
		return MCPError(TEXT("PIE not running - start PIE before resolving the player pawn"));
	}

	const int32 PlayerIndex = OptionalInt(Params, TEXT("playerIndex"), 0);
	APlayerController* PC = UGameplayStatics::GetPlayerController(PieWorld, PlayerIndex);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn) return MCPError(FString::Printf(TEXT("No controlled pawn for player index %d"), PlayerIndex));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Pawn->GetActorLabel());
	Result->SetStringField(TEXT("actorName"), Pawn->GetName());
	Result->SetStringField(TEXT("class"), Pawn->GetClass()->GetPathName());
	Result->SetStringField(TEXT("path"), Pawn->GetPathName());
	const FVector L = Pawn->GetActorLocation();
	TSharedPtr<FJsonObject> LocO = MakeShared<FJsonObject>();
	LocO->SetNumberField(TEXT("x"), L.X);
	LocO->SetNumberField(TEXT("y"), L.Y);
	LocO->SetNumberField(TEXT("z"), L.Z);
	Result->SetObjectField(TEXT("location"), LocO);
	const FRotator R = Pawn->GetActorRotation();
	TSharedPtr<FJsonObject> RotO = MakeShared<FJsonObject>();
	RotO->SetNumberField(TEXT("pitch"), R.Pitch);
	RotO->SetNumberField(TEXT("yaw"), R.Yaw);
	RotO->SetNumberField(TEXT("roll"), R.Roll);
	Result->SetObjectField(TEXT("rotation"), RotO);
	return MCPResult(Result);
}

// #228/#229: invoke a BlueprintCallable / Exec UFUNCTION on a target.
// Target resolution: actorLabel against the chosen world (editor by
// default; world="pie" for PIE). The 'args' object maps parameter names
// to JSON values which are converted via FProperty ImportText. Out
// parameters and return values are read back via the same export path.


// #228/#229: invoke a BlueprintCallable / Exec UFUNCTION on a target.
// Target resolution: actorLabel against the chosen world (editor by
// default; world="pie" for PIE). The 'args' object maps parameter names
// to JSON values which are converted via FProperty ImportText. Out
// parameters and return values are read back via the same export path.
TSharedPtr<FJsonValue> FEditorHandlers::InvokeFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor")).ToLower();
	UWorld* World = nullptr;
	if (WorldScope == TEXT("pie"))
	{
		FWorldContext* PieCtx = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
		World = PieCtx ? PieCtx->World() : nullptr;
		if (!World) return MCPError(TEXT("PIE not running - cannot invoke against PIE world"));
	}
	else
	{
		World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World) return MCPError(TEXT("No editor world available"));
	}

	AActor* Target = FindActorByLabel(World, ActorLabel);
	if (!Target)
	{
		return MCPError(FString::Printf(TEXT("Actor not found in %s world: %s"), WorldScope == TEXT("pie") ? TEXT("PIE") : TEXT("editor"), *ActorLabel));
	}

	// #382: optional `component` redirects the call target to a named subobject
	// (e.g. invoke `Server_Deconstruct` on the actor's BuildModeComponent).
	// Without this, RPCs / UFUNCTIONs that live on a component subobject can't
	// be reached via invoke_function and force execute_python.
	UObject* CallTarget = Target;
	FString ComponentName;
	if (Params->TryGetStringField(TEXT("component"), ComponentName) && !ComponentName.IsEmpty())
	{
		UActorComponent* FoundComp = nullptr;
		for (UActorComponent* Comp : Target->GetComponents())
		{
			if (Comp && Comp->GetName() == ComponentName) { FoundComp = Comp; break; }
		}
		if (!FoundComp)
		{
			TArray<FString> CompNames;
			for (UActorComponent* Comp : Target->GetComponents())
			{
				if (Comp) CompNames.Add(Comp->GetName());
			}
			return MCPError(FString::Printf(
				TEXT("Component '%s' not found on actor '%s'. Available: [%s]"),
				*ComponentName, *ActorLabel, *FString::Join(CompNames, TEXT(", "))));
		}
		CallTarget = FoundComp;
	}

	UFunction* Func = CallTarget->FindFunction(FName(*FunctionName));
	if (!Func) return MCPError(FString::Printf(TEXT("Function '%s' not found on %s"), *FunctionName, *CallTarget->GetClass()->GetName()));

	TArray<uint8> ParamBuf;
	ParamBuf.SetNumZeroed(Func->ParmsSize);
	// Initialise default param values
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(ParamBuf.GetData());
	}

	const TSharedPtr<FJsonObject>* ArgObj = nullptr;
	Params->TryGetObjectField(TEXT("args"), ArgObj);

	// #383: optional actorArgs maps UObject* parameters to live actor labels in
	// the active world. LoadObject only resolves asset paths, so any actor-to-
	// actor RPC (Horse->ServerMount(PlayerCharacter)) previously had to be
	// done via execute_python. With actorArgs, the same call becomes:
	//   invoke_function(actorLabel="Horse_01", functionName="ServerMount",
	//                   actorArgs={Player: "PlayerCharacter_0"})
	const TSharedPtr<FJsonObject>* ActorArgObj = nullptr;
	Params->TryGetObjectField(TEXT("actorArgs"), ActorArgObj);

	if (ArgObj && (*ArgObj).IsValid())
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->PropertyFlags & CPF_ReturnParm) continue;
			if ((P->PropertyFlags & CPF_OutParm) && !(P->PropertyFlags & CPF_ReferenceParm)) continue;
			TSharedPtr<FJsonValue> Val = (*ArgObj)->TryGetField(P->GetName());
			if (!Val.IsValid()) continue;
			void* PtrAddr = P->ContainerPtrToValuePtr<void>(ParamBuf.GetData());
			FString E;
			if (!MCPJsonProperty::SetJsonOnProperty(P, PtrAddr, Val, E))
			{
				for (TFieldIterator<FProperty> CleanupIt(Func); CleanupIt && (CleanupIt->PropertyFlags & CPF_Parm); ++CleanupIt)
				{
					CleanupIt->DestroyValue_InContainer(ParamBuf.GetData());
				}
				return MCPError(FString::Printf(TEXT("Argument '%s': %s"), *P->GetName(), *E));
			}
		}
	}

	// Apply actor-label resolution AFTER plain args so explicit args[paramName]
	// stays authoritative when both are supplied. Walk every UObject* parm and
	// resolve the matching actorArgs entry.
	if (ActorArgObj && (*ActorArgObj).IsValid())
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->PropertyFlags & CPF_ReturnParm) continue;
			if ((P->PropertyFlags & CPF_OutParm) && !(P->PropertyFlags & CPF_ReferenceParm)) continue;

			FObjectProperty* OP = CastField<FObjectProperty>(P);
			if (!OP) continue;
			FString ActorArgLabel;
			if (!(*ActorArgObj)->TryGetStringField(P->GetName(), ActorArgLabel) || ActorArgLabel.IsEmpty())
			{
				continue;
			}

			AActor* RefActor = FindActorByLabel(World, ActorArgLabel);
			if (!RefActor)
			{
				for (TFieldIterator<FProperty> CleanupIt(Func); CleanupIt && (CleanupIt->PropertyFlags & CPF_Parm); ++CleanupIt)
				{
					CleanupIt->DestroyValue_InContainer(ParamBuf.GetData());
				}
				return MCPError(FString::Printf(
					TEXT("actorArgs[%s]: actor '%s' not found in %s world"),
					*P->GetName(), *ActorArgLabel,
					WorldScope == TEXT("pie") ? TEXT("PIE") : TEXT("editor")));
			}
			if (!RefActor->IsA(OP->PropertyClass))
			{
				// Allow component look-up: if the property expects a UActorComponent
				// subclass, walk the actor's components and pick the first match.
				if (OP->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					UActorComponent* MatchedComp = nullptr;
					for (UActorComponent* Comp : RefActor->GetComponents())
					{
						if (Comp && Comp->IsA(OP->PropertyClass)) { MatchedComp = Comp; break; }
					}
					if (MatchedComp)
					{
						OP->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), MatchedComp);
						continue;
					}
				}
				for (TFieldIterator<FProperty> CleanupIt(Func); CleanupIt && (CleanupIt->PropertyFlags & CPF_Parm); ++CleanupIt)
				{
					CleanupIt->DestroyValue_InContainer(ParamBuf.GetData());
				}
				return MCPError(FString::Printf(
					TEXT("actorArgs[%s]: actor '%s' (%s) is not assignable to expected type %s"),
					*P->GetName(), *ActorArgLabel,
					*RefActor->GetClass()->GetName(), *OP->PropertyClass->GetName()));
			}
			OP->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), RefActor);
		}
	}

	CallTarget->ProcessEvent(Func, ParamBuf.GetData());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	if (!ComponentName.IsEmpty()) Result->SetStringField(TEXT("component"), ComponentName);
	Result->SetStringField(TEXT("functionName"), FunctionName);

	TSharedPtr<FJsonObject> OutVals = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		FProperty* P = *It;
		if (P->PropertyFlags & (CPF_ReturnParm | CPF_OutParm))
		{
			FString S;
			P->ExportTextItem_Direct(S, P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), nullptr, CallTarget, PPF_None);
			OutVals->SetStringField(P->GetName(), S);
		}
	}
	Result->SetObjectField(TEXT("returnValues"), OutVals);

	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(ParamBuf.GetData());
	}
	return MCPResult(Result);
}

// Call a static UFUNCTION on a UBlueprintFunctionLibrary, with no actor
// instance. invoke_function can only reach functions that live on an actor or
// component; the static *_BlueprintOnly libraries (Voxel sculpt/query/stamp,
// GeometryScript, Kismet math, etc.) have no instance to target, which
// previously forced execute_python. This resolves the library class, runs the
// call on its CDO, and reuses the same arg/return marshalling as InvokeFunction.
//
// Params: className (short name or /Script/Module.Class path), functionName,
// args? (name -> JSON value), actorArgs? (name -> actor label, for UObject*
// params that are actors), worldContextParam? (name of a UObject* param to fill
// with the editor/PIE world; auto-detected for params named WorldContextObject),
// world? (editor|pie). Returns return/out params under returnValues.
TSharedPtr<FJsonValue> FEditorHandlers::InvokeStaticFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor")).ToLower();
	UWorld* World = nullptr;
	if (WorldScope == TEXT("pie"))
	{
		FWorldContext* PieCtx = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
		World = PieCtx ? PieCtx->World() : nullptr;
		if (!World) return MCPError(TEXT("PIE not running - cannot invoke against PIE world"));
	}
	else
	{
		World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World) return MCPError(TEXT("No editor world available"));
	}

	// Resolve the function-library class: a /Script/Module.Class path, or a bare
	// class name (with or without the leading U).
	UClass* LibClass = nullptr;
	if (ClassName.Contains(TEXT(".")) || ClassName.StartsWith(TEXT("/")))
	{
		LibClass = LoadObject<UClass>(nullptr, *ClassName);
	}
	if (!LibClass)
	{
		FString Bare = ClassName;
		Bare.RemoveFromStart(TEXT("U"));
		LibClass = FindFirstObject<UClass>(*Bare, EFindFirstObjectOptions::NativeFirst);
	}
	if (!LibClass)
	{
		return MCPError(FString::Printf(TEXT("Function library class not found: %s"), *ClassName));
	}

	UObject* CDO = LibClass->GetDefaultObject();
	if (!CDO) return MCPError(FString::Printf(TEXT("No CDO for class %s"), *LibClass->GetName()));

	UFunction* Func = LibClass->FindFunctionByName(FName(*FunctionName));
	if (!Func) return MCPError(FString::Printf(TEXT("Static function '%s' not found on %s"), *FunctionName, *LibClass->GetName()));

	TArray<uint8> ParamBuf;
	ParamBuf.SetNumZeroed(Func->ParmsSize);
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(ParamBuf.GetData());
	}

	auto Cleanup = [&]()
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(ParamBuf.GetData());
		}
	};

	const TSharedPtr<FJsonObject>* ArgObj = nullptr;
	Params->TryGetObjectField(TEXT("args"), ArgObj);
	if (ArgObj && (*ArgObj).IsValid())
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->PropertyFlags & CPF_ReturnParm) continue;
			if ((P->PropertyFlags & CPF_OutParm) && !(P->PropertyFlags & CPF_ReferenceParm)) continue;
			TSharedPtr<FJsonValue> Val = (*ArgObj)->TryGetField(P->GetName());
			if (!Val.IsValid()) continue;
			void* PtrAddr = P->ContainerPtrToValuePtr<void>(ParamBuf.GetData());
			FString E;
			if (!MCPJsonProperty::SetJsonOnProperty(P, PtrAddr, Val, E))
			{
				Cleanup();
				return MCPError(FString::Printf(TEXT("Argument '%s': %s"), *P->GetName(), *E));
			}
		}
	}

	// actorArgs: UObject* params resolved from live actor labels (e.g. the sculpt
	// actor that a Voxel sculpt op operates on).
	const TSharedPtr<FJsonObject>* ActorArgObj = nullptr;
	Params->TryGetObjectField(TEXT("actorArgs"), ActorArgObj);
	if (ActorArgObj && (*ActorArgObj).IsValid())
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* P = *It;
			if (P->PropertyFlags & CPF_ReturnParm) continue;
			FObjectProperty* OP = CastField<FObjectProperty>(P);
			if (!OP) continue;
			FString ActorArgLabel;
			if (!(*ActorArgObj)->TryGetStringField(P->GetName(), ActorArgLabel) || ActorArgLabel.IsEmpty()) continue;
			AActor* RefActor = FindActorByLabel(World, ActorArgLabel);
			if (!RefActor)
			{
				Cleanup();
				return MCPError(FString::Printf(TEXT("actorArgs[%s]: actor '%s' not found in %s world"), *P->GetName(), *ActorArgLabel, WorldScope == TEXT("pie") ? TEXT("PIE") : TEXT("editor")));
			}
			if (!RefActor->IsA(OP->PropertyClass))
			{
				if (OP->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					UActorComponent* MatchedComp = nullptr;
					for (UActorComponent* Comp : RefActor->GetComponents())
					{
						if (Comp && Comp->IsA(OP->PropertyClass)) { MatchedComp = Comp; break; }
					}
					if (MatchedComp)
					{
						OP->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), MatchedComp);
						continue;
					}
				}
				Cleanup();
				return MCPError(FString::Printf(TEXT("actorArgs[%s]: actor '%s' (%s) is not assignable to expected type %s"), *P->GetName(), *ActorArgLabel, *RefActor->GetClass()->GetName(), *OP->PropertyClass->GetName()));
			}
			OP->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), RefActor);
		}
	}

	// World-context injection: static library functions commonly take a UObject*
	// WorldContextObject. Fill any unset object param matching worldContextParam
	// (or named WorldContextObject) with the resolved world.
	const FString WcParam = OptionalString(Params, TEXT("worldContextParam"), TEXT(""));
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		FProperty* P = *It;
		if (P->PropertyFlags & CPF_ReturnParm) continue;
		FObjectProperty* OP = CastField<FObjectProperty>(P);
		if (!OP) continue;
		const FString PName = P->GetName();
		const bool bIsWc = (!WcParam.IsEmpty() && PName == WcParam) || PName.Contains(TEXT("WorldContext"));
		if (!bIsWc) continue;
		void* Addr = P->ContainerPtrToValuePtr<void>(ParamBuf.GetData());
		if (OP->GetObjectPropertyValue(Addr) != nullptr) continue;
		if (World->IsA(OP->PropertyClass) || OP->PropertyClass == UObject::StaticClass())
		{
			OP->SetObjectPropertyValue(Addr, World);
		}
	}

	CDO->ProcessEvent(Func, ParamBuf.GetData());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("className"), LibClass->GetName());
	Result->SetStringField(TEXT("functionName"), FunctionName);

	TSharedPtr<FJsonObject> OutVals = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		FProperty* P = *It;
		if (P->PropertyFlags & (CPF_ReturnParm | CPF_OutParm))
		{
			FString S;
			P->ExportTextItem_Direct(S, P->ContainerPtrToValuePtr<void>(ParamBuf.GetData()), nullptr, CDO, PPF_None);
			OutVals->SetStringField(P->GetName(), S);
		}
	}
	Result->SetObjectField(TEXT("returnValues"), OutVals);

	Cleanup();
	return MCPResult(Result);
}

// #384: read/write ULevelEditorPlaySettings via reflection. Direct member
// access is blocked because the fields are private; FProperty lookup gives
// us the same access path Python's set_editor_property uses.


TSharedPtr<FJsonValue> FEditorHandlers::ConfigurePie(const TSharedPtr<FJsonObject>& Params)
{
	ULevelEditorPlaySettings* Settings = GetPlaySettingsForRW();
	if (!Settings) return MCPError(TEXT("LevelEditorPlaySettings CDO not available"));

	bool bAny = false;
	int32 NumClients = 0;
	if (Params->TryGetNumberField(TEXT("numClients"), NumClients) && NumClients > 0)
	{
		if (!SetIntPropOn(Settings, TEXT("PlayNumberOfClients"), NumClients))
		{
			return MCPError(TEXT("PlayNumberOfClients property not found - engine drift?"));
		}
		bAny = true;
	}

	FString NetMode;
	if (Params->TryGetStringField(TEXT("netMode"), NetMode) && !NetMode.IsEmpty())
	{
		int64 Resolved = static_cast<int64>(EPlayNetMode::PIE_Standalone);
		const FString N = NetMode.ToLower();
		if      (N == TEXT("standalone"))    Resolved = static_cast<int64>(EPlayNetMode::PIE_Standalone);
		else if (N == TEXT("listen") || N == TEXT("listenserver")) Resolved = static_cast<int64>(EPlayNetMode::PIE_ListenServer);
		else if (N == TEXT("client"))        Resolved = static_cast<int64>(EPlayNetMode::PIE_Client);
		else
		{
			return MCPError(FString::Printf(
				TEXT("Unknown netMode '%s'. Use standalone | listen | client."), *NetMode));
		}
		if (!SetEnumPropOn(Settings, TEXT("PlayNetMode"), Resolved))
		{
			return MCPError(TEXT("PlayNetMode property not found - engine drift?"));
		}
		bAny = true;
	}

	bool BVal = false;
	if (Params->TryGetBoolField(TEXT("runUnderOneProcess"), BVal))
	{
		SetBoolPropOn(Settings, TEXT("RunUnderOneProcess"), BVal);
		bAny = true;
	}
	if (Params->TryGetBoolField(TEXT("launchSeparateServer"), BVal))
	{
		SetBoolPropOn(Settings, TEXT("bLaunchSeparateServer"), BVal);
		bAny = true;
	}

	if (!bAny)
	{
		return MCPError(TEXT("Nothing to configure - provide numClients / netMode / runUnderOneProcess / launchSeparateServer"));
	}

	Settings->SaveConfig();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("numClients"), GetIntPropOn(Settings, TEXT("PlayNumberOfClients"), 1));
	Result->SetStringField(TEXT("netMode"), NetModeNameFromValue(GetEnumPropOn(Settings, TEXT("PlayNetMode"))));
	Result->SetBoolField(TEXT("runUnderOneProcess"), GetBoolPropOn(Settings, TEXT("RunUnderOneProcess"), true));
	Result->SetBoolField(TEXT("launchSeparateServer"), GetBoolPropOn(Settings, TEXT("bLaunchSeparateServer"), false));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FEditorHandlers::GetPieConfig(const TSharedPtr<FJsonObject>& Params)
{
	const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
	if (!Settings) return MCPError(TEXT("LevelEditorPlaySettings CDO not available"));

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("numClients"), GetIntPropOn(Settings, TEXT("PlayNumberOfClients"), 1));
	Result->SetStringField(TEXT("netMode"), NetModeNameFromValue(GetEnumPropOn(Settings, TEXT("PlayNetMode"))));
	Result->SetBoolField(TEXT("runUnderOneProcess"), GetBoolPropOn(Settings, TEXT("RunUnderOneProcess"), true));
	Result->SetBoolField(TEXT("launchSeparateServer"), GetBoolPropOn(Settings, TEXT("bLaunchSeparateServer"), false));
	return MCPResult(Result);
}
