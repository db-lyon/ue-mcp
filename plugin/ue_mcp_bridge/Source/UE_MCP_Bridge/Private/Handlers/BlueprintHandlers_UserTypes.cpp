// User Defined Enum and User Defined Struct authoring (V14).
//
// All functions below are still members of FBlueprintHandlers - this file is a
// translation-unit partition, not a new class. Registration stays in
// BlueprintHandlers.cpp::RegisterHandlers.
//
// What already shipped, and is deliberately NOT repeated here: the `asset`
// category owns creation and the coarse entry CRUD
// (create_user_defined_enum / list_enum_values / edit_user_defined_enum, and
// create_user_defined_struct / list_struct_fields / edit_user_defined_struct /
// rename_struct_field, in AssetHandlers_Enum.cpp and AssetHandlers_Struct.cpp).
// This file adds only the parts of a user type that no existing action and no
// property write can reach:
//
//   ordering      FEnumEditorUtils::MoveEnumeratorInUserDefinedEnum and
//                 FStructureEditorUtils::MoveVariable. Field and enumerator
//                 order is authored data, not a UPROPERTY on the asset.
//   entry data    per-enumerator tooltips are package metadata on the UEnum
//                 (UEnum::SetMetaData with a NameIndex), and the bitflags
//                 switch is a class-level metadata key, so neither is a
//                 property write either.
//   field data    a UUserDefinedStruct member is an FStructVariableDescription
//                 inside UUserDefinedStructEditorData, NOT a UPROPERTY on the
//                 struct asset. Its default value, tooltip, instance-editable,
//                 SaveGame, multi-line and 3D-widget switches only take effect
//                 through FStructureEditorUtils, which recompiles the struct
//                 and repairs every dependent default.
//   read-back     the whole definition in one call, in a form that can be
//                 handed straight back to the authoring actions.
//
// Explicitly left to asset(set_property): UUserDefinedEnum::EnumDescription is
// an ordinary EditAnywhere UPROPERTY, so it gets no setter here. Both read
// actions return `objectPath` for exactly that purpose.

#include "BlueprintHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Uniquely named on purpose: this module is a unity build, so a file-local
// helper sharing a name with one in another .cpp is a redefinition error that
// only appears once the adaptive-unity working set shifts.
namespace MCPUserTypes
{

// ── enum helpers ─────────────────────────────────────────────────────────────

/** UEnum keeps an implicit trailing "_MAX" that is not an authored value. */
static bool IsMaxSentinel(const UEnum* Enum, int32 Index)
{
	return Enum->GetNameStringByIndex(Index).EndsWith(TEXT("_MAX"));
}

/** Authored enumerator count, excluding the trailing sentinel. */
static int32 AuthoredCount(const UEnum* Enum)
{
	int32 Count = 0;
	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		if (!IsMaxSentinel(Enum, i)) ++Count;
	}
	return Count;
}

/** Every authored index, in current order. */
static void AuthoredIndices(const UEnum* Enum, TArray<int32>& Out)
{
	Out.Reset();
	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		if (!IsMaxSentinel(Enum, i)) Out.Add(i);
	}
}

/** The per-enumerator tooltip, which lives in package metadata rather than on
 *  the asset. Empty when none was authored. */
static FString EnumeratorTooltip(const UEnum* Enum, int32 Index)
{
	// Unguarded on purpose. UEnum's metadata accessors moved from WITH_EDITOR to
	// WITH_METADATA during the 5.x line, and both are 1 in every build this
	// editor-only module is compiled into, so a #if on either would be dead in
	// one engine version and a silent no-op in another.
	return Enum->GetMetaData(TEXT("ToolTip"), Index);
}

static TSharedPtr<FJsonObject> EnumeratorJson(const UEnum* Enum, int32 Index)
{
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
	V->SetNumberField(TEXT("index"), Index);
	// The authored short name is auto-generated ("NewEnumerator3") and is NOT
	// user-settable through the public API; displayName is the editable half
	// and is what every UI and chooser shows.
	V->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(Index));
	V->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(Index).ToString());
	V->SetNumberField(TEXT("value"), (double)Enum->GetValueByIndex(Index));
	V->SetStringField(TEXT("tooltip"), EnumeratorTooltip(Enum, Index));
	return V;
}

/** Resolve one enumerator from an "index" number or a "name" matching either
 *  the authored short name or the display name. INDEX_NONE when unresolved. */
static int32 ResolveEnumerator(const UEnum* Enum, const TSharedPtr<FJsonValue>& Entry)
{
	if (!Entry.IsValid()) return INDEX_NONE;

	double AsNumber = 0.0;
	if (Entry->TryGetNumber(AsNumber))
	{
		const int32 Index = (int32)AsNumber;
		if (Index < 0 || Index >= Enum->NumEnums() || IsMaxSentinel(Enum, Index)) return INDEX_NONE;
		return Index;
	}

	FString AsString;
	if (!Entry->TryGetString(AsString) || AsString.IsEmpty()) return INDEX_NONE;
	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		if (IsMaxSentinel(Enum, i)) continue;
		if (Enum->GetNameStringByIndex(i) == AsString ||
			Enum->GetDisplayNameTextByIndex(i).ToString() == AsString)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

/** Every authored enumerator, named the way the error messages and the
 *  rollback payload spell them. */
static FString EnumeratorNameList(const UEnum* Enum)
{
	TArray<FString> Names;
	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		if (IsMaxSentinel(Enum, i)) continue;
		Names.Add(FString::Printf(TEXT("%s (%s)"),
			*Enum->GetDisplayNameTextByIndex(i).ToString(),
			*Enum->GetNameStringByIndex(i)));
	}
	return FString::Join(Names, TEXT(", "));
}

// ── struct type specs ────────────────────────────────────────────────────────
//
// FBlueprintHandlers::MakePinType covers every scalar an authoring surface in
// this plugin accepts, but it has no container form, and a struct member is
// exactly where arrays, sets and maps are authored. The helpers below are the
// parsing half of that container layer; they back the two public entry points
// defined under this namespace, which are INVERSES of each other. What
// read_struct reports as a field's `type` can be handed straight back to a
// write, because a label that cannot be replayed is not a read-back.

/** Split "K,V" on the comma that is not inside a nested <> or [] group. */
static bool SplitTopLevelComma(const FString& In, FString& OutLeft, FString& OutRight)
{
	int32 Depth = 0;
	for (int32 i = 0; i < In.Len(); ++i)
	{
		const TCHAR C = In[i];
		if (C == TEXT('<') || C == TEXT('[')) ++Depth;
		else if (C == TEXT('>') || C == TEXT(']')) Depth = FMath::Max(0, Depth - 1);
		else if (C == TEXT(',') && Depth == 0)
		{
			OutLeft = In.Left(i).TrimStartAndEnd();
			OutRight = In.Mid(i + 1).TrimStartAndEnd();
			return !OutLeft.IsEmpty() && !OutRight.IsEmpty();
		}
	}
	return false;
}

