#include "MetaHumanHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerFunctionCall.h"
#include "HandlerAssetCreate.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EditorSubsystem.h"
#include "Factories/Factory.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/UObjectGlobals.h"

// Every MetaHuman type is reached by its reflected path, so the bridge builds
// and loads with the plugin disabled. What reflection cannot reach is listed
// where it is reported: the cloud login state and the rig state are C++ only.
namespace UEMCP::MetaHuman
{
	const TCHAR* const SubsystemClass = TEXT("/Script/MetaHumanCharacterEditor.MetaHumanCharacterEditorSubsystem");
	const TCHAR* const CharacterClass = TEXT("/Script/MetaHumanCharacter.MetaHumanCharacter");
	const TCHAR* const FactoryClass = TEXT("/Script/MetaHumanCharacterEditor.MetaHumanCharacterFactoryNew");
	const TCHAR* const WardrobeItemClass = TEXT("/Script/MetaHumanCharacterPalette.MetaHumanWardrobeItem");

	const TCHAR* const LoginStateNote = TEXT(
		"The MetaHuman cloud login state is not readable here: the only API for it (CheckHasLoggedInUserAsync in MetaHumanSDKEditor) is C++ without reflection, "
		"and linking it would make the bridge depend on a plugin that is off by default. Cloud steps report it instead: request_rig and request_textures "
		"say loginRequired when the service refused for want of a login. request_rig runs first because it is the step that opens the Epic login.");

