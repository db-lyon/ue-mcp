#include "HandlerRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HandlerUtils.h"
#include "MCPHandlerRegistration.h"
#include "UE_MCP_BridgeModule.h"

FMCPHandlerRegistry::FMCPHandlerRegistry()
{
}

FMCPHandlerRegistry::~FMCPHandlerRegistry()
{
	Clear();
}

void FMCPHandlerRegistry::TagCategory(const FString& MethodName)
{
	if (RegistrationCategory.IsEmpty())
	{
		HandlerCategories.Remove(MethodName);
	}
	else
	{
		HandlerCategories.Add(MethodName, RegistrationCategory);
	}
}

void FMCPHandlerRegistry::RegisterHandler(const FString& MethodName, FHandlerFunction Handler)
{
	CppHandlers.Add(MethodName, Handler);
	TagCategory(MethodName);
	HandlerSpecs.Remove(MethodName);
	RefusedSpecs.Remove(MethodName);
}

bool FMCPHandlerRegistry::RegisterHandler(const FString& MethodName, FHandlerFunction Handler, const TArray<FMCPParamSpec>& Params)
{
	return RegisterHandler(MethodName, MoveTemp(Handler), Params, FMCPSpecRules());
}

bool FMCPHandlerRegistry::RegisterHandler(const FString& MethodName, FHandlerFunction Handler, const TArray<FMCPParamSpec>& Params, const FMCPSpecRules& Rules)
{
	RegisterHandler(MethodName, MoveTemp(Handler));
	FMCPHandlerSpec Spec;
	Spec.Params = Params;
	Spec.Choices = Rules.Choices;
	Spec.ContractExemptReason = Rules.ContractExemptReason;
	Spec.CommitPolicy = Rules.CommitPolicy;
	const FString Problem = ValidateHandlerSpec(Spec);
	if (!Problem.IsEmpty())
	{
		HandlerSpecs.Remove(MethodName);
		RefusedSpecs.Add(MethodName, Problem);
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Parameter spec for '%s' refused: %s"), *MethodName, *Problem);
		return false;
	}
	HandlerSpecs.Add(MethodName, MoveTemp(Spec));
	return true;
}

bool FMCPHandlerRegistry::IsParamIdentifier(const FString& Name)
{
	if (Name.IsEmpty() || FChar::IsDigit(Name[0])) return false;
	for (const TCHAR C : Name)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_')) return false;
	}
	return true;
}

FString FMCPHandlerRegistry::ValidateParamSpecs(const TArray<FMCPParamSpec>& Params)
{
	TSet<FString> Seen;
	auto Claim = [&](const FString& Name, const FString& Owner) -> FString
	{
		if (!IsParamIdentifier(Name))
		{
			return FString::Printf(TEXT("'%s' (on '%s') is not an identifier"), *Name, *Owner);
		}
		if (MCPRoutingParamNames().Contains(Name))
		{
			return FString::Printf(TEXT("'%s' (on '%s') is a routing name the dispatcher consumes before any handler runs"), *Name, *Owner);
		}
		if (Seen.Contains(Name))
		{
			return FString::Printf(TEXT("'%s' (on '%s') is declared twice"), *Name, *Owner);
		}
		Seen.Add(Name);
		return FString();
	};

	for (const FMCPParamSpec& Param : Params)
	{
		FString Problem = Claim(Param.Name, Param.Name);
		for (int32 Index = 0; Problem.IsEmpty() && Index < Param.Aliases.Num(); ++Index)
		{
			Problem = Claim(Param.Aliases[Index], Param.Name);
		}
		if (Problem.IsEmpty() && Param.Type != EMCPParamType::Array && Param.ItemType != EMCPParamType::Any)
		{
			Problem = FString::Printf(TEXT("'%s' declares an item type but is not an array"), *Param.Name);
		}
		if (Problem.IsEmpty())
		{
			Problem = ValidateValueShape(Param);
		}
		if (Problem.IsEmpty())
		{
			Problem = ValidateRoleAndDefault(Param);
		}
		if (!Problem.IsEmpty()) return Problem;
	}
	return FString();
}

