// SoundCue node-graph authoring for the audio category.
//
// USoundCue is a tree of USoundNode objects rooted at FirstNode. Nodes are made
// with ConstructSoundNode<T> (which also creates the paired editor graph node, so
// the AudioEditor module must be loaded), wired via each node's ChildNodes array,
// then synced to the editor graph with LinkGraphNodesFromSoundNodes. Nodes are
// addressed by their object name, which is unique within the cue.

#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"

#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundNodeLooping.h"
#include "Sound/SoundNodeConcatenator.h"
#include "Sound/SoundNodeDelay.h"
#include "Sound/SoundNodeSwitch.h"
#include "Sound/SoundWave.h"

namespace
{
	USoundNode* FindNodeByName(USoundCue* Cue, const FString& NodeName)
	{
#if WITH_EDITORONLY_DATA
		for (USoundNode* N : Cue->AllNodes)
		{
			if (N && N->GetName() == NodeName) return N;
		}
#endif
		return nullptr;
	}

	const TCHAR* NodeTypeLabel(USoundNode* N)
	{
		return N ? *N->GetClass()->GetName() : TEXT("");
	}
}

TSharedPtr<FJsonValue> FAudioHandlers::SoundCueAddNode(const TSharedPtr<FJsonObject>& Params)
{
	FString CuePath, NodeType;
	if (auto Err = RequireStringAlt(Params, TEXT("cuePath"), TEXT("assetPath"), CuePath)) return Err;
	if (auto Err = RequireString(Params, TEXT("nodeType"), NodeType)) return Err;

	USoundCue* Cue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(CuePath));
	if (!Cue) return MCPError(FString::Printf(TEXT("SoundCue not found: %s"), *CuePath));

	const FString T = NodeType.ToLower();
	USoundNode* Node = nullptr;

	if (T == TEXT("wave_player"))
	{
		USoundNodeWavePlayer* WP = Cue->ConstructSoundNode<USoundNodeWavePlayer>();
		FString WavePath;
		if (Params->TryGetStringField(TEXT("soundWavePath"), WavePath) && !WavePath.IsEmpty())
		{
			if (USoundWave* Wave = Cast<USoundWave>(UEditorAssetLibrary::LoadAsset(WavePath)))
			{
				WP->SetSoundWave(Wave);
			}
		}
		Node = WP;
	}
	else if (T == TEXT("mixer"))        Node = Cue->ConstructSoundNode<USoundNodeMixer>();
	else if (T == TEXT("random"))       Node = Cue->ConstructSoundNode<USoundNodeRandom>();
	else if (T == TEXT("modulator"))    Node = Cue->ConstructSoundNode<USoundNodeModulator>();
	else if (T == TEXT("attenuation"))  Node = Cue->ConstructSoundNode<USoundNodeAttenuation>();
	else if (T == TEXT("looping"))      Node = Cue->ConstructSoundNode<USoundNodeLooping>();
	else if (T == TEXT("concatenator")) Node = Cue->ConstructSoundNode<USoundNodeConcatenator>();
	else if (T == TEXT("delay"))        Node = Cue->ConstructSoundNode<USoundNodeDelay>();
	else if (T == TEXT("switch"))       Node = Cue->ConstructSoundNode<USoundNodeSwitch>();
	else return MCPError(TEXT("nodeType must be one of: wave_player, mixer, random, modulator, attenuation, looping, concatenator, delay, switch."));

	if (!Node) return MCPError(TEXT("Failed to construct sound node (is the AudioEditor module loaded?)."));

	// Apply node-specific properties.
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj)
	{
		for (const auto& Pair : (*PropsObj)->Values)
		{
			FString E;
			MCPJsonProperty::SetDottedPropertyFromJson(Node, Pair.Key, Pair.Value, E);
		}
	}

	Cue->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(CuePath);

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("cuePath"), CuePath);
	Res->SetStringField(TEXT("nodeId"), Node->GetName());
	Res->SetStringField(TEXT("nodeType"), NodeType);
	Res->SetNumberField(TEXT("maxChildren"), Node->GetMaxChildNodes());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::SoundCueConnect(const TSharedPtr<FJsonObject>& Params)
{
	FString CuePath, ChildNodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("cuePath"), TEXT("assetPath"), CuePath)) return Err;
	if (auto Err = RequireString(Params, TEXT("childNodeId"), ChildNodeId)) return Err;

	USoundCue* Cue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(CuePath));
	if (!Cue) return MCPError(FString::Printf(TEXT("SoundCue not found: %s"), *CuePath));

	USoundNode* Child = FindNodeByName(Cue, ChildNodeId);
	if (!Child) return MCPError(FString::Printf(TEXT("Child node '%s' not found in cue."), *ChildNodeId));

	const FString ParentNodeId = OptionalString(Params, TEXT("parentNodeId"));
	if (ParentNodeId.IsEmpty())
	{
		// No parent -> this node becomes the cue root.
		Cue->FirstNode = Child;
	}
	else
	{
		USoundNode* Parent = FindNodeByName(Cue, ParentNodeId);
		if (!Parent) return MCPError(FString::Printf(TEXT("Parent node '%s' not found in cue."), *ParentNodeId));

		int32 Index = Params->HasField(TEXT("childIndex"))
			? (int32)OptionalNumber(Params, TEXT("childIndex"), 0)
			: Parent->ChildNodes.Num();
		Index = FMath::Clamp(Index, 0, Parent->ChildNodes.Num());

		if (Index >= Parent->GetMaxChildNodes())
		{
			return MCPError(FString::Printf(TEXT("Parent node '%s' accepts at most %d children."), *ParentNodeId, Parent->GetMaxChildNodes()));
		}
		Parent->InsertChildNode(Index);
		Parent->ChildNodes[Index] = Child;
	}

#if WITH_EDITOR
	Cue->LinkGraphNodesFromSoundNodes();
	Cue->PostEditChange();
#endif
	Cue->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(CuePath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("cuePath"), CuePath);
	Res->SetStringField(TEXT("parentNodeId"), ParentNodeId);
	Res->SetStringField(TEXT("childNodeId"), ChildNodeId);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::SoundCueGetGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString CuePath;
	if (auto Err = RequireStringAlt(Params, TEXT("cuePath"), TEXT("assetPath"), CuePath)) return Err;

	USoundCue* Cue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(CuePath));
	if (!Cue) return MCPError(FString::Printf(TEXT("SoundCue not found: %s"), *CuePath));

	TArray<TSharedPtr<FJsonValue>> Nodes;
#if WITH_EDITORONLY_DATA
	for (USoundNode* N : Cue->AllNodes)
	{
		if (!N) continue;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("nodeId"), N->GetName());
		O->SetStringField(TEXT("type"), NodeTypeLabel(N));
		TArray<TSharedPtr<FJsonValue>> Children;
		for (USoundNode* C : N->ChildNodes)
		{
			Children.Add(MakeShared<FJsonValueString>(C ? C->GetName() : TEXT("(empty)")));
		}
		O->SetArrayField(TEXT("children"), Children);
		Nodes.Add(MakeShared<FJsonValueObject>(O));
	}
#endif

	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("cuePath"), CuePath);
	Res->SetStringField(TEXT("root"), Cue->FirstNode ? Cue->FirstNode->GetName() : TEXT(""));
	Res->SetArrayField(TEXT("nodes"), Nodes);
	Res->SetNumberField(TEXT("count"), Nodes.Num());
	return MCPResult(Res);
}