/** Strip a `Wrapper<inner>` if present, case-insensitively. */
static bool UnwrapContainer(const FString& In, const TCHAR* Wrapper, FString& OutInner)
{
	if (!In.StartsWith(Wrapper, ESearchCase::IgnoreCase)) return false;
	const int32 Open = In.Find(TEXT("<"));
	const int32 Close = In.Find(TEXT(">"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (Open == INDEX_NONE || Close == INDEX_NONE || Close <= Open) return false;
	// Guard against TSubclassOf<...> and friends, which MakePinType owns.
	if (Open != FCString::Strlen(Wrapper)) return false;
	OutInner = In.Mid(Open + 1, Close - Open - 1).TrimStartAndEnd();
	return !OutInner.IsEmpty();
}

/** The scalar half of a pin type as a string FBlueprintHandlers::ParsePinTypeSpec accepts again.
 *  bOutRoundTrips is false for the categories this plugin's type vocabulary
 *  cannot express, which is reported rather than silently mislabelled. */
static FString ScalarSpec(const FName Category, const FName SubCategory, UObject* SubObject, bool& bOutRoundTrips)
{
	bOutRoundTrips = true;
	const FString ObjectPath = SubObject ? SubObject->GetPathName() : FString();

	if (Category == UEdGraphSchema_K2::PC_Boolean) return TEXT("bool");
	if (Category == UEdGraphSchema_K2::PC_Int)     return TEXT("int");
	if (Category == UEdGraphSchema_K2::PC_Int64)   return TEXT("int64");
	if (Category == UEdGraphSchema_K2::PC_Float)   return TEXT("float");
	if (Category == UEdGraphSchema_K2::PC_Double)  return TEXT("double");
	if (Category == UEdGraphSchema_K2::PC_Real)
	{
		return SubCategory == UEdGraphSchema_K2::PC_Float ? TEXT("float") : TEXT("double");
	}
	if (Category == UEdGraphSchema_K2::PC_String) return TEXT("string");
	if (Category == UEdGraphSchema_K2::PC_Name)   return TEXT("name");
	if (Category == UEdGraphSchema_K2::PC_Text)   return TEXT("text");
	if (Category == UEdGraphSchema_K2::PC_Byte || Category == UEdGraphSchema_K2::PC_Enum)
	{
		if (Cast<UEnum>(SubObject)) return TEXT("enum:") + ObjectPath;
		return TEXT("byte");
	}
	if (Category == UEdGraphSchema_K2::PC_Struct)
	{
		if (SubObject) return TEXT("struct:") + ObjectPath;
		bOutRoundTrips = false;
		return TEXT("struct");
	}
	if (Category == UEdGraphSchema_K2::PC_Object)
	{
		if (SubObject) return TEXT("object:") + ObjectPath;
		return TEXT("object");
	}
	if (Category == UEdGraphSchema_K2::PC_Class)
	{
		return SubObject ? FString::Printf(TEXT("TSubclassOf<%s>"), *ObjectPath) : TEXT("class");
	}
	if (Category == UEdGraphSchema_K2::PC_SoftObject)
	{
		return SubObject ? FString::Printf(TEXT("TSoftObjectPtr<%s>"), *ObjectPath) : TEXT("softobject");
	}
	if (Category == UEdGraphSchema_K2::PC_SoftClass)
	{
		return SubObject ? FString::Printf(TEXT("TSoftClassPtr<%s>"), *ObjectPath) : TEXT("softclass");
	}

	// PC_Interface, PC_FieldPath, PC_Delegate, PC_MCDelegate, PC_Wildcard: real
	// pin categories with no spelling in this plugin's type vocabulary. Say so
	// rather than emit a label a write would reject.
	bOutRoundTrips = false;
	return Category.ToString();
}

/** Full type spec for one struct member. Delegates to the shared spec so a
 *  member's reported type is exactly what the parser accepts back. */
static FString FieldTypeSpec(const FStructVariableDescription& Desc, bool& bOutRoundTrips)
{
	return FBlueprintHandlers::PinTypeSpec(Desc.ToPinType(), bOutRoundTrips);
}

/** Resolve a member by "fieldGuid" or by "fieldName" against the friendly name
 *  or the internal VarName - the same two spellings the shipped
 *  edit_user_defined_struct accepts, so one addressing rule serves both. */
static bool ResolveField(const TSharedPtr<FJsonObject>& Params, const UUserDefinedStruct* Struct, FGuid& OutGuid)
{
	const FString GuidStr = OptionalString(Params, TEXT("fieldGuid"));
	if (!GuidStr.IsEmpty()) return FGuid::Parse(GuidStr, OutGuid);

	const FString FieldName = OptionalString(Params, TEXT("fieldName"));
	if (FieldName.IsEmpty()) return false;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		if (Desc.FriendlyName == FieldName || Desc.VarName.ToString() == FieldName)
		{
			OutGuid = Desc.VarGuid;
			return true;
		}
	}
	return false;
}

/** Every member, spelled the way the errors and rollbacks spell them. */
static FString FieldNameList(const UUserDefinedStruct* Struct)
{
	TArray<FString> Names;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		Names.Add(FString::Printf(TEXT("%s (%s)"), *Desc.FriendlyName, *Desc.VarName.ToString()));
	}
	return FString::Join(Names, TEXT(", "));
}

static const TCHAR* StructureErrorName(FStructureEditorUtils::EStructureError Error)
{
	switch (Error)
	{
	case FStructureEditorUtils::Ok:              return TEXT("ok");
	case FStructureEditorUtils::Recursion:        return TEXT("recursion");
	case FStructureEditorUtils::FallbackStruct:   return TEXT("fallbackStruct");
	case FStructureEditorUtils::NotCompiled:      return TEXT("notCompiled");
	case FStructureEditorUtils::NotBlueprintType: return TEXT("notBlueprintType");
	case FStructureEditorUtils::NotSupportedType: return TEXT("notSupportedType");
	case FStructureEditorUtils::EmptyStructure:   return TEXT("emptyStructure");
	default:                                      return TEXT("unknown");
	}
}

static const TCHAR* StructStatusName(EUserDefinedStructureStatus Status)
{
	switch (Status)
	{
	case UDSS_UpToDate:  return TEXT("upToDate");
	case UDSS_Dirty:     return TEXT("dirty");
	case UDSS_Error:     return TEXT("error");
	case UDSS_Duplicate: return TEXT("duplicate");
	default:             return TEXT("unknown");
	}
}