FString FMCPHandlerRegistry::ValidateHandlerSpec(const FMCPHandlerSpec& Spec)
{
	const FString ParamsProblem = ValidateParamSpecs(Spec.Params);
	if (!ParamsProblem.IsEmpty()) return ParamsProblem;

	TMap<FString, bool> RequiredByName;
	for (const FMCPParamSpec& Param : Spec.Params)
	{
		RequiredByName.Add(Param.Name, Param.bRequired);
	}

	TSet<FString> Chosen;
	for (int32 ChoiceIndex = 0; ChoiceIndex < Spec.Choices.Num(); ++ChoiceIndex)
	{
		const FMCPParamChoice& Choice = Spec.Choices[ChoiceIndex];
		if (Choice.Branches.Num() < 2)
		{
			return FString::Printf(TEXT("choice %d offers fewer than two branches"), ChoiceIndex);
		}
		for (const TArray<FString>& Branch : Choice.Branches)
		{
			if (Branch.Num() == 0)
			{
				return FString::Printf(TEXT("choice %d has an empty branch"), ChoiceIndex);
			}
			for (const FString& Name : Branch)
			{
				const bool* bRequired = RequiredByName.Find(Name);
				if (!bRequired)
				{
					return FString::Printf(TEXT("choice %d names '%s', which is not a declared parameter"), ChoiceIndex, *Name);
				}
				if (*bRequired)
				{
					return FString::Printf(TEXT("'%s' is required and also a side of choice %d; the choice is what is required"), *Name, ChoiceIndex);
				}
				if (Chosen.Contains(Name))
				{
					return FString::Printf(TEXT("'%s' appears in more than one branch or choice"), *Name);
				}
				Chosen.Add(Name);
			}
		}
	}

	if (!Spec.ContractExemptReason.IsEmpty() && Spec.ContractExemptReason.TrimStartAndEnd().IsEmpty())
	{
		return TEXT("a contract exemption needs a reason");
	}
	return FString();
}

