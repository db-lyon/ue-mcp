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
#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Factories/SoundCueFactoryNew.h"

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

	/** Construct a cue node by friendly type name. Returns null for unknown types. */
	USoundNode* ConstructCueNode(USoundCue* Cue, const FString& TypeLower)
	{
		if (TypeLower == TEXT("wave_player"))  return Cue->ConstructSoundNode<USoundNodeWavePlayer>();
		if (TypeLower == TEXT("mixer"))        return Cue->ConstructSoundNode<USoundNodeMixer>();
		if (TypeLower == TEXT("random"))       return Cue->ConstructSoundNode<USoundNodeRandom>();
		if (TypeLower == TEXT("modulator"))    return Cue->ConstructSoundNode<USoundNodeModulator>();
		if (TypeLower == TEXT("attenuation"))  return Cue->ConstructSoundNode<USoundNodeAttenuation>();
		if (TypeLower == TEXT("looping"))      return Cue->ConstructSoundNode<USoundNodeLooping>();
		if (TypeLower == TEXT("concatenator")) return Cue->ConstructSoundNode<USoundNodeConcatenator>();
		if (TypeLower == TEXT("delay"))        return Cue->ConstructSoundNode<USoundNodeDelay>();
		if (TypeLower == TEXT("switch"))       return Cue->ConstructSoundNode<USoundNodeSwitch>();
		return nullptr;
	}

	void ApplyNodeProps(USoundNode* Node, const TSharedPtr<FJsonObject>& O)
	{
		const TSharedPtr<FJsonObject>* PropsObj = nullptr;
		if (O.IsValid() && O->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj)
		{
			for (const auto& Pair : (*PropsObj)->Values)
			{
				FString E;
				MCPJsonProperty::SetDottedPropertyFromJson(Node, FString(*Pair.Key), Pair.Value, E);
			}
		}
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
	USoundNode* Node = ConstructCueNode(Cue, T);
	if (!Node) return MCPError(TEXT("Unknown nodeType, or AudioEditor module not loaded. Types: wave_player, mixer, random, modulator, attenuation, looping, concatenator, delay, switch."));

	if (USoundNodeWavePlayer* WP = Cast<USoundNodeWavePlayer>(Node))
	{
		FString WavePath;
		if (Params->TryGetStringField(TEXT("soundWavePath"), WavePath) && !WavePath.IsEmpty())
		{
			if (USoundWave* Wave = Cast<USoundWave>(UEditorAssetLibrary::LoadAsset(WavePath)))
			{
				WP->SetSoundWave(Wave);
			}
		}
	}

	ApplyNodeProps(Node, Params);
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

// One-shot declarative authoring: create a SoundCue and stamp its whole node tree
// from a single JSON spec.
//
//   name, packagePath?, onConflict?
//   nodes:       [ { id, type, soundWavePath?, properties?: {field: value} } ]
//   connections: [ { parent, child, index? } ]   (parent omitted/empty => child is root)
//   root:        <nodeId>   (optional explicit root; else inferred from a parentless connection)
//
// Every element reports its own success. The cue is linked and saved at the end.
TSharedPtr<FJsonValue> FAudioHandlers::SoundCueAuthor(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/SoundCues"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	USoundCueFactoryNew* Factory = NewObject<USoundCueFactoryNew>();
	auto Created = MCPCreateAssetIdempotent<USoundCue>(Name, PackagePath, OnConflict, TEXT("SoundCue"), Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	USoundCue* Cue = Created.Asset;

	int32 Errors = 0;
	TArray<TSharedPtr<FJsonValue>> Diag;
	auto Note = [&Diag](const FString& Kind, const FString& Ref, bool bOkFlag, const FString& Msg)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("kind"), Kind);
		O->SetStringField(TEXT("ref"), Ref);
		O->SetBoolField(TEXT("ok"), bOkFlag);
		if (!Msg.IsEmpty()) O->SetStringField(TEXT("error"), Msg);
		Diag.Add(MakeShared<FJsonValueObject>(O));
	};

	// 1. Nodes (localId -> USoundNode*).
	TMap<FString, USoundNode*> NodeMap;
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (Params->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
	{
		for (const TSharedPtr<FJsonValue>& E : *Nodes)
		{
			const TSharedPtr<FJsonObject> O = E->AsObject();
			if (!O.IsValid()) continue;
			const FString Id = O->GetStringField(TEXT("id"));
			const FString Type = O->GetStringField(TEXT("type")).ToLower();
			USoundNode* Node = ConstructCueNode(Cue, Type);
			if (!Node) { Errors++; Note(TEXT("node"), Id, false, FString::Printf(TEXT("unknown type '%s'"), *Type)); continue; }

			if (USoundNodeWavePlayer* WP = Cast<USoundNodeWavePlayer>(Node))
			{
				FString WavePath;
				if (O->TryGetStringField(TEXT("soundWavePath"), WavePath) && !WavePath.IsEmpty())
				{
					if (USoundWave* Wave = Cast<USoundWave>(UEditorAssetLibrary::LoadAsset(WavePath)))
					{
						WP->SetSoundWave(Wave);
					}
				}
			}
			ApplyNodeProps(Node, O);
			NodeMap.Add(Id, Node);
			Note(TEXT("node"), Id, true, TEXT(""));
		}
	}

	// 2. Connections.
	const TArray<TSharedPtr<FJsonValue>>* Conns = nullptr;
	if (Params->TryGetArrayField(TEXT("connections"), Conns) && Conns)
	{
		for (const TSharedPtr<FJsonValue>& E : *Conns)
		{
			const TSharedPtr<FJsonObject> O = E->AsObject();
			if (!O.IsValid()) continue;
			const FString ChildId = O->GetStringField(TEXT("child"));
			FString ParentId; O->TryGetStringField(TEXT("parent"), ParentId);
			const FString Ref = (ParentId.IsEmpty() ? TEXT("root") : *ParentId) + FString(TEXT(" -> ")) + ChildId;

			USoundNode** Child = NodeMap.Find(ChildId);
			if (!Child) { Errors++; Note(TEXT("connection"), Ref, false, TEXT("no such child")); continue; }

			if (ParentId.IsEmpty() || ParentId == TEXT("root"))
			{
				Cue->FirstNode = *Child;
				Note(TEXT("connection"), Ref, true, TEXT(""));
				continue;
			}
			USoundNode** Parent = NodeMap.Find(ParentId);
			if (!Parent) { Errors++; Note(TEXT("connection"), Ref, false, TEXT("no such parent")); continue; }

			int32 Index = O->HasField(TEXT("index"))
				? (int32)O->GetNumberField(TEXT("index"))
				: (*Parent)->ChildNodes.Num();
			Index = FMath::Clamp(Index, 0, (*Parent)->ChildNodes.Num());
			if (Index >= (*Parent)->GetMaxChildNodes())
			{
				Errors++; Note(TEXT("connection"), Ref, false, FString::Printf(TEXT("parent accepts at most %d children"), (*Parent)->GetMaxChildNodes()));
				continue;
			}
			(*Parent)->InsertChildNode(Index);
			(*Parent)->ChildNodes[Index] = *Child;
			Note(TEXT("connection"), Ref, true, TEXT(""));
		}
	}

	// 3. Explicit root override.
	FString RootId;
	if (Params->TryGetStringField(TEXT("root"), RootId) && !RootId.IsEmpty())
	{
		if (USoundNode** Root = NodeMap.Find(RootId))
		{
			Cue->FirstNode = *Root;
		}
	}

#if WITH_EDITOR
	Cue->LinkGraphNodesFromSoundNodes();
	Cue->PostEditChange();
#endif
	Cue->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Cue->GetPathName());

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Cue->GetPathName());
	Res->SetStringField(TEXT("root"), Cue->FirstNode ? Cue->FirstNode->GetName() : TEXT(""));
	Res->SetNumberField(TEXT("nodes"), NodeMap.Num());
	Res->SetNumberField(TEXT("errors"), Errors);
	Res->SetArrayField(TEXT("elements"), Diag);
	MCPSetDeleteAssetRollback(Res, Cue->GetPathName());
	return MCPResult(Res);
}