static TSharedPtr<FJsonObject> FieldJson(const FStructVariableDescription& Desc, int32 Index)
{
	TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
	V->SetNumberField(TEXT("index"), Index);
	V->SetStringField(TEXT("name"), Desc.FriendlyName);
	V->SetStringField(TEXT("guid"), Desc.VarGuid.ToString());

	bool bRoundTrips = true;
	V->SetStringField(TEXT("type"), FieldTypeSpec(Desc, bRoundTrips));
	V->SetBoolField(TEXT("typeRoundTrips"), bRoundTrips);

	// The generated FProperty name, which is what a DataTable row key, an
	// exported default-value string and any ExportText dump actually carry. It
	// is minted as "<name>_<n>_<guid>" and is a different thing from the display
	// name, so the two can diverge: renaming a member is GUID-preserving, which
	// is what lets existing Blueprint pins and DataTable rows survive, and the
	// storage key that carries the old spelling is the visible cost of that.
	// nameMatchesProperty reports the current state of that divergence rather
	// than predicting it, so a caller can check it after any rename.
	V->SetStringField(TEXT("propertyName"), Desc.VarName.ToString());
	V->SetBoolField(TEXT("nameMatchesProperty"),
		Desc.FriendlyName.IsEmpty() || Desc.VarName.ToString().StartsWith(Desc.FriendlyName + TEXT("_")));

	V->SetStringField(TEXT("defaultValue"), Desc.DefaultValue);
	V->SetStringField(TEXT("currentDefaultValue"), Desc.CurrentDefaultValue);
	V->SetStringField(TEXT("tooltip"), Desc.ToolTip);
	V->SetBoolField(TEXT("editableOnInstance"), !Desc.bDontEditOnInstance);
	V->SetBoolField(TEXT("saveGame"), (bool)Desc.bEnableSaveGame);
	V->SetBoolField(TEXT("multiLineText"), (bool)Desc.bEnableMultiLineText);
	V->SetBoolField(TEXT("widget3D"), (bool)Desc.bEnable3dWidget);
	V->SetBoolField(TEXT("invalid"), (bool)Desc.bInvalidMember);

	TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
	for (const TPair<FName, FString>& Pair : Desc.MetaData)
	{
		Meta->SetStringField(Pair.Key.ToString(), Pair.Value);
	}
	V->SetObjectField(TEXT("metadata"), Meta);
	return V;
}

} // namespace MCPUserTypes

// ─────────────────────────────────────────────────────────────────────────────
// Shared type vocabulary
//
// FBlueprintHandlers::MakePinType already resolves every scalar this plugin's
// authoring surfaces accept, but it has no container form and no inverse. These
// two are that layer, and they are inverses of each other: what PinTypeSpec
// reports can be handed straight back to ParsePinTypeSpec. They live on
// FBlueprintHandlers rather than in a file-local helper because the depth
// actions in BlueprintHandlers_Depth.cpp need the identical vocabulary, and the
// module is a unity build where a copied helper is a redefinition waiting for
// the next working-set shuffle.
// ─────────────────────────────────────────────────────────────────────────────

bool FBlueprintHandlers::ParsePinTypeSpec(const FString& TypeStr, FEdGraphPinType& OutType, FString& OutError)
{
	using namespace MCPUserTypes;

	static const TCHAR* Accepted =
		TEXT("bool, int, int64, float, double, string, name, text, byte, ")
		TEXT("a struct (Vector, struct:/Script/CoreUObject.Vector), ")
		TEXT("an enum (enum:/Game/Enums/E_Foo.E_Foo), ")
		TEXT("an object ref (Actor, object:/Script/Engine.Actor, TSubclassOf<Actor>, TSoftObjectPtr<Actor>), ")
		TEXT("or a container over any of those: 'int[]', 'array<int>', 'set<Name>', 'map<Name,int>'");

	FString Spec = TypeStr;
	Spec.TrimStartAndEndInline();
	if (Spec.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Empty type. Accepted: %s"), Accepted);
		return false;
	}

	EPinContainerType Container = EPinContainerType::None;
	FString ValueSpec;
	FString Inner;

	if (Spec.EndsWith(TEXT("[]")))
	{
		Container = EPinContainerType::Array;
		Spec = Spec.LeftChop(2).TrimStartAndEnd();
	}
	else if (UnwrapContainer(Spec, TEXT("array"), Inner))
	{
		Container = EPinContainerType::Array;
		Spec = Inner;
	}
	else if (UnwrapContainer(Spec, TEXT("set"), Inner))
	{
		Container = EPinContainerType::Set;
		Spec = Inner;
	}
	else if (UnwrapContainer(Spec, TEXT("map"), Inner))
	{
		Container = EPinContainerType::Map;
		FString KeySpec;
		if (!SplitTopLevelComma(Inner, KeySpec, ValueSpec))
		{
			OutError = FString::Printf(
				TEXT("Map type '%s' needs a key and a value: map<Name,int>. Accepted: %s"), *TypeStr, Accepted);
			return false;
		}
		Spec = KeySpec;
	}

	if (Spec.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Container '%s' has no element type. Accepted: %s"), *TypeStr, Accepted);
		return false;
	}

	OutType = FBlueprintHandlers::MakePinType(Spec);
	if (OutType.PinCategory == NAME_None)
	{
		OutError = FString::Printf(TEXT("Unrecognized type '%s'. Accepted: %s"), *Spec, Accepted);
		return false;
	}

	if (Container == EPinContainerType::Map)
	{
		const FEdGraphPinType ValueType = FBlueprintHandlers::MakePinType(ValueSpec);
		if (ValueType.PinCategory == NAME_None)
		{
			OutError = FString::Printf(TEXT("Unrecognized map value type '%s'. Accepted: %s"), *ValueSpec, Accepted);
			return false;
		}
		OutType.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
	}

	OutType.ContainerType = Container;
	return true;
}