FString FMCPHandlerRegistry::ValidateValueShape(const FMCPParamSpec& Param)
{
	for (int32 Index = 0; Index < Param.OrTypes.Num(); ++Index)
	{
		const EMCPParamType OrType = Param.OrTypes[Index];
		if (OrType == EMCPParamType::Any || Param.Type == EMCPParamType::Any)
		{
			return FString::Printf(TEXT("'%s' is a union with any, which already accepts everything"), *Param.Name);
		}
		if (OrType == EMCPParamType::Array)
		{
			return FString::Printf(TEXT("'%s' lists array as an alternative type; declare the array as the parameter's own type"), *Param.Name);
		}
		if (OrType == Param.Type)
		{
			return FString::Printf(TEXT("'%s' lists its own type as an alternative"), *Param.Name);
		}
		for (int32 Other = 0; Other < Index; ++Other)
		{
			if (Param.OrTypes[Other] == OrType)
			{
				return FString::Printf(TEXT("'%s' lists an alternative type twice"), *Param.Name);
			}
		}
	}

	if (Param.LiteralValue.IsValid())
	{
		const EJson Kind = Param.LiteralValue->Type;
		const bool bFits =
			(Param.Type == EMCPParamType::Boolean && Kind == EJson::Boolean)
			|| (Param.Type == EMCPParamType::String && Kind == EJson::String)
			|| ((Param.Type == EMCPParamType::Number || Param.Type == EMCPParamType::Integer) && Kind == EJson::Number);
		if (!bFits)
		{
			return FString::Printf(TEXT("'%s' declares a literal that is not a value of its type"), *Param.Name);
		}
		if (Param.OrTypes.Num() > 0)
		{
			return FString::Printf(TEXT("'%s' is both a literal and a union"), *Param.Name);
		}
	}

	const bool bObjectShaped = Param.Type == EMCPParamType::Object
		|| (Param.Type == EMCPParamType::Array && Param.ItemType == EMCPParamType::Object);

	if (Param.Fields.Num() > 0)
	{
		if (!bObjectShaped)
		{
			return FString::Printf(TEXT("'%s' declares fields but is neither an object nor an array of objects"), *Param.Name);
		}
		TSet<FString> FieldNames;
		for (const FMCPParamField& Field : Param.Fields)
		{
			if (Field.Name.IsEmpty() || FieldNames.Contains(Field.Name))
			{
				return FString::Printf(TEXT("'%s' declares a field twice, or one with no name ('%s')"), *Param.Name, *Field.Name);
			}
			FieldNames.Add(Field.Name);
			const FString FieldProblem = ValidateField(Param.Name, Field);
			if (!FieldProblem.IsEmpty()) return FieldProblem;
		}
	}

	if (Param.Forms.Num() > 0)
	{
		if (Param.Type != EMCPParamType::Any)
		{
			return FString::Printf(TEXT("'%s' declares forms but is not of type any; the forms are its type"), *Param.Name);
		}
		if (Param.LiteralValue.IsValid() || Param.Fields.Num() > 0 || Param.Variants.Num() > 0)
		{
			return FString::Printf(TEXT("'%s' declares forms together with a literal, fields or variants"), *Param.Name);
		}
		for (int32 Index = 0; Index < Param.Forms.Num(); ++Index)
		{
			for (int32 Other = 0; Other < Index; ++Other)
			{
				if (Param.Forms[Other] == Param.Forms[Index])
				{
					return FString::Printf(TEXT("'%s' lists a form twice"), *Param.Name);
				}
			}
		}
	}

	if (Param.Variants.Num() > 0 || !Param.VariantKey.IsEmpty())
	{
		if (!bObjectShaped)
		{
			return FString::Printf(TEXT("'%s' is a tagged union but is neither an object nor an array of objects"), *Param.Name);
		}
		if (Param.Fields.Num() > 0 || Param.OrTypes.Num() > 0 || Param.LiteralValue.IsValid())
		{
			return FString::Printf(TEXT("'%s' is a tagged union and also declares fields, a union or a literal"), *Param.Name);
		}
		if (!IsParamIdentifier(Param.VariantKey))
		{
			return FString::Printf(TEXT("'%s' is a tagged union whose tag field '%s' is not an identifier"), *Param.Name, *Param.VariantKey);
		}
		if (Param.Variants.Num() < 2)
		{
			return FString::Printf(TEXT("'%s' is a tagged union with fewer than two variants"), *Param.Name);
		}
		TSet<FString> Tags;
		for (const FMCPParamVariant& Variant : Param.Variants)
		{
			if (Variant.Tag.IsEmpty() || Tags.Contains(Variant.Tag))
			{
				return FString::Printf(TEXT("'%s' declares a variant tag twice, or an empty one ('%s')"), *Param.Name, *Variant.Tag);
			}
			Tags.Add(Variant.Tag);
			const FString Owner = FString::Printf(TEXT("%s[%s=%s]"), *Param.Name, *Param.VariantKey, *Variant.Tag);
			TSet<FString> FieldNames;
			for (const FMCPParamField& Field : Variant.Fields)
			{
				if (Field.Name.IsEmpty() || FieldNames.Contains(Field.Name) || Field.Name == Param.VariantKey)
				{
					return FString::Printf(TEXT("'%s' declares a field twice, one with no name, or one named after its tag ('%s')"), *Owner, *Field.Name);
				}
				FieldNames.Add(Field.Name);
				const FString FieldProblem = ValidateField(Owner, Field);
				if (!FieldProblem.IsEmpty()) return FieldProblem;
			}
		}
	}

	if (Param.Bounds.IsSet() && Param.LiteralValue.IsValid())
	{
		return FString::Printf(TEXT("'%s' bounds a literal, which already takes one value"), *Param.Name);
	}
	return ValidateBounds(Param.Name, Param.Type, Param.ItemType, Param.Bounds);
}

