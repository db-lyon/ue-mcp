#include "ReflectionHandlers.h"
#include "HandlerRegistry.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "GameplayTagContainer.h"
#include "Misc/ConfigCacheIni.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonSerializer.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void FReflectionHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("reflect_class"), &ReflectClass);
	Registry.RegisterHandler(TEXT("reflect_struct"), &ReflectStruct);
	Registry.RegisterHandler(TEXT("reflect_enum"), &ReflectEnum);
	Registry.RegisterHandler(TEXT("list_classes"), &ListClasses);
	Registry.RegisterHandler(TEXT("list_gameplay_tags"), &ListGameplayTags);
	Registry.RegisterHandler(TEXT("create_gameplay_tag"), &CreateGameplayTag);
}

TSharedPtr<FJsonValue> FReflectionHandlers::ReflectClass(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (!Params->TryGetStringField(TEXT("className"), ClassName))
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("error"), TEXT("className parameter required"));
		return MakeShared<FJsonValueObject>(Error);
	}

	UClass* Class = FindClass(ClassName);
	if (!Class)
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("error"), FString::Printf(TEXT("Class not found: %s"), *ClassName));
		return MakeShared<FJsonValueObject>(Error);
	}

	bool bIncludeInherited = false;
	Params->TryGetBoolField(TEXT("includeInherited"), bIncludeInherited);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("className"), Class->GetName());

	if (Class->GetSuperClass())
	{
		Result->SetStringField(TEXT("parentClass"), Class->GetSuperClass()->GetName());
	}

	// Build parent chain
	TArray<TSharedPtr<FJsonValue>> ParentChain;
	UClass* Parent = Class->GetSuperClass();
	while (Parent)
	{
		ParentChain.Add(MakeShared<FJsonValueString>(Parent->GetName()));
		Parent = Parent->GetSuperClass();
	}
	Result->SetArrayField(TEXT("parentChain"), ParentChain);

	Result->SetBoolField(TEXT("isAbstract"), Class->HasAnyClassFlags(CLASS_Abstract));

	// Get properties
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;
	for (TFieldIterator<FProperty> PropIt(Class, bIncludeInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Prop->GetName());
		PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());
		PropertiesArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}
	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	Result->SetNumberField(TEXT("propertyCount"), PropertiesArray.Num());

	// Get functions
	TArray<TSharedPtr<FJsonValue>> FunctionsArray;
	for (TFieldIterator<UFunction> FuncIt(Class, bIncludeInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
	{
		UFunction* Func = *FuncIt;
		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Func->GetName());
		FunctionsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
	}
	Result->SetArrayField(TEXT("functions"), FunctionsArray);
	Result->SetNumberField(TEXT("functionCount"), FunctionsArray.Num());

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::ReflectStruct(const TSharedPtr<FJsonObject>& Params)
{
	FString StructName;
	if (!Params->TryGetStringField(TEXT("structName"), StructName))
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("error"), TEXT("structName parameter required"));
		return MakeShared<FJsonValueObject>(Error);
	}

	UScriptStruct* Struct = FindStruct(StructName);
	if (!Struct)
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("error"), FString::Printf(TEXT("Struct not found: %s"), *StructName));
		return MakeShared<FJsonValueObject>(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("structName"), Struct->GetName());

	TArray<TSharedPtr<FJsonValue>> FieldsArray;
	for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		TSharedPtr<FJsonObject> FieldObj = MakeShared<FJsonObject>();
		FieldObj->SetStringField(TEXT("name"), Prop->GetName());
		FieldObj->SetStringField(TEXT("type"), Prop->GetCPPType());
		FieldsArray.Add(MakeShared<FJsonValueObject>(FieldObj));
	}
	Result->SetArrayField(TEXT("fields"), FieldsArray);
	Result->SetNumberField(TEXT("fieldCount"), FieldsArray.Num());

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::ReflectEnum(const TSharedPtr<FJsonObject>& Params)
{
	FString EnumName;
	if (!Params->TryGetStringField(TEXT("enumName"), EnumName))
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("error"), TEXT("enumName parameter required"));
		return MakeShared<FJsonValueObject>(Error);
	}

	UEnum* Enum = FindEnum(EnumName);
	if (!Enum)
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("error"), FString::Printf(TEXT("Enum not found: %s"), *EnumName));
		return MakeShared<FJsonValueObject>(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("enumName"), Enum->GetName());

	TArray<TSharedPtr<FJsonValue>> ValuesArray;
	int32 NumEnums = Enum->NumEnums();
	for (int32 i = 0; i < NumEnums - 1; ++i) // -1 to exclude _MAX
	{
		FString EnumNameStr = Enum->GetNameStringByIndex(i);
		if (!EnumNameStr.IsEmpty() && !EnumNameStr.EndsWith(TEXT("_MAX")))
		{
			TSharedPtr<FJsonObject> ValueObj = MakeShared<FJsonObject>();
			ValueObj->SetStringField(TEXT("name"), EnumNameStr);
			ValueObj->SetNumberField(TEXT("value"), Enum->GetValueByIndex(i));
			ValueObj->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(i).ToString());
			ValuesArray.Add(MakeShared<FJsonValueObject>(ValueObj));
		}
	}
	Result->SetArrayField(TEXT("values"), ValuesArray);
	Result->SetNumberField(TEXT("valueCount"), ValuesArray.Num());

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::ListClasses(const TSharedPtr<FJsonObject>& Params)
{
	FString ParentFilter;
	Params->TryGetStringField(TEXT("parentFilter"), ParentFilter);

	int32 Limit = 100;
	Params->TryGetNumberField(TEXT("limit"), Limit);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!ParentFilter.IsEmpty())
	{
		UClass* ParentClass = FindClass(ParentFilter);
		if (!ParentClass)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent class not found: %s"), *ParentFilter));
			return MakeShared<FJsonValueObject>(Result);
		}

		TArray<TSharedPtr<FJsonValue>> ClassesArray;
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (Class && Class->IsChildOf(ParentClass))
			{
				TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
				ClassObj->SetStringField(TEXT("name"), Class->GetName());
				if (Class->GetSuperClass())
				{
					ClassObj->SetStringField(TEXT("parent"), Class->GetSuperClass()->GetName());
				}
				ClassesArray.Add(MakeShared<FJsonValueObject>(ClassObj));
				if (ClassesArray.Num() >= Limit)
				{
					break;
				}
			}
		}
		Result->SetStringField(TEXT("parentFilter"), ParentFilter);
		Result->SetArrayField(TEXT("classes"), ClassesArray);
		Result->SetNumberField(TEXT("count"), ClassesArray.Num());
	}
	else
	{
		// List common base classes
		TArray<FString> CommonClasses = {
			TEXT("Actor"), TEXT("Pawn"), TEXT("Character"), TEXT("PlayerController"), TEXT("GameModeBase"),
			TEXT("GameStateBase"), TEXT("PlayerState"), TEXT("HUD"), TEXT("ActorComponent"),
			TEXT("SceneComponent"), TEXT("PrimitiveComponent"), TEXT("StaticMeshComponent"),
			TEXT("SkeletalMeshComponent"), TEXT("CameraComponent"), TEXT("AudioComponent"),
			TEXT("LightComponent"), TEXT("UserWidget"), TEXT("AnimInstance"),
			TEXT("GameInstance"), TEXT("SaveGame"), TEXT("DataAsset"), TEXT("PrimaryDataAsset"),
			TEXT("BlueprintFunctionLibrary"), TEXT("DeveloperSettings"),
			TEXT("CheatManager"), TEXT("WorldSubsystem"), TEXT("GameInstanceSubsystem"),
			TEXT("LocalPlayerSubsystem"),
		};

		TArray<TSharedPtr<FJsonValue>> ClassesArray;
		for (const FString& ClassName : CommonClasses)
		{
			UClass* Class = FindClass(ClassName);
			if (Class)
			{
				TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
				ClassObj->SetStringField(TEXT("name"), Class->GetName());
				if (Class->GetSuperClass())
				{
					ClassObj->SetStringField(TEXT("parent"), Class->GetSuperClass()->GetName());
				}
				ClassesArray.Add(MakeShared<FJsonValueObject>(ClassObj));
			}
		}
		Result->SetStringField(TEXT("note"), TEXT("Showing common base classes. Use parentFilter to find derived classes."));
		Result->SetArrayField(TEXT("classes"), ClassesArray);
		Result->SetNumberField(TEXT("count"), ClassesArray.Num());
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::ListGameplayTags(const TSharedPtr<FJsonObject>& Params)
{
	FString FilterPrefix;
	Params->TryGetStringField(TEXT("filter"), FilterPrefix);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UGameplayTagsManager& TagsManager = UGameplayTagsManager::Get();
	FGameplayTagContainer AllTags;
	TagsManager.RequestAllGameplayTags(AllTags, false);

	TArray<TSharedPtr<FJsonValue>> TagsArray;
	for (const FGameplayTag& Tag : AllTags)
	{
		FString TagString = Tag.ToString();
		if (FilterPrefix.IsEmpty() || TagString.StartsWith(FilterPrefix))
		{
			TagsArray.Add(MakeShared<FJsonValueString>(TagString));
		}
	}

	TagsArray.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B) {
		return A->AsString() < B->AsString();
	});

	Result->SetStringField(TEXT("filter"), FilterPrefix.IsEmpty() ? TEXT("(all)") : FilterPrefix);
	Result->SetArrayField(TEXT("tags"), TagsArray);
	Result->SetNumberField(TEXT("count"), TagsArray.Num());

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::CreateGameplayTag(const TSharedPtr<FJsonObject>& Params)
{
	FString Tag;
	if (!Params->TryGetStringField(TEXT("tag"), Tag) || Tag.IsEmpty())
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("error"), TEXT("tag parameter is required (e.g. 'Combat.Damage.Fire')"));
		return MakeShared<FJsonValueObject>(Error);
	}

	FString Comment;
	Params->TryGetStringField(TEXT("comment"), Comment);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("tag"), Tag);

	// Try to add via GameplayTagsManager
	UGameplayTagsManager& TagsManager = UGameplayTagsManager::Get();
	FName TagName(*Tag);
	TagsManager.AddNativeGameplayTag(TagName, Comment);
	FGameplayTag NewTag = TagsManager.RequestGameplayTag(TagName, false);
	bool bSuccess = NewTag.IsValid();
	if (bSuccess)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("method"), TEXT("add_native_gameplay_tag"));
		return MakeShared<FJsonValueObject>(Result);
	}

	// Fallback: append to DefaultGameplayTags.ini
	FString ProjectDir = FPaths::ProjectDir();
	FString TagFile = FPaths::Combine(ProjectDir, TEXT("Config"), TEXT("DefaultGameplayTags.ini"));
	
	FString Section = TEXT("[/Script/GameplayTags.GameplayTagsSettings]");
	FString Entry = FString::Printf(TEXT("+GameplayTagList=(Tag=\"%s\",DevComment=\"%s\")"), *Tag, *Comment);

	FString FileContent;
	if (FFileHelper::LoadFileToString(FileContent, *TagFile))
	{
		if (!FileContent.Contains(Section))
		{
			FileContent += TEXT("\n\n") + Section + TEXT("\n") + Entry + TEXT("\n");
		}
		else if (!FileContent.Contains(Entry))
		{
			FileContent = FileContent.Replace(*Section, *(Section + TEXT("\n") + Entry));
		}
	}
	else
	{
		FileContent = Section + TEXT("\n") + Entry + TEXT("\n");
	}

	if (FFileHelper::SaveStringToFile(FileContent, *TagFile))
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("method"), TEXT("ini_append"));
		Result->SetStringField(TEXT("note"), TEXT("Restart editor to pick up new tag"));
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), TEXT("Could not add gameplay tag via available APIs"));
	return MakeShared<FJsonValueObject>(Result);
}

