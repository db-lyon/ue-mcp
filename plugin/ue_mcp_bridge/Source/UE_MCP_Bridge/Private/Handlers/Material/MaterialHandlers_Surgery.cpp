#include "MaterialHandlers.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	TSharedPtr<FJsonObject> SurgerySourceJson(const FExpressionInput& Input)
	{
		auto Source = MakeShared<FJsonObject>();
		Source->SetStringField(TEXT("expressionName"), Input.Expression ? Input.Expression->GetName() : TEXT(""));
		Source->SetNumberField(TEXT("outputIndex"), Input.OutputIndex);
		Source->SetNumberField(TEXT("mask"), Input.Mask);
		Source->SetNumberField(TEXT("maskR"), Input.MaskR);
		Source->SetNumberField(TEXT("maskG"), Input.MaskG);
		Source->SetNumberField(TEXT("maskB"), Input.MaskB);
		Source->SetNumberField(TEXT("maskA"), Input.MaskA);
		return Source;
	}

	FString SurgeryParameterField(UMaterialExpression* Expression, const TCHAR* Field)
	{
		if (auto* Property = FindFProperty<FNameProperty>(Expression->GetClass(), Field))
			return Property->GetPropertyValue_InContainer(Expression).ToString();
		return FString();
	}

	TSharedPtr<FJsonObject> SurgeryNodeJson(UMaterialExpression* Expression)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("expressionName"), Expression->GetName());
		Row->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
		Row->SetStringField(TEXT("parameterName"), SurgeryParameterField(Expression, TEXT("ParameterName")));
		Row->SetStringField(TEXT("group"), SurgeryParameterField(Expression, TEXT("Group")));
		return Row;
	}
}

FExpressionInput* FMaterialHandlers::ResolveSurgeryInput(UMaterial* Material, const FString& ExpressionName,
	const FString& PropertyName, const FString& InputName, FString& Error)
{
	if (ExpressionName.IsEmpty() == PropertyName.IsEmpty())
	{
		Error = TEXT("Pass exactly one of targetExpression or property");
		return nullptr;
	}
	if (!ExpressionName.IsEmpty())
	{
		auto* Expression = FindExpressionByName(Material, ExpressionName);
		int32 Index = 0;
		if (!Expression) { Error = TEXT("targetExpression was not found"); return nullptr; }
		if (!ResolveExpressionInputIndex(Expression, InputName, Index, Error))
		{
			// Attribute pins may use a display label such as "Base Color".
			const FString Compact = InputName.Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT(""));
			Index = INDEX_NONE;
			for (int32 Pin = 0; Expression->GetInput(Pin); ++Pin)
			{
				const FString Candidate = Expression->GetInputName(Pin).ToString().Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT(""));
				if (Candidate.Equals(Compact, ESearchCase::IgnoreCase)) { Index = Pin; break; }
			}
			if (Index == INDEX_NONE) return nullptr;
			Error.Empty();
		}
		return Expression->GetInput(Index);
	}
	EMaterialProperty Property;
	if (PropertyName.Equals(TEXT("MaterialAttributes"), ESearchCase::IgnoreCase)) Property = MP_MaterialAttributes;
	else if (!ParseMaterialProperty(PropertyName, Property)) { Error = TEXT("Unknown material property"); return nullptr; }
	auto* Input = Material->GetExpressionInputForProperty(Property);
	if (!Input) Error = TEXT("Material property has no expression input");
	return Input;
}

