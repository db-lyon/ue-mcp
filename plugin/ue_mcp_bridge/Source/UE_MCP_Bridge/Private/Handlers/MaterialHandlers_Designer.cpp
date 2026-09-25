// Material Designer (DynamicMaterial plugin) layer stacks: read the model
// behind a DynamicMaterialInstance, set a value on any of its components, and
// add or remove layers (#1131).
//
// A translation-unit partition of FMaterialHandlers. Registration stays in
// MaterialHandlers.cpp::RegisterHandlers.
//
// Everything goes through reflection: the plugin is optional and its C++ API
// moved between 5.4 and 5.8, while its UPROPERTYs and BlueprintCallable
// UFUNCTIONs (UDMMaterialSlot::AddDefaultLayer / RemoveLayer, the value
// setters, UDMMaterialComponent::Update) are the stable surface.

#include "MaterialHandlers.h"

#include "HandlerAssetCreate.h"
#include "HandlerFunctionCall.h"
#include "HandlerJsonProperty.h"
#include "HandlerUtils.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "UObject/Class.h"
#include "Factories/Factory.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MCPMaterialDesigner
{
	/** The plugin's classes, looked up by path so nothing links against it. */
	struct FDesignerClasses
	{
		UClass* Instance = nullptr;
		UClass* ModelBase = nullptr;
		UClass* Model = nullptr;
		UClass* Component = nullptr;
		UClass* EditorOnlyData = nullptr;
		UClass* Slot = nullptr;
		UClass* Layer = nullptr;
		UClass* Stage = nullptr;
		UClass* Property = nullptr;

		bool Load(FString& OutError)
		{
			Instance = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterial.DynamicMaterialInstance"));
			ModelBase = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterial.DynamicMaterialModelBase"));
			Model = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterial.DynamicMaterialModel"));
			Component = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterial.DMMaterialComponent"));
			EditorOnlyData = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterialEditor.DynamicMaterialModelEditorOnlyData"));
			Slot = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterialEditor.DMMaterialSlot"));
			Layer = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterialEditor.DMMaterialLayerObject"));
			Stage = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterialEditor.DMMaterialStage"));
			Property = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterialEditor.DMMaterialProperty"));
			if (!Instance || !ModelBase || !Model || !Component)
			{
				OutError = TEXT("Material Designer is not available: the DynamicMaterial plugin (Material Designer) is not loaded in this editor. Enable it with project(action=\"enable_plugin\", pluginName=\"DynamicMaterial\") and restart the editor.");
				return false;
			}
			if (!EditorOnlyData || !Slot || !Layer || !Stage)
			{
				OutError = TEXT("Material Designer is not available: the DynamicMaterialEditor module of the DynamicMaterial plugin is not loaded, or this engine version names its slot/layer/stage classes differently.");
				return false;
			}
			return true;
		}
	};

	/** One reflected UFUNCTION call on an object, with a default-constructed frame. */
	struct FDesignerCall
	{
		UObject* Target = nullptr;
		UFunction* Function = nullptr;
		TArray<uint8> Frame;

		FDesignerCall() = default;
		FDesignerCall(const FDesignerCall&) = delete;
		FDesignerCall& operator=(const FDesignerCall&) = delete;
		~FDesignerCall() { Release(); }

		bool Bind(UObject* InTarget, const TCHAR* FunctionName)
		{
			Release();
			if (!IsValid(InTarget)) return false;
			// FindFunctionByName also searches implemented interfaces, which is
			// where RequestMaterialBuild is declared.
			UFunction* Found = InTarget->GetClass()->FindFunctionByName(FName(FunctionName));
			if (!Found) return false;
			Target = InTarget;
			Function = Found;
			Frame.SetNumZeroed(FMath::Max<int32>(Function->ParmsSize, 1));
			for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
			{
				It->InitializeValue_InContainer(Frame.GetData());
			}
			return true;
		}

		void Release()
		{
			if (Function && Frame.Num() > 0)
			{
				MCPFunctionCall::DestroyFrame(Function, Frame.GetData());
			}
			Frame.Reset();
			Function = nullptr;
			Target = nullptr;
		}

		static bool IsInput(const FProperty* Prop)
		{
			if (Prop->PropertyFlags & CPF_ReturnParm) return false;
			// A const& parameter carries CPF_OutParm too; only a non-const one is a real out.
			return !(Prop->PropertyFlags & CPF_OutParm) || (Prop->PropertyFlags & CPF_ConstParm);
		}

		TArray<FProperty*> Inputs() const
		{
			TArray<FProperty*> Out;
			if (!Function) return Out;
			for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
			{
				if (IsInput(*It)) Out.Add(*It);
			}
			return Out;
		}

		void* ValuePtr(FProperty* Prop) { return Prop->ContainerPtrToValuePtr<void>(Frame.GetData()); }
		FProperty* ReturnProperty() const { return Function ? Function->GetReturnProperty() : nullptr; }

		UObject* ReturnObject()
		{
			FObjectPropertyBase* Ret = CastField<FObjectPropertyBase>(ReturnProperty());
			return Ret ? Ret->GetObjectPropertyValue(ValuePtr(Ret)) : nullptr;
		}

		bool ReturnBool(bool bDefault)
		{
			FBoolProperty* Ret = CastField<FBoolProperty>(ReturnProperty());
			return Ret ? Ret->GetPropertyValue(ValuePtr(Ret)) : bDefault;
		}

		void Invoke() { if (Target && Function) Target->ProcessEvent(Function, Frame.GetData()); }
	};

	inline FProperty* FindProp(const UObject* Obj, const FString& Name)
	{
		return Obj ? Obj->GetClass()->FindPropertyByName(FName(*Name)) : nullptr;
	}

	inline UObject* GetObjectProp(UObject* Obj, const TCHAR* Name)
	{
		FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(FindProp(Obj, Name));
		return Prop ? Prop->GetObjectPropertyValue_InContainer(Obj) : nullptr;
	}

	inline TArray<UObject*> GetObjectArrayProp(UObject* Obj, const TCHAR* Name)
	{
		TArray<UObject*> Out;
		FArrayProperty* Array = CastField<FArrayProperty>(FindProp(Obj, Name));
		FObjectPropertyBase* Inner = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
		if (!Inner) return Out;
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Obj));
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			Out.Add(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
		}
		return Out;
	}

	inline TSharedPtr<FJsonValue> PropJson(UObject* Obj, FProperty* Prop)
	{
		return MCPFunctionCall::ValueToJson(Prop, Prop->ContainerPtrToValuePtr<void>(Obj), Obj);
	}

	inline FString PropText(UObject* Obj, FProperty* Prop)
	{
		FString Out;
		Prop->ExportTextItem_Direct(Out, Prop->ContainerPtrToValuePtr<void>(Obj), nullptr, Obj, PPF_None);
		return Out;
	}

	/** A zero-argument UFUNCTION returning FString, FText or FName, as a string. */
	inline FString CallString(UObject* Target, const TCHAR* FunctionName)
	{
		FDesignerCall Call;
		if (!Call.Bind(Target, FunctionName) || Call.Inputs().Num() != 0) return FString();
		FProperty* Ret = Call.ReturnProperty();
		if (!Ret) return FString();
		Call.Invoke();
		const void* Ptr = Call.ValuePtr(Ret);
		if (FStrProperty* Str = CastField<FStrProperty>(Ret)) return Str->GetPropertyValue(Ptr);
		if (FTextProperty* Text = CastField<FTextProperty>(Ret)) return Text->GetPropertyValue(Ptr).ToString();
		if (FNameProperty* Name = CastField<FNameProperty>(Ret)) return Name->GetPropertyValue(Ptr).ToString();
		return FString();
	}

	inline UEnum* EnumOf(FProperty* Prop)
	{
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop)) return EnumProp->GetEnum();
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop)) return ByteProp->Enum;
		return nullptr;
	}

	inline void SetIntegral(FProperty* Prop, void* Ptr, int64 Value)
	{
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, Value);
		}
		else if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			NumProp->SetIntPropertyValue(Ptr, Value);
		}
	}

	/** Match an enum entry by name, display name or ShortName metadata ("Base"). */
	inline bool MatchEnum(UEnum* Enum, const FString& Text, int64& OutValue)
	{
		if (!Enum) return false;
		const int32 Count = Enum->NumEnums();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			bool bMatch = Enum->GetNameStringByIndex(Index).Equals(Text, ESearchCase::IgnoreCase)
				|| Enum->GetDisplayNameTextByIndex(Index).ToString().Equals(Text, ESearchCase::IgnoreCase);
#if WITH_EDITORONLY_DATA
			bMatch = bMatch || Enum->GetMetaData(TEXT("ShortName"), Index).Equals(Text, ESearchCase::IgnoreCase);
#endif
			if (bMatch)
			{
				OutValue = Enum->GetValueByIndex(Index);
				return true;
			}
		}
		return false;
	}

	inline FString EnumNames(UEnum* Enum)
	{
		TArray<FString> Names;
		if (Enum)
		{
			for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index) Names.Add(Enum->GetNameStringByIndex(Index));
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** Write JSON into a property value. FText takes a plain string. */
	inline bool WriteJson(FProperty* Prop, void* Ptr, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		{
			FString Str;
			if (Value.IsValid() && Value->TryGetString(Str))
			{
				TextProp->SetPropertyValue(Ptr, FText::FromString(Str));
				return true;
			}
		}
		return MCPJsonProperty::SetJsonOnProperty(Prop, Ptr, Value, OutError);
	}

	/** Update(Source, Type) on a component, which propagates the change through the model. */
	inline bool CallUpdate(UObject* Component, bool bStructure)
	{
		FDesignerCall Call;
		if (!Call.Bind(Component, TEXT("Update"))) return false;
		for (FProperty* Input : Call.Inputs())
		{
			if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Input))
			{
				ObjProp->SetObjectPropertyValue(Call.ValuePtr(Input), Component);
			}
			else if (EnumOf(Input) || Input->IsA<FNumericProperty>())
			{
				// EDMUpdateType::Structure = 1, Value = 0.
				SetIntegral(Input, Call.ValuePtr(Input), bStructure ? 1 : 0);
			}
		}
		Call.Invoke();
		return true;
	}

	/** RequestMaterialBuild(Async) on the model's editor-only data. */
	inline bool RequestBuild(UObject* EditorOnlyData)
	{
		FDesignerCall Call;
		if (!Call.Bind(EditorOnlyData, TEXT("RequestMaterialBuild"))) return false;
		const TArray<FProperty*> Inputs = Call.Inputs();
		if (Inputs.Num() > 0)
		{
			int64 Value = 0;
			if (MatchEnum(EnumOf(Inputs[0]), TEXT("Async"), Value)) SetIntegral(Inputs[0], Call.ValuePtr(Inputs[0]), Value);
		}
		Call.Invoke();
		return true;
	}

	struct FDesignerTarget
	{
		UObject* Instance = nullptr;
		UObject* Model = nullptr;
		UObject* EditorOnlyData = nullptr;
		AActor* Actor = nullptr;
		UPrimitiveComponent* MeshComponent = nullptr;
		int32 MaterialSlot = INDEX_NONE;
	};

	/** Walk from an instance, model, editor-only data or component to the resolved model. */
	inline UObject* ResolveModel(UObject* Start, const FDesignerClasses& C)
	{
		UObject* Cur = Start;
		for (int32 Guard = 0; Cur && Guard < 32; ++Guard)
		{
			if (Cur->IsA(C.Model)) return Cur;
			if (Cur->IsA(C.ModelBase))
			{
				FDesignerCall Call;
				if (Call.Bind(Cur, TEXT("ResolveMaterialModel")))
				{
					Call.Invoke();
					return Call.ReturnObject();
				}
				return nullptr;
			}
			if (Cur->IsA(C.Instance)) { Cur = GetObjectProp(Cur, TEXT("MaterialModelBase")); continue; }
			if (Cur->IsA(C.EditorOnlyData)) { Cur = GetObjectProp(Cur, TEXT("MaterialModel")); continue; }
			Cur = Cur->GetOuter();
		}
		return nullptr;
	}

	inline UObject* ResolveEditorOnlyData(UObject* Model, const FDesignerClasses& C)
	{
		UObject* Data = GetObjectProp(Model, TEXT("EditorOnlyDataSI"));
		if (Data && Data->IsA(C.EditorOnlyData)) return Data;
		UObject* Found = nullptr;
		ForEachObjectWithOuter(Model, [&Found, &C](UObject* Inner)
		{
			if (!Found && Inner->IsA(C.EditorOnlyData)) Found = Inner;
		}, false);
		return Found;
	}

	inline UObject* FindAnyObject(const FString& Path)
	{
		if (Path.IsEmpty()) return nullptr;
		// A package path finds the UPackage; the asset inside it is what is meant.
		UObject* Found = FindObject<UObject>(nullptr, *Path);
		if (Found && !Found->IsA<UPackage>()) return Found;
		return MCPLoadAssetObject(Path);
	}

	/**
	 * assetPath (a DynamicMaterialInstance, a model, or any object inside one),
	 * or actorLabel/actorPath with componentName?, slotIndex?/slotName?.
	 * Returns an error response on failure, nullptr on success.
	 */
	inline TSharedPtr<FJsonValue> ResolveTarget(const TSharedPtr<FJsonObject>& Params, const FDesignerClasses& C, FDesignerTarget& Out)
	{
		const FString AssetPath = OptionalString(Params, TEXT("assetPath"));
		UObject* Start = nullptr;
		if (!AssetPath.IsEmpty())
		{
			Start = FindAnyObject(AssetPath);
			if (!Start) return MCPError(FString::Printf(TEXT("Nothing found at assetPath '%s'. Pass a DynamicMaterialInstance, a DynamicMaterialModel, or an object path inside one."), *AssetPath));
		}
		else if (HasParam(Params, TEXT("actorLabel")) || HasParam(Params, TEXT("actorPath")))
		{
			UWorld* World = ResolveWorldFromParams(Params, TEXT("editor"));
			if (!World) return MCPError(TEXT("The requested world is not available. Start PIE first, or pass world=editor."));
			FMCPActorSelector Selector;
			Selector.Match = EMCPActorMatch::LabelOrName;
			TSharedPtr<FJsonValue> ActorError;
			AActor* Actor = MCPResolveActor(World, Params, ActorError, Selector);
			if (!Actor) return ActorError;

			const FString ComponentName = OptionalString(Params, TEXT("componentName"));
			const FString SlotName = OptionalString(Params, TEXT("slotName"));
			const bool bSlotIndexGiven = HasParam(Params, TEXT("slotIndex"));
			const int32 SlotIndex = OptionalInt(Params, TEXT("slotIndex"), 0);

			TArray<UPrimitiveComponent*> Components;
			Actor->GetComponents<UPrimitiveComponent>(Components);
			TArray<FString> Candidates;
			for (UPrimitiveComponent* Component : Components)
			{
				if (!Component) continue;
				const TArray<FName> SlotNames = Component->GetMaterialSlotNames();
				for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
				{
					UMaterialInterface* Material = Component->GetMaterial(Index);
					if (!Material || !Material->IsA(C.Instance)) continue;
					Candidates.Add(FString::Printf(TEXT("%s[%d]"), *Component->GetName(), Index));
					if (Start) continue;
					if (!ComponentName.IsEmpty() && Component->GetName() != ComponentName) continue;
					if (bSlotIndexGiven && Index != SlotIndex) continue;
					if (!SlotName.IsEmpty() && !(SlotNames.IsValidIndex(Index) && SlotNames[Index].ToString() == SlotName)) continue;
					Start = Material;
					Out.MeshComponent = Component;
					Out.MaterialSlot = Index;
				}
			}
			Out.Actor = Actor;
			if (!Start)
			{
				return MCPError(FString::Printf(TEXT("No Material Designer material matches on actor '%s'. Slots holding a DynamicMaterialInstance: [%s]"),
					*Actor->GetActorLabel(), *FString::Join(Candidates, TEXT(", "))));
			}
		}
		else
		{
			return MCPError(TEXT("Missing target: pass assetPath (a DynamicMaterialInstance or DynamicMaterialModel), or actorLabel/actorPath with componentName?, slotIndex? or slotName?."));
		}

		Out.Model = ResolveModel(Start, C);
		if (!Out.Model && Start->IsA(C.Instance))
		{
			return MCPError(FString::Printf(TEXT("DynamicMaterialInstance '%s' has no Material Designer model (MaterialModelBase is None), so it has no layer stack. Create a working one with material(action=\"create_designer\")."), *Start->GetPathName()));
		}
		if (!Out.Model)
		{
			return MCPError(FString::Printf(TEXT("'%s' (%s) is not a Material Designer material or model and is not inside one."), *Start->GetPathName(), *Start->GetClass()->GetName()));
		}
		Out.Instance = GetObjectProp(Out.Model, TEXT("DynamicMaterialInstance"));
		if (!Out.Instance && Start->IsA(C.Instance)) Out.Instance = Start;
		Out.EditorOnlyData = ResolveEditorOnlyData(Out.Model, C);
		if (!Out.EditorOnlyData)
		{
			return MCPError(FString::Printf(TEXT("Model '%s' has no editor-only data (slots and layers live there), so its layer stack cannot be read."), *Out.Model->GetPathName()));
		}
		return nullptr;
	}

	/** PropertySlotMap as (property name, slot) pairs. */
	inline TArray<TPair<FString, UObject*>> PropertySlots(UObject* EditorOnlyData)
	{
		TArray<TPair<FString, UObject*>> Out;
		FMapProperty* MapProp = CastField<FMapProperty>(FindProp(EditorOnlyData, TEXT("PropertySlotMap")));
		FObjectPropertyBase* ValueProp = MapProp ? CastField<FObjectPropertyBase>(MapProp->ValueProp) : nullptr;
		if (!ValueProp) return Out;
		FScriptMapHelper Helper(MapProp, MapProp->ContainerPtrToValuePtr<void>(EditorOnlyData));
		for (int32 Index = 0, Max = Helper.GetMaxIndex(); Index < Max; ++Index)
		{
			if (!Helper.IsValidIndex(Index)) continue;
			const FString Key = MCPFunctionCall::ValueToJson(MapProp->KeyProp, Helper.GetKeyPtr(Index), EditorOnlyData)->AsString();
			Out.Emplace(Key, ValueProp->GetObjectPropertyValue(Helper.GetValuePtr(Index)));
		}
		return Out;
	}

	/** designerSlot: an index, or a material property name (BaseColor, or its short name Base). */
	inline UObject* ResolveSlot(const FDesignerTarget& T, const TSharedPtr<FJsonObject>& Params, const FString& DefaultProperty, FString& OutError)
	{
		const TArray<UObject*> Slots = GetObjectArrayProp(T.EditorOnlyData, TEXT("Slots"));
		const TArray<TPair<FString, UObject*>> Mapped = PropertySlots(T.EditorOnlyData);
		TSharedPtr<FJsonValue> Field = TryGetParam(Params, TEXT("designerSlot"));
		FString Text;
		int32 Index = INDEX_NONE;
		if (Field.IsValid() && Field->Type == EJson::Number)
		{
			Index = static_cast<int32>(Field->AsNumber());
		}
		else if (Field.IsValid() && Field->TryGetString(Text) && Text.IsNumeric())
		{
			Index = FCString::Atoi(*Text);
		}
		else if (!Field.IsValid() || Text.IsEmpty())
		{
			Text = DefaultProperty;
		}

		if (Index != INDEX_NONE)
		{
			if (Slots.IsValidIndex(Index) && Slots[Index]) return Slots[Index];
			OutError = FString::Printf(TEXT("designerSlot %d is out of range (the model has %d slots)."), Index, Slots.Num());
			return nullptr;
		}

		// Accept the enum name or its ShortName ("Base" for BaseColor).
		UEnum* PropertyEnum = nullptr;
		if (FMapProperty* MapProp = CastField<FMapProperty>(FindProp(T.EditorOnlyData, TEXT("PropertySlotMap"))))
		{
			PropertyEnum = EnumOf(MapProp->KeyProp);
		}
		int64 Value = 0;
		FString Canonical = Text;
		if (MatchEnum(PropertyEnum, Text, Value)) Canonical = PropertyEnum->GetNameStringByValue(Value);
		TArray<FString> Known;
		for (const TPair<FString, UObject*>& Pair : Mapped)
		{
			if (Pair.Key.Equals(Canonical, ESearchCase::IgnoreCase) && Pair.Value) return Pair.Value;
			Known.Add(Pair.Key);
		}
		if (Field.IsValid() || Slots.Num() == 0)
		{
			OutError = FString::Printf(TEXT("No Material Designer slot for '%s'. Pass designerSlot as an index (0-%d) or a mapped property: [%s]."),
				*Text, Slots.Num() - 1, *FString::Join(Known, TEXT(", ")));
			return nullptr;
		}
		return Slots[0];
	}

	inline FString DesignerLayerName(UObject* Layer)
	{
		FTextProperty* Prop = CastField<FTextProperty>(FindProp(Layer, TEXT("LayerName")));
		return Prop ? Prop->GetPropertyValue_InContainer(Layer).ToString() : FString();
	}

	/** layerIndex, or layerName (case-insensitive). */
	inline UObject* ResolveLayer(UObject* Slot, const TSharedPtr<FJsonObject>& Params, FString& OutError)
	{
		const TArray<UObject*> Layers = GetObjectArrayProp(Slot, TEXT("LayerObjects"));
		if (HasParam(Params, TEXT("layerIndex")))
		{
			const int32 Index = OptionalInt(Params, TEXT("layerIndex"), -1);
			if (Layers.IsValidIndex(Index) && Layers[Index]) return Layers[Index];
			OutError = FString::Printf(TEXT("layerIndex %d is out of range (the slot has %d layers)."), Index, Layers.Num());
			return nullptr;
		}
		const FString Name = OptionalString(Params, TEXT("layerName"));
		if (!Name.IsEmpty())
		{
			TArray<FString> Names;
			for (UObject* Layer : Layers)
			{
				if (!Layer) continue;
				const FString Candidate = DesignerLayerName(Layer);
				if (Candidate.Equals(Name, ESearchCase::IgnoreCase)) return Layer;
				Names.Add(Candidate);
			}
			OutError = FString::Printf(TEXT("No layer named '%s' in this slot. Layers: [%s]"), *Name, *FString::Join(Names, TEXT(", ")));
			return nullptr;
		}
		OutError = TEXT("Missing layer selector: pass layerIndex or layerName.");
		return nullptr;
	}

	/** EDMMaterialLayerStage name of a stage within its layer ("Base", "Mask"). */
	inline FString StageType(UObject* Layer, UObject* Stage)
	{
		FDesignerCall Call;
		if (!Call.Bind(Layer, TEXT("GetStageType"))) return FString();
		const TArray<FProperty*> Inputs = Call.Inputs();
		FObjectPropertyBase* StageParam = Inputs.Num() == 1 ? CastField<FObjectPropertyBase>(Inputs[0]) : nullptr;
		FProperty* Ret = Call.ReturnProperty();
		if (!StageParam || !Ret) return FString();
		StageParam->SetObjectPropertyValue(Call.ValuePtr(StageParam), Stage);
		Call.Invoke();
		return MCPFunctionCall::ValueToJson(Ret, Call.ValuePtr(Ret), nullptr)->AsString();
	}

	/** stage: "base" | "mask" | an index into the layer's Stages. */
	inline UObject* ResolveStage(UObject* Layer, const FString& Selector, FString& OutError)
	{
		const TArray<UObject*> Stages = GetObjectArrayProp(Layer, TEXT("Stages"));
		if (Selector.IsNumeric())
		{
			const int32 Index = FCString::Atoi(*Selector);
			if (Stages.IsValidIndex(Index) && Stages[Index]) return Stages[Index];
		}
		for (UObject* Stage : Stages)
		{
			if (Stage && StageType(Layer, Stage).Equals(Selector, ESearchCase::IgnoreCase)) return Stage;
		}
		OutError = FString::Printf(TEXT("No stage '%s' on this layer. Use base, mask or an index (0-%d)."), *Selector, Stages.Num() - 1);
		return nullptr;
	}

	struct FDescribeContext
	{
		const FDesignerClasses* Classes = nullptr;
		TSet<const UObject*> Visited;
		int32 MaxDepth = 10;
	};

	inline void AddIdentity(const TSharedPtr<FJsonObject>& Out, UObject* Obj, const FDesignerClasses& C)
	{
		Out->SetStringField(TEXT("class"), Obj->GetClass()->GetName());
		Out->SetStringField(TEXT("objectPath"), Obj->GetPathName());
		if (Obj->IsA(C.Component))
		{
			const FString ComponentPath = CallString(Obj, TEXT("GetComponentPath"));
			if (!ComponentPath.IsEmpty()) Out->SetStringField(TEXT("componentPath"), ComponentPath);
			const FString Description = CallString(Obj, TEXT("GetComponentDescription"));
			if (!Description.IsEmpty()) Out->SetStringField(TEXT("description"), Description);
		}
	}

	/** Writable property names: editable UPROPERTYs plus the component's own EditableProperties list. */
	inline TArray<FString> WritableProperties(UObject* Obj)
	{
		TArray<FString> Out;
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			if ((It->PropertyFlags & CPF_Edit) && !(It->PropertyFlags & (CPF_EditConst | CPF_Deprecated | CPF_Transient)))
			{
				Out.AddUnique(It->GetName());
			}
		}
		FArrayProperty* Editable = CastField<FArrayProperty>(FindProp(Obj, TEXT("EditableProperties")));
		if (Editable && CastField<FNameProperty>(Editable->Inner))
		{
			FScriptArrayHelper Helper(Editable, Editable->ContainerPtrToValuePtr<void>(Obj));
			FNameProperty* Inner = CastField<FNameProperty>(Editable->Inner);
			for (int32 Index = 0; Index < Helper.Num(); ++Index) Out.AddUnique(Inner->GetPropertyValue(Helper.GetRawPtr(Index)).ToString());
		}
		return Out;
	}

	TSharedPtr<FJsonObject> DescribeComponent(UObject* Obj, int32 Depth, FDescribeContext& Ctx);

	inline TSharedPtr<FJsonValue> DescribeChild(UObject* Child, int32 Depth, FDescribeContext& Ctx)
	{
		if (!Child) return MakeShared<FJsonValueNull>();
		const FDesignerClasses& C = *Ctx.Classes;
		// Slots and layers are described by the slot walk; a reference from a
		// stage input (a slot used as a source) stays a reference.
		if (Child->IsA(C.Slot) || Child->IsA(C.Layer) || Child->IsA(C.Model) || Child->IsA(C.EditorOnlyData))
		{
			TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
			Ref->SetStringField(TEXT("ref"), Child->GetPathName());
			Ref->SetStringField(TEXT("class"), Child->GetClass()->GetName());
			return MakeShared<FJsonValueObject>(Ref);
		}
		return MakeShared<FJsonValueObject>(DescribeComponent(Child, Depth + 1, Ctx));
	}

	/** A DM component: identity, plain property values, and sub-components under "components". */
	TSharedPtr<FJsonObject> DescribeComponent(UObject* Obj, int32 Depth, FDescribeContext& Ctx)
	{
		const FDesignerClasses& C = *Ctx.Classes;
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (Ctx.Visited.Contains(Obj))
		{
			Out->SetStringField(TEXT("ref"), Obj->GetPathName());
			Out->SetStringField(TEXT("class"), Obj->GetClass()->GetName());
			return Out;
		}
		Ctx.Visited.Add(Obj);
		AddIdentity(Out, Obj, C);
		if (Depth > Ctx.MaxDepth)
		{
			Out->SetBoolField(TEXT("truncated"), true);
			return Out;
		}

		static const FName Skip[] = { FName(TEXT("ParentComponent")), FName(TEXT("ParentStage")), FName(TEXT("ComponentState")) };
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Children = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (Prop->PropertyFlags & (CPF_Deprecated | CPF_Transient)) continue;
			bool bSkip = false;
			for (const FName& Name : Skip) bSkip |= Prop->GetFName() == Name;
			if (bSkip) continue;

			// Strong references to DM components are the tree; weak ones point back up.
			if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
			{
				if (!Prop->IsA<FWeakObjectProperty>())
				{
					UObject* Child = ObjProp->GetObjectPropertyValue_InContainer(Obj);
					if (Child && Child->IsA(C.Component))
					{
						Children->SetField(Prop->GetName(), DescribeChild(Child, Depth, Ctx));
						continue;
					}
				}
			}
			if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
			{
				FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(ArrayProp->Inner);
				UClass* InnerClass = Inner ? Inner->PropertyClass : nullptr;
				if (InnerClass && InnerClass->IsChildOf(C.Component) && !Inner->IsA<FWeakObjectProperty>())
				{
					TArray<TSharedPtr<FJsonValue>> Items;
					for (UObject* Child : GetObjectArrayProp(Obj, *Prop->GetName())) Items.Add(DescribeChild(Child, Depth, Ctx));
					Children->SetArrayField(Prop->GetName(), Items);
					continue;
				}
			}
			Props->SetField(Prop->GetName(), PropJson(Obj, Prop));
		}
		Out->SetObjectField(TEXT("properties"), Props);
		if (Children->Values.Num() > 0) Out->SetObjectField(TEXT("components"), Children);
		TArray<TSharedPtr<FJsonValue>> Writable;
		for (const FString& Name : WritableProperties(Obj)) Writable.Add(MakeShared<FJsonValueString>(Name));
		Out->SetArrayField(TEXT("writableProperties"), Writable);
		return Out;
	}

	inline TSharedPtr<FJsonObject> DescribeLayer(UObject* Layer, int32 Index, FDescribeContext& Ctx)
	{
		const FDesignerClasses& C = *Ctx.Classes;
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Ctx.Visited.Add(Layer);
		Out->SetNumberField(TEXT("index"), Index);
		AddIdentity(Out, Layer, C);
		Out->SetStringField(TEXT("name"), DesignerLayerName(Layer));
		for (const TCHAR* Name : { TEXT("bEnabled"), TEXT("MaterialProperty"), TEXT("bLinkedUVs") })
		{
			if (FProperty* Prop = FindProp(Layer, Name)) Out->SetField(Name, PropJson(Layer, Prop));
		}

		TArray<TSharedPtr<FJsonValue>> StageItems;
		UObject* BaseStage = nullptr;
		const TArray<UObject*> Stages = GetObjectArrayProp(Layer, TEXT("Stages"));
		for (int32 StageIndex = 0; StageIndex < Stages.Num(); ++StageIndex)
		{
			UObject* Stage = Stages[StageIndex];
			if (!Stage) { StageItems.Add(MakeShared<FJsonValueNull>()); continue; }
			const FString Type = StageType(Layer, Stage);
			if (Type.Equals(TEXT("Base"), ESearchCase::IgnoreCase) || (!BaseStage && StageIndex == 0)) BaseStage = Stage;
			TSharedPtr<FJsonObject> StageJson = DescribeComponent(Stage, 1, Ctx);
			StageJson->SetNumberField(TEXT("index"), StageIndex);
			if (!Type.IsEmpty()) StageJson->SetStringField(TEXT("stageType"), Type);
			StageItems.Add(MakeShared<FJsonValueObject>(StageJson));
		}
		Out->SetArrayField(TEXT("stages"), StageItems);

		// The base stage's source is the blend (a DMMaterialStageBlend subclass).
		if (UObject* Source = BaseStage ? GetObjectProp(BaseStage, TEXT("Source")) : nullptr)
		{
			TSharedPtr<FJsonObject> Blend = MakeShared<FJsonObject>();
			Blend->SetStringField(TEXT("class"), Source->GetClass()->GetName());
			Blend->SetStringField(TEXT("description"), CallString(Source, TEXT("GetComponentDescription")));
			Out->SetObjectField(TEXT("blendMode"), Blend);
		}
		if (UObject* Effects = GetObjectProp(Layer, TEXT("EffectStack")))
		{
			Out->SetObjectField(TEXT("effectStack"), DescribeComponent(Effects, 1, Ctx));
		}
		return Out;
	}

	inline void AddTargetFields(const TSharedPtr<FJsonObject>& Result, const FDesignerTarget& T)
	{
		Result->SetStringField(TEXT("modelPath"), T.Model->GetPathName());
		if (T.Instance) Result->SetStringField(TEXT("instancePath"), T.Instance->GetPathName());
		if (T.Actor)
		{
			Result->SetStringField(TEXT("actorLabel"), T.Actor->GetActorLabel());
			Result->SetStringField(TEXT("actorPath"), T.Actor->GetPathName());
		}
		if (T.MeshComponent)
		{
			Result->SetStringField(TEXT("componentName"), T.MeshComponent->GetName());
			Result->SetNumberField(TEXT("slotIndex"), T.MaterialSlot);
		}
	}

	/** The component a set addresses: objectPath, componentPath, or designerSlot + layer (+ stage). */
	inline UObject* ResolveComponent(const TSharedPtr<FJsonObject>& Params, const FDesignerClasses& C, FDesignerTarget& T, TSharedPtr<FJsonValue>& OutError)
	{
		const FString ObjectPath = OptionalString(Params, TEXT("objectPath"));
		if (!ObjectPath.IsEmpty())
		{
			UObject* Found = FindObject<UObject>(nullptr, *ObjectPath);
			if (!Found)
			{
				OutError = MCPError(FString::Printf(TEXT("No object at objectPath '%s'. read_material_designer reports the objectPath of every slot, layer, stage and value."), *ObjectPath));
				return nullptr;
			}
			T.Model = ResolveModel(Found, C);
			if (!T.Model)
			{
				OutError = MCPError(FString::Printf(TEXT("'%s' is not inside a Material Designer model."), *ObjectPath));
				return nullptr;
			}
			T.EditorOnlyData = ResolveEditorOnlyData(T.Model, C);
			T.Instance = GetObjectProp(T.Model, TEXT("DynamicMaterialInstance"));
			return Found;
		}

		if (TSharedPtr<FJsonValue> Err = ResolveTarget(Params, C, T)) { OutError = Err; return nullptr; }

		const FString ComponentPath = OptionalString(Params, TEXT("componentPath"));
		if (!ComponentPath.IsEmpty())
		{
			FDesignerCall Call;
			FStrProperty* PathParam = nullptr;
			if (Call.Bind(T.Model, TEXT("GetComponentByPath")))
			{
				const TArray<FProperty*> Inputs = Call.Inputs();
				PathParam = Inputs.Num() == 1 ? CastField<FStrProperty>(Inputs[0]) : nullptr;
			}
			if (!PathParam)
			{
				OutError = MCPError(TEXT("This engine's Material Designer model has no GetComponentByPath(FString). Address the component by objectPath instead."));
				return nullptr;
			}
			PathParam->SetPropertyValue(Call.ValuePtr(PathParam), ComponentPath);
			Call.Invoke();
			if (UObject* Found = Call.ReturnObject()) return Found;
			OutError = MCPError(FString::Printf(TEXT("No component at componentPath '%s'. read_material_designer reports each component's componentPath and objectPath."), *ComponentPath));
			return nullptr;
		}

		if (HasParam(Params, TEXT("layerIndex")) || HasParam(Params, TEXT("layerName")))
		{
			FString Error;
			UObject* Slot = ResolveSlot(T, Params, TEXT("BaseColor"), Error);
			UObject* Layer = Slot ? ResolveLayer(Slot, Params, Error) : nullptr;
			if (Layer)
			{
				const FString StageSel = OptionalString(Params, TEXT("stage"));
				if (StageSel.IsEmpty()) return Layer;
				if (UObject* Stage = ResolveStage(Layer, StageSel, Error)) return Stage;
			}
			OutError = MCPError(Error);
			return nullptr;
		}

		OutError = MCPError(TEXT("Missing component selector: pass objectPath, componentPath, or designerSlot? with layerIndex/layerName (and stage? for a stage)."));
		return nullptr;
	}

	/** The layer a remove addresses: objectPath of a layer, or target + designerSlot + layerIndex/layerName. */
	inline UObject* ResolveLayerTarget(const TSharedPtr<FJsonObject>& Params, const FDesignerClasses& C, FDesignerTarget& T, TSharedPtr<FJsonValue>& OutError)
	{
		const FString ObjectPath = OptionalString(Params, TEXT("objectPath"));
		if (!ObjectPath.IsEmpty())
		{
			UObject* Found = FindObject<UObject>(nullptr, *ObjectPath);
			if (!Found || !Found->IsA(C.Layer))
			{
				OutError = MCPError(FString::Printf(TEXT("objectPath '%s' is not a Material Designer layer (DMMaterialLayerObject)."), *ObjectPath));
				return nullptr;
			}
			T.Model = ResolveModel(Found, C);
			T.EditorOnlyData = T.Model ? ResolveEditorOnlyData(T.Model, C) : nullptr;
			T.Instance = T.Model ? GetObjectProp(T.Model, TEXT("DynamicMaterialInstance")) : nullptr;
			if (!T.Model)
			{
				OutError = MCPError(FString::Printf(TEXT("Layer '%s' is not inside a Material Designer model."), *ObjectPath));
				return nullptr;
			}
			return Found;
		}
		if (TSharedPtr<FJsonValue> Err = ResolveTarget(Params, C, T)) { OutError = Err; return nullptr; }
		FString Error;
		UObject* Slot = ResolveSlot(T, Params, TEXT("BaseColor"), Error);
		UObject* Layer = Slot ? ResolveLayer(Slot, Params, Error) : nullptr;
		if (!Layer) OutError = MCPError(Error);
		return Layer;
	}

	inline UObject* SlotOfLayer(UObject* Layer, const FDesignerClasses& C)
	{
		FDesignerCall Call;
		if (Call.Bind(Layer, TEXT("GetSlot")) && Call.Inputs().Num() == 0)
		{
			Call.Invoke();
			if (UObject* Slot = Call.ReturnObject()) return Slot;
		}
		return Layer->GetTypedOuter(C.Slot);
	}
}