FString FBlueprintHandlers::PinTypeSpec(const FEdGraphPinType& PinType, bool& bOutRoundTrips)
{
	using namespace MCPUserTypes;

	UObject* SubObject = PinType.PinSubCategoryObject.IsValid() ? PinType.PinSubCategoryObject.Get() : nullptr;
	const FString Base = ScalarSpec(PinType.PinCategory, PinType.PinSubCategory, SubObject, bOutRoundTrips);

	switch (PinType.ContainerType)
	{
	case EPinContainerType::Array:
		return Base + TEXT("[]");
	case EPinContainerType::Set:
		return FString::Printf(TEXT("set<%s>"), *Base);
	case EPinContainerType::Map:
	{
		bool bValueRoundTrips = true;
		const FString ValueSpec = ScalarSpec(
			PinType.PinValueType.TerminalCategory,
			PinType.PinValueType.TerminalSubCategory,
			PinType.PinValueType.TerminalSubCategoryObject.IsValid()
				? PinType.PinValueType.TerminalSubCategoryObject.Get()
				: nullptr,
			bValueRoundTrips);
		bOutRoundTrips = bOutRoundTrips && bValueRoundTrips;
		return FString::Printf(TEXT("map<%s,%s>"), *Base, *ValueSpec);
	}
	default:
		return Base;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// read_user_defined_enum
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadUserDefinedEnum(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPUserTypes;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UEnum* Enum = LoadAssetByPath<UEnum>(AssetPath);
	if (!Enum) return MCPAssetLoadError(AssetPath, TEXT("UEnum"));

	UUserDefinedEnum* UserEnum = Cast<UUserDefinedEnum>(Enum);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("name"), Enum->GetName());
	// Where asset(set_property) has to be aimed for the fields that ARE plain
	// UPROPERTYs on the asset (EnumDescription).
	Res->SetStringField(TEXT("objectPath"), Enum->GetPathName());
	Res->SetBoolField(TEXT("isUserDefined"), UserEnum != nullptr);
#if WITH_EDITORONLY_DATA
	Res->SetStringField(TEXT("description"), UserEnum ? UserEnum->EnumDescription.ToString() : FString());
#else
	Res->SetStringField(TEXT("description"), FString());
#endif
	Res->SetBoolField(TEXT("bitflags"), UserEnum ? FEnumEditorUtils::IsEnumeratorBitflagsType(UserEnum) : false);

	TArray<TSharedPtr<FJsonValue>> Values;
	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		if (IsMaxSentinel(Enum, i)) continue;
		Values.Add(MakeShared<FJsonValueObject>(EnumeratorJson(Enum, i)));
	}
	Res->SetArrayField(TEXT("values"), Values);
	Res->SetNumberField(TEXT("count"), Values.Num());
	return MCPResult(Res);
}