UClass* FReflectionHandlers::FindClass(const FString& ClassName)
{
	// Try direct lookup
	UClass* Class = FindObject<UClass>(nullptr, *ClassName);
	if (Class)
	{
		return Class;
	}

	// Try with /Script/ prefix
	FString ScriptPath = FString::Printf(TEXT("/Script/%s"), *ClassName);
	Class = FindObject<UClass>(nullptr, *ScriptPath);
	if (Class)
	{
		return Class;
	}

	// Try with common prefixes
	TArray<FString> Prefixes = { TEXT(""), TEXT("A"), TEXT("U"), TEXT("F") };
	for (const FString& Prefix : Prefixes)
	{
		FString CandidateName = Prefix + ClassName;
		Class = FindObject<UClass>(nullptr, *CandidateName);
		if (Class)
		{
			return Class;
		}

		FString CandidatePath = FString::Printf(TEXT("/Script/%s"), *CandidateName);
		Class = FindObject<UClass>(nullptr, *CandidatePath);
		if (Class)
		{
			return Class;
		}
	}

	return nullptr;
}

UScriptStruct* FReflectionHandlers::FindStruct(const FString& StructName)
{
	// Try direct lookup (handles full paths like /Script/ModuleName.StructName)
	UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructName);
	if (Struct)
	{
		return Struct;
	}

	// Try with /Script/ prefix
	FString ScriptPath = FString::Printf(TEXT("/Script/%s"), *StructName);
	Struct = FindObject<UScriptStruct>(nullptr, *ScriptPath);
	if (Struct)
	{
		return Struct;
	}

	// Try with F prefix
	FString FName = TEXT("F") + StructName;
	Struct = FindObject<UScriptStruct>(nullptr, *FName);
	if (Struct)
	{
		return Struct;
	}

	FString FPath = FString::Printf(TEXT("/Script/%s"), *FName);
	Struct = FindObject<UScriptStruct>(nullptr, *FPath);
	if (Struct)
	{
		return Struct;
	}

	// Iterate all loaded UScriptStruct objects to find by short name
	// This catches project-defined structs in any module (e.g. /Script/MyModule.FMyStruct)
	FString NameToFind = StructName;
	FString FNameToFind = FName;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Current = *It;
		if (Current)
		{
			FString CurrentName = Current->GetName();
			if (CurrentName == NameToFind || CurrentName == FNameToFind)
			{
				return Current;
			}
		}
	}

	return nullptr;
}

