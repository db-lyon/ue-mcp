// Helpers shared by the audio handler files. The module is a unity build, so a
// helper used by more than one AudioHandlers_*.cpp lives here, not as copies.
#pragma once

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundClass.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

namespace MCPAudio
{
	/** Save every asset and record the outcome on Result. Every asset is
	 *  attempted; the first one that did not write fails the result. */
	inline bool SaveAndNote(const TSharedPtr<FJsonObject>& Result, std::initializer_list<UObject*> Assets)
	{
		FString FailedPath;
		FString FailedReason;
		for (UObject* Asset : Assets)
		{
			if (!Asset) continue;
			FString Reason;
			if (!SaveAssetPackageChecked(Asset, Reason) && FailedPath.IsEmpty())
			{
				FailedPath = Asset->GetPathName();
				FailedReason = Reason;
			}
		}
		const bool bSaved = FailedPath.IsEmpty();
		MCPNoteSaveOutcome(Result, FailedPath, bSaved, FailedReason);
		return bSaved;
	}

	/** Up to Limit MetaSound source and patch assets in the project, by object
	 *  path, so a bad path can name the good ones. */
	inline TArray<FString> KnownMetaSoundPaths(int32 Limit)
	{
		TArray<FString> Paths;
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		const FTopLevelAssetPath ClassPaths[] = {
			FTopLevelAssetPath(TEXT("/Script/MetasoundEngine"), TEXT("MetaSoundSource")),
			FTopLevelAssetPath(TEXT("/Script/MetasoundEngine"), TEXT("MetaSoundPatch")),
		};
		for (const FTopLevelAssetPath& ClassPath : ClassPaths)
		{
			TArray<FAssetData> Found;
			AssetRegistry.GetAssetsByClass(ClassPath, Found, /*bSearchSubClasses*/ true);
			for (const FAssetData& Data : Found)
			{
				if (Paths.Num() >= Limit) return Paths;
				Paths.Add(Data.GetSoftObjectPath().ToString());
			}
		}
		return Paths;
	}

	/** The answer every MetaSound document action gives below 5.5: 5.4 has a
	 *  different document model, so the actions stay registered and name the
	 *  engine requirement instead of half-authoring a document. */
	inline TSharedPtr<FJsonValue> MetaSoundUnsupportedEngine(const TCHAR* Action)
	{
		return MCPUnsupportedEngineError(
			FString::Printf(TEXT("audio(%s)"), Action), TEXT("5.5"),
			TEXT("The MetaSound document model it drives (graph pages and the document builder registry) does not exist in 5.4."));
	}
	/** Reparent a sound class on both ends. SetParentClass only drops the class
	 *  from the old parent's ChildClasses; the new parent's list is ours to fill. */
	inline void SetSoundClassParent(USoundClass* Class, USoundClass* NewParent)
	{
		USoundClass* OldParent = Class->ParentClass;
		if (OldParent && OldParent != NewParent)
		{
			OldParent->Modify();
			OldParent->ChildClasses.Remove(Class);
		}
#if WITH_EDITOR
		Class->SetParentClass(NewParent);
#else
		Class->ParentClass = NewParent;
#endif
		if (NewParent)
		{
			NewParent->Modify();
			NewParent->ChildClasses.AddUnique(Class);
		}
	}

	/** Single-input cue nodes (delay, looping, attenuation, modulator) read
	 *  ChildNodes[0] unguarded, so they must always keep their one slot. */
	inline void EnsureSingleSlot(USoundNode* Node)
	{
		if (Node && Node->GetMaxChildNodes() == 1 && Node->ChildNodes.Num() == 0)
		{
			Node->InsertChildNode(0);
		}
	}

	/** Put Child under Parent: fill an empty slot (the requested one, else the
	 *  first), otherwise insert. False with OutError when the parent is full. */
	inline bool PlaceCueChild(USoundNode* Parent, USoundNode* Child, bool bHasIndex, int32 RequestedIndex, FString& OutError)
	{
		EnsureSingleSlot(Parent);
		const int32 Num = Parent->ChildNodes.Num();
		int32 Index = bHasIndex ? FMath::Clamp(RequestedIndex, 0, Num) : Parent->ChildNodes.IndexOfByKey(nullptr);
		if (Index == INDEX_NONE) Index = Num;
		if (Index < Num && Parent->ChildNodes[Index] == nullptr)
		{
			Parent->ChildNodes[Index] = Child;
			return true;
		}
		if (Num >= Parent->GetMaxChildNodes())
		{
			OutError = FString::Printf(TEXT("Parent node '%s' accepts at most %d children."), *Parent->GetName(), Parent->GetMaxChildNodes());
			return false;
		}
		Parent->InsertChildNode(Index);
		Parent->ChildNodes[Index] = Child;
		return true;
	}

	/** Detach slot Index. A node above its minimum child count drops the slot
	 *  and its input pin; any other node keeps the slot empty. The editor graph
	 *  asserts that a node's input pins match its ChildNodes, so the pin goes
	 *  exactly when the slot does. */
	inline void DetachCueSlot(USoundNode* Parent, int32 Index)
	{
		Parent->Modify();
		const int32 SlotsBefore = Parent->ChildNodes.Num();
		if (Parent->GetMaxChildNodes() != 1)
		{
			Parent->RemoveChildNode(Index);
		}
		const bool bSlotRemoved = Parent->ChildNodes.Num() < SlotsBefore;
		if (!bSlotRemoved)
		{
			Parent->ChildNodes[Index] = nullptr;
		}
#if WITH_EDITORONLY_DATA
		if (UEdGraphNode* GraphNode = Parent->GetGraphNode())
		{
			int32 InputIndex = 0;
			for (UEdGraphPin* Pin : GraphNode->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input) continue;
				if (InputIndex++ != Index) continue;
				Pin->BreakAllPinLinks();
				if (bSlotRemoved)
				{
					GraphNode->Modify();
					GraphNode->RemovePin(Pin);
				}
				break;
			}
		}
#endif
	}
}