// ─────────────────────────────────────────────────────────────────────────────
// reorder_enum_values
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::ReorderEnumValues(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPUserTypes;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UUserDefinedEnum* Enum = LoadAssetByPath<UUserDefinedEnum>(AssetPath);
	if (!Enum)
	{
		return MCPError(FString::Printf(
			TEXT("UserDefinedEnum not found: %s. Native UEnums have a fixed declared order and cannot be reordered."),
			*AssetPath));
	}

	const TArray<TSharedPtr<FJsonValue>>* Order = nullptr;
	if (!Params->TryGetArrayField(TEXT("order"), Order) || !Order)
	{
		return MCPError(FString::Printf(
			TEXT("Missing required parameter 'order': the COMPLETE list of enumerators in their desired order, by display name, authored name or index. This enum has %d: %s"),
			AuthoredCount(Enum), *EnumeratorNameList(Enum)));
	}

	// Validate the whole batch before touching anything. A partially applied
	// reorder is worse than a refused one: nothing in the result would say
	// which half landed.
	const int32 Expected = AuthoredCount(Enum);
	if (Order->Num() != Expected)
	{
		return MCPError(FString::Printf(
			TEXT("'order' lists %d entries but this enum has %d enumerators. Pass the complete order, not a partial one. Enumerators: %s"),
			Order->Num(), Expected, *EnumeratorNameList(Enum)));
	}

	TArray<int32> Desired;
	TSet<int32> Seen;
	for (int32 Slot = 0; Slot < Order->Num(); ++Slot)
	{
		const int32 Index = ResolveEnumerator(Enum, (*Order)[Slot]);
		if (Index == INDEX_NONE)
		{
			FString Spelling;
			(*Order)[Slot]->TryGetString(Spelling);
			return MCPError(FString::Printf(
				TEXT("order[%d] ('%s') does not name an enumerator of this enum. Valid values: %s"),
				Slot, *Spelling, *EnumeratorNameList(Enum)));
		}
		if (Seen.Contains(Index))
		{
			return MCPError(FString::Printf(
				TEXT("order[%d] repeats enumerator '%s'. Every enumerator must appear exactly once."),
				Slot, *Enum->GetDisplayNameTextByIndex(Index).ToString()));
		}
		Seen.Add(Index);
		Desired.Add(Index);
	}

	// Record the CURRENT order in authored names before anything moves, so the
	// rollback can be replayed against an enum whose indices have shifted.
	TArray<int32> Current;
	AuthoredIndices(Enum, Current);
	TArray<TSharedPtr<FJsonValue>> PreviousOrder;
	for (const int32 Index : Current)
	{
		PreviousOrder.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
	}

	if (Current == Desired)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetArrayField(TEXT("order"), PreviousOrder);
		Noop->SetNumberField(TEXT("count"), Expected);
		return MCPResult(Noop);
	}

	// Resolve the requested order to authored names FIRST: MoveEnumerator
	// renumbers everything behind it, so an index captured up front is stale by
	// the second move. Names are stable across a reorder.
	TArray<FString> DesiredNames;
	for (const int32 Index : Desired)
	{
		DesiredNames.Add(Enum->GetNameStringByIndex(Index));
	}

	int32 Moves = 0;
	for (int32 Slot = 0; Slot < DesiredNames.Num(); ++Slot)
	{
		TArray<int32> Live;
		AuthoredIndices(Enum, Live);
		int32 From = INDEX_NONE;
		for (int32 i = Slot; i < Live.Num(); ++i)
		{
			if (Enum->GetNameStringByIndex(Live[i]) == DesiredNames[Slot]) { From = Live[i]; break; }
		}
		if (From == INDEX_NONE || From == Live[Slot]) continue;
		FEnumEditorUtils::MoveEnumeratorInUserDefinedEnum(Enum, From, Live[Slot]);
		++Moves;
	}

	UEditorAssetLibrary::SaveLoadedAsset(Enum);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetNumberField(TEXT("moves"), Moves);
	Result->SetNumberField(TEXT("count"), Expected);

	TArray<TSharedPtr<FJsonValue>> AppliedOrder;
	TArray<int32> After;
	AuthoredIndices(Enum, After);
	for (const int32 Index : After)
	{
		AppliedOrder.Add(MakeShared<FJsonValueObject>(EnumeratorJson(Enum, Index)));
	}
	Result->SetArrayField(TEXT("values"), AppliedOrder);

	// Exact inverse: replay the order that was in place before this call.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetArrayField(TEXT("order"), PreviousOrder);
	MCPSetRollback(Result, TEXT("reorder_enum_values"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_enum_metadata
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::SetEnumMetadata(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPUserTypes;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UUserDefinedEnum* Enum = LoadAssetByPath<UUserDefinedEnum>(AssetPath);
	if (!Enum)
	{
		return MCPError(FString::Printf(
			TEXT("UserDefinedEnum not found: %s. Native UEnums carry their metadata in C++ and cannot be edited here."),
			*AssetPath));
	}

	bool bBitflags = false;
	const bool bHasBitflags = Params->TryGetBoolField(TEXT("bitflags"), bBitflags);

	// Validate every entry against the enum BEFORE the first write, so a bad
	// entry at position nine does not leave the first eight applied.
	struct FPlannedTooltip
	{
		int32 Index = INDEX_NONE;
		FString AuthoredName;
		FString Tooltip;
		FString PreviousTooltip;
	};
	TArray<FPlannedTooltip> Planned;

	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (Params->TryGetArrayField(TEXT("entries"), Entries) && Entries)
	{
		for (int32 Slot = 0; Slot < Entries->Num(); ++Slot)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!(*Entries)[Slot]->TryGetObject(Obj) || !Obj || !Obj->IsValid())
			{
				return MCPError(FString::Printf(
					TEXT("entries[%d] is not an object. Each entry is {name or index, tooltip}."), Slot));
			}

			int32 Index = INDEX_NONE;
			double AsNumber = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("index"), AsNumber))
			{
				Index = ResolveEnumerator(Enum, MakeShared<FJsonValueNumber>(AsNumber));
			}
			else
			{
				const FString Name = OptionalString(*Obj, TEXT("name"));
				if (!Name.IsEmpty()) Index = ResolveEnumerator(Enum, MakeShared<FJsonValueString>(Name));
			}
			if (Index == INDEX_NONE)
			{
				return MCPError(FString::Printf(
					TEXT("entries[%d] does not name an enumerator of this enum (pass 'index', or 'name' matching a display or authored name). Valid values: %s"),
					Slot, *EnumeratorNameList(Enum)));
			}

			FString Tooltip;
			if (!(*Obj)->TryGetStringField(TEXT("tooltip"), Tooltip))
			{
				return MCPError(FString::Printf(
					TEXT("entries[%d] carries no 'tooltip'. That is the only per-enumerator field this action writes; the display name is edit_user_defined_enum(op=rename_value)'s and the description is a UPROPERTY, so use asset(set_property) with propertyName='EnumDescription'."),
					Slot));
			}

			FPlannedTooltip Entry;
			Entry.Index = Index;
			Entry.AuthoredName = Enum->GetNameStringByIndex(Index);
			Entry.Tooltip = Tooltip;
			Entry.PreviousTooltip = EnumeratorTooltip(Enum, Index);
			Planned.Add(Entry);
		}
	}

	const bool bPrevBitflags = FEnumEditorUtils::IsEnumeratorBitflagsType(Enum);
	bool bAnyChange = bHasBitflags && bBitflags != bPrevBitflags;
	for (const FPlannedTooltip& Entry : Planned)
	{
		if (Entry.Tooltip != Entry.PreviousTooltip) { bAnyChange = true; break; }
	}

	if (!bHasBitflags && Planned.Num() == 0)
	{
		return MCPError(TEXT("Nothing to set. Pass 'bitflags' and/or 'entries' ([{name or index, tooltip}])."));
	}

	if (!bAnyChange)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetBoolField(TEXT("bitflags"), bPrevBitflags);
		return MCPResult(Noop);
	}

	if (bHasBitflags && bBitflags != bPrevBitflags)
	{
		FEnumEditorUtils::SetEnumeratorBitflagsTypeState(Enum, bBitflags);
	}
	for (const FPlannedTooltip& Entry : Planned)
	{
		if (Entry.Tooltip == Entry.PreviousTooltip) continue;
		Enum->SetMetaData(TEXT("ToolTip"), *Entry.Tooltip, Entry.Index);
	}

	UEditorAssetLibrary::SaveLoadedAsset(Enum);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("bitflags"), FEnumEditorUtils::IsEnumeratorBitflagsType(Enum));
	Result->SetNumberField(TEXT("tooltipsWritten"), Planned.Num());

	// Exact inverse: the previous bitflags state and the previous tooltip text,
	// addressed by authored name so it survives a reorder in between.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	if (bHasBitflags) Payload->SetBoolField(TEXT("bitflags"), bPrevBitflags);
	if (Planned.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> PrevEntries;
		for (const FPlannedTooltip& Entry : Planned)
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("name"), Entry.AuthoredName);
			E->SetStringField(TEXT("tooltip"), Entry.PreviousTooltip);
			PrevEntries.Add(MakeShared<FJsonValueObject>(E));
		}
		Payload->SetArrayField(TEXT("entries"), PrevEntries);
	}
	MCPSetRollback(Result, TEXT("set_enum_metadata"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// read_user_defined_struct
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadUserDefinedStruct(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPUserTypes;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UUserDefinedStruct* Struct = LoadAssetByPath<UUserDefinedStruct>(AssetPath);
	if (!Struct)
	{
		return MCPError(FString::Printf(
			TEXT("UserDefinedStruct not found: %s. Native structs are declared in C++ and have no editor data to read; use reflection(reflect_struct) for those."),
			*AssetPath));
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("name"), Struct->GetName());
	Res->SetStringField(TEXT("objectPath"), Struct->GetPathName());
	Res->SetStringField(TEXT("guid"), Struct->Guid.ToString());
	Res->SetStringField(TEXT("status"), StructStatusName(Struct->Status.GetValue()));
#if WITH_EDITORONLY_DATA
	Res->SetStringField(TEXT("errorMessage"), Struct->ErrorMessage);
#else
	Res->SetStringField(TEXT("errorMessage"), FString());
#endif
	Res->SetStringField(TEXT("tooltip"), FStructureEditorUtils::GetTooltip(Struct));

	FString ValidityMessage;
	const FStructureEditorUtils::EStructureError Validity =
		FStructureEditorUtils::IsStructureValid(Struct, nullptr, &ValidityMessage);
	Res->SetStringField(TEXT("validity"), StructureErrorName(Validity));
	Res->SetStringField(TEXT("validityMessage"), ValidityMessage);

	TArray<TSharedPtr<FJsonValue>> FieldList;
	int32 Index = 0;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		FieldList.Add(MakeShared<FJsonValueObject>(FieldJson(Desc, Index++)));
	}
	Res->SetArrayField(TEXT("fields"), FieldList);
	Res->SetNumberField(TEXT("count"), FieldList.Num());
	return MCPResult(Res);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_struct_field_default
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::SetStructFieldDefault(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPUserTypes;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UUserDefinedStruct* Struct = LoadAssetByPath<UUserDefinedStruct>(AssetPath);
	if (!Struct)
	{
		return MCPError(FString::Printf(
			TEXT("UserDefinedStruct not found: %s. A native struct's defaults live in its C++ constructor."),
			*AssetPath));
	}

	FString DefaultValue;
	if (!Params->TryGetStringField(TEXT("defaultValue"), DefaultValue))
	{
		return MCPError(TEXT("Missing required parameter 'defaultValue': the member's default as Unreal export text (e.g. '5', 'true', 'Hello', '(X=1.000000,Y=2.000000,Z=0.000000)'). Pass an empty string to clear it."));
	}

	FGuid Guid;
	if (!ResolveField(Params, Struct, Guid))
	{
		return MCPError(FString::Printf(
			TEXT("Could not resolve the field (pass 'fieldGuid', or 'fieldName' matching a display or internal name). Fields: %s"),
			*FieldNameList(Struct)));
	}

	const FStructVariableDescription* Before = FStructureEditorUtils::GetVarDescByGuid(Struct, Guid);
	if (!Before)
	{
		return MCPError(FString::Printf(
			TEXT("No field with GUID %s on this struct. Fields: %s"), *Guid.ToString(), *FieldNameList(Struct)));
	}
	const FString PreviousDefault = Before->DefaultValue;
	const FString FieldDisplayName = Before->FriendlyName;

	if (PreviousDefault == DefaultValue)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("fieldGuid"), Guid.ToString());
		Noop->SetStringField(TEXT("fieldName"), FieldDisplayName);
		Noop->SetStringField(TEXT("defaultValue"), PreviousDefault);
		return MCPResult(Noop);
	}

	// ChangeVariableDefaultValue parses the text against the member's real
	// property and refuses a value the type cannot hold, which is why a raw
	// write to FStructVariableDescription::DefaultValue is not equivalent: it
	// would store an unparseable string that only surfaces at compile time.
	if (!FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Guid, DefaultValue))
	{
		bool bRoundTrips = true;
		return MCPError(FString::Printf(
			TEXT("The editor refused '%s' as the default for field '%s' (type %s). The value has to be Unreal export text for that type; nothing was changed."),
			*DefaultValue, *FieldDisplayName, *FieldTypeSpec(*Before, bRoundTrips)));
	}

	UEditorAssetLibrary::SaveLoadedAsset(Struct);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("fieldGuid"), Guid.ToString());
	Result->SetStringField(TEXT("fieldName"), FieldDisplayName);
	if (const FStructVariableDescription* After = FStructureEditorUtils::GetVarDescByGuid(Struct, Guid))
	{
		Result->SetObjectField(TEXT("field"), FieldJson(*After, INDEX_NONE));
	}
	Result->SetStringField(TEXT("previousDefaultValue"), PreviousDefault);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("fieldGuid"), Guid.ToString());
	Payload->SetStringField(TEXT("defaultValue"), PreviousDefault);
	MCPSetRollback(Result, TEXT("set_struct_field_default"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// reorder_struct_fields
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::ReorderStructFields(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPUserTypes;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UUserDefinedStruct* Struct = LoadAssetByPath<UUserDefinedStruct>(AssetPath);
	if (!Struct)
	{
		return MCPError(FString::Printf(
			TEXT("UserDefinedStruct not found: %s. A native struct's member order is fixed by its C++ declaration."),
			*AssetPath));
	}

	const TArray<TSharedPtr<FJsonValue>>* Order = nullptr;
	if (!Params->TryGetArrayField(TEXT("order"), Order) || !Order)
	{
		return MCPError(FString::Printf(
			TEXT("Missing required parameter 'order': the COMPLETE list of members in their desired order, by display name, internal name or GUID. This struct has %d: %s"),
			FStructureEditorUtils::GetVarDesc(Struct).Num(), *FieldNameList(Struct)));
	}

	const TArray<FStructVariableDescription>& Current = FStructureEditorUtils::GetVarDesc(Struct);
	if (Order->Num() != Current.Num())
	{
		return MCPError(FString::Printf(
			TEXT("'order' lists %d entries but this struct has %d members. Pass the complete order, not a partial one. Members: %s"),
			Order->Num(), Current.Num(), *FieldNameList(Struct)));
	}

	// Whole-batch validation up front; nothing moves until every entry resolves.
	TArray<FGuid> Desired;
	TSet<FGuid> Seen;
	for (int32 Slot = 0; Slot < Order->Num(); ++Slot)
	{
		FString Spelling;
		if (!(*Order)[Slot]->TryGetString(Spelling) || Spelling.IsEmpty())
		{
			return MCPError(FString::Printf(
				TEXT("order[%d] is not a string. Each entry names a member by display name, internal name or GUID. Members: %s"),
				Slot, *FieldNameList(Struct)));
		}

		FGuid Resolved;
		bool bFound = false;
		if (FGuid::Parse(Spelling, Resolved) && FStructureEditorUtils::GetVarDescByGuid(Struct, Resolved))
		{
			bFound = true;
		}
		else
		{
			for (const FStructVariableDescription& Desc : Current)
			{
				if (Desc.FriendlyName == Spelling || Desc.VarName.ToString() == Spelling)
				{
					Resolved = Desc.VarGuid;
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
		{
			return MCPError(FString::Printf(
				TEXT("order[%d] ('%s') does not name a member of this struct. Members: %s"),
				Slot, *Spelling, *FieldNameList(Struct)));
		}
		if (Seen.Contains(Resolved))
		{
			return MCPError(FString::Printf(
				TEXT("order[%d] repeats member '%s'. Every member must appear exactly once."), Slot, *Spelling));
		}
		Seen.Add(Resolved);
		Desired.Add(Resolved);
	}

	TArray<FGuid> Before;
	TArray<TSharedPtr<FJsonValue>> PreviousOrder;
	for (const FStructVariableDescription& Desc : Current)
	{
		Before.Add(Desc.VarGuid);
		PreviousOrder.Add(MakeShared<FJsonValueString>(Desc.VarGuid.ToString()));
	}

	if (Before == Desired)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetArrayField(TEXT("order"), PreviousOrder);
		Noop->SetNumberField(TEXT("count"), Before.Num());
		return MCPResult(Noop);
	}

	// MoveVariable is expressed relative to another member rather than as an
	// absolute index, so the order is built left to right: put each member
	// below the one that should precede it. The first entry is placed above the
	// member that currently leads, which is a no-op when it already leads.
	int32 Moves = 0;
	for (int32 Slot = 0; Slot < Desired.Num(); ++Slot)
	{
		const TArray<FStructVariableDescription>& Live = FStructureEditorUtils::GetVarDesc(Struct);
		if (Live.Num() != Desired.Num()) break;
		if (Live[Slot].VarGuid == Desired[Slot]) continue;

		const bool bMoved = (Slot == 0)
			? FStructureEditorUtils::MoveVariable(Struct, Desired[0], Live[0].VarGuid, FStructureEditorUtils::PositionAbove)
			: FStructureEditorUtils::MoveVariable(Struct, Desired[Slot], Desired[Slot - 1], FStructureEditorUtils::PositionBelow);
		if (!bMoved)
		{
			// Report what actually landed rather than claiming the whole order
			// applied. The struct is left in the partially reordered state the
			// engine refused to continue from, and the rollback below restores
			// the original order exactly.
			auto Partial = MCPSuccess();
			MCPSetUpdated(Partial);
			Partial->SetStringField(TEXT("path"), AssetPath);
			Partial->SetBoolField(TEXT("complete"), false);
			Partial->SetNumberField(TEXT("moves"), Moves);
			Partial->SetStringField(TEXT("stoppedAtGuid"), Desired[Slot].ToString());
			Partial->SetStringField(TEXT("reason"), TEXT("MoveVariable refused this move; the order before this call is restorable through the rollback record."));
			TSharedPtr<FJsonObject> UndoPayload = MakeShared<FJsonObject>();
			UndoPayload->SetStringField(TEXT("assetPath"), AssetPath);
			UndoPayload->SetArrayField(TEXT("order"), PreviousOrder);
			MCPSetRollback(Partial, TEXT("reorder_struct_fields"), UndoPayload);
			UEditorAssetLibrary::SaveLoadedAsset(Struct);
			return MCPResult(Partial);
		}
		++Moves;
	}

	UEditorAssetLibrary::SaveLoadedAsset(Struct);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("complete"), true);
	Result->SetNumberField(TEXT("moves"), Moves);

	TArray<TSharedPtr<FJsonValue>> FieldList;
	int32 Index = 0;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		FieldList.Add(MakeShared<FJsonValueObject>(FieldJson(Desc, Index++)));
	}
	Result->SetArrayField(TEXT("fields"), FieldList);
	Result->SetNumberField(TEXT("count"), FieldList.Num());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetArrayField(TEXT("order"), PreviousOrder);
	MCPSetRollback(Result, TEXT("reorder_struct_fields"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// edit_struct_metadata
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::EditStructMetadata(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPUserTypes;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UUserDefinedStruct* Struct = LoadAssetByPath<UUserDefinedStruct>(AssetPath);
	if (!Struct)
	{
		return MCPError(FString::Printf(
			TEXT("UserDefinedStruct not found: %s. A native struct's metadata is declared in C++."), *AssetPath));
	}

	FString StructTooltip;
	const bool bHasStructTooltip = Params->TryGetStringField(TEXT("tooltip"), StructTooltip);
	const FString PreviousStructTooltip = FStructureEditorUtils::GetTooltip(Struct);

	// One planned edit per member. Everything is resolved and validated here,
	// before the first write, so a refusal on entry nine leaves entries one
	// through eight untouched as well.
	struct FPlannedField
	{
		FGuid Guid;
		FString DisplayName;

		bool bHasTooltip = false;      FString Tooltip;         FString PrevTooltip;
		bool bHasEditable = false;     bool bEditable = false;  bool bPrevEditable = false;
		bool bHasSaveGame = false;     bool bSaveGame = false;  bool bPrevSaveGame = false;
		bool bHasMultiLine = false;    bool bMultiLine = false; bool bPrevMultiLine = false;
		bool bHasWidget3D = false;     bool bWidget3D = false;  bool bPrevWidget3D = false;

		TMap<FName, FString> Metadata;
		TMap<FName, FString> PrevMetadata;
	};
	TArray<FPlannedField> Planned;

	const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
	if (Params->TryGetArrayField(TEXT("fields"), Fields) && Fields)
	{
		TSet<FGuid> SeenFields;
		for (int32 Slot = 0; Slot < Fields->Num(); ++Slot)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!(*Fields)[Slot]->TryGetObject(Obj) || !Obj || !Obj->IsValid())
			{
				return MCPError(FString::Printf(
					TEXT("fields[%d] is not an object. Each entry is {fieldName or fieldGuid, tooltip?, editableOnInstance?, saveGame?, multiLineText?, widget3D?, metadata?}."),
					Slot));
			}

			FGuid Guid;
			if (!ResolveField(*Obj, Struct, Guid) || !FStructureEditorUtils::GetVarDescByGuid(Struct, Guid))
			{
				return MCPError(FString::Printf(
					TEXT("fields[%d] does not name a member of this struct (pass 'fieldGuid', or 'fieldName' matching a display or internal name). Members: %s"),
					Slot, *FieldNameList(Struct)));
			}
			if (SeenFields.Contains(Guid))
			{
				return MCPError(FString::Printf(
					TEXT("fields[%d] repeats a member already listed. Give each member one entry."), Slot));
			}
			SeenFields.Add(Guid);

			const FStructVariableDescription* Desc = FStructureEditorUtils::GetVarDescByGuid(Struct, Guid);
			FPlannedField Plan;
			Plan.Guid = Guid;
			Plan.DisplayName = Desc->FriendlyName;

			Plan.bHasTooltip  = (*Obj)->TryGetStringField(TEXT("tooltip"), Plan.Tooltip);
			Plan.PrevTooltip  = Desc->ToolTip;
			Plan.bHasEditable = (*Obj)->TryGetBoolField(TEXT("editableOnInstance"), Plan.bEditable);
			Plan.bPrevEditable = !Desc->bDontEditOnInstance;
			Plan.bHasSaveGame = (*Obj)->TryGetBoolField(TEXT("saveGame"), Plan.bSaveGame);
			Plan.bPrevSaveGame = (bool)Desc->bEnableSaveGame;
			Plan.bHasMultiLine = (*Obj)->TryGetBoolField(TEXT("multiLineText"), Plan.bMultiLine);
			Plan.bPrevMultiLine = (bool)Desc->bEnableMultiLineText;
			Plan.bHasWidget3D = (*Obj)->TryGetBoolField(TEXT("widget3D"), Plan.bWidget3D);
			Plan.bPrevWidget3D = (bool)Desc->bEnable3dWidget;

			// Both of these are type-gated by the engine (multi-line is for
			// text-like members, the 3D widget for Vector/Transform), so ask
			// before promising rather than writing a flag the details panel
			// will not honour.
			if (Plan.bHasMultiLine && Plan.bMultiLine && !FStructureEditorUtils::CanEnableMultiLineText(Struct, Guid))
			{
				bool bRoundTrips = true;
				return MCPError(FString::Printf(
					TEXT("fields[%d] ('%s', type %s) cannot enable multiLineText: the engine only offers it for text-like members (string, text). Nothing was changed."),
					Slot, *Plan.DisplayName, *FieldTypeSpec(*Desc, bRoundTrips)));
			}
			if (Plan.bHasWidget3D && Plan.bWidget3D && !FStructureEditorUtils::CanEnable3dWidget(Struct, Guid))
			{
				bool bRoundTrips = true;
				return MCPError(FString::Printf(
					TEXT("fields[%d] ('%s', type %s) cannot enable widget3D: the engine only offers it for members a 3D handle can edit (Vector, Transform). Nothing was changed."),
					Slot, *Plan.DisplayName, *FieldTypeSpec(*Desc, bRoundTrips)));
			}

			const TSharedPtr<FJsonObject>* MetaObj = nullptr;
			if ((*Obj)->TryGetObjectField(TEXT("metadata"), MetaObj) && MetaObj && MetaObj->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MetaObj)->Values)
				{
					if (Pair.Key.IsEmpty())
					{
						return MCPError(FString::Printf(TEXT("fields[%d] has an empty metadata key."), Slot));
					}
					FString Value;
					if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(Value))
					{
						return MCPError(FString::Printf(
							TEXT("fields[%d] metadata['%s'] must be a string; Unreal stores every metadata value as text (e.g. ClampMin: '0', EditCondition: 'bEnabled')."),
							Slot, *Pair.Key));
					}
					const FName Key(*Pair.Key);
					Plan.Metadata.Add(Key, Value);
					if (const FString* Prev = FStructureEditorUtils::GetMetaData(Struct, Guid, Key))
					{
						Plan.PrevMetadata.Add(Key, *Prev);
					}
				}
			}

			Planned.Add(Plan);
		}
	}

	if (!bHasStructTooltip && Planned.Num() == 0)
	{
		return MCPError(TEXT("Nothing to set. Pass 'tooltip' (the struct's own) and/or 'fields' ([{fieldName or fieldGuid, tooltip?, editableOnInstance?, saveGame?, multiLineText?, widget3D?, metadata?}])."));
	}

	bool bAnyChange = bHasStructTooltip && StructTooltip != PreviousStructTooltip;
	for (const FPlannedField& Plan : Planned)
	{
		if (bAnyChange) break;
		if (Plan.bHasTooltip   && Plan.Tooltip    != Plan.PrevTooltip)    { bAnyChange = true; break; }
		if (Plan.bHasEditable  && Plan.bEditable  != Plan.bPrevEditable)  { bAnyChange = true; break; }
		if (Plan.bHasSaveGame  && Plan.bSaveGame  != Plan.bPrevSaveGame)  { bAnyChange = true; break; }
		if (Plan.bHasMultiLine && Plan.bMultiLine != Plan.bPrevMultiLine) { bAnyChange = true; break; }
		if (Plan.bHasWidget3D  && Plan.bWidget3D  != Plan.bPrevWidget3D)  { bAnyChange = true; break; }
		for (const TPair<FName, FString>& Pair : Plan.Metadata)
		{
			const FString* Prev = Plan.PrevMetadata.Find(Pair.Key);
			if (!Prev || *Prev != Pair.Value) { bAnyChange = true; break; }
		}
	}

	if (!bAnyChange)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetNumberField(TEXT("fieldsEdited"), 0);
		return MCPResult(Noop);
	}

	if (bHasStructTooltip && StructTooltip != PreviousStructTooltip)
	{
		FStructureEditorUtils::ChangeTooltip(Struct, StructTooltip);
	}
	for (const FPlannedField& Plan : Planned)
	{
		if (Plan.bHasTooltip && Plan.Tooltip != Plan.PrevTooltip)
		{
			FStructureEditorUtils::ChangeVariableTooltip(Struct, Plan.Guid, Plan.Tooltip);
		}
		if (Plan.bHasEditable && Plan.bEditable != Plan.bPrevEditable)
		{
			FStructureEditorUtils::ChangeEditableOnBPInstance(Struct, Plan.Guid, Plan.bEditable);
		}
		if (Plan.bHasSaveGame && Plan.bSaveGame != Plan.bPrevSaveGame)
		{
			FStructureEditorUtils::ChangeSaveGameEnabled(Struct, Plan.Guid, Plan.bSaveGame);
		}
		if (Plan.bHasMultiLine && Plan.bMultiLine != Plan.bPrevMultiLine)
		{
			FStructureEditorUtils::ChangeMultiLineTextEnabled(Struct, Plan.Guid, Plan.bMultiLine);
		}
		if (Plan.bHasWidget3D && Plan.bWidget3D != Plan.bPrevWidget3D)
		{
			FStructureEditorUtils::Change3dWidgetEnabled(Struct, Plan.Guid, Plan.bWidget3D);
		}
		for (const TPair<FName, FString>& Pair : Plan.Metadata)
		{
			const FString* Prev = Plan.PrevMetadata.Find(Pair.Key);
			if (Prev && *Prev == Pair.Value) continue;
			FStructureEditorUtils::SetMetaData(Struct, Plan.Guid, Pair.Key, Pair.Value);
		}
	}

	UEditorAssetLibrary::SaveLoadedAsset(Struct);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("tooltip"), FStructureEditorUtils::GetTooltip(Struct));
	Result->SetNumberField(TEXT("fieldsEdited"), Planned.Num());

	TArray<TSharedPtr<FJsonValue>> FieldList;
	int32 Index = 0;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		FieldList.Add(MakeShared<FJsonValueObject>(FieldJson(Desc, Index++)));
	}
	Result->SetArrayField(TEXT("fields"), FieldList);

	// The inverse restores every value this call replaced, keyed by GUID so it
	// survives a rename or a reorder in between. One caveat, stated rather than
	// implied: a metadata key that did NOT exist before cannot be un-set by
	// replaying this action, so it is restored to the empty string instead of
	// being removed. Everything else round-trips exactly.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	if (bHasStructTooltip) Payload->SetStringField(TEXT("tooltip"), PreviousStructTooltip);
	if (Planned.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> PrevFields;
		for (const FPlannedField& Plan : Planned)
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("fieldGuid"), Plan.Guid.ToString());
			if (Plan.bHasTooltip)   E->SetStringField(TEXT("tooltip"), Plan.PrevTooltip);
			if (Plan.bHasEditable)  E->SetBoolField(TEXT("editableOnInstance"), Plan.bPrevEditable);
			if (Plan.bHasSaveGame)  E->SetBoolField(TEXT("saveGame"), Plan.bPrevSaveGame);
			if (Plan.bHasMultiLine) E->SetBoolField(TEXT("multiLineText"), Plan.bPrevMultiLine);
			if (Plan.bHasWidget3D)  E->SetBoolField(TEXT("widget3D"), Plan.bPrevWidget3D);
			if (Plan.Metadata.Num() > 0)
			{
				TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
				for (const TPair<FName, FString>& Pair : Plan.Metadata)
				{
					const FString* Prev = Plan.PrevMetadata.Find(Pair.Key);
					Meta->SetStringField(Pair.Key.ToString(), Prev ? *Prev : FString());
				}
				E->SetObjectField(TEXT("metadata"), Meta);
			}
			PrevFields.Add(MakeShared<FJsonValueObject>(E));
		}
		Payload->SetArrayField(TEXT("fields"), PrevFields);
	}
	MCPSetRollback(Result, TEXT("edit_struct_metadata"), Payload);
	return MCPResult(Result);
}