FString FMCPHandlerRegistry::ValidateRoleAndDefault(const FMCPParamSpec& Param)
{
	const bool bArray = Param.Type == EMCPParamType::Array;
	auto IsOrHolds = [&Param, bArray](EMCPParamType Type)
	{
		return Param.Type == Type || (bArray && Param.ItemType == Type);
	};
	const bool bObjectElements = bArray && Param.ItemType == EMCPParamType::Object;

	if (Param.RoleKeys.Num() > 0 && !bObjectElements)
	{
		return FString::Printf(TEXT("'%s' names role keys but is not an array of objects"), *Param.Name);
	}
	switch (Param.ParamRole)
	{
	case EMCPParamRole::None:
		break;
	case EMCPParamRole::SlotIndex:
		if (!IsOrHolds(EMCPParamType::Integer) && !IsOrHolds(EMCPParamType::Number))
		{
			return FString::Printf(TEXT("'%s' is a slot index but not a number"), *Param.Name);
		}
		break;
	case EMCPParamRole::NodeRef:
	case EMCPParamRole::PinRef:
		if (!IsOrHolds(EMCPParamType::String) && !IsOrHolds(EMCPParamType::Integer) && !(bObjectElements && Param.RoleKeys.Num() > 0))
		{
			return FString::Printf(TEXT("'%s' addresses a node or pin but is not a string, an integer or an array of them"), *Param.Name);
		}
		break;
	default:
		if (!IsOrHolds(EMCPParamType::String) && !(bObjectElements && Param.RoleKeys.Num() > 0))
		{
			return FString::Printf(TEXT("'%s' has a path role but is not a string, an array of strings, or an array of objects with role keys"), *Param.Name);
		}
		break;
	}
	if (bObjectElements && Param.ParamRole != EMCPParamRole::None && Param.RoleKeys.Num() == 0)
	{
		return FString::Printf(TEXT("'%s' is an array of objects with a role and no role keys naming the element keys that carry it"), *Param.Name);
	}
	if (Param.RoleKeys.Num() > 0 && Param.ParamRole == EMCPParamRole::None)
	{
		return FString::Printf(TEXT("'%s' names role keys without a role"), *Param.Name);
	}
	for (const FString& Key : Param.RoleKeys)
	{
		if (!IsParamIdentifier(Key))
		{
			return FString::Printf(TEXT("'%s' role key '%s' is not an identifier"), *Param.Name, *Key);
		}
		if (Param.Fields.Num() > 0 && !Param.Fields.ContainsByPredicate([&Key](const FMCPParamField& Field) { return Field.Name == Key; }))
		{
			return FString::Printf(TEXT("'%s' role key '%s' is not one of its declared fields"), *Param.Name, *Key);
		}
	}

	if (!Param.DefaultValue.IsValid())
	{
		return FString();
	}
	if (Param.bRequired)
	{
		return FString::Printf(TEXT("'%s' is required and also has a default"), *Param.Name);
	}
	if (Param.LiteralValue.IsValid())
	{
		return FString::Printf(TEXT("'%s' has both a literal and a default"), *Param.Name);
	}
	auto Fits = [&Param](EMCPParamType Type) -> bool
	{
		const TSharedPtr<FJsonValue>& Value = Param.DefaultValue;
		switch (Type)
		{
		case EMCPParamType::Any: return true;
		case EMCPParamType::String: return Value->Type == EJson::String;
		case EMCPParamType::Boolean: return Value->Type == EJson::Boolean;
		case EMCPParamType::Number: return Value->Type == EJson::Number;
		case EMCPParamType::Integer: return Value->Type == EJson::Number && FMath::IsNearlyEqual(Value->AsNumber(), FMath::RoundToDouble(Value->AsNumber()));
		case EMCPParamType::Array: return Value->Type == EJson::Array;
		default: return Value->Type == EJson::Object;
		}
	};
	bool bFits = (Param.DefaultValue->Type == EJson::Null && Param.bNullable) || Fits(Param.Type);
	for (const EMCPParamType OrType : Param.OrTypes)
	{
		bFits = bFits || Fits(OrType);
	}
	if (!bFits)
	{
		return FString::Printf(TEXT("'%s' has a default that is not a value of its type"), *Param.Name);
	}
	return FString();
}

FString FMCPHandlerRegistry::ValidateField(const FString& Owner, const FMCPParamField& Field)
{
	if (!IsParamIdentifier(Field.Name))
	{
		return FString::Printf(TEXT("'%s.%s' is not an identifier"), *Owner, *Field.Name);
	}
	if (Field.Type != EMCPParamType::Array && Field.ItemType != EMCPParamType::Any)
	{
		return FString::Printf(TEXT("'%s.%s' declares an item type but is not an array"), *Owner, *Field.Name);
	}
	if (Field.Forms.Num() > 0 && Field.Type != EMCPParamType::Any)
	{
		return FString::Printf(TEXT("'%s.%s' declares forms but is not of type any; the forms are its type"), *Owner, *Field.Name);
	}
	for (int32 Index = 0; Index < Field.Forms.Num(); ++Index)
	{
		for (int32 Other = 0; Other < Index; ++Other)
		{
			if (Field.Forms[Other] == Field.Forms[Index])
			{
				return FString::Printf(TEXT("'%s.%s' lists a form twice"), *Owner, *Field.Name);
			}
		}
	}
	return ValidateBounds(Owner + TEXT(".") + Field.Name, Field.Type, Field.ItemType, Field.Bounds);
}