	bool PluginEnabled()
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MetaHumanCharacter"));
		return Plugin.IsValid() && Plugin->IsEnabled();
	}

	UClass* FindClass(const TCHAR* Path) { return FindObject<UClass>(nullptr, Path); }

	/** The editor subsystem, or the error saying why it is not there. */
	UObject* RequireSubsystem(TSharedPtr<FJsonValue>& OutError)
	{
		UClass* Cls = FindClass(SubsystemClass);
		UObject* Subsystem = (Cls && GEditor) ? GEditor->GetEditorSubsystemBase(Cls) : nullptr;
		if (!Subsystem)
		{
			OutError = MCPErrorWithCode(TEXT("PLUGIN_NOT_AVAILABLE"), FString::Printf(
				TEXT("MetaHuman plugin not available: the MetaHumanCharacter (MetaHuman Creator) plugin is %s. Enable it with project(enable_plugin, pluginName=\"MetaHumanCharacter\") and restart the editor."),
				PluginEnabled() ? TEXT("enabled but its editor module is not loaded") : TEXT("not enabled in this project")));
		}
		return Subsystem;
	}

	UObject* LoadCharacter(const FString& Path, TSharedPtr<FJsonValue>& OutError)
	{
		UObject* Obj = LoadAssetByPath<UObject>(Path);
		UClass* Cls = FindClass(CharacterClass);
		if (!Obj || !Cls || !Obj->IsA(Cls))
		{
			OutError = MCPAssetLoadError(Path, TEXT("MetaHumanCharacter"));
			return nullptr;
		}
		return Obj;
	}

	/** Call a subsystem function taking only InCharacter, returning the bool it returns (false when it returns none). */
	bool CallOnCharacter(UObject* Subsystem, const TCHAR* Function, UObject* Character, FString& OutError, bool* bOutCalled = nullptr)
	{
		FMCPReflectedCall Call;
		if (!Call.BindInstance(Subsystem, Function, OutError, TEXT("MetaHuman editor subsystem")))
		{
			if (bOutCalled) *bOutCalled = false;
			return false;
		}
		Call.SetObject(TEXT("InCharacter"), Character);
		Call.Invoke();
		if (bOutCalled) *bOutCalled = true;
		return Call.BoolReturn();
	}

	bool IsOpenForEdit(UObject* Subsystem, UObject* Character)
	{
		FString Err;
		return CallOnCharacter(Subsystem, TEXT("IsObjectAddedForEditing"), Character, Err);
	}

	/** Register the character for editing when it is not; most subsystem calls need it. */
	bool EnsureOpen(UObject* Subsystem, UObject* Character, bool& bOutOpenedHere, FString& OutError)
	{
		bOutOpenedHere = false;
		if (IsOpenForEdit(Subsystem, Character)) return true;
		if (!CallOnCharacter(Subsystem, TEXT("TryAddObjectToEdit"), Character, OutError))
		{
			if (OutError.IsEmpty()) OutError = TEXT("The MetaHuman editor subsystem refused to open the character for editing (TryAddObjectToEdit returned false)");
			return false;
		}
		bOutOpenedHere = true;
		return true;
	}

	void CloseIfOpenedHere(UObject* Subsystem, UObject* Character, bool bOpenedHere)
	{
		if (!bOpenedHere) return;
		FString Err;
		CallOnCharacter(Subsystem, TEXT("RemoveObjectToEdit"), Character, Err);
	}

	FProperty* FindProp(const UStruct* Struct, const TCHAR* Name) { return Struct ? Struct->FindPropertyByName(FName(Name)) : nullptr; }

	/** Set an enum field of a struct parameter by enumerator name (case-insensitive, prefix optional). */
	bool SetStructEnumByName(FMCPReflectedCall& Call, const TCHAR* Param, const TCHAR* Field, const FString& Name, FString& OutError)
	{
		FProperty* FieldProp = nullptr;
		void* Ptr = Call.StructField(Param, Field, FieldProp);
		UEnum* Enum = nullptr;
		FNumericProperty* Underlying = nullptr;
		if (FEnumProperty* EP = CastField<FEnumProperty>(FieldProp)) { Enum = EP->GetEnum(); Underlying = EP->GetUnderlyingProperty(); }
		else if (FByteProperty* BP = CastField<FByteProperty>(FieldProp)) { Enum = BP->Enum; Underlying = BP; }
		if (!Ptr || !Enum || !Underlying)
		{
			OutError = FString::Printf(TEXT("%s.%s is not an enum field in this engine build"), Param, Field);
			return false;
		}
		TArray<FString> Names;
		for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
		{
			const FString Short = Enum->GetNameStringByIndex(i);
			Names.Add(Short);
			if (Short.Equals(Name, ESearchCase::IgnoreCase) || Enum->GetDisplayNameTextByIndex(i).ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				Underlying->SetIntPropertyValue(Ptr, Enum->GetValueByIndex(i));
				return true;
			}
		}
		OutError = FString::Printf(TEXT("'%s' is not a %s. Use one of: %s"), *Name, *Enum->GetName(), *FString::Join(Names, TEXT(", ")));
		return false;
	}

	struct FConstraint
	{
		FString Name;
		bool bActive = false;
		double Target = 0.0;
		double Min = 0.0;
		double Max = 0.0;
	};

	/** Read FMetaHumanCharacterBodyConstraint elements out of a reflected array. */
	TArray<FConstraint> ReadConstraints(FArrayProperty* Array, const void* ArrayPtr)
	{
		TArray<FConstraint> Out;
		FStructProperty* Inner = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
		if (!Inner) return Out;
		FScriptArrayHelper Helper(Array, ArrayPtr);
		FNameProperty* NameP = CastField<FNameProperty>(FindProp(Inner->Struct, TEXT("Name")));
		FBoolProperty* ActiveP = CastField<FBoolProperty>(FindProp(Inner->Struct, TEXT("bIsActive")));
		FNumericProperty* TargetP = CastField<FNumericProperty>(FindProp(Inner->Struct, TEXT("TargetMeasurement")));
		FNumericProperty* MinP = CastField<FNumericProperty>(FindProp(Inner->Struct, TEXT("MinMeasurement")));
		FNumericProperty* MaxP = CastField<FNumericProperty>(FindProp(Inner->Struct, TEXT("MaxMeasurement")));
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			const uint8* Elem = Helper.GetRawPtr(i);
			FConstraint C;
			if (NameP) C.Name = NameP->GetPropertyValue_InContainer(Elem).ToString();
			if (ActiveP) C.bActive = ActiveP->GetPropertyValue_InContainer(Elem);
			if (TargetP) C.Target = TargetP->GetFloatingPointPropertyValue(TargetP->ContainerPtrToValuePtr<void>(Elem));
			if (MinP) C.Min = MinP->GetFloatingPointPropertyValue(MinP->ContainerPtrToValuePtr<void>(Elem));
			if (MaxP) C.Max = MaxP->GetFloatingPointPropertyValue(MaxP->ContainerPtrToValuePtr<void>(Elem));
			Out.Add(C);
		}
		return Out;
	}

	TSharedPtr<FJsonObject> ConstraintJson(const FConstraint& C)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), C.Name);
		Obj->SetBoolField(TEXT("active"), C.bActive);
		Obj->SetNumberField(TEXT("target"), C.Target);
		Obj->SetNumberField(TEXT("min"), C.Min);
		Obj->SetNumberField(TEXT("max"), C.Max);
		return Obj;
	}

	bool GetBodyConstraints(UObject* Subsystem, UObject* Character, TArray<FConstraint>& Out, FString& OutError)
	{
		FMCPReflectedCall Call;
		if (!Call.BindInstance(Subsystem, TEXT("GetBodyConstraints"), OutError)) return false;
		Call.SetObject(TEXT("InCharacter"), Character);
		Call.SetBool(TEXT("bScaleMeasurementRangesWithHeight"), true);
		Call.Invoke();
		FArrayProperty* Ret = CastField<FArrayProperty>(Call.ReturnParam());
		if (!Ret) { OutError = TEXT("GetBodyConstraints returned no array in this engine build"); return false; }
		Out = ReadConstraints(Ret, Ret->ContainerPtrToValuePtr<void>(Call.Frame.GetData()));
		return true;
	}

	/** The slot selections of the character's preview collection, as {slot, item} text. */
	TArray<TSharedPtr<FJsonValue>> SlotSelections(UObject* Subsystem, UObject* Character)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		FMCPReflectedCall GetCollection;
		FString Err;
		if (!GetCollection.BindInstance(Subsystem, TEXT("GetPreviewCollection"), Err)) return Out;
		GetCollection.SetObject(TEXT("InCharacter"), Character);
		GetCollection.Invoke();
		UObject* Collection = GetCollection.ReturnObject();
		FObjectPropertyBase* InstanceProp = Collection ? CastField<FObjectPropertyBase>(Collection->GetClass()->FindPropertyByName(TEXT("DefaultInstance"))) : nullptr;
		UObject* Instance = InstanceProp ? InstanceProp->GetObjectPropertyValue_InContainer(Collection) : nullptr;
		FMCPReflectedCall GetData;
		if (!Instance || !GetData.BindInstance(Instance, TEXT("GetSlotSelectionData"), Err)) return Out;
		GetData.Invoke();
		FArrayProperty* Ret = CastField<FArrayProperty>(GetData.ReturnParam());
		FStructProperty* Inner = Ret ? CastField<FStructProperty>(Ret->Inner) : nullptr;
		if (!Inner) return Out;
		FScriptArrayHelper Helper(Ret, Ret->ContainerPtrToValuePtr<void>(GetData.Frame.GetData()));
		FStructProperty* SelectionProp = CastField<FStructProperty>(FindProp(Inner->Struct, TEXT("Selection")));
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			const uint8* Elem = Helper.GetRawPtr(i);
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			if (SelectionProp)
			{
				const void* Sel = SelectionProp->ContainerPtrToValuePtr<void>(Elem);
				if (FNameProperty* Slot = CastField<FNameProperty>(FindProp(SelectionProp->Struct, TEXT("SlotName"))))
				{
					Row->SetStringField(TEXT("slot"), Slot->GetPropertyValue_InContainer(Sel).ToString());
				}
				if (FProperty* Item = FindProp(SelectionProp->Struct, TEXT("SelectedItem")))
				{
					FString Text;
					Item->ExportTextItem_Direct(Text, Item->ContainerPtrToValuePtr<void>(Sel), nullptr, nullptr, PPF_None);
					Row->SetStringField(TEXT("item"), Text);
				}
			}
			Out.Add(MakeShared<FJsonValueObject>(Row));
		}
		return Out;
	}

	int32 MapNum(UObject* Obj, const TCHAR* Name)
	{
		FMapProperty* Prop = CastField<FMapProperty>(Obj->GetClass()->FindPropertyByName(FName(Name)));
		if (!Prop) return -1;
		FScriptMapHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Obj));
		return Helper.Num();
	}

	bool BoolProp(UObject* Obj, const TCHAR* Name, bool& Out)
	{
		FBoolProperty* Prop = CastField<FBoolProperty>(Obj->GetClass()->FindPropertyByName(FName(Name)));
		if (!Prop) return false;
		Out = Prop->GetPropertyValue_InContainer(Obj);
		return true;
	}

	/** Texture and rig-adjacent state readable through the asset's own properties. */
	TSharedPtr<FJsonObject> AssetState(UObject* Character)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		bool B = false;
		if (BoolProp(Character, TEXT("bHasHighResolutionTextures"), B)) Obj->SetBoolField(TEXT("hasHighResolutionTextures"), B);
		if (BoolProp(Character, TEXT("bHasFaceDNABlendshapes"), B)) Obj->SetBoolField(TEXT("hasFaceDNABlendshapes"), B);
		if (BoolProp(Character, TEXT("bFixedBodyType"), B)) Obj->SetBoolField(TEXT("fixedBodyType"), B);
		Obj->SetNumberField(TEXT("synthesizedFaceTextureCount"), MapNum(Character, TEXT("SynthesizedFaceTextures")));
		Obj->SetNumberField(TEXT("bodyTextureCount"), MapNum(Character, TEXT("BodyTextures")));
		if (FEnumProperty* Template = CastField<FEnumProperty>(Character->GetClass()->FindPropertyByName(TEXT("TemplateType"))))
		{
			const int64 Raw = Template->GetUnderlyingProperty()->GetSignedIntPropertyValue(Template->ContainerPtrToValuePtr<void>(Character));
			Obj->SetStringField(TEXT("templateType"), Template->GetEnum()->GetNameStringByValue(Raw));
		}
		return Obj;
	}

	/** The cloud requests' refusal when this install runs the Creator with limited features. Without the
	 *  plugin's Optional content the editor subsystem has no cloud request state, and RequestAutoRigging
	 *  and RequestTextureSources assert on it and take the editor down (#1354). Null when it is present. */
	TSharedPtr<FJsonValue> CloudRequestsUnavailable(const TCHAR* Action)
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MetaHumanCharacter"));
		const FString Optional = Plugin.IsValid() ? FPaths::Combine(Plugin->GetContentDir(), TEXT("Optional")) : FString();
		if (!Optional.IsEmpty() && IFileManager::Get().DirectoryExists(*Optional)) return nullptr;
		TSharedPtr<FJsonObject> ErrObj = MCPErrorObject(FString::Printf(
			TEXT("%s needs the MetaHuman Creator's optional content, which this engine install does not have (%s), so the Creator runs with limited features and has no cloud request service. Install the MetaHuman optional content through the launcher, restart the editor, then retry."),
			Action, *Optional));
		ErrObj->SetStringField(TEXT("reason"), TEXT("optional_content_missing"));
		return MCPResult(ErrObj);
	}

	bool CanBuild(UObject* Subsystem, UObject* Character)
	{
		FMCPReflectedCall Call;
		FString Err;
		if (!Call.BindInstance(Subsystem, TEXT("CanBuildMetaHuman"), Err)) return false;
		Call.SetObject(TEXT("InCharacter"), Character);
		Call.SetBool(TEXT("bInLogError"), true);
		Call.Invoke();
		return Call.BoolReturn();
	}

	/** True when a captured cloud failure was for want of an Epic login. */
	bool SaysNotLoggedIn(const FMCPScopedLogCapture& Log)
	{
		return Log.Contains(TEXT("not logged in")) || Log.Contains(TEXT("LoginFailed")) || Log.Contains(TEXT("login failed")) || Log.Contains(TEXT("No logged in user"));
	}

	const TArray<FString>& LogCategories()
	{
		static const TArray<FString> Categories = { TEXT("LogMetaHuman"), TEXT("LogMetaHumanCharacter"), TEXT("LogMetaHumanSDK"), TEXT("LogMetaHumanCloud") };
		return Categories;
	}
}

