#pragma once

// What a BehaviorTree declares to the F1 edit scope: its compile is the
// editor graph's UpdateAsset, which rebuilds the runnable tree from the graph,
// followed by the notifications an open BehaviorTree editor listens for. A
// tree with no editor graph has nothing to compile, and the call fails without
// saving, as the authoring actions did before the scope.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "Families/MCPEditScope.h"
#include "Handlers/Gameplay/GameplayHandlers.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTreeGraph.h"

namespace MCPBehaviorTree
{
	/** A graph edit: UpdateAsset, then save. No graph refuses the call. */
	inline FMCPCommitPolicy GraphEdit()
	{
		return FMCPCommitPolicy::EditCompileSave(EMCPSettle::None, EMCPCompileFailure::Refuse);
	}

	/** A write straight onto the runnable tree or a node instance: save, no rebuild. */
	inline FMCPCommitPolicy AssetEdit()
	{
		return FMCPCommitPolicy::EditSave(EMCPSettle::None);
	}
}

template <>
struct TMCPEditTraits<UBehaviorTree>
{
	static const TCHAR* TypeName() { return TEXT("UBehaviorTree"); }

	static UBehaviorTree* Load(const FString& AssetPath, TSharedPtr<FJsonValue>& OutError)
	{
		UBehaviorTree* Tree = FGameplayHandlers::LoadBehaviorTree(AssetPath);
		if (!Tree) OutError = MCPAssetLoadError(AssetPath, TypeName());
		return Tree;
	}

	static TSharedPtr<FJsonValue> CheckOpen(UBehaviorTree*, const FString&) { return nullptr; }

	static void Settle(UBehaviorTree*, EMCPSettle) {}

	/** Rebuild the runnable tree from the graph, and tell an open editor tab it changed. */
	static bool Compile(UBehaviorTree* Tree, const TSharedPtr<FJsonObject>& Out)
	{
		UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(Tree->BTGraph);
		if (!Graph)
		{
			Out->SetStringField(TEXT("error"), TEXT("The tree or its graph is missing, so nothing was saved."));
			return false;
		}
		Graph->Modify();
		Graph->UpdateAsset();
		// Without the notification an open BehaviorTree editor keeps drawing the old graph.
		Graph->NotifyGraphChanged();
		Tree->PostEditChange();
		Tree->MarkPackageDirty();
		return true;
	}

	static void SaveSet(UBehaviorTree* Tree, TArray<UObject*>& Out)
	{
		Out.Add(Tree);
	}

	static constexpr EMCPResultDialect Dialect = EMCPResultDialect::SavedSaveError;
};
