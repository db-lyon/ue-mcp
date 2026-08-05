#include "WidgetHandlers.h"

#include "HandlerUtils.h"
#include "JsonSerializer.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace WidgetRuntimeState
{
	static TArray<FString> ReadStringArray(const TSharedPtr<FJsonObject>& Params, const TCHAR* FieldName)
	{
		TArray<FString> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params.IsValid() || !Params->TryGetArrayField(FieldName, Values) || !Values)
		{
			return Result;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString StringValue;
			if (Value.IsValid() && Value->TryGetString(StringValue) && !StringValue.IsEmpty())
			{
				Result.AddUnique(StringValue);
			}
		}
		return Result;
	}

	static FString BuildHierarchyPath(const UWidget* Widget)
	{
		TArray<FString> Segments;
		for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
		{
			Segments.Insert(Current->GetName(), 0);
		}
		return FString::Join(Segments, TEXT("/"));
	}

	/** A widget queued for serialization, with its hierarchy path resolved once. */
	struct FInspectNode
	{
		UWidget* Widget = nullptr;
		FString HierarchyPath;
	};

	static TSharedPtr<FJsonObject> SerializeWidgetNode(const FInspectNode& Target, const TArray<FString>& PropertyNames)
	{
		UWidget* Widget = Target.Widget;
		TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("name"), Widget->GetName());
		Node->SetStringField(TEXT("class"), Widget->GetClass()->GetPathName());
		Node->SetStringField(TEXT("path"), Widget->GetPathName());
		Node->SetStringField(TEXT("outerPath"), GetPathNameSafe(Widget->GetOuter()));
		Node->SetStringField(TEXT("hierarchyPath"), Target.HierarchyPath);

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> MissingProperties;
		for (const FString& PropertyName : PropertyNames)
		{
			FProperty* Property = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
			if (!Property)
			{
				MissingProperties.Add(MakeShared<FJsonValueString>(PropertyName));
				continue;
			}
			Properties->SetField(PropertyName, FMCPJsonSerializer::SerializeObjectProperty(Widget, Property));
		}
		Node->SetObjectField(TEXT("properties"), Properties);
		if (MissingProperties.Num() > 0)
		{
			Node->SetArrayField(TEXT("missingProperties"), MissingProperties);
		}
		return Node;
	}

	static bool MatchesChildFilter(const UWidget* Widget, const FString& ChildName, const FString& ChildClassFilter)
	{
		if (!ChildName.IsEmpty() && Widget->GetName() != ChildName)
		{
			return false;
		}
		return ChildClassFilter.IsEmpty()
			|| Widget->GetClass()->GetName().Contains(ChildClassFilter, ESearchCase::IgnoreCase);
	}

	static int32 ResolvePIEInstance(const UWorld* World)
	{
		if (!GEngine || !World) return INDEX_NONE;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() == World) return Context.PIEInstance;
		}
		return INDEX_NONE;
	}
}