UEnum* FReflectionHandlers::FindEnum(const FString& EnumName)
{
	// Try direct lookup
	UEnum* Enum = FindObject<UEnum>(nullptr, *EnumName);
	if (Enum)
	{
		return Enum;
	}

	// Try with /Script/ prefix
	FString ScriptPath = FString::Printf(TEXT("/Script/%s"), *EnumName);
	Enum = FindObject<UEnum>(nullptr, *ScriptPath);
	if (Enum)
	{
		return Enum;
	}

	// Try with E prefix
	FString EName = TEXT("E") + EnumName;
	Enum = FindObject<UEnum>(nullptr, *EName);
	if (Enum)
	{
		return Enum;
	}

	FString EPath = FString::Printf(TEXT("/Script/%s"), *EName);
	Enum = FindObject<UEnum>(nullptr, *EPath);
	if (Enum)
	{
		return Enum;
	}

	return nullptr;
}

TSharedPtr<FJsonValue> FReflectionHandlers::SerializeProperty(FProperty* Prop, void* Data)
{
	// Basic property serialization - can be extended
	if (CastField<FStrProperty>(Prop))
	{
		return MakeShared<FJsonValueString>(CastField<FStrProperty>(Prop)->GetPropertyValue(Data));
	}
	else if (CastField<FIntProperty>(Prop))
	{
		return MakeShared<FJsonValueNumber>(CastField<FIntProperty>(Prop)->GetPropertyValue(Data));
	}
	else if (CastField<FFloatProperty>(Prop))
	{
		return MakeShared<FJsonValueNumber>(CastField<FFloatProperty>(Prop)->GetPropertyValue(Data));
	}
	else if (CastField<FBoolProperty>(Prop))
	{
		return MakeShared<FJsonValueBoolean>(CastField<FBoolProperty>(Prop)->GetPropertyValue(Data));
	}
	return MakeShared<FJsonValueString>(TEXT("(unserializable)"));
}