TSharedPtr<FJsonValue> FMaterialHandlers::InsertExpressionBetween(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath, ExpressionType;
	if (auto Err = RequireString(Params, TEXT("materialPath"), MaterialPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("expressionType"), ExpressionType)) return Err;
	const FString TargetName = OptionalString(Params, TEXT("targetExpression"));
	const FString PropertyName = OptionalString(Params, TEXT("property"));
	const FString InputName = OptionalString(Params, TEXT("targetInput"));
	const FString UpstreamName = OptionalString(Params, TEXT("inputForUpstream"));
	const TSharedPtr<FJsonObject>* Properties = nullptr;
	TryGetObjectParam(Params, TEXT("properties"), Properties);
	if (MCPIsProtectedAssetPath(MaterialPath)) return MCPProtectedPathError(MaterialPath);
	auto* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material) return MCPError(TEXT("materialPath must load a Material"));
	FString Error;
	FExpressionInput* Target = ResolveSurgeryInput(Material, TargetName, PropertyName, InputName, Error);
	if (!Target) return MCPError(Error);
	UClass* Class = ResolveExpressionClass(ExpressionType);
	if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		return MCPError(TEXT("expressionType must name a concrete material expression"));
	const FString Key = TargetName + TEXT("|") + PropertyName + TEXT("|") + InputName + TEXT("|") + Class->GetPathName() + TEXT("|") + UpstreamName;
	const FName NodeName(*FString::Printf(TEXT("MCPInserted_%08x"), GetTypeHash(Key)));
	for (UMaterialExpression* Existing : Material->GetExpressions())
	{
		if (Existing && Existing->GetFName() == NodeName)
		{
			if (Target->Expression != Existing || Existing->GetClass() != Class)
				return MCPError(TEXT("Insertion name exists but the target wire changed; remove or rename that node before inserting again"));
			auto Result = MCPSuccess();
			MCPSetExisted(Result);
			Result->SetStringField(TEXT("expressionName"), Existing->GetName());
			return MCPResult(Result);
		}
	}
	// Configure and validate off-graph before changing any material or input.
	TStrongObjectPtr<UMaterialExpression> Probe(NewObject<UMaterialExpression>(GetTransientPackage(), Class, NAME_None, RF_Transient));
	if (Properties)
	{
		for (const auto& Pair : (*Properties)->Values)
		{
			const FString Name = FString(Pair.Key);
			FProperty* Field = MCPJsonProperty::FindMemberProperty(Class, Name);
			if (!Field || !Field->HasAnyPropertyFlags(CPF_Edit) || Field->HasAnyPropertyFlags(CPF_EditConst))
				return MCPError(FString::Printf(TEXT("Property '%s' is not editable on this expression"), *Name));
			if (!MCPJsonProperty::SetJsonOnProperty(Field, Field->ContainerPtrToValuePtr<void>(Probe.Get()), Pair.Value, Error)) return MCPError(Error);
		}
	}
	int32 UpstreamIndex = 0;
	if (!ResolveExpressionInputIndex(Probe.Get(), UpstreamName, UpstreamIndex, Error)) return MCPError(Error);
	if (Probe->GetOutputs().IsEmpty()) return MCPError(TEXT("New expression has no output"));
	const FExpressionInput Previous = *Target;
	Material->Modify();
	Material->PreEditChange(nullptr);
	auto* NewNode = DuplicateObject<UMaterialExpression>(Probe.Get(), Material, NodeName);
	NewNode->ClearFlags(RF_Transient);
	NewNode->SetFlags(RF_Transactional);
	NewNode->Material = Material;
	NewNode->UpdateMaterialExpressionGuid(true, false);
	NewNode->UpdateParameterGuid(true, false);
	*NewNode->GetInput(UpstreamIndex) = Previous;
	Material->GetExpressionCollection().AddExpression(NewNode);
	Target->Connect(0, NewNode);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("expressionName"), NewNode->GetName());
	Result->SetObjectField(TEXT("previousSource"), SurgerySourceJson(Previous));
	auto Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Payload->SetStringField(TEXT("expressionName"), NewNode->GetName());
	Payload->SetObjectField(TEXT("insertion"), MakeShared<FJsonObject>(*Params));
	Payload->SetObjectField(TEXT("previousSource"), SurgerySourceJson(Previous));
	MCPSetRollback(Result, TEXT("restore_expression_insertion"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::RestoreExpressionInsertion(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath, NodeName;
	if (auto Err = RequireString(Params, TEXT("materialPath"), MaterialPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("expressionName"), NodeName)) return Err;
	const TSharedPtr<FJsonObject>* Insertion = nullptr;
	const TSharedPtr<FJsonObject>* Previous = nullptr;
	TryGetObjectParam(Params, TEXT("insertion"), Insertion);
	TryGetObjectParam(Params, TEXT("previousSource"), Previous);
	if (!Insertion || !Previous) return MCPError(TEXT("insertion and previousSource from the rollback record are required"));
	if (MCPIsProtectedAssetPath(MaterialPath)) return MCPProtectedPathError(MaterialPath);
	auto* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material) return MCPError(TEXT("materialPath must load a Material"));
	auto* Node = FindExpressionByName(Material, NodeName);
	if (!Node)
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Result);
	}
	FString Error;
	FExpressionInput* Target = ResolveSurgeryInput(Material, OptionalString(*Insertion, TEXT("targetExpression")),
		OptionalString(*Insertion, TEXT("property")), OptionalString(*Insertion, TEXT("targetInput")), Error);
	if (!Target) return MCPError(Error);
	if (Target->Expression != Node) return MCPError(TEXT("Rollback refused: the target wire changed since insertion"));
	// Refuse to delete a node now shared by another wire.
	for (UMaterialExpression* Other : Material->GetExpressions())
		for (int32 Index = 0; Other && Other->GetInput(Index); ++Index)
			if (Other->GetInput(Index) != Target && Other->GetInput(Index)->Expression == Node)
				return MCPError(TEXT("Rollback refused: another expression now uses the inserted node"));
	for (int32 Index = 0; Index < MP_MAX; ++Index)
		if (auto* Input = Material->GetExpressionInputForProperty(static_cast<EMaterialProperty>(Index)))
			if (Input != Target && Input->Expression == Node) return MCPError(TEXT("Rollback refused: another property now uses the inserted node"));
	const FString SourceName = OptionalString(*Previous, TEXT("expressionName"));
	auto* Source = SourceName.IsEmpty() ? nullptr : FindExpressionByName(Material, SourceName);
	if (!SourceName.IsEmpty() && !Source) return MCPError(TEXT("Rollback source no longer exists"));
	int32 PreviousOutput = OptionalInt(*Previous, TEXT("outputIndex"), 0);
	if (Source && !ResolveExpressionOutputIndex(Source, FString::FromInt(PreviousOutput), PreviousOutput, Error)) return MCPError(Error);
	Material->Modify();
	Material->PreEditChange(nullptr);
	Target->Expression = Source;
	Target->OutputIndex = PreviousOutput;
	Target->Mask = OptionalInt(*Previous, TEXT("mask"), 0);
	Target->MaskR = OptionalInt(*Previous, TEXT("maskR"), 0);
	Target->MaskG = OptionalInt(*Previous, TEXT("maskG"), 0);
	Target->MaskB = OptionalInt(*Previous, TEXT("maskB"), 0);
	Target->MaskA = OptionalInt(*Previous, TEXT("maskA"), 0);
	Material->GetExpressionCollection().RemoveExpression(Node);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	MCPSetRollback(Result, TEXT("insert_expression_between"), *Insertion);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ReadMaterialSummary(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (auto Err = RequireString(Params, TEXT("materialPath"), MaterialPath)) return Err;
	const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ParameterNames = nullptr;
	const TSharedPtr<FJsonObject>* ChainRequest = nullptr;
	TryGetArrayParam(Params, TEXT("classes"), Classes);
	TryGetArrayParam(Params, TEXT("parameterNames"), ParameterNames);
	TryGetObjectParam(Params, TEXT("propertyChain"), ChainRequest);
	auto* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material) return MCPError(TEXT("materialPath must load a Material"));
	auto Matches = [](const TArray<TSharedPtr<FJsonValue>>* Filter, const FString& Name, const FString& ShortName)
	{
		if (!Filter) return true;
		for (const auto& Value : *Filter)
			if (Value->AsString().Equals(Name, ESearchCase::IgnoreCase) || Value->AsString().Equals(ShortName, ESearchCase::IgnoreCase)) return true;
		return false;
	};
	TMap<FString, int32> Counts;
	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!Expression) continue;
		const FString Class = Expression->GetClass()->GetName();
		Counts.FindOrAdd(Class)++;
		if ((Classes || ParameterNames) && Matches(Classes, Class, Class.Replace(TEXT("MaterialExpression"), TEXT("")))
			&& Matches(ParameterNames, SurgeryParameterField(Expression, TEXT("ParameterName")), TEXT("")))
			Nodes.Add(MakeShared<FJsonValueObject>(SurgeryNodeJson(Expression)));
	}
	auto ClassCounts = MakeShared<FJsonObject>();
	for (const auto& Pair : Counts) ClassCounts->SetNumberField(Pair.Key, Pair.Value);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("expressionCount"), Material->GetExpressions().Num());
	Result->SetObjectField(TEXT("countsByClass"), ClassCounts);
	Result->SetArrayField(TEXT("nodes"), Nodes);
	if (ChainRequest)
	{
		const int32 DepthLimit = OptionalInt(*ChainRequest, TEXT("depthLimit"), 8);
		if (DepthLimit < 0 || DepthLimit > 64) return MCPError(TEXT("propertyChain.depthLimit must be from 0 to 64"));
		FString Error;
		auto* Root = ResolveSurgeryInput(Material, OptionalString(*ChainRequest, TEXT("targetExpression")),
			OptionalString(*ChainRequest, TEXT("property")), OptionalString(*ChainRequest, TEXT("targetInput")), Error);
		if (!Root) return MCPError(Error);
		TArray<TSharedPtr<FJsonValue>> Chain;
		TSet<UMaterialExpression*> Seen;
		bool Truncated = false;
		TArray<TPair<UMaterialExpression*, int32>> Queue;
		Queue.Emplace(Root->Expression, 0);
		for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
		{
			UMaterialExpression* Expression = Queue[QueueIndex].Key;
			const int32 Depth = Queue[QueueIndex].Value;
			if (!Expression || Seen.Contains(Expression)) continue;
			if (Depth > DepthLimit) { Truncated = true; continue; }
			if (Seen.Num() >= 2000) { Truncated = true; break; }
			Seen.Add(Expression);
			auto Row = SurgeryNodeJson(Expression);
			Row->SetNumberField(TEXT("depth"), Depth);
			TArray<TSharedPtr<FJsonValue>> Inputs;
			for (int32 Index = 0; Expression->GetInput(Index); ++Index)
			{
				auto* Input = Expression->GetInput(Index);
				auto Wire = SurgerySourceJson(*Input);
				Wire->SetStringField(TEXT("inputName"), Expression->GetInputName(Index).ToString());
				Inputs.Add(MakeShared<FJsonValueObject>(Wire));
				Queue.Emplace(Input->Expression, Depth + 1);
			}
			Row->SetArrayField(TEXT("inputs"), Inputs);
			Chain.Add(MakeShared<FJsonValueObject>(Row));
		}
		auto ChainResult = SurgerySourceJson(*Root);
		ChainResult->SetArrayField(TEXT("nodes"), Chain);
		ChainResult->SetBoolField(TEXT("truncated"), Truncated);
		Result->SetObjectField(TEXT("propertyChain"), ChainResult);
	}
	return MCPResult(Result);
}
