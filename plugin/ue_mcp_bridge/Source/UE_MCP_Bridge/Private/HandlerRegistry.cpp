#include "HandlerRegistry.h"
#include "MCPContract.h"
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
	HandlerFacts.Remove(MethodName);
}

void FMCPHandlerRegistry::SetFacts(const FString& MethodName, const FMCPActionFacts& Facts)
{
	if (Facts.IsDefault())
	{
		HandlerFacts.Remove(MethodName);
	}
	else
	{
		HandlerFacts.Add(MethodName, Facts);
	}
}

bool FMCPHandlerRegistry::RegisterHandler(const FString& MethodName, FHandlerFunction Handler, const TArray<FMCPParamSpec>& Params)
{
	return RegisterHandler(MethodName, MoveTemp(Handler), Params, FMCPSpecRules());
}

bool FMCPHandlerRegistry::RegisterHandler(const FString& MethodName, FHandlerFunction Handler, const TArray<FMCPParamSpec>& Params, const FMCPSpecRules& Rules)
{
	RegisterHandler(MethodName, MoveTemp(Handler));
	// The facts hold whether or not the parameter list is accepted: a refused
	// spec must not also lose the handler its timeout or its modal safety.
	SetFacts(MethodName, Rules.Facts);
	FMCPHandlerSpec Spec;
	Spec.Params = Params;
	Spec.Choices = Rules.Choices;
	Spec.ContractExemptReason = Rules.ContractExemptReason;
	Spec.Facts = Rules.Facts;
	const FString Problem = ValidateHandlerSpec(Spec);
	if (!Problem.IsEmpty())
	{
		HandlerSpecs.Remove(MethodName);
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
			Problem = ValidateDefault(Param);
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
	return ValidateFacts(Spec.Facts);
}

FString FMCPHandlerRegistry::ValidateFacts(const FMCPActionFacts& Facts)
{
	if (Facts.TimeoutSeconds < 0.0f)
	{
		return TEXT("a timeout is never negative; 0 is the bridge default");
	}
	if (!Facts.RefusedInBatchReason.IsEmpty() && Facts.RefusedInBatchReason.TrimStartAndEnd().IsEmpty())
	{
		return TEXT("a batch refusal needs a reason");
	}
	if (!Facts.ServerInternalCaller.IsEmpty() && Facts.ServerInternalCaller.TrimStartAndEnd().IsEmpty())
	{
		return TEXT("a server-internal action needs to name its caller");
	}
	if (Facts.bServedOffGameThread && Facts.TimeoutSeconds > 0.0f)
	{
		return TEXT("an action served off the game thread never waits on it, so it has no game-thread timeout");
	}
	return FString();
}

FString FMCPHandlerRegistry::ValidateDefault(const FMCPParamSpec& Param)
{
	if (!Param.DefaultValue.IsValid()) return FString();
	if (Param.bRequired)
	{
		return FString::Printf(TEXT("'%s' is required and also declares a default"), *Param.Name);
	}
	const EJson Kind = Param.DefaultValue->Type;
	auto Fits = [Kind](EMCPParamType Type)
	{
		switch (Type)
		{
		case EMCPParamType::Boolean: return Kind == EJson::Boolean;
		case EMCPParamType::String:  return Kind == EJson::String;
		case EMCPParamType::Number:
		case EMCPParamType::Integer: return Kind == EJson::Number;
		default:                     return false;
		}
	};
	bool bFits = Fits(Param.Type);
	for (const EMCPParamType OrType : Param.OrTypes)
	{
		bFits = bFits || Fits(OrType);
	}
	if (!bFits)
	{
		return FString::Printf(TEXT("'%s' declares a default that is not a boolean, string or number of its type"), *Param.Name);
	}
	if (Kind == EJson::Number)
	{
		const double Value = Param.DefaultValue->AsNumber();
		if (Param.Type == EMCPParamType::Integer && FMath::Frac(Value) != 0.0)
		{
			return FString::Printf(TEXT("'%s' is an integer with a fractional default"), *Param.Name);
		}
		if ((Param.Minimum.IsSet() && Value < Param.Minimum.GetValue()) || (Param.Maximum.IsSet() && Value > Param.Maximum.GetValue()))
		{
			return FString::Printf(TEXT("'%s' declares a default outside its own range"), *Param.Name);
		}
	}
	if (Kind == EJson::String && Param.EnumValues.Num() > 0 && !Param.EnumValues.Contains(Param.DefaultValue->AsString()))
	{
		return FString::Printf(TEXT("'%s' declares a default that is not one of its enum values"), *Param.Name);
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

	{
		const FString RuleProblem = ValidateValueRules(Param.Name, Param.Type, Param.ItemType, Param.EnumValues, Param.Minimum, Param.Maximum);
		if (!RuleProblem.IsEmpty()) return RuleProblem;
		if (Param.EnumValues.Num() > 0 && (Param.LiteralValue.IsValid() || Param.OrTypes.Num() > 0))
		{
			return FString::Printf(TEXT("'%s' is an enum and also a literal or a union"), *Param.Name);
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
	const FString Path = FString::Printf(TEXT("%s.%s"), *Owner, *Field.Name);
	const FString RuleProblem = ValidateValueRules(Path, Field.Type, Field.ItemType, Field.EnumValues, Field.Minimum, Field.Maximum);
	if (!RuleProblem.IsEmpty()) return RuleProblem;
	if (Field.Fields.Num() > 0)
	{
		const bool bObjectShaped = Field.Type == EMCPParamType::Object
			|| (Field.Type == EMCPParamType::Array && Field.ItemType == EMCPParamType::Object);
		if (!bObjectShaped)
		{
			return FString::Printf(TEXT("'%s' declares fields but is neither an object nor an array of objects"), *Path);
		}
		TSet<FString> Names;
		for (const FMCPParamField& Nested : Field.Fields)
		{
			if (Nested.Name.IsEmpty() || Names.Contains(Nested.Name))
			{
				return FString::Printf(TEXT("'%s' declares a field twice, or one with no name ('%s')"), *Path, *Nested.Name);
			}
			Names.Add(Nested.Name);
			const FString NestedProblem = ValidateField(Path, Nested);
			if (!NestedProblem.IsEmpty()) return NestedProblem;
		}
	}
	return FString();
}

FString FMCPHandlerRegistry::ValidateValueRules(const FString& Owner, EMCPParamType Type, EMCPParamType ItemType,
	const TArray<FString>& EnumValues, const TOptional<double>& Minimum, const TOptional<double>& Maximum)
{
	// Rules on an array apply to each element.
	const EMCPParamType ValueType = Type == EMCPParamType::Array ? ItemType : Type;
	if (EnumValues.Num() > 0)
	{
		if (ValueType != EMCPParamType::String)
		{
			return FString::Printf(TEXT("'%s' declares enum values but is not a string or an array of strings"), *Owner);
		}
		TSet<FString> Seen;
		for (const FString& Value : EnumValues)
		{
			if (Value.IsEmpty() || Seen.Contains(Value))
			{
				return FString::Printf(TEXT("'%s' lists an enum value twice, or an empty one"), *Owner);
			}
			int32 Unused = 0;
			if (Value.FindChar(TEXT(','), Unused) || Value.FindChar(TEXT('{'), Unused) || Value.FindChar(TEXT('}'), Unused))
			{
				return FString::Printf(TEXT("'%s' enum value '%s' contains ',', '{' or '}', which a signature cannot write"), *Owner, *Value);
			}
			Seen.Add(Value);
		}
	}
	if (Minimum.IsSet() || Maximum.IsSet())
	{
		if (ValueType != EMCPParamType::Number && ValueType != EMCPParamType::Integer)
		{
			return FString::Printf(TEXT("'%s' declares a numeric range but is not a number or an integer"), *Owner);
		}
		if (Minimum.IsSet() && Maximum.IsSet() && Minimum.GetValue() > Maximum.GetValue())
		{
			return FString::Printf(TEXT("'%s' declares a minimum above its maximum"), *Owner);
		}
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
	case EMCPValueForm::ScalarMap:    return TEXT("scalarMap");
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

TSharedPtr<FJsonObject> FMCPHandlerRegistry::BuildHandlerSpecsJson() const
{
	return SpecsToJson(HandlerSpecs, HandlerCategories);
}

TSharedPtr<FJsonObject> FMCPHandlerRegistry::BuildExternalHandlerSpecsJson()
{
	return SpecsToJson(UEMCP::GetExternalHandlerSpecs(), TMap<FString, FString>());
}

TSharedPtr<FJsonObject> FMCPHandlerRegistry::SpecsToJson(const TMap<FString, FMCPHandlerSpec>& Specs, const TMap<FString, FString>& Categories)
{
	TArray<FString> Methods;
	Specs.GetKeys(Methods);
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
	// Written only when set, so a spec that uses none of them publishes exactly what it did before.
	auto AddValueRules = [](const TSharedPtr<FJsonObject>& Entry, const TArray<FString>& EnumValues,
		const TOptional<double>& Minimum, const TOptional<double>& Maximum)
	{
		if (EnumValues.Num() > 0)
		{
			Entry->SetArrayField(TEXT("enum"), MCPStringListToJson(EnumValues));
		}
		if (Minimum.IsSet())
		{
			Entry->SetNumberField(TEXT("minimum"), Minimum.GetValue());
		}
		if (Maximum.IsSet())
		{
			Entry->SetNumberField(TEXT("maximum"), Maximum.GetValue());
		}
	};
	TFunction<TArray<TSharedPtr<FJsonValue>>(const TArray<FMCPParamField>&)> FieldsJson;
	FieldsJson = [&FormsJson, &AddValueRules, &FieldsJson](const TArray<FMCPParamField>& Fields)
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
			AddValueRules(FieldEntry, Field.EnumValues, Field.Minimum, Field.Maximum);
			if (Field.Fields.Num() > 0)
			{
				FieldEntry->SetArrayField(TEXT("fields"), FieldsJson(Field.Fields));
			}
			FieldValues.Add(MakeShared<FJsonValueObject>(FieldEntry));
		}
		return FieldValues;
	};

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	for (const FString& Method : Methods)
	{
		const FMCPHandlerSpec& Spec = Specs.FindChecked(Method);
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
			if (Param.DefaultValue.IsValid())
			{
				Entry->SetField(TEXT("default"), Param.DefaultValue);
			}
			AddValueRules(Entry, Param.EnumValues, Param.Minimum, Param.Maximum);
			if (Param.Fields.Num() > 0)
			{
				Entry->SetArrayField(TEXT("fields"), FieldsJson(Param.Fields));
			}
			if (Param.Forms.Num() > 0)
			{
				Entry->SetArrayField(TEXT("forms"), FormsJson(Param.Forms));
			}
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
			ParamValues.Add(MakeShared<FJsonValueObject>(Entry));
		}

		TSharedPtr<FJsonObject> MethodEntry = MakeShared<FJsonObject>();
		if (const FString* Category = Categories.Find(Method))
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
	// Adds a timeout to whatever the method already declared: a spec registered
	// just before, by the same name, keeps its parameters and its other facts.
	CppHandlers.Add(MethodName, MoveTemp(Handler));
	TagCategory(MethodName);
	FMCPActionFacts Facts = HandlerFacts.FindRef(MethodName);
	Facts.TimeoutSeconds = FMath::Max(TimeoutSeconds, 0.0f);
	SetFacts(MethodName, Facts);
	if (FMCPHandlerSpec* Spec = HandlerSpecs.Find(MethodName))
	{
		Spec->Facts = Facts;
	}
}

bool FMCPHandlerRegistry::RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds, const TArray<FMCPParamSpec>& Params)
{
	return RegisterHandlerWithTimeout(MethodName, MoveTemp(Handler), TimeoutSeconds, Params, FMCPSpecRules());
}

bool FMCPHandlerRegistry::RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds, const TArray<FMCPParamSpec>& Params, const FMCPSpecRules& Rules)
{
	return RegisterHandler(MethodName, MoveTemp(Handler), Params, Rules.Timeout(FMath::Max(TimeoutSeconds, 0.0f)));
}

float FMCPHandlerRegistry::GetHandlerTimeout(const FString& MethodName) const
{
	return GetActionFacts(MethodName).TimeoutSeconds;
}

FMCPActionFacts FMCPHandlerRegistry::GetActionFacts(const FString& MethodName) const
{
	if (CppHandlers.Contains(MethodName))
	{
		const FMCPActionFacts* Facts = HandlerFacts.Find(MethodName);
		return Facts ? *Facts : FMCPActionFacts();
	}
	// External (plugin-contributed) handlers declare theirs with the contract, or a timeout alone.
	FMCPHandlerSpec ExternalSpec;
	FMCPActionFacts Facts;
	if (UEMCP::LookupExternalHandlerSpec(MethodName, ExternalSpec))
	{
		Facts = ExternalSpec.Facts;
	}
	const float ExternalTimeout = UEMCP::GetExternalHandlerTimeout(MethodName);
	if (ExternalTimeout > 0.0f)
	{
		Facts.TimeoutSeconds = ExternalTimeout;
	}
	return Facts;
}

TSharedPtr<FJsonObject> FMCPHandlerRegistry::FactsToJson(const FMCPActionFacts& Facts)
{
	// Only what differs from the common case, so most actions publish nothing.
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	if (Facts.TimeoutSeconds > 0.0f) Entry->SetNumberField(TEXT("timeoutSeconds"), Facts.TimeoutSeconds);
	if (Facts.bModalSafe) Entry->SetBoolField(TEXT("modalSafe"), true);
	if (Facts.bServedOffGameThread) Entry->SetBoolField(TEXT("servedOffGameThread"), true);
	if (Facts.bNotEchoed) Entry->SetBoolField(TEXT("notEchoed"), true);
	if (!Facts.RefusedInBatchReason.IsEmpty()) Entry->SetStringField(TEXT("refusedInBatch"), Facts.RefusedInBatchReason);
	if (!Facts.ServerInternalCaller.IsEmpty()) Entry->SetStringField(TEXT("serverInternal"), Facts.ServerInternalCaller);
	return Entry;
}

TSharedPtr<FJsonObject> FMCPHandlerRegistry::BuildActionFactsJson() const
{
	TArray<FString> Methods;
	HandlerFacts.GetKeys(Methods);
	Methods.Sort();
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	for (const FString& Method : Methods)
	{
		Out->SetObjectField(Method, FactsToJson(HandlerFacts.FindChecked(Method)));
	}
	return Out;
}

TSharedPtr<FJsonObject> FMCPHandlerRegistry::BuildExternalActionFactsJson() const
{
	TArray<FString> Methods = UEMCP::GetExternalHandlerNames();
	Methods.Sort();
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	for (const FString& Method : Methods)
	{
		if (CppHandlers.Contains(Method)) continue;
		const FMCPActionFacts Facts = GetActionFacts(Method);
		if (!Facts.IsDefault()) Out->SetObjectField(Method, FactsToJson(Facts));
	}
	return Out;
}

namespace
{
	// Per thread: a handler runs on the game thread, a test may run one
	// wherever it likes, and neither must see the other's registry.
	thread_local FMCPHandlerRegistry* GExecutingHandlerRegistry = nullptr;
}

FMCPHandlerRegistry* FMCPHandlerRegistry::Executing()
{
	return GExecutingHandlerRegistry;
}

TSharedPtr<FJsonValue> FMCPHandlerRegistry::ExecuteHandler(const FString& MethodName, const TSharedPtr<FJsonObject>& Params)
{
	// Restored on the way out, so a batch step's own dispatch nests and a
	// handler that returns leaves the slot as it found it.
	TGuardValue<FMCPHandlerRegistry*> ExecutingScope(GExecutingHandlerRegistry, this);

	// Try C++ handler first
	if (const FHandlerFunction* Handler = CppHandlers.Find(MethodName))
	{
		// #1057: a spec'd handler reads its parameters by their declared names only.
		const FMCPHandlerSpec* Spec = HandlerSpecs.Find(MethodName);
		const TSharedPtr<FJsonObject> Effective = Spec ? ResolveParamAliases(*Spec, Params) : Params;

		// Files a write checked out in auto checkout mode are reported on the result.
		const int32 CheckoutMark = MCPSourceControl::AutoCheckoutMark();
		const FString* Category = HandlerCategories.Find(MethodName);
		if (!Category || !Effective.IsValid())
		{
			TSharedPtr<FJsonValue> Result = (*Handler)(Effective);
			MCPSourceControl::AttachAutoCheckouts(Result, CheckoutMark);
			CheckRollback(MethodName, Result);
			return Result;
		}
		// #1057: a key the handler never read had no effect, so say so.
		FMCPParamReadScope ReadScope(Effective);
		// #1910: save=false keeps every package this call saves in memory, dirty.
		TOptional<MCPSavePolicy::FScope> SaveScope;
		if (SavePolicyMethods.Contains(MethodName))
		{
			SaveScope.Emplace(!OptionalBool(Effective, TEXT("save"), true));
		}
		TSharedPtr<FJsonValue> Result = (*Handler)(Effective);
		if (SaveScope.IsSet())
		{
			MCPSavePolicy::StampDeferred(Result, SaveScope->Deferred());
			SaveScope.Reset();
		}
		MCPAttachParamsNotRead(Result, ReadScope.Unread());
		MCPSourceControl::AttachAutoCheckouts(Result, CheckoutMark);
		CheckRollback(MethodName, Result);
		return Result;
	}

	// Plugin-contributed external handler (registered via UEMCP::RegisterExternalHandler).
	{
		UEMCP::FExternalHandlerFn External;
		float Unused = 0.0f;
		if (UEMCP::LookupExternalHandler(MethodName, External, Unused))
		{
			// #1282: a plugin handler with a contract is held to it here, whoever sent the call, and reads its
			// parameters by their declared names. Plugins read FJsonObject directly, which read tracking cannot
			// see, so an undeclared key is refused rather than reported as unread.
			FMCPHandlerSpec Spec;
			if (!UEMCP::LookupExternalHandlerSpec(MethodName, Spec))
			{
				return External(Params);
			}
			const FString Problem = UEMCP::ContractViolation(Spec, Params);
			if (!Problem.IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("Invalid parameters for %s: %s"), *MethodName, *Problem));
			}
			return External(ResolveParamAliases(Spec, Params));
		}
	}

	// Handler not found - return nullptr so BridgeServer sends "Unknown method" error
	return nullptr;
}