FString FMCPHandlerRegistry::ValidateBounds(const FString& Owner, EMCPParamType Type, EMCPParamType ItemType, const FMCPValueBounds& Bounds)
{
	const bool bArray = Type == EMCPParamType::Array;
	const EMCPParamType ValueType = bArray ? ItemType : Type;
	const bool bNumeric = ValueType == EMCPParamType::Number || ValueType == EMCPParamType::Integer;

	if ((Bounds.Min.IsSet() || Bounds.Max.IsSet()) && !bNumeric)
	{
		return FString::Printf(TEXT("'%s' has a minimum or maximum but is not a number or an array of numbers"), *Owner);
	}
	if (ValueType == EMCPParamType::Integer)
	{
		for (const TOptional<double>& Limit : { Bounds.Min, Bounds.Max })
		{
			if (Limit.IsSet() && FMath::Frac(Limit.GetValue()) != 0.0)
			{
				return FString::Printf(TEXT("'%s' is an integer with a fractional bound"), *Owner);
			}
		}
	}
	if ((Bounds.MinLength.IsSet() || Bounds.MaxLength.IsSet()) && ValueType != EMCPParamType::String)
	{
		return FString::Printf(TEXT("'%s' has a length bound but is not a string or an array of strings"), *Owner);
	}
	if ((Bounds.MinItems.IsSet() || Bounds.MaxItems.IsSet()) && !bArray)
	{
		return FString::Printf(TEXT("'%s' has an item count bound but is not an array"), *Owner);
	}
	for (const TOptional<int32>& Count : { Bounds.MinLength, Bounds.MaxLength, Bounds.MinItems, Bounds.MaxItems })
	{
		if (Count.IsSet() && Count.GetValue() < 0)
		{
			return FString::Printf(TEXT("'%s' has a negative length or item count bound"), *Owner);
		}
	}
	if ((Bounds.Min.IsSet() && Bounds.Max.IsSet() && Bounds.Min.GetValue() > Bounds.Max.GetValue())
		|| (Bounds.MinLength.IsSet() && Bounds.MaxLength.IsSet() && Bounds.MinLength.GetValue() > Bounds.MaxLength.GetValue())
		|| (Bounds.MinItems.IsSet() && Bounds.MaxItems.IsSet() && Bounds.MinItems.GetValue() > Bounds.MaxItems.GetValue()))
	{
		return FString::Printf(TEXT("'%s' has a lower bound above its upper bound"), *Owner);
	}
	return FString();
}

const TCHAR* FMCPHandlerRegistry::ValueFormName(EMCPValueForm Form)
{
	switch (Form)
	{
	case EMCPValueForm::ArgMap:       return TEXT("argMap");
	case EMCPValueForm::ArgEntryList: return TEXT("argEntryList");
	case EMCPValueForm::StringList:   return TEXT("stringList");
	default:                          return TEXT("string");
	}
}

const TCHAR* FMCPHandlerRegistry::ChoiceModeName(EMCPChoiceMode Mode)
{
	return Mode == EMCPChoiceMode::AtLeastOne ? TEXT("atLeastOne") : TEXT("exactlyOne");
}

const TCHAR* FMCPHandlerRegistry::ParamTypeName(EMCPParamType Type)
{
	switch (Type)
	{
	case EMCPParamType::String:  return TEXT("string");
	case EMCPParamType::Number:  return TEXT("number");
	case EMCPParamType::Integer: return TEXT("integer");
	case EMCPParamType::Boolean: return TEXT("boolean");
	case EMCPParamType::Object:  return TEXT("object");
	case EMCPParamType::Array:   return TEXT("array");
	case EMCPParamType::Vec3:    return TEXT("vec3");
	case EMCPParamType::Rotator: return TEXT("rotator");
	case EMCPParamType::Color:   return TEXT("color");
	default:                     return TEXT("any");
	}
}

const TCHAR* FMCPHandlerRegistry::ParamRoleName(EMCPParamRole Role)
{
	switch (Role)
	{
	case EMCPParamRole::EditTarget: return TEXT("editTarget");
	case EMCPParamRole::NodeRef:    return TEXT("nodeRef");
	case EMCPParamRole::PinRef:     return TEXT("pinRef");
	case EMCPParamRole::SlotIndex:  return TEXT("slotIndex");
	case EMCPParamRole::ActorRef:   return TEXT("actorRef");
	case EMCPParamRole::OutputPath: return TEXT("outputPath");
	default:                        return TEXT("none");
	}
}