TSharedPtr<FJsonValue> FWidgetHandlers::InspectRuntimeInstances(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	using namespace WidgetRuntimeState;

	// "auto" (and an explicit editor scope) resolve to the editor world when no
	// session is running. Editor Utility Widgets live in that world, so without
	// this guard a query made before PIE started would answer with editor-side
	// instances and label them with a PIE net mode. Refuse anything that is not
	// a live play world instead.
	UWorld* World = ResolveWorldFromParams(Params, TEXT("pie"));
	if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
	{
		return MCPError(TEXT("No live PIE/Game world. Runtime widget inspection reads a running session only: start PIE, or pass a pieInstance that exists."));
	}

	const FString WidgetName = OptionalString(Params, TEXT("widgetName"));
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	if (WidgetName.IsEmpty() && ClassFilter.IsEmpty())
	{
		return MCPError(TEXT("Provide widgetName (exact instance name) or classFilter (class-name substring)."));
	}

	const bool bViewportOnly = OptionalBool(Params, TEXT("viewportOnly"), false);
	const FString ChildName = OptionalString(Params, TEXT("childName"));
	const FString ChildClassFilter = OptionalString(Params, TEXT("childClassFilter"));
	// A child filter only ever selects subtree nodes, so passing one without
	// includeSubtree used to return the root node alone and look like "no child
	// matched". Treat either filter as an implicit request for the subtree.
	const bool bIncludeSubtree = OptionalBool(Params, TEXT("includeSubtree"), false)
		|| !ChildName.IsEmpty() || !ChildClassFilter.IsEmpty();
	const int32 MaxInstances = FMath::Clamp(OptionalInt(Params, TEXT("maxInstances"), 100), 1, 500);
	const int32 MaxNodesPerInstance = FMath::Clamp(OptionalInt(Params, TEXT("maxNodesPerInstance"), 250), 1, 2000);
	const TArray<FString> PropertyNames = ReadStringArray(Params, TEXT("propertyNames"));

	TArray<UUserWidget*> Matches;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;
		if (Widget->GetWorld() != World) continue;
		if (!WidgetName.IsEmpty() && Widget->GetName() != WidgetName) continue;
		if (!ClassFilter.IsEmpty() && !Widget->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase)) continue;
		if (bViewportOnly && !Widget->IsInViewport()) continue;
		Matches.Add(Widget);
	}
	Matches.Sort([](const UUserWidget& A, const UUserWidget& B)
	{
		return A.GetPathName() < B.GetPathName();
	});

	const int32 TotalMatchCount = Matches.Num();
	if (Matches.Num() > MaxInstances)
	{
		Matches.SetNum(MaxInstances);
	}

	TArray<TSharedPtr<FJsonValue>> InstanceResults;
	for (UUserWidget* Match : Matches)
	{
		TSharedPtr<FJsonObject> Instance = MakeShared<FJsonObject>();
		Instance->SetStringField(TEXT("name"), Match->GetName());
		Instance->SetStringField(TEXT("class"), Match->GetClass()->GetPathName());
		Instance->SetStringField(TEXT("path"), Match->GetPathName());
		Instance->SetStringField(TEXT("outerPath"), GetPathNameSafe(Match->GetOuter()));
		Instance->SetBoolField(TEXT("inViewport"), Match->IsInViewport());

		if (APlayerController* OwningPlayer = Match->GetOwningPlayer())
		{
			TSharedPtr<FJsonObject> Owner = MakeShared<FJsonObject>();
			Owner->SetStringField(TEXT("name"), OwningPlayer->GetName());
			Owner->SetStringField(TEXT("path"), OwningPlayer->GetPathName());
			Owner->SetStringField(TEXT("playerStatePath"), GetPathNameSafe(OwningPlayer->PlayerState.Get()));
			if (const ULocalPlayer* LocalPlayer = OwningPlayer->GetLocalPlayer())
			{
				Owner->SetNumberField(TEXT("controllerId"), LocalPlayer->GetControllerId());
			}
			Instance->SetObjectField(TEXT("owningPlayer"), Owner);
		}

		TArray<FInspectNode> Nodes;
		Nodes.Add({ Match, BuildHierarchyPath(Match) });
		if (bIncludeSubtree && Match->WidgetTree)
		{
			Match->WidgetTree->ForEachWidget([&](UWidget* Child)
			{
				if (Child && Child != Match && MatchesChildFilter(Child, ChildName, ChildClassFilter))
				{
					Nodes.Add({ Child, BuildHierarchyPath(Child) });
				}
			});
		}
		// Nested user widgets repeat their designer names, so hierarchy paths
		// tie, and TArray::Sort is not stable: without a unique tie-break two
		// calls in one session could return the same nodes in a different order
		// and a diff of the two would report changes that never happened.
		Nodes.Sort([](const FInspectNode& A, const FInspectNode& B)
		{
			if (A.HierarchyPath != B.HierarchyPath) return A.HierarchyPath < B.HierarchyPath;
			return A.Widget->GetPathName() < B.Widget->GetPathName();
		});

		const int32 TotalNodeCount = Nodes.Num();
		if (Nodes.Num() > MaxNodesPerInstance)
		{
			Nodes.SetNum(MaxNodesPerInstance);
		}
		TArray<TSharedPtr<FJsonValue>> NodeValues;
		for (const FInspectNode& Node : Nodes)
		{
			NodeValues.Add(MakeShared<FJsonValueObject>(SerializeWidgetNode(Node, PropertyNames)));
		}
		Instance->SetArrayField(TEXT("nodes"), NodeValues);
		Instance->SetNumberField(TEXT("nodeCount"), TotalNodeCount);
		Instance->SetBoolField(TEXT("nodesTruncated"), TotalNodeCount > Nodes.Num());
		InstanceResults.Add(MakeShared<FJsonValueObject>(Instance));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetStringField(TEXT("netMode"), DescribePIENetMode(World));
	const int32 PIEInstance = ResolvePIEInstance(World);
	if (PIEInstance != INDEX_NONE) Result->SetNumberField(TEXT("pieInstance"), PIEInstance);
	Result->SetBoolField(TEXT("includeSubtree"), bIncludeSubtree);
	Result->SetArrayField(TEXT("instances"), InstanceResults);
	Result->SetNumberField(TEXT("matchCount"), TotalMatchCount);
	Result->SetBoolField(TEXT("truncated"), TotalMatchCount > Matches.Num());
	return MCPResult(Result);
}