FString FMCPHandlerRegistry::RollbackProblem(const TSharedPtr<FJsonValue>& Result) const
{
	const TSharedPtr<FJsonObject>* Body = nullptr;
	const TSharedPtr<FJsonObject>* Rollback = nullptr;
	if (!Result.IsValid() || !Result->TryGetObject(Body) || !(*Body)->TryGetObjectField(TEXT("rollback"), Rollback))
	{
		return FString();
	}
	FString Inverse;
	if (!(*Rollback)->TryGetStringField(TEXT("method"), Inverse) || Inverse.IsEmpty())
	{
		return TEXT("the rollback names no method");
	}
	if (!HasHandler(Inverse))
	{
		return bCatalogComplete
			? FString::Printf(TEXT("the rollback names '%s', which nothing registered"), *Inverse)
			: FString();
	}
	FMCPHandlerSpec ExternalSpec;
	const FMCPHandlerSpec* Spec = HandlerSpecs.Find(Inverse);
	if (!Spec && UEMCP::LookupExternalHandlerSpec(Inverse, ExternalSpec))
	{
		Spec = &ExternalSpec;
	}
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	if (!Spec || !(*Rollback)->TryGetObjectField(TEXT("payload"), Payload))
	{
		return FString();
	}
	TSet<FString> Accepted;
	for (const FMCPParamSpec& Param : Spec->Params)
	{
		Accepted.Add(Param.Name);
		Accepted.Append(Param.Aliases);
	}
	for (const auto& Entry : (*Payload)->Values)
	{
		// The key type of FJsonObject::Values differs between engine versions.
		const FString Key(Entry.Key);
		if (!Accepted.Contains(Key))
		{
			return FString::Printf(TEXT("the rollback payload sends '%s', which '%s' does not declare"), *Key, *Inverse);
		}
	}
	return FString();
}