const TCHAR* FMCPHandlerRegistry::CommitPolicyName(EMCPCommitPolicy Policy)
{
	switch (Policy)
	{
	case EMCPCommitPolicy::Compile: return TEXT("compile");
	case EMCPCommitPolicy::Save:    return TEXT("save");
	case EMCPCommitPolicy::Both:    return TEXT("both");
	default:                        return TEXT("none");
	}
}

TSharedPtr<FJsonObject> FMCPHandlerRegistry::BuildHandlerSpecsJson() const
{
	TArray<FString> Methods;
	HandlerSpecs.GetKeys(Methods);
	Methods.Sort();

	auto FormsJson = [](const TArray<EMCPValueForm>& Forms)
	{
		TArray<FString> Names;
		for (const EMCPValueForm Form : Forms)
		{
			Names.Add(ValueFormName(Form));
		}
		return MCPStringListToJson(Names);
	};
	// Written only when set, so an unbounded value publishes what it did before bounds existed.
	auto WriteBounds = [](const TSharedPtr<FJsonObject>& Entry, const FMCPValueBounds& Bounds)
	{
		if (Bounds.Min.IsSet()) Entry->SetNumberField(TEXT("min"), Bounds.Min.GetValue());
		if (Bounds.Max.IsSet()) Entry->SetNumberField(TEXT("max"), Bounds.Max.GetValue());
		if (Bounds.MinLength.IsSet()) Entry->SetNumberField(TEXT("minLength"), Bounds.MinLength.GetValue());
		if (Bounds.MaxLength.IsSet()) Entry->SetNumberField(TEXT("maxLength"), Bounds.MaxLength.GetValue());
		if (Bounds.MinItems.IsSet()) Entry->SetNumberField(TEXT("minItems"), Bounds.MinItems.GetValue());
		if (Bounds.MaxItems.IsSet()) Entry->SetNumberField(TEXT("maxItems"), Bounds.MaxItems.GetValue());
	};
	auto FieldsJson = [&FormsJson, &WriteBounds](const TArray<FMCPParamField>& Fields)
	{
		TArray<TSharedPtr<FJsonValue>> FieldValues;
		for (const FMCPParamField& Field : Fields)
		{
			TSharedPtr<FJsonObject> FieldEntry = MakeShared<FJsonObject>();
			FieldEntry->SetStringField(TEXT("name"), Field.Name);
			FieldEntry->SetStringField(TEXT("type"), ParamTypeName(Field.Type));
			FieldEntry->SetBoolField(TEXT("required"), Field.bRequired);
			FieldEntry->SetStringField(TEXT("description"), Field.Description);
			if (Field.Type == EMCPParamType::Array && Field.ItemType != EMCPParamType::Any)
			{
				FieldEntry->SetStringField(TEXT("items"), ParamTypeName(Field.ItemType));
			}
			if (Field.Forms.Num() > 0)
			{
				FieldEntry->SetArrayField(TEXT("forms"), FormsJson(Field.Forms));
			}
			WriteBounds(FieldEntry, Field.Bounds);
			FieldValues.Add(MakeShared<FJsonValueObject>(FieldEntry));
		}
		return FieldValues;
	};

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	for (const FString& Method : Methods)
	{
		const FMCPHandlerSpec& Spec = HandlerSpecs.FindChecked(Method);
		TArray<TSharedPtr<FJsonValue>> ParamValues;
		for (const FMCPParamSpec& Param : Spec.Params)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Param.Name);
			Entry->SetStringField(TEXT("type"), ParamTypeName(Param.Type));
			Entry->SetBoolField(TEXT("required"), Param.bRequired);
			Entry->SetStringField(TEXT("description"), Param.Description);
			if (Param.Aliases.Num() > 0)
			{
				Entry->SetArrayField(TEXT("aliases"), MCPStringListToJson(Param.Aliases));
			}
			if (Param.Type == EMCPParamType::Array && Param.ItemType != EMCPParamType::Any)
			{
				Entry->SetStringField(TEXT("items"), ParamTypeName(Param.ItemType));
			}
			// Written only when set, so a spec that uses none of them publishes
			// exactly what it did before they existed.
			if (Param.bNullable)
			{
				Entry->SetBoolField(TEXT("nullable"), true);
			}
			if (Param.OrTypes.Num() > 0)
			{
				TArray<FString> OrNames;
				for (const EMCPParamType OrType : Param.OrTypes)
				{
					OrNames.Add(ParamTypeName(OrType));
				}
				Entry->SetArrayField(TEXT("orTypes"), MCPStringListToJson(OrNames));
			}
			if (Param.LiteralValue.IsValid())
			{
				Entry->SetField(TEXT("literal"), Param.LiteralValue);
			}
			if (Param.Fields.Num() > 0)
			{
				Entry->SetArrayField(TEXT("fields"), FieldsJson(Param.Fields));
			}
			if (Param.Forms.Num() > 0)
			{
				Entry->SetArrayField(TEXT("forms"), FormsJson(Param.Forms));
			}
			WriteBounds(Entry, Param.Bounds);
			if (Param.Variants.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> VariantValues;
				for (const FMCPParamVariant& Variant : Param.Variants)
				{
					TSharedPtr<FJsonObject> VariantEntry = MakeShared<FJsonObject>();
					VariantEntry->SetStringField(TEXT("tag"), Variant.Tag);
					VariantEntry->SetStringField(TEXT("description"), Variant.Description);
					VariantEntry->SetArrayField(TEXT("fields"), FieldsJson(Variant.Fields));
					VariantValues.Add(MakeShared<FJsonValueObject>(VariantEntry));
				}
				TSharedPtr<FJsonObject> OneOf = MakeShared<FJsonObject>();
				OneOf->SetStringField(TEXT("key"), Param.VariantKey);
				OneOf->SetArrayField(TEXT("variants"), VariantValues);
				Entry->SetObjectField(TEXT("oneOf"), OneOf);
			}
			if (Param.ParamRole != EMCPParamRole::None)
			{
				Entry->SetStringField(TEXT("role"), ParamRoleName(Param.ParamRole));
			}
			if (Param.RoleKeys.Num() > 0)
			{
				Entry->SetArrayField(TEXT("roleKeys"), MCPStringListToJson(Param.RoleKeys));
			}
			if (Param.DefaultValue.IsValid())
			{
				Entry->SetField(TEXT("default"), Param.DefaultValue);
			}
			ParamValues.Add(MakeShared<FJsonValueObject>(Entry));
		}

		TSharedPtr<FJsonObject> MethodEntry = MakeShared<FJsonObject>();
		if (const FString* Category = HandlerCategories.Find(Method))
		{
			MethodEntry->SetStringField(TEXT("category"), *Category);
		}
		MethodEntry->SetArrayField(TEXT("params"), ParamValues);
		if (Spec.Choices.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ChoiceValues;
			for (const FMCPParamChoice& Choice : Spec.Choices)
			{
				TArray<TSharedPtr<FJsonValue>> BranchValues;
				for (const TArray<FString>& Branch : Choice.Branches)
				{
					BranchValues.Add(MakeShared<FJsonValueArray>(MCPStringListToJson(Branch)));
				}
				TSharedPtr<FJsonObject> ChoiceEntry = MakeShared<FJsonObject>();
				ChoiceEntry->SetStringField(TEXT("mode"), ChoiceModeName(Choice.Mode));
				ChoiceEntry->SetArrayField(TEXT("branches"), BranchValues);
				ChoiceValues.Add(MakeShared<FJsonValueObject>(ChoiceEntry));
			}
			MethodEntry->SetArrayField(TEXT("choices"), ChoiceValues);
		}
		if (!Spec.ContractExemptReason.IsEmpty())
		{
			MethodEntry->SetStringField(TEXT("contractExempt"), Spec.ContractExemptReason);
		}
		if (Spec.CommitPolicy.IsSet())
		{
			MethodEntry->SetStringField(TEXT("commit"), CommitPolicyName(Spec.CommitPolicy.GetValue()));
		}
		Out->SetObjectField(Method, MethodEntry);
	}
	return Out;
}