TSharedPtr<FJsonValue> FMaterialHandlers::ReadMaterialDesigner(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPMaterialDesigner;
	FDesignerClasses C;
	FString ClassError;
	if (!C.Load(ClassError)) return MCPError(ClassError);

	FDesignerTarget T;
	if (TSharedPtr<FJsonValue> Err = ResolveTarget(Params, C, T)) return Err;

	FDescribeContext Ctx;
	Ctx.Classes = &C;
	Ctx.MaxDepth = FMath::Clamp(OptionalInt(Params, TEXT("maxDepth"), 10), 1, 32);

	auto Result = MCPSuccess();
	AddTargetFields(Result, T);
	Result->SetStringField(TEXT("editorOnlyDataPath"), T.EditorOnlyData->GetPathName());

	// Material-wide settings: the editable properties of the editor-only data.
	TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(T.EditorOnlyData->GetClass()); It; ++It)
	{
		if (!(It->PropertyFlags & CPF_Edit) || (It->PropertyFlags & (CPF_EditConst | CPF_Deprecated))) continue;
		if (It->IsA<FObjectPropertyBase>() || MCPFunctionCall::IsContainerProperty(*It)) continue;
		Settings->SetField(It->GetName(), PropJson(T.EditorOnlyData, *It));
	}
	Result->SetObjectField(TEXT("settings"), Settings);

	const TArray<UObject*> Slots = GetObjectArrayProp(T.EditorOnlyData, TEXT("Slots"));
	const TArray<TPair<FString, UObject*>> Mapped = PropertySlots(T.EditorOnlyData);

	// Material properties (BaseColor, Opacity, ...) with their enabled state and slot.
	TArray<TSharedPtr<FJsonValue>> MaterialProperties;
	if (C.Property)
	{
		for (TFieldIterator<FObjectPropertyBase> It(T.EditorOnlyData->GetClass()); It; ++It)
		{
			UObject* PropObj = It->GetObjectPropertyValue_InContainer(T.EditorOnlyData);
			if (!PropObj || !PropObj->IsA(C.Property)) continue;
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("property"), It->GetName());
			Entry->SetStringField(TEXT("objectPath"), PropObj->GetPathName());
			if (FProperty* Enabled = FindProp(PropObj, TEXT("bEnabled"))) Entry->SetField(TEXT("enabled"), PropJson(PropObj, Enabled));
			for (const TPair<FString, UObject*>& Pair : Mapped)
			{
				if (Pair.Key == It->GetName()) Entry->SetNumberField(TEXT("designerSlot"), Slots.IndexOfByKey(Pair.Value));
			}
			MaterialProperties.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
	Result->SetArrayField(TEXT("materialProperties"), MaterialProperties);

	// Optional narrowing to one slot and/or layer.
	UObject* OnlySlot = nullptr;
	if (HasParam(Params, TEXT("designerSlot")))
	{
		FString Error;
		OnlySlot = ResolveSlot(T, Params, FString(), Error);
		if (!OnlySlot) return MCPError(Error);
	}
	const bool bOnlyLayer = HasParam(Params, TEXT("layerIndex"));
	const int32 OnlyLayer = OptionalInt(Params, TEXT("layerIndex"), -1);

	TArray<TSharedPtr<FJsonValue>> SlotItems;
	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		UObject* Slot = Slots[SlotIndex];
		if (!Slot || (OnlySlot && Slot != OnlySlot)) continue;
		Ctx.Visited.Add(Slot);
		TSharedPtr<FJsonObject> SlotJson = MakeShared<FJsonObject>();
		SlotJson->SetNumberField(TEXT("index"), SlotIndex);
		AddIdentity(SlotJson, Slot, C);
		const FString SlotDescription = CallString(Slot, TEXT("GetDescription"));
		if (!SlotDescription.IsEmpty()) SlotJson->SetStringField(TEXT("description"), SlotDescription);
		TArray<TSharedPtr<FJsonValue>> SlotProps;
		for (const TPair<FString, UObject*>& Pair : Mapped)
		{
			if (Pair.Value == Slot) SlotProps.Add(MakeShared<FJsonValueString>(Pair.Key));
		}
		SlotJson->SetArrayField(TEXT("materialProperties"), SlotProps);

		const TArray<UObject*> Layers = GetObjectArrayProp(Slot, TEXT("LayerObjects"));
		if (!FindProp(Slot, TEXT("LayerObjects")))
		{
			SlotJson->SetStringField(TEXT("layersNote"), TEXT("This engine's DMMaterialSlot has no LayerObjects property, so its layers cannot be listed."));
		}
		TArray<TSharedPtr<FJsonValue>> LayerItems;
		for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
		{
			if (!Layers[LayerIndex] || (bOnlyLayer && LayerIndex != OnlyLayer)) continue;
			LayerItems.Add(MakeShared<FJsonValueObject>(DescribeLayer(Layers[LayerIndex], LayerIndex, Ctx)));
		}
		SlotJson->SetNumberField(TEXT("layerCount"), Layers.Num());
		SlotJson->SetArrayField(TEXT("layers"), LayerItems);
		SlotItems.Add(MakeShared<FJsonValueObject>(SlotJson));
	}
	Result->SetNumberField(TEXT("slotCount"), Slots.Num());
	Result->SetArrayField(TEXT("slots"), SlotItems);

	// Global parameter values and model-level values, last so the layer walk
	// describes a shared value inline and these report it as a ref.
	if (!OnlySlot && !bOnlyLayer)
	{
		TSharedPtr<FJsonObject> Globals = MakeShared<FJsonObject>();
		for (TFieldIterator<FObjectPropertyBase> It(T.Model->GetClass()); It; ++It)
		{
			if (It->PropertyFlags & CPF_Deprecated) continue;
			if (!It->GetName().StartsWith(TEXT("Global"))) continue;
			UObject* Value = It->GetObjectPropertyValue_InContainer(T.Model);
			if (Value && Value->IsA(C.Component)) Globals->SetObjectField(It->GetName(), DescribeComponent(Value, 1, Ctx));
		}
		Result->SetObjectField(TEXT("globalValues"), Globals);
		TArray<TSharedPtr<FJsonValue>> Values;
		for (UObject* Value : GetObjectArrayProp(T.Model, TEXT("Values")))
		{
			if (Value) Values.Add(MakeShared<FJsonValueObject>(DescribeComponent(Value, 1, Ctx)));
		}
		Result->SetArrayField(TEXT("values"), Values);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialDesignerValue(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPMaterialDesigner;
	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;
	TSharedPtr<FJsonValue> Value = TryGetParam(Params, TEXT("value"));
	if (!Value.IsValid()) return MCPError(TEXT("Missing required parameter 'value'"));

	FDesignerClasses C;
	FString ClassError;
	if (!C.Load(ClassError)) return MCPError(ClassError);

	FDesignerTarget T;
	TSharedPtr<FJsonValue> ResolveError;
	UObject* Component = ResolveComponent(Params, C, T, ResolveError);
	if (!Component) return ResolveError;

	FProperty* Prop = FindProp(Component, PropertyName);
	if (!Prop && !PropertyName.StartsWith(TEXT("b"))) Prop = FindProp(Component, TEXT("b") + PropertyName);
	if (!Prop)
	{
		return MCPError(FString::Printf(TEXT("%s has no property '%s'. Writable: [%s]"),
			*Component->GetClass()->GetName(), *PropertyName, *FString::Join(WritableProperties(Component), TEXT(", "))));
	}
	const FString PropName = Prop->GetName();

	// Prefer the component's own setter: it runs the update the Material
	// Designer UI runs (SetValue, SetOffset, SetText, SetEnabled, ...).
	TArray<FString> SetterNames;
#if WITH_EDITORONLY_DATA
	const FString MetaSetter = Prop->GetMetaData(TEXT("BlueprintSetter"));
	if (!MetaSetter.IsEmpty()) SetterNames.Add(MetaSetter);
#endif
	SetterNames.AddUnique(TEXT("Set") + PropName);
	if (PropName.Len() > 1 && PropName[0] == TEXT('b') && FChar::IsUpper(PropName[1])) SetterNames.AddUnique(TEXT("Set") + PropName.Mid(1));

	FDesignerCall Setter;
	FProperty* SetterParam = nullptr;
	for (const FString& Name : SetterNames)
	{
		if (!Setter.Bind(Component, *Name)) continue;
		const TArray<FProperty*> Inputs = Setter.Inputs();
		if (Inputs.Num() == 1 && Inputs[0]->SameType(Prop)) { SetterParam = Inputs[0]; break; }
		Setter.Release();
	}

	const bool bWritable = WritableProperties(Component).Contains(PropName);
	if (!SetterParam && !bWritable)
	{
		return MCPError(FString::Printf(TEXT("'%s' on %s is read-only: it is not editable and has no setter. Writable: [%s]"),
			*PropName, *Component->GetClass()->GetName(), *FString::Join(WritableProperties(Component), TEXT(", "))));
	}

	const TSharedPtr<FJsonValue> Previous = PropJson(Component, Prop);
	const FString PreviousText = PropText(Component, Prop);

	FScopedTransaction Transaction(NSLOCTEXT("UE_MCP", "SetMaterialDesignerValue", "Set Material Designer Value"));
	Component->Modify();

	FString WriteError;
	FString Method;
	bool bBuildRequested = false;
	if (SetterParam)
	{
		if (!WriteJson(SetterParam, Setter.ValuePtr(SetterParam), Value, WriteError))
		{
			Transaction.Cancel();
			return MCPError(FString::Printf(TEXT("Could not convert value for %s: %s"), *PropName, *WriteError));
		}
		Setter.Invoke();
		Method = Setter.Function->GetName();
	}
	else
	{
		Component->PreEditChange(Prop);
		if (!WriteJson(Prop, Prop->ContainerPtrToValuePtr<void>(Component), Value, WriteError))
		{
			Transaction.Cancel();
			return MCPError(FString::Printf(TEXT("Could not convert value for %s: %s"), *PropName, *WriteError));
		}
		FPropertyChangedEvent Event(Prop, EPropertyChangeType::ValueSet);
		Component->PostEditChangeProperty(Event);
		if (Component->IsA(C.Component)) CallUpdate(Component, true);
		Method = TEXT("property");
	}
	if (OptionalBool(Params, TEXT("rebuild"), !SetterParam) && T.EditorOnlyData)
	{
		bBuildRequested = RequestBuild(T.EditorOnlyData);
	}
	Component->MarkPackageDirty();

	const bool bUnchanged = PropText(Component, Prop) == PreviousText;
	auto Result = MCPSuccess();
	AddTargetFields(Result, T);
	Result->SetStringField(TEXT("objectPath"), Component->GetPathName());
	Result->SetStringField(TEXT("class"), Component->GetClass()->GetName());
	const FString ComponentPath = CallString(Component, TEXT("GetComponentPath"));
	if (!ComponentPath.IsEmpty()) Result->SetStringField(TEXT("componentPath"), ComponentPath);
	Result->SetStringField(TEXT("propertyName"), PropName);
	Result->SetStringField(TEXT("method"), Method);
	Result->SetField(TEXT("previousValue"), Previous);
	Result->SetField(TEXT("value"), PropJson(Component, Prop));
	Result->SetBoolField(TEXT("buildRequested"), bBuildRequested);
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);
	if (!bUnchanged) MCPSetUpdated(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("objectPath"), Component->GetPathName());
	Payload->SetStringField(TEXT("propertyName"), PropName);
	Payload->SetField(TEXT("value"), Previous);
	MCPSetRollback(Result, TEXT("set_material_designer_value"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::AddMaterialDesignerLayer(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPMaterialDesigner;
	FDesignerClasses C;
	FString ClassError;
	if (!C.Load(ClassError)) return MCPError(ClassError);

	FDesignerTarget T;
	if (TSharedPtr<FJsonValue> Err = ResolveTarget(Params, C, T)) return Err;

	const FString MaterialProperty = OptionalString(Params, TEXT("materialProperty"), TEXT("BaseColor"));
	FString Error;
	UObject* Slot = ResolveSlot(T, Params, MaterialProperty, Error);
	if (!Slot) return MCPError(Error);

	FDesignerCall Call;
	if (!Call.Bind(Slot, TEXT("AddDefaultLayer")) || Call.Inputs().Num() != 1)
	{
		return MCPError(TEXT("This engine's Material Designer slot has no AddDefaultLayer(EDMMaterialPropertyType), so layers cannot be added through reflection."));
	}
	FProperty* PropertyParam = Call.Inputs()[0];
	UEnum* PropertyEnum = EnumOf(PropertyParam);
	int64 PropertyValue = 0;
	if (!MatchEnum(PropertyEnum, MaterialProperty, PropertyValue))
	{
		return MCPError(FString::Printf(TEXT("Unknown materialProperty '%s'. Valid: [%s]"), *MaterialProperty, *EnumNames(PropertyEnum)));
	}
	SetIntegral(PropertyParam, Call.ValuePtr(PropertyParam), PropertyValue);

	FScopedTransaction Transaction(NSLOCTEXT("UE_MCP", "AddMaterialDesignerLayer", "Add Material Designer Layer"));
	Slot->Modify();
	Call.Invoke();
	UObject* NewLayer = Call.ReturnObject();
	if (!NewLayer)
	{
		Transaction.Cancel();
		return MCPError(FString::Printf(TEXT("AddDefaultLayer(%s) returned no layer. The slot may not accept that material property: %s"),
			*MaterialProperty, *Slot->GetPathName()));
	}

	const FString NewName = OptionalString(Params, TEXT("layerName"));
	if (!NewName.IsEmpty())
	{
		FDesignerCall Rename;
		if (Rename.Bind(NewLayer, TEXT("SetLayerName")) && Rename.Inputs().Num() == 1)
		{
			if (FTextProperty* NameParam = CastField<FTextProperty>(Rename.Inputs()[0]))
			{
				NameParam->SetPropertyValue(Rename.ValuePtr(NameParam), FText::FromString(NewName));
				Rename.Invoke();
			}
		}
	}
	const bool bBuildRequested = RequestBuild(T.EditorOnlyData);
	Slot->MarkPackageDirty();

	FDescribeContext Ctx;
	Ctx.Classes = &C;
	const int32 LayerIndex = GetObjectArrayProp(Slot, TEXT("LayerObjects")).IndexOfByKey(NewLayer);
	auto Result = MCPSuccess();
	AddTargetFields(Result, T);
	Result->SetNumberField(TEXT("designerSlot"), GetObjectArrayProp(T.EditorOnlyData, TEXT("Slots")).IndexOfByKey(Slot));
	Result->SetObjectField(TEXT("layer"), DescribeLayer(NewLayer, LayerIndex, Ctx));
	Result->SetBoolField(TEXT("buildRequested"), bBuildRequested);
	MCPSetCreated(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("objectPath"), NewLayer->GetPathName());
	MCPSetRollback(Result, TEXT("remove_material_designer_layer"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::RemoveMaterialDesignerLayer(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPMaterialDesigner;
	FDesignerClasses C;
	FString ClassError;
	if (!C.Load(ClassError)) return MCPError(ClassError);

	FDesignerTarget T;
	TSharedPtr<FJsonValue> ResolveError;
	UObject* Layer = ResolveLayerTarget(Params, C, T, ResolveError);
	if (!Layer) return ResolveError;
	UObject* Slot = SlotOfLayer(Layer, C);
	if (!Slot) return MCPError(FString::Printf(TEXT("Layer '%s' has no owning slot."), *Layer->GetPathName()));

	const TArray<UObject*> Before = GetObjectArrayProp(Slot, TEXT("LayerObjects"));
	const int32 Index = Before.IndexOfByKey(Layer);
	if (Index == INDEX_NONE)
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("objectPath"), Layer->GetPathName());
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		MCPSetNoRollback(Result, TEXT("The layer was not in its slot's layer list, so nothing was removed."));
		return MCPResult(Result);
	}

	FDesignerCall CanRemove;
	if (CanRemove.Bind(Slot, TEXT("CanRemoveLayer")) && CanRemove.Inputs().Num() == 1)
	{
		if (FObjectPropertyBase* LayerParam = CastField<FObjectPropertyBase>(CanRemove.Inputs()[0]))
		{
			LayerParam->SetObjectPropertyValue(CanRemove.ValuePtr(LayerParam), Layer);
			CanRemove.Invoke();
			if (!CanRemove.ReturnBool(true))
			{
				return MCPError(FString::Printf(TEXT("Material Designer refuses to remove layer %d ('%s'): CanRemoveLayer is false. A slot keeps its base layer."), Index, *DesignerLayerName(Layer)));
			}
		}
	}

	FDesignerCall Remove;
	FObjectPropertyBase* RemoveParam = nullptr;
	if (Remove.Bind(Slot, TEXT("RemoveLayer")) && Remove.Inputs().Num() == 1)
	{
		RemoveParam = CastField<FObjectPropertyBase>(Remove.Inputs()[0]);
	}
	if (!RemoveParam)
	{
		return MCPError(TEXT("This engine's Material Designer slot has no RemoveLayer(Layer), so layers cannot be removed through reflection."));
	}
	const FString RemovedName = DesignerLayerName(Layer);
	const FString RemovedPath = Layer->GetPathName();

	FScopedTransaction Transaction(NSLOCTEXT("UE_MCP", "RemoveMaterialDesignerLayer", "Remove Material Designer Layer"));
	Slot->Modify();
	Layer->Modify();
	RemoveParam->SetObjectPropertyValue(Remove.ValuePtr(RemoveParam), Layer);
	Remove.Invoke();
	if (!Remove.ReturnBool(true))
	{
		Transaction.Cancel();
		return MCPError(FString::Printf(TEXT("RemoveLayer returned false for layer %d ('%s')."), Index, *RemovedName));
	}
	const bool bBuildRequested = T.EditorOnlyData ? RequestBuild(T.EditorOnlyData) : false;
	Slot->MarkPackageDirty();

	auto Result = MCPSuccess();
	if (T.Model) AddTargetFields(Result, T);
	Result->SetNumberField(TEXT("layerIndex"), Index);
	Result->SetStringField(TEXT("layerName"), RemovedName);
	Result->SetStringField(TEXT("objectPath"), RemovedPath);
	Result->SetNumberField(TEXT("layerCount"), GetObjectArrayProp(Slot, TEXT("LayerObjects")).Num());
	Result->SetBoolField(TEXT("buildRequested"), bBuildRequested);
	MCPSetUpdated(Result);

	// A removed layer's stages cannot be rebuilt from parameters, so the inverse is the editor undo of this transaction.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("steps"), 1);
	Payload->SetStringField(TEXT("direction"), TEXT("undo"));
	MCPSetRollback(Result, TEXT("undo_redo_steps"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterialDesigner(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPMaterialDesigner;
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Materials"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	FDesignerClasses C;
	FString ClassError;
	if (!C.Load(ClassError)) return MCPError(ClassError);

	// The Material Designer's own factory builds the instance together with
	// its model; a bare NewObject leaves MaterialModelBase empty.
	UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/DynamicMaterialEditor.DynamicMaterialInstanceFactory"));
	if (!FactoryClass || !FactoryClass->IsChildOf(UFactory::StaticClass()))
	{
		return MCPError(TEXT("Material Designer is not available: DynamicMaterialEditor.DynamicMaterialInstanceFactory is not loaded. Enable the DynamicMaterial plugin and restart the editor."));
	}
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("Material Designer material"), C.Instance, Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UObject* Instance = Created.Asset;

	UObject* Model = ResolveModel(Instance, C);
	if (!Model)
	{
		return MCPError(FString::Printf(TEXT("The Material Designer factory created '%s' without a model."), *Instance->GetPathName()));
	}
	UObject* EditorOnlyData = ResolveEditorOnlyData(Model, C);

	// A new model can start with no slots until the Material Designer wizard
	// runs; seed the Base Color slot so layers can be added straight away.
	bool bSeeded = false;
	if (EditorOnlyData && GetObjectArrayProp(EditorOnlyData, TEXT("Slots")).Num() == 0)
	{
		FDesignerCall AddSlot;
		if (AddSlot.Bind(EditorOnlyData, TEXT("AddSlotForMaterialProperty")) && AddSlot.Inputs().Num() == 1)
		{
			FProperty* PropertyParam = AddSlot.Inputs()[0];
			int64 BaseColor = 0;
			if (MatchEnum(EnumOf(PropertyParam), TEXT("BaseColor"), BaseColor))
			{
				SetIntegral(PropertyParam, AddSlot.ValuePtr(PropertyParam), BaseColor);
				AddSlot.Invoke();
				bSeeded = AddSlot.ReturnObject() != nullptr;
			}
		}
	}
	const bool bBuildRequested = EditorOnlyData ? RequestBuild(EditorOnlyData) : false;
	const bool bSaved = SaveAssetPackage(Instance);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Instance->GetPathName());
	Result->SetStringField(TEXT("objectPath"), Instance->GetPathName());
	Result->SetStringField(TEXT("modelPath"), Model->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetNumberField(TEXT("slotCount"), EditorOnlyData ? GetObjectArrayProp(EditorOnlyData, TEXT("Slots")).Num() : 0);
	Result->SetBoolField(TEXT("seededBaseColorSlot"), bSeeded);
	Result->SetBoolField(TEXT("buildRequested"), bBuildRequested);
	Result->SetBoolField(TEXT("saved"), bSaved);
	MCPSetDeleteAssetRollback(Result, Instance->GetPathName());
	return MCPResult(Result);
}