void FMCPHandlerRegistry::CheckRollback(const FString& MethodName, const TSharedPtr<FJsonValue>& Result) const
{
	// The inverse is a method name and a payload built at runtime, so it is held
	// to the registry here, on every call that returns one. An error log fails
	// whichever automation test reached it.
	const FString Problem = RollbackProblem(Result);
	if (Problem.IsEmpty()) return;
	UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] %s returned a rollback the bridge cannot run: %s"), *MethodName, *Problem);
	Result->AsObject()->SetStringField(TEXT("rollbackProblem"), Problem);
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
	HandlerFacts.Empty();
	HandlerCategories.Empty();
	HandlerSpecs.Empty();
	SavePolicyMethods.Empty();
}

bool FMCPHandlerRegistry::AdoptSavePolicy(const FString& MethodName, const FMCPParamSpec& Save)
{
	FMCPHandlerSpec* Spec = HandlerSpecs.Find(MethodName);
	if (!Spec)
	{
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] AdoptSavePolicy: '%s' is not registered with a spec, so it cannot take '%s'."),
			*MethodName, *Save.Name);
		return false;
	}
	const bool bDeclared = Spec->Params.ContainsByPredicate(
		[&Save](const FMCPParamSpec& Param) { return Param.Name == Save.Name; });
	if (!bDeclared) Spec->Params.Add(Save);
	SavePolicyMethods.Add(MethodName);
	return true;
}