TSharedPtr<FJsonObject> FMCPHandlerRegistry::ResolveParamAliases(const FMCPHandlerSpec& Spec, const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) return Params;

	TSharedPtr<FJsonObject> Resolved;
	for (const FMCPParamSpec& Param : Spec.Params)
	{
		if (Param.Aliases.Num() == 0 || Params->HasField(Param.Name)) continue;
		for (const FString& Alias : Param.Aliases)
		{
			const TSharedPtr<FJsonValue> Value = Params->TryGetField(Alias);
			if (!Value.IsValid()) continue;
			if (!Resolved.IsValid())
			{
				// Copied rather than edited: the caller's object is also what the
				// parameter echo recorded.
				Resolved = MakeShared<FJsonObject>();
				for (const auto& JsonEntry : Params->Values)
				{
					const TPair<FString, TSharedPtr<FJsonValue>> Pair(JsonEntry.Key, JsonEntry.Value);
					Resolved->SetField(Pair.Key, Pair.Value);
				}
			}
			Resolved->RemoveField(Alias);
			Resolved->SetField(Param.Name, Value);
			break;
		}
	}
	return Resolved.IsValid() ? Resolved : Params;
}

void FMCPHandlerRegistry::RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds)
{
	CppHandlers.Add(MethodName, Handler);
	TagCategory(MethodName);
	if (TimeoutSeconds > 0.0f)
	{
		HandlerTimeouts.Add(MethodName, TimeoutSeconds);
	}
}