// ─── Handlers ────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FMetaHumanHandlers::Status(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::MetaHuman;
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("pluginEnabled"), PluginEnabled());
	TSharedPtr<FJsonValue> Err;
	UObject* Subsystem = RequireSubsystem(Err);
	Result->SetBoolField(TEXT("available"), Subsystem != nullptr);
	Result->SetStringField(TEXT("loginState"), TEXT("unknown"));
	Result->SetStringField(TEXT("loginStateNote"), LoginStateNote);

	TArray<TSharedPtr<FJsonValue>> Characters;
	if (UClass* Cls = FindClass(CharacterClass))
	{
		TArray<FAssetData> Assets;
		IAssetRegistry::Get()->GetAssetsByClass(Cls->GetClassPathName(), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.PackageName.ToString().StartsWith(TEXT("/Game"))) continue;
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("assetPath"), Asset.GetObjectPathString());
			if (Subsystem && Asset.IsAssetLoaded()) Row->SetBoolField(TEXT("openForEdit"), IsOpenForEdit(Subsystem, Asset.GetAsset()));
			Characters.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	Result->SetArrayField(TEXT("characters"), Characters);
	if (!Subsystem) Result->SetStringField(TEXT("hint"), Err->AsObject()->GetStringField(TEXT("error")));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMetaHumanHandlers::CreateCharacter(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::MetaHuman;
	FString Name;
	TSharedPtr<FJsonValue> NameErr = RequireString(Params, TEXT("name"), Name);
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/MetaHumans"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	TSharedPtr<FJsonValue> Err;
	if (!RequireSubsystem(Err)) return Err;
	if (NameErr) return NameErr;
	UClass* Cls = FindClass(CharacterClass);
	UClass* FactoryCls = FindClass(FactoryClass);
	if (!Cls || !FactoryCls) return MCPError(TEXT("MetaHuman plugin not available: the MetaHumanCharacter asset class or its factory is not loaded. ") + MCPEnablePluginStep(TEXT("MetaHumanCharacter")));
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryCls);

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("MetaHumanCharacter"), Cls, Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UObject* Character = Created.Asset;

	// Saved like every other create, so the character survives the session that made it.
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Character, SaveReason);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, Character->GetPathName(), bSaved, SaveReason);
	Result->SetStringField(TEXT("assetPath"), Character->GetPathName());
	Result->SetObjectField(TEXT("state"), AssetState(Character));
	MCPSetDeleteAssetRollback(Result, Character->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMetaHumanHandlers::ReadCharacter(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::MetaHuman;
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("assetPath"), Path);
	const bool bIncludeBody = OptionalBool(Params, TEXT("includeBody"), true);

	TSharedPtr<FJsonValue> Err;
	UObject* Subsystem = RequireSubsystem(Err);
	if (!Subsystem) return Err;
	if (PathErr) return PathErr;
	UObject* Character = LoadCharacter(Path, Err);
	if (!Character) return Err;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Character->GetPathName());
	Result->SetObjectField(TEXT("state"), AssetState(Character));
	const bool bWasOpen = IsOpenForEdit(Subsystem, Character);
	Result->SetBoolField(TEXT("openForEdit"), bWasOpen);

	bool bOpenedHere = false;
	FString OpenErr;
	if (bIncludeBody && EnsureOpen(Subsystem, Character, bOpenedHere, OpenErr))
	{
		TArray<FConstraint> Constraints;
		FString CErr;
		if (GetBodyConstraints(Subsystem, Character, Constraints, CErr))
		{
			TArray<TSharedPtr<FJsonValue>> Rows;
			for (const FConstraint& C : Constraints) Rows.Add(MakeShared<FJsonValueObject>(ConstraintJson(C)));
			Result->SetArrayField(TEXT("bodyConstraints"), Rows);
		}
		Result->SetArrayField(TEXT("wardrobe"), SlotSelections(Subsystem, Character));
		Result->SetBoolField(TEXT("canBuild"), CanBuild(Subsystem, Character));
		CloseIfOpenedHere(Subsystem, Character, bOpenedHere);
	}
	else if (bIncludeBody)
	{
		Result->SetStringField(TEXT("bodyError"), OpenErr);
	}
	Result->SetStringField(TEXT("rigState"), TEXT("unknown"));
	Result->SetStringField(TEXT("rigStateNote"), TEXT("UMetaHumanCharacter::GetRigState and HasFaceDNA are C++ only, so the rig state is not readable through reflection. hasFaceDNABlendshapes and canBuild are the readable signals."));
	Result->SetStringField(TEXT("loginStateNote"), LoginStateNote);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMetaHumanHandlers::SetBody(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::MetaHuman;
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("assetPath"), Path);
	const TSharedPtr<FJsonObject>* ConstraintsParam = nullptr;
	const bool bHasConstraints = TryGetObjectParam(Params, TEXT("constraints"), ConstraintsParam) && ConstraintsParam && ConstraintsParam->IsValid();

	TSharedPtr<FJsonValue> Err;
	UObject* Subsystem = RequireSubsystem(Err);
	if (!Subsystem) return Err;
	if (PathErr) return PathErr;
	if (!bHasConstraints || (*ConstraintsParam)->Values.Num() == 0)
	{
		return MCPError(TEXT("Pass constraints as {Name: target} or {Name: {target, active}}, e.g. {\"Height\": 180, \"Muscularity\": 0.9}"));
	}
	UObject* Character = LoadCharacter(Path, Err);
	if (!Character) return Err;

	bool bOpenedHere = false;
	FString OpenErr;
	if (!EnsureOpen(Subsystem, Character, bOpenedHere, OpenErr)) return MCPError(OpenErr);

	// Get, edit by index, write back: the array comes back by value.
	FMCPReflectedCall Get;
	FString CallErr;
	if (!Get.BindInstance(Subsystem, TEXT("GetBodyConstraints"), CallErr)) return MCPError(CallErr);
	Get.SetObject(TEXT("InCharacter"), Character);
	Get.SetBool(TEXT("bScaleMeasurementRangesWithHeight"), true);
	Get.Invoke();
	FArrayProperty* GetRet = CastField<FArrayProperty>(Get.ReturnParam());
	if (!GetRet) return MCPError(TEXT("GetBodyConstraints returned no array in this engine build"));
	const TArray<FConstraint> Before = ReadConstraints(GetRet, GetRet->ContainerPtrToValuePtr<void>(Get.Frame.GetData()));

	FMCPReflectedCall Set;
	if (!Set.BindInstance(Subsystem, TEXT("SetBodyConstraints"), CallErr)) return MCPError(CallErr);
	Set.SetObject(TEXT("InCharacter"), Character);
	FArrayProperty* SetArr = CastField<FArrayProperty>(Set.Param(TEXT("InBodyConstraints")));
	FStructProperty* Inner = SetArr ? CastField<FStructProperty>(SetArr->Inner) : nullptr;
	if (!SetArr || !Inner) return MCPError(TEXT("SetBodyConstraints takes no InBodyConstraints array in this engine build"));
	void* SetArrPtr = SetArr->ContainerPtrToValuePtr<void>(Set.Frame.GetData());
	SetArr->CopyCompleteValue(SetArrPtr, GetRet->ContainerPtrToValuePtr<void>(Get.Frame.GetData()));

	FNameProperty* NameP = CastField<FNameProperty>(FindProp(Inner->Struct, TEXT("Name")));
	FBoolProperty* ActiveP = CastField<FBoolProperty>(FindProp(Inner->Struct, TEXT("bIsActive")));
	FNumericProperty* TargetP = CastField<FNumericProperty>(FindProp(Inner->Struct, TEXT("TargetMeasurement")));
	if (!NameP || !ActiveP || !TargetP) return MCPError(TEXT("FMetaHumanCharacterBodyConstraint has no Name/bIsActive/TargetMeasurement in this engine build"));

	FScriptArrayHelper Helper(SetArr, SetArrPtr);
	TArray<FString> Unknown;
	TArray<FString> Applied;
	for (const auto& Pair : (*ConstraintsParam)->Values)
	{
		const FString Wanted(Pair.Key);
		double Target = 0.0;
		bool bActive = true;
		bool bHasTarget = false;
		const TSharedPtr<FJsonObject>* Spec = nullptr;
		if (Pair.Value->TryGetNumber(Target)) bHasTarget = true;
		else if (Pair.Value->TryGetObject(Spec) && Spec)
		{
			bHasTarget = (*Spec)->TryGetNumberField(TEXT("target"), Target);
			(*Spec)->TryGetBoolField(TEXT("active"), bActive);
		}
		int32 Found = INDEX_NONE;
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			if (NameP->GetPropertyValue_InContainer(Helper.GetRawPtr(i)).ToString().Equals(Wanted, ESearchCase::IgnoreCase)) { Found = i; break; }
		}
		if (Found == INDEX_NONE) { Unknown.Add(Wanted); continue; }
		uint8* Elem = Helper.GetRawPtr(Found);
		ActiveP->SetPropertyValue_InContainer(Elem, bActive);
		if (bHasTarget) TargetP->SetFloatingPointPropertyValue(TargetP->ContainerPtrToValuePtr<void>(Elem), Target);
		Applied.Add(Wanted);
	}
	if (Unknown.Num() > 0)
	{
		TArray<FString> Names;
		for (const FConstraint& C : Before) Names.Add(C.Name);
		CloseIfOpenedHere(Subsystem, Character, bOpenedHere);
		return MCPError(FString::Printf(TEXT("Unknown body constraint(s): %s. Constraints: %s"), *FString::Join(Unknown, TEXT(", ")), *FString::Join(Names, TEXT(", "))));
	}

	Character->Modify();
	Set.Invoke();
	CallOnCharacter(Subsystem, TEXT("CommitBodyState"), Character, CallErr);
	Character->MarkPackageDirty();

	TArray<FConstraint> After;
	GetBodyConstraints(Subsystem, Character, After, CallErr);
	TArray<TSharedPtr<FJsonValue>> Changes;
	TSharedPtr<FJsonObject> Previous = MakeShared<FJsonObject>();
	for (const FString& Name : Applied)
	{
		const FConstraint* B = Before.FindByPredicate([&Name](const FConstraint& C) { return C.Name.Equals(Name, ESearchCase::IgnoreCase); });
		const FConstraint* A = After.FindByPredicate([&Name](const FConstraint& C) { return C.Name.Equals(Name, ESearchCase::IgnoreCase); });
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Name);
		if (B) Row->SetObjectField(TEXT("before"), ConstraintJson(*B));
		if (A) Row->SetObjectField(TEXT("after"), ConstraintJson(*A));
		Changes.Add(MakeShared<FJsonValueObject>(Row));
		if (B)
		{
			TSharedPtr<FJsonObject> Prior = MakeShared<FJsonObject>();
			Prior->SetNumberField(TEXT("target"), B->Target);
			Prior->SetBoolField(TEXT("active"), B->bActive);
			Previous->SetObjectField(B->Name, Prior);
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), Character->GetPathName());
	Result->SetArrayField(TEXT("constraints"), Changes);
	Result->SetBoolField(TEXT("openForEdit"), true);
	Result->SetStringField(TEXT("note"), TEXT("A target is the solver's goal; the read-back 'after' target is what the body model accepted within its min/max range."));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Character->GetPathName());
	Payload->SetObjectField(TEXT("constraints"), Previous);
	MCPSetRollback(Result, TEXT("metahuman_set_body"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMetaHumanHandlers::SetWardrobe(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::MetaHuman;
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("assetPath"), Path);
	const TSharedPtr<FJsonObject>* ItemsParam = nullptr;
	const bool bHasItems = TryGetObjectParam(Params, TEXT("items"), ItemsParam) && ItemsParam && ItemsParam->IsValid();

	TSharedPtr<FJsonValue> Err;
	UObject* Subsystem = RequireSubsystem(Err);
	if (!Subsystem) return Err;
	if (PathErr) return PathErr;
	if (!bHasItems || (*ItemsParam)->Values.Num() == 0)
	{
		return MCPError(TEXT("Pass items as {slotName: wardrobeItemPath}, e.g. {\"Top Garment\": \"/Game/Fab/MetaHuman/WI_Crewneck\"}"));
	}
	UObject* Character = LoadCharacter(Path, Err);
	if (!Character) return Err;
	UClass* ItemClass = FindClass(WardrobeItemClass);

	// Resolve every item before changing anything.
	TArray<TPair<FName, UObject*>> Wanted;
	for (const auto& Pair : (*ItemsParam)->Values)
	{
		FString ItemPath;
		if (!Pair.Value->TryGetString(ItemPath)) return MCPError(FString::Printf(TEXT("items['%s'] must be a wardrobe item asset path"), *FString(Pair.Key)));
		UObject* Item = LoadAssetByPath<UObject>(ItemPath);
		if (!Item || (ItemClass && !Item->IsA(ItemClass))) return MCPAssetLoadError(ItemPath, TEXT("MetaHumanWardrobeItem"));
		Wanted.Add({ FName(*FString(Pair.Key)), Item });
	}

	bool bOpenedHere = false;
	FString CallErr;
	if (!EnsureOpen(Subsystem, Character, bOpenedHere, CallErr)) return MCPError(CallErr);

	FMCPReflectedCall GetCollection;
	if (!GetCollection.BindInstance(Subsystem, TEXT("GetPreviewCollection"), CallErr)) return MCPError(CallErr);
	GetCollection.SetObject(TEXT("InCharacter"), Character);
	GetCollection.Invoke();
	UObject* Collection = GetCollection.ReturnObject();
	FObjectPropertyBase* InstanceProp = Collection ? CastField<FObjectPropertyBase>(Collection->GetClass()->FindPropertyByName(TEXT("DefaultInstance"))) : nullptr;
	UObject* Instance = InstanceProp ? InstanceProp->GetObjectPropertyValue_InContainer(Collection) : nullptr;
	if (!Collection || !Instance) return MCPError(TEXT("The character has no preview collection with a default instance to dress"));

	const TArray<TSharedPtr<FJsonValue>> Before = SlotSelections(Subsystem, Character);
	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Failures = 0;
	Collection->Modify();
	Instance->Modify();
	for (const TPair<FName, UObject*>& Entry : Wanted)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("slot"), Entry.Key.ToString());
		Row->SetStringField(TEXT("item"), Entry.Value->GetPathName());
		FMCPReflectedCall Add;
		if (!Add.BindInstance(Collection, TEXT("TryAddItemFromWardrobeItem"), CallErr)) return MCPError(CallErr);
		Add.SetName(TEXT("SlotName"), Entry.Key);
		Add.SetObject(TEXT("WardrobeItem"), Entry.Value);
		Add.Invoke();
		if (!Add.BoolReturn())
		{
			Row->SetBoolField(TEXT("applied"), false);
			Row->SetStringField(TEXT("error"), TEXT("The collection refused the item for that slot (TryAddItemFromWardrobeItem returned false): check the slot name and that the item fits it."));
			++Failures;
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}
		FStructProperty* KeyOut = CastField<FStructProperty>(Add.Param(TEXT("OutNewItemKey")));
		FMCPReflectedCall Select;
		if (!KeyOut || !Select.BindInstance(Instance, TEXT("SetSingleSlotSelection"), CallErr)) return MCPError(CallErr.IsEmpty() ? FString(TEXT("TryAddItemFromWardrobeItem has no OutNewItemKey")) : CallErr);
		FStructProperty* KeyIn = CastField<FStructProperty>(Select.Param(TEXT("ItemKey")));
		if (!KeyIn || KeyIn->Struct != KeyOut->Struct) return MCPError(TEXT("SetSingleSlotSelection takes no matching ItemKey in this engine build"));
		Select.SetName(TEXT("SlotName"), Entry.Key);
		KeyIn->Struct->CopyScriptStruct(KeyIn->ContainerPtrToValuePtr<void>(Select.Frame.GetData()), KeyOut->ContainerPtrToValuePtr<void>(Add.Frame.GetData()));
		Select.Invoke();
		Row->SetBoolField(TEXT("applied"), true);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	CallOnCharacter(Subsystem, TEXT("OnEditPreviewCollection"), Character, CallErr);
	Character->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), Character->GetPathName());
	Result->SetArrayField(TEXT("items"), Rows);
	Result->SetArrayField(TEXT("selectionsBefore"), Before);
	Result->SetArrayField(TEXT("selections"), SlotSelections(Subsystem, Character));
	if (Failures > 0) Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("%d item(s) were not applied; see items"), Failures));
	MCPSetNoRollback(Result, TEXT("Adding a wardrobe item also adds it to the character's collection, and the previous selections are item keys no action takes back in, so the dressing is not undone by a call."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMetaHumanHandlers::RequestRig(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::MetaHuman;
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("assetPath"), Path);
	const FString RigType = OptionalString(Params, TEXT("rigType"), TEXT("JointsOnly"));
	const bool bBlocking = OptionalBool(Params, TEXT("blocking"), true);

	TSharedPtr<FJsonValue> Err;
	UObject* Subsystem = RequireSubsystem(Err);
	if (!Subsystem) return Err;
	if (PathErr) return PathErr;
	UObject* Character = LoadCharacter(Path, Err);
	if (!Character) return Err;
	bool bOpenedHere = false;
	FString CallErr;
	if (TSharedPtr<FJsonValue> Unavailable = CloudRequestsUnavailable(TEXT("request_rig"))) return Unavailable;
	if (!EnsureOpen(Subsystem, Character, bOpenedHere, CallErr)) return MCPError(CallErr);

	FMCPReflectedCall Call;
	if (!Call.BindInstance(Subsystem, TEXT("RequestAutoRigging"), CallErr)) return MCPError(CallErr);
	Call.SetObject(TEXT("InCharacter"), Character);
	if (!SetStructEnumByName(Call, TEXT("InParams"), TEXT("RigType"), RigType, CallErr)) return MCPError(CallErr);
	Call.SetStructBool(TEXT("InParams"), TEXT("bBlocking"), bBlocking);
	Call.SetStructBool(TEXT("InParams"), TEXT("bReportProgress"), false);
	bool bBlendshapesBefore = false;
	BoolProp(Character, TEXT("bHasFaceDNABlendshapes"), bBlendshapesBefore);
	FMCPScopedLogCapture Log(LogCategories());
	Call.Invoke();

	const bool bLoginRequired = SaysNotLoggedIn(Log);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Character->GetPathName());
	Result->SetStringField(TEXT("rigType"), RigType);
	Result->SetBoolField(TEXT("blocking"), bBlocking);
	Result->SetBoolField(TEXT("loginRequired"), bLoginRequired);
	Result->SetStringField(TEXT("loginState"), bLoginRequired ? TEXT("not-logged-in") : TEXT("unknown"));
	Result->SetObjectField(TEXT("state"), AssetState(Character));
	Result->SetBoolField(TEXT("hadFaceDNABlendshapes"), bBlendshapesBefore);
	Result->SetBoolField(TEXT("canBuild"), CanBuild(Subsystem, Character));
	Result->SetArrayField(TEXT("log"), Log.ProblemsJson());
	if (bLoginRequired)
	{
		TSharedPtr<FJsonObject> ErrObj = MCPErrorObject(TEXT("The MetaHuman cloud service refused the auto-rig request because no Epic account is logged in. Open the MetaHuman Creator, log in when it prompts, then call request_rig again."));
		ErrObj->SetStringField(TEXT("loginState"), TEXT("not-logged-in"));
		ErrObj->SetArrayField(TEXT("log"), Log.ProblemsJson());
		return MCPResult(ErrObj);
	}
	if (!bBlocking) Result->SetStringField(TEXT("note"), TEXT("The request was queued and returns before the rig arrives; call read_character for canBuild."));
	MCPSetUpdated(Result);
	MCPSetNoRollback(Result, TEXT("Auto-rigging replaces the face rig with the cloud service's result; the previous rig is not kept for a call to restore."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMetaHumanHandlers::RequestTextures(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::MetaHuman;
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("assetPath"), Path);
	const bool bBlocking = OptionalBool(Params, TEXT("blocking"), true);

	TSharedPtr<FJsonValue> Err;
	UObject* Subsystem = RequireSubsystem(Err);
	if (!Subsystem) return Err;
	if (PathErr) return PathErr;
	UObject* Character = LoadCharacter(Path, Err);
	if (!Character) return Err;
	bool bOpenedHere = false;
	FString CallErr;
	if (TSharedPtr<FJsonValue> Unavailable = CloudRequestsUnavailable(TEXT("request_textures"))) return Unavailable;
	if (!EnsureOpen(Subsystem, Character, bOpenedHere, CallErr)) return MCPError(CallErr);

	FMCPReflectedCall Call;
	if (!Call.BindInstance(Subsystem, TEXT("RequestTextureSources"), CallErr)) return MCPError(CallErr);
	Call.SetObject(TEXT("InCharacter"), Character);
	Call.SetStructBool(TEXT("InParams"), TEXT("bBlocking"), bBlocking);
	Call.SetStructBool(TEXT("InParams"), TEXT("bReportProgress"), false);
	const TSharedPtr<FJsonObject> Before = AssetState(Character);
	FMCPScopedLogCapture Log(LogCategories());
	Call.Invoke();

	if (SaysNotLoggedIn(Log))
	{
		TSharedPtr<FJsonObject> ErrObj = MCPErrorObject(TEXT("The MetaHuman cloud service refused the texture request because no Epic account is logged in. Run request_rig first: it is the step that opens the Epic login."));
		ErrObj->SetStringField(TEXT("loginState"), TEXT("not-logged-in"));
		ErrObj->SetArrayField(TEXT("log"), Log.ProblemsJson());
		return MCPResult(ErrObj);
	}
	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), Character->GetPathName());
	Result->SetBoolField(TEXT("blocking"), bBlocking);
	Result->SetObjectField(TEXT("stateBefore"), Before);
	Result->SetObjectField(TEXT("state"), AssetState(Character));
	Result->SetStringField(TEXT("loginState"), TEXT("unknown"));
	Result->SetArrayField(TEXT("log"), Log.ProblemsJson());
	if (!bBlocking) Result->SetStringField(TEXT("note"), TEXT("The request was queued and returns before the textures arrive; call read_character for hasHighResolutionTextures."));
	MCPSetNoRollback(Result, TEXT("The requested texture sources replace the character's synthesized textures; the previous ones are not kept for a call to restore."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMetaHumanHandlers::Assemble(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::MetaHuman;
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("assetPath"), Path);
	const FString PipelineType = OptionalString(Params, TEXT("pipelineType"), TEXT("Optimized"));
	const FString Quality = OptionalString(Params, TEXT("quality"), TEXT("Medium"));
	const FString NameOverride = OptionalString(Params, TEXT("nameOverride"));
	const FString CommonFolder = OptionalString(Params, TEXT("commonFolderPath"));

	TSharedPtr<FJsonValue> Err;
	UObject* Subsystem = RequireSubsystem(Err);
	if (!Subsystem) return Err;
	if (PathErr) return PathErr;
	if (auto PieErr = MCPRefuseDuringPlayInEditor(TEXT("metahuman_assemble"))) return PieErr;
	UObject* Character = LoadCharacter(Path, Err);
	if (!Character) return Err;
	bool bOpenedHere = false;
	FString CallErr;
	if (!EnsureOpen(Subsystem, Character, bOpenedHere, CallErr)) return MCPError(CallErr);

	FMCPReflectedCall Call;
	if (!Call.BindInstance(Subsystem, TEXT("BuildMetaHuman"), CallErr)) return MCPError(CallErr);
	Call.SetObject(TEXT("InCharacter"), Character);
	if (!SetStructEnumByName(Call, TEXT("InParams"), TEXT("PipelineType"), PipelineType, CallErr)) return MCPError(CallErr);
	if (!SetStructEnumByName(Call, TEXT("InParams"), TEXT("PipelineQuality"), Quality, CallErr)) return MCPError(CallErr);
	auto SetStructString = [&Call](const TCHAR* Field, const FString& Value)
	{
		FProperty* FieldProp = nullptr;
		void* Ptr = Call.StructField(TEXT("InParams"), Field, FieldProp);
		if (FStrProperty* S = CastField<FStrProperty>(FieldProp)) { if (Ptr) S->SetPropertyValue(Ptr, Value); }
	};
	if (!NameOverride.IsEmpty()) SetStructString(TEXT("NameOverride"), NameOverride);
	if (!CommonFolder.IsEmpty()) SetStructString(TEXT("CommonFolderPath"), CommonFolder);

	FMCPScopedLogCapture Log(LogCategories());
	if (!CanBuild(Subsystem, Character))
	{
		TSharedPtr<FJsonObject> ErrObj = MCPErrorObject(TEXT("The character cannot be assembled yet (CanBuildMetaHuman returned false). log holds the subsystem's reason; a missing rig or textures is fixed by request_rig and request_textures."));
		ErrObj->SetArrayField(TEXT("log"), Log.ProblemsJson());
		return MCPResult(ErrObj);
	}

	// Collect what the build writes, since it does not return its outputs.
	TArray<FString> NewAssets;
	FDelegateHandle Handle = IAssetRegistry::Get()->OnAssetAdded().AddLambda([&NewAssets](const FAssetData& Asset)
	{
		if (NewAssets.Num() < 2000) NewAssets.Add(Asset.GetObjectPathString());
	});
	const double Start = FPlatformTime::Seconds();
	Call.Invoke();
	IAssetRegistry::Get()->OnAssetAdded().Remove(Handle);
	const double Seconds = FPlatformTime::Seconds() - Start;

	const bool bFailed = Log.Contains(TEXT("assembly failed")) || Log.Contains(TEXT("failed to assemble"));
	if (bFailed)
	{
		TSharedPtr<FJsonObject> ErrObj = MCPErrorObject(TEXT("MetaHuman assembly failed; log holds the pipeline's reason"));
		ErrObj->SetArrayField(TEXT("log"), Log.ProblemsJson());
		return MCPResult(ErrObj);
	}

	TArray<TSharedPtr<FJsonValue>> Meshes, Blueprints, Others;
	for (const FString& Asset : NewAssets)
	{
		FAssetData Data = IAssetRegistry::Get()->GetAssetByObjectPath(FSoftObjectPath(Asset));
		const FString ClassName = Data.AssetClassPath.GetAssetName().ToString();
		if (ClassName == TEXT("SkeletalMesh")) Meshes.Add(MakeShared<FJsonValueString>(Asset));
		else if (ClassName == TEXT("Blueprint")) Blueprints.Add(MakeShared<FJsonValueString>(Asset));
		else if (Others.Num() < 200) Others.Add(MakeShared<FJsonValueString>(Asset));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), Character->GetPathName());
	Result->SetStringField(TEXT("pipelineType"), PipelineType);
	Result->SetStringField(TEXT("quality"), Quality);
	Result->SetNumberField(TEXT("seconds"), Seconds);
	Result->SetBoolField(TEXT("succeededInLog"), Log.Contains(TEXT("assembly succeeded")));
	Result->SetNumberField(TEXT("createdAssetCount"), NewAssets.Num());
	Result->SetArrayField(TEXT("skeletalMeshes"), Meshes);
	Result->SetArrayField(TEXT("blueprints"), Blueprints);
	Result->SetArrayField(TEXT("otherAssets"), Others);
	Result->SetArrayField(TEXT("log"), Log.ProblemsJson());
	MCPSetNoRollback(Result, TEXT("Assembly writes the skeletal meshes, blueprint and their dependencies listed here; no single call removes that set. asset(delete) on those paths undoes it by hand."));
	return MCPResult(Result);
}

// ─── Registration ────────────────────────────────────────────────────────────

void FMetaHumanHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("metahuman"));
	using EType = EMCPParamType;
	auto AssetPathParam = []()
	{
		return MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaHumanCharacter asset path")).Alias(TEXT("characterPath"));
	};

	Registry.RegisterHandler(TEXT("metahuman_status"), &Status, {});
	Registry.RegisterHandler(TEXT("metahuman_create_character"), &CreateCharacter, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Name of the new MetaHumanCharacter asset")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/MetaHumans)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns an existing asset untouched, error refuses")),
	}, MCPSpec::ContractExempt(TEXT("creates a MetaHumanCharacter asset")));
	Registry.RegisterHandlerWithTimeout(TEXT("metahuman_read_character"), &ReadCharacter, 300.0f, {
		AssetPathParam(),
		MCPParam::Optional(TEXT("includeBody"), EType::Boolean, TEXT("Open the character for editing to read body constraints, wardrobe and canBuild (default true)")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("metahuman_set_body"), &SetBody, 300.0f, {
		AssetPathParam(),
		MCPParam::Required(TEXT("constraints"), EType::Object, TEXT("{ConstraintName: target} or {ConstraintName: {target, active}}, e.g. {\"Height\": 180, \"Muscularity\": 0.9}")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("metahuman_set_wardrobe"), &SetWardrobe, 300.0f, {
		AssetPathParam(),
		MCPParam::Required(TEXT("items"), EType::Object, TEXT("{slotName: wardrobeItemPath}, e.g. {\"Top Garment\": \"/Game/Fab/MetaHuman/WI_Crewneck\"}")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("metahuman_request_rig"), &RequestRig, 900.0f, {
		AssetPathParam(),
		MCPParam::Optional(TEXT("rigType"), EType::String, TEXT("JointsOnly (default) | JointsAndBlendshapes")),
		MCPParam::Optional(TEXT("blocking"), EType::Boolean, TEXT("Wait for the cloud service to return the rig (default true)")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("metahuman_request_textures"), &RequestTextures, 900.0f, {
		AssetPathParam(),
		MCPParam::Optional(TEXT("blocking"), EType::Boolean, TEXT("Wait for the cloud service to return the textures (default true)")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("metahuman_assemble"), &Assemble, 900.0f, {
		AssetPathParam(),
		MCPParam::Optional(TEXT("pipelineType"), EType::String, TEXT("Cinematic | Optimized (default) | UEFN")),
		MCPParam::Optional(TEXT("quality"), EType::String, TEXT("Pipeline quality level, e.g. Cinematic | High | Medium (default) | Low")),
		MCPParam::Optional(TEXT("nameOverride"), EType::String, TEXT("Name for the assembled assets instead of the character's")),
		MCPParam::Optional(TEXT("commonFolderPath"), EType::String, TEXT("Content folder for the shared MetaHuman assets")),
	});
}