bool FMCPHandlerRegistry::RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds, const TArray<FMCPParamSpec>& Params)
{
	return RegisterHandlerWithTimeout(MethodName, MoveTemp(Handler), TimeoutSeconds, Params, FMCPSpecRules());
}

bool FMCPHandlerRegistry::RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds, const TArray<FMCPParamSpec>& Params, const FMCPSpecRules& Rules)
{
	const bool bAccepted = RegisterHandler(MethodName, MoveTemp(Handler), Params, Rules);
	if (TimeoutSeconds > 0.0f)
	{
		HandlerTimeouts.Add(MethodName, TimeoutSeconds);
	}
	return bAccepted;
}

float FMCPHandlerRegistry::GetHandlerTimeout(const FString& MethodName) const
{
	if (const float* V = HandlerTimeouts.Find(MethodName))
	{
		return *V;
	}
	// External (plugin-contributed) handlers may register their own timeout.
	return UEMCP::GetExternalHandlerTimeout(MethodName);
}

TSharedPtr<FJsonValue> FMCPHandlerRegistry::ExecuteHandler(const FString& MethodName, const TSharedPtr<FJsonObject>& Params)
{
	// Try C++ handler first
	if (const FHandlerFunction* Handler = CppHandlers.Find(MethodName))
	{
		// #1057: a spec'd handler reads its parameters by their declared names only.
		const FMCPHandlerSpec* Spec = HandlerSpecs.Find(MethodName);
		const TSharedPtr<FJsonObject> Effective = Spec ? ResolveParamAliases(*Spec, Params) : Params;

		const FString* Category = HandlerCategories.Find(MethodName);
		if (!Category || !Effective.IsValid())
		{
			return (*Handler)(Effective);
		}
		// #1057: a key the handler never read had no effect, so say so.
		FMCPParamReadScope ReadScope(Effective);
		TSharedPtr<FJsonValue> Result = (*Handler)(Effective);
		MCPAttachParamsNotRead(Result, ReadScope.Unread());
		return Result;
	}

	// Plugin-contributed external handler (registered via UEMCP::RegisterExternalHandler).
	{
		UEMCP::FExternalHandlerFn External;
		float Unused = 0.0f;
		if (UEMCP::LookupExternalHandler(MethodName, External, Unused))
		{
			return External(Params);
		}
	}

	// Handler not found - return nullptr so BridgeServer sends "Unknown method" error
	return nullptr;
}

bool FMCPHandlerRegistry::HasHandler(const FString& MethodName) const
{
	if (CppHandlers.Contains(MethodName))
	{
		return true;
	}
	return UEMCP::HasExternalHandler(MethodName);
}

TArray<FString> FMCPHandlerRegistry::GetHandlerNames() const
{
	TArray<FString> Names;
	CppHandlers.GetKeys(Names);

	Names.Append(UEMCP::GetExternalHandlerNames());

	return Names;
}

void FMCPHandlerRegistry::Clear()
{
	CppHandlers.Empty();
	HandlerTimeouts.Empty();
	HandlerCategories.Empty();
	HandlerSpecs.Empty();
	RefusedSpecs.Empty();
}
