// Audio authoring depth: MetaSound and SoundCue graph EDITING, sound class
// reparenting, and the routing read that verifies any of it.
//
// All functions below are still members of FAudioHandlers - this file is a
// translation-unit partition, not a new class. Handler registration stays in
// AudioHandlers.cpp::RegisterHandlers.
//
// The audit behind this file found one structural problem and three CRUD holes,
// rather than a broad shortfall against the 42 audio actions already shipped.
//
//  1. EVERY METASOUND WRITE REQUIRED A SESSION OPENED THIS EDITOR RUN.
//     The authoring half used to keep its builders in a file-local map filled
//     only by create_metasound / metasound_author, so restarting the editor, or
//     trying to edit a MetaSound somebody else made, closed the whole authoring
//     half. Every action here resolves through
//     Metasound::Engine::FDocumentBuilderRegistry::FindOrBeginBuilding instead,
//     which attaches a builder to the asset's own document exactly as the
//     MetaSound editor does, so an asset already on disk is editable with no
//     create call. AudioHandlers_MetaSound.cpp now resolves the same way.
//
//     There is therefore ONE document, the asset's own RootMetasoundDocument.
//     An edit lands in it immediately and this file saves it, so no result is
//     ever pending a build and audio(metasound_build) is a save. `source` says
//     whether a builder was already attached when the call arrived, which is
//     the same thing the read half's `source` says.
//  2. METASOUND ADD WITH NO REMOVE. add_node, add_graph_input, add_graph_output
//     and four connect actions had no inverse of any kind, so an authoring
//     mistake could only be fixed by deleting the asset.
//  3. SOUNDCUE ADD WITH NO REMOVE. Same shape: soundcue_add_node and
//     soundcue_connect, no removal, no detach.
//  4. NO WAY TO READ ROUTING BACK. Six actions assign a submix, sends, a class,
//     an attenuation and a concurrency onto a sound, and nothing reported where
//     a sound actually goes or whether the chain reaches the master submix.
//
// Deliberately NOT built, because a property write already reaches them:
//  - Submix effect chain removal and reordering. USoundSubmix::SubmixEffectChain
//    is UPROPERTY(EditAnywhere), so asset(set_property, "SubmixEffectChain",
//    [paths]) rewrites the whole chain, which covers remove and reorder.
//  - Submix send removal. USoundBase::SoundSubmixSends is a UPROPERTY array of
//    FSoundSubmixSendInfo, so the same call rewrites it. Worth knowing:
//    add_sound_submix_send appends unconditionally and so duplicates on replay,
//    and rewriting the array is how a duplicate gets cleaned up.
//  - Every attenuation, concurrency, sound class and sound mix tunable. All
//    plain UPROPERTYs on assets whose paths the create_* actions return.
//  - Submix reparenting. set_submix_parent already ships and already updates
//    both ends.

#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundSubmixSend.h"
#include "Sound/SoundEffectSubmix.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundWave.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

#include "MetasoundBuilderBase.h"
// MetasoundDocumentBuilderRegistry.h, and never MetasoundFrontendDocumentBuilderRegistry.h:
// the frontend header ships on 5.8 only. This MetasoundEngine header reaches
// Metasound::Frontend::IDocumentBuilderRegistry on every supported engine, by
// including the frontend header on 5.8 and MetasoundDocumentInterface.h, where
// 5.7 and earlier declare the class, on all of them.
#include "MetasoundDocumentBuilderRegistry.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundSource.h"

#include "EditorAssetLibrary.h"
#include "UObject/Package.h"

// ── MetaSound edit target resolution ─────────────────────────────────────────
//
// Names are prefixed MSEdit so they cannot collide with the MSRead* statics in
// AudioHandlers_MetaSoundRead.cpp or the anonymous-namespace helpers in
// AudioHandlers_MetaSound.cpp. The module is a unity build, so two file-local
// symbols with the same signature landing in one blob are a redefinition.

namespace
{
	/** A MetaSound that can be WRITTEN, plus how it was reached. */
	struct FMSEditTarget
	{
		UMetaSoundBuilderBase* Builder = nullptr;
		UObject* Asset = nullptr;
		FString AssetPath;
		/** A builder was already attached to this document before this call, so
		 *  another authoring path may be mid-edit on the same document. Asked
		 *  before attaching, because attaching makes the answer true. */
		bool bBuilderWasAttached = false;
		/** "builder" = a builder was already attached; "asset" = one was attached
		 *  on demand by this call. Both write the same document: the asset's own
		 *  RootMetasoundDocument. Same meaning as the read half's `source`. */
		FString Source;
	};

	bool MSEditOk(EMetaSoundBuilderResult R) { return R == EMetaSoundBuilderResult::Succeeded; }

	/** Every MetaSound in the project, so a bad path can name the good ones. */
	FString MSEditKnownMetaSounds()
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
				if (Paths.Num() >= 20) break;
				Paths.Add(Data.GetSoftObjectPath().ToString());
			}
		}
		return Paths.Num() > 0 ? FString::Join(Paths, TEXT(", ")) : FString(TEXT("(none in this project)"));
	}

	/**
	 * Resolve assetPath to a WRITABLE builder.
	 *
	 * There is one document and it is the asset's own RootMetasoundDocument, so
	 * there is nothing to choose between: this attaches a builder to that
	 * document if none is attached already, through
	 * Metasound::Engine::FDocumentBuilderRegistry::FindOrBeginBuilding. That is
	 * how the MetaSound editor itself opens an asset and what the authoring half
	 * in AudioHandlers_MetaSound.cpp uses, and it is what makes these actions
	 * work on a MetaSound this editor run did not create.
	 *
	 * Every caller below is a MUTATION, which is why this takes the find-OR-BEGIN
	 * form. The find-only form, IDocumentBuilderRegistry::FindBuilder, is what a
	 * read must use so that reading cannot attach a session as a side effect; it
	 * is used here too, but only to answer whether a builder was already there,
	 * never to obtain the one that does the writing.
	 *
	 * This used to begin with UMetaSoundBuilderSubsystem::FindSourceBuilder /
	 * FindBuilder(FName(*assetPath)), and that could never have matched. That
	 * subsystem's map is keyed by a caller-chosen BuilderName and holds only
	 * builders somebody passed to RegisterBuilder; its own header says "the
	 * builder manually registered ... with the provided custom name". Nothing in
	 * this plugin ever registered one, so the lookup missed on every call,
	 * silently, and the whole branch it guarded was dead. The registry actually
	 * keyed by the MetaSound is Metasound::Frontend::IDocumentBuilderRegistry.
	 */
	bool MSEditResolve(const TSharedPtr<FJsonObject>& Params, FMSEditTarget& Out, TSharedPtr<FJsonValue>& OutError)
	{
		if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("metasoundPath"), Out.AssetPath))
		{
			OutError = Err;
			return false;
		}

		Out.Asset = MCPLoadAssetObject(Out.AssetPath);
		if (!Out.Asset)
		{
			OutError = MCPError(FString::Printf(
				TEXT("No MetaSound found at '%s'. MetaSounds in this project: %s. Create one with audio(metasound_author)."),
				*Out.AssetPath, *MSEditKnownMetaSounds()));
			return false;
		}
		if (!Cast<IMetaSoundDocumentInterface>(Out.Asset))
		{
			OutError = MCPError(FString::Printf(
				TEXT("'%s' is a %s, not a MetaSound. The audio(metasound_*) actions work on MetaSoundSource and MetaSoundPatch assets only."),
				*Out.AssetPath, *Out.Asset->GetClass()->GetName()));
			return false;
		}

#if WITH_EDITORONLY_DATA
		using namespace Metasound::Frontend;
		IDocumentBuilderRegistry* Registry = IDocumentBuilderRegistry::Get();
		if (!Registry)
		{
			OutError = MCPError(TEXT("The MetaSound document builder registry is not available, so an existing MetaSound cannot be opened for editing. Enable the MetaSound plugin."));
			return false;
		}

		// Asked BEFORE attaching, because FindOrBeginBuilding makes the answer
		// true whatever it was. FindBuilder only asks.
		{
			TScriptInterface<IMetaSoundDocumentInterface> DocIface(Out.Asset);
			Out.bBuilderWasAttached = Registry->FindBuilder(DocIface) != nullptr;
		}

		// FindOrBeginBuilding check()s that the object is an asset, and a check
		// is a fatal assert rather than a failure a handler can report. Refuse
		// the transient case here instead of taking the editor down with it.
		if (!Out.Asset->IsAsset())
		{
			OutError = MCPError(FString::Printf(
				TEXT("'%s' resolves to a %s that is not a saved asset, and the MetaSound builder registry only opens assets. ")
				TEXT("Pass the path of a MetaSound asset on disk, such as one of: %s."),
				*Out.AssetPath, *Out.Asset->GetClass()->GetName(), *MSEditKnownMetaSounds()));
			return false;
		}

		Metasound::Engine::FDocumentBuilderRegistry& EngineRegistry =
			static_cast<Metasound::Engine::FDocumentBuilderRegistry&>(*Registry);
		Out.Builder = &EngineRegistry.FindOrBeginBuilding<UMetaSoundBuilderBase>(*Out.Asset);
		Out.Source = Out.bBuilderWasAttached ? TEXT("builder") : TEXT("asset");
		return true;
#else
		OutError = MCPError(TEXT("MetaSound graph editing requires an editor build."));
		return false;
#endif
	}

	/** Persist. Every edit ends here. */
	void MSEditFinish(const TSharedPtr<FJsonObject>& Res, const FMSEditTarget& T)
	{
		Res->SetStringField(TEXT("assetPath"), T.AssetPath);
		Res->SetStringField(TEXT("source"), T.Source);
		Res->SetBoolField(TEXT("hasActiveBuilder"), true);

		// The builder writes into the asset's own document, so an edit is never
		// waiting on a flush. The field is kept because it is advertised, and it
		// is now always false: metasound_build is a save, and this already saved.
		Res->SetBoolField(TEXT("pendingBuild"), false);
		Res->SetStringField(TEXT("sourceNote"), T.bBuilderWasAttached
			? TEXT("Applied to the asset's own document through the builder that was already attached to it, and saved. audio(metasound_build) is a save, so it is not needed after this call.")
			: TEXT("Applied to the asset's own document through a builder attached on demand, and saved. This is the path that works on a MetaSound this editor session did not create."));

		if (T.Asset)
		{
			T.Asset->MarkPackageDirty();
			UEditorAssetLibrary::SaveLoadedAsset(T.Asset, /*bOnlyIfIsDirty*/ true);
		}
	}

	/** The document the resolved builder is editing. */
	const FMetasoundFrontendDocument* MSEditDocument(const FMSEditTarget& T)
	{
		if (!T.Builder) return nullptr;
		// Through the builder's own document rather than through the MetaSound
		// object it is editing. UMetaSoundBuilderBase::GetMetaSound arrived in
		// 5.8, and the two accessors that would stand in for it on 5.7 are
		// private in both engines, but GetConstDocumentChecked has been public
		// on FMetaSoundFrontendDocumentBuilder throughout and is what the
		// GetMetaSound round trip was reaching for anyway.
		//
		// IsValid first, because the Checked form asserts on a builder with no
		// document while the round trip it replaces returned null for it.
		const FMetaSoundFrontendDocumentBuilder& Builder = T.Builder->GetConstBuilder();
		if (!Builder.IsValid()) return nullptr;
		return &Builder.GetConstDocumentChecked();
	}

	/**
	 * The page graph of the document being edited, asked SAFELY.
	 *
	 * NEVER GetConstDefaultGraph(). FMetasoundFrontendGraphClass::GetConstDefaultGraph()
	 * ends in a check() rather than returning null when the document holds no
	 * page under Metasound::Frontend::DefaultPageID, and a fatal assert takes the
	 * whole editor down. That happened twice from the sibling read path, which is
	 * why AudioHandlers_MetaSoundRead.cpp::MSReadResolve resolves this way; this
	 * follows the same pattern rather than inventing a second one.
	 *
	 * FindConstGraph returns a pointer and asks the same question. A document
	 * whose default page is missing but which holds pages under other ids is
	 * still editable, so the first page it does hold is used and bOutFellBack
	 * says so. Only a document with no pages at all yields null, and the caller
	 * reports that as an ordinary error naming the asset.
	 */
	const FMetasoundFrontendGraph* MSEditFindGraph(const FMetasoundFrontendDocument* Doc, bool* bOutFellBackFromDefaultPage = nullptr)
	{
		if (bOutFellBackFromDefaultPage) { *bOutFellBackFromDefaultPage = false; }
		if (!Doc) { return nullptr; }

		if (const FMetasoundFrontendGraph* Graph = Doc->RootGraph.FindConstGraph(Metasound::Frontend::DefaultPageID))
		{
			return Graph;
		}

		const TArray<FMetasoundFrontendGraph>& Pages = Doc->RootGraph.GetConstGraphPages();
		if (Pages.Num() == 0) { return nullptr; }

		if (bOutFellBackFromDefaultPage) { *bOutFellBackFromDefaultPage = true; }
		return &Pages[0];
	}

	/** A document with no graph pages, told as the recreate it needs. */
	TSharedPtr<FJsonValue> MSEditNoGraphPagesError(const FMSEditTarget& T)
	{
		const FMetasoundFrontendDocument* Doc = MSEditDocument(T);
		return MCPError(FString::Printf(
			TEXT("'%s' holds no graph pages at all, so there is nothing to edit: its document is empty ")
			TEXT("(%d interfaces declared, %d dependencies). A MetaSound is only initialized by its own ")
			TEXT("asset factory, so an asset built any other way loads as a valid MetaSound with a ")
			TEXT("completely blank document. Recreate it with audio(metasound_author) or ")
			TEXT("audio(create_metasound), which go through the factory, and delete this one."),
			*T.AssetPath,
			Doc ? Doc->Interfaces.Num() : 0,
			Doc ? Doc->Dependencies.Num() : 0));
	}

	/** Say which page was read whenever it was not the default one, because two
	 *  calls that silently disagree about the page are unreadable. */
	void MSEditStampPage(const TSharedPtr<FJsonObject>& Res, const FMetasoundFrontendGraph* Graph, bool bFellBackFromDefaultPage)
	{
		if (!Graph) { return; }
		Res->SetStringField(TEXT("pageId"), Graph->PageID.ToString());
		Res->SetBoolField(TEXT("readDefaultPage"), !bFellBackFromDefaultPage);
		if (bFellBackFromDefaultPage)
		{
			Res->SetStringField(TEXT("pageNote"), FString::Printf(
				TEXT("This document holds no page under the default page id, so page '%s' was read instead."),
				*Graph->PageID.ToString()));
		}
	}

	FMetaSoundNodeHandle MSEditNodeFromId(const FString& Id)
	{
		FMetaSoundNodeHandle Handle;
		FGuid::Parse(Id, Handle.NodeID);
		return Handle;
	}

	/** "That node id is not in this graph", told with the ids that are. */
	TSharedPtr<FJsonValue> MSEditUnknownNodeError(const FMSEditTarget& T, const FString& NodeId)
	{
		const FMetasoundFrontendDocument* Doc = MSEditDocument(T);
		bool bFellBack = false;
		const FMetasoundFrontendGraph* Graph = MSEditFindGraph(Doc, &bFellBack);
		if (Doc && !Graph)
		{
			// The blank-document case is a better answer than "no nodes": it says
			// why there are none and what to do about it.
			return MSEditNoGraphPagesError(T);
		}

		TArray<FString> Ids;
		FString PageNote;
		if (Graph)
		{
			for (const FMetasoundFrontendNode& Node : Graph->Nodes)
			{
				if (Ids.Num() >= 25) break;
				Ids.Add(FString::Printf(TEXT("%s (%s)"), *Node.GetID().ToString(), *Node.Name.ToString()));
			}
			if (bFellBack)
			{
				PageNote = FString::Printf(
					TEXT(" (page '%s', since the document holds no default page)"), *Graph->PageID.ToString());
			}
		}
		return MCPError(FString::Printf(
			TEXT("Node id '%s' is not in '%s'%s. Nodes in this graph: %s. Call audio(metasound_read_document) or audio(metasound_search_nodes) for the full list."),
			*NodeId, *T.AssetPath, *PageNote,
			Ids.Num() > 0 ? *FString::Join(Ids, TEXT(", ")) : TEXT("(none)")));
	}

	// ── SoundCue helpers ────────────────────────────────────────────────

	/** A cue node by object name, which is what soundcue_add_node hands back. */
	USoundNode* MSEditFindCueNode(USoundCue* Cue, const FString& NodeName)
	{
#if WITH_EDITORONLY_DATA
		for (USoundNode* N : Cue->AllNodes)
		{
			if (N && N->GetName() == NodeName) return N;
		}
#endif
		return nullptr;
	}

	FString MSEditCueNodeIds(USoundCue* Cue)
	{
		TArray<FString> Ids;
#if WITH_EDITORONLY_DATA
		for (USoundNode* N : Cue->AllNodes)
		{
			if (N) Ids.Add(FString::Printf(TEXT("%s (%s)"), *N->GetName(), *N->GetClass()->GetName()));
		}
#endif
		return Ids.Num() > 0 ? FString::Join(Ids, TEXT(", ")) : FString(TEXT("(none)"));
	}

	/** Push the node tree back into the editor graph and save. */
	void MSEditSaveCue(USoundCue* Cue, const FString& CuePath)
	{
#if WITH_EDITOR
		Cue->LinkGraphNodesFromSoundNodes();
		Cue->PostEditChange();
#endif
		Cue->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(CuePath);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// metasound_remove_node
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundRemoveNode(const TSharedPtr<FJsonObject>& Params)
{
	FString NodeId;
	if (auto Err = RequireString(Params, TEXT("nodeId"), NodeId)) return Err;

	FMSEditTarget Target;
	TSharedPtr<FJsonValue> Error;
	if (!MSEditResolve(Params, Target, Error)) return Error;

	const FMetaSoundNodeHandle Node = MSEditNodeFromId(NodeId);
	if (!Node.IsSet())
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is not a node id. Node ids are GUIDs, as returned by audio(metasound_add_node) and audio(metasound_read_document)."), *NodeId));
	}

	if (!Target.Builder->ContainsNode(Node))
	{
		// Idempotent: a node that is already gone is a success that says so,
		// because a retried flow step must not fail on its own prior success.
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("nodeId"), NodeId);
		Noop->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Noop->SetStringField(TEXT("source"), Target.Source);
		Noop->SetStringField(TEXT("note"), TEXT("No node with that id is in the graph; nothing was removed."));
		return MCPResult(Noop);
	}

	// Capture the class before the node goes, because it is the rollback payload.
	FString NodeName, NodeClassName, NodeNamespace, NodeVariant;
	int32 MajorVersion = 1;
	const FMetasoundFrontendDocument* Doc = MSEditDocument(Target);
	bool bFellBackFromDefaultPage = false;
	const FMetasoundFrontendGraph* Graph = MSEditFindGraph(Doc, &bFellBackFromDefaultPage);
	if (Doc && !Graph)
	{
		// ContainsNode said the node is there, so a document with no pages at all
		// is a contradiction rather than a miss. Report it instead of asserting.
		return MSEditNoGraphPagesError(Target);
	}
	if (Doc && Graph)
	{
		for (const FMetasoundFrontendNode& N : Graph->Nodes)
		{
			if (N.GetID() != Node.NodeID) continue;
			NodeName = N.Name.ToString();
			for (const FMetasoundFrontendClass& Dep : Doc->Dependencies)
			{
				if (Dep.ID != N.ClassID) continue;
				const FMetasoundFrontendClassName& ClassName = Dep.Metadata.GetClassName();
				NodeClassName = ClassName.Name.ToString();
				NodeNamespace = ClassName.Namespace.ToString();
				NodeVariant = ClassName.Variant.ToString();
				MajorVersion = Dep.Metadata.GetVersion().Major;
				break;
			}
			break;
		}
	}

	const bool bRemoveUnusedDependencies = OptionalBool(Params, TEXT("removeUnusedDependencies"), true);
	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	Target.Builder->RemoveNode(Node, R, bRemoveUnusedDependencies);
	if (!MSEditOk(R))
	{
		return MCPError(FString::Printf(
			TEXT("The MetaSound builder refused to remove node '%s' from '%s'."), *NodeId, *Target.AssetPath));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetStringField(TEXT("nodeName"), NodeName);
	Result->SetStringField(TEXT("nodeClassName"), NodeClassName);
	Result->SetStringField(TEXT("nodeNamespace"), NodeNamespace);
	Result->SetStringField(TEXT("nodeVariant"), NodeVariant);
	Result->SetNumberField(TEXT("majorVersion"), MajorVersion);
	MSEditStampPage(Result, Graph, bFellBackFromDefaultPage);
	MSEditFinish(Result, Target);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
	Payload->SetStringField(TEXT("nodeClassName"), NodeClassName);
	Payload->SetStringField(TEXT("nodeNamespace"), NodeNamespace);
	Payload->SetStringField(TEXT("nodeVariant"), NodeVariant);
	Payload->SetNumberField(TEXT("majorVersion"), MajorVersion);
	MCPSetRollback(Result, TEXT("metasound_add_node"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("metasound_add_node restores a FRESH node of the same class, with a NEW node id. RemoveNode also drops every edge that touched the node, ")
		TEXT("and its input defaults, and neither is restored. Read the edges with audio(metasound_list_connections) before removing if they matter."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// metasound_disconnect
//
// One action for the inverse of all four connect actions, because they all
// reduce to the same two builder handles. Which form is used is decided by what
// is passed, and an ambiguous or empty call is refused rather than guessed at.
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundDisconnect(const TSharedPtr<FJsonObject>& Params)
{
	FMSEditTarget Target;
	TSharedPtr<FJsonValue> Error;
	if (!MSEditResolve(Params, Target, Error)) return Error;

	const FString FromNodeId = OptionalString(Params, TEXT("fromNodeId"));
	const FString FromOutput = OptionalString(Params, TEXT("fromOutput"));
	const FString ToNodeId = OptionalString(Params, TEXT("toNodeId"));
	const FString ToInput = OptionalString(Params, TEXT("toInput"));
	const FString GraphOutput = OptionalString(Params, TEXT("graphOutput"));

	const bool bHasFrom = !FromNodeId.IsEmpty() && !FromOutput.IsEmpty();
	const bool bHasTo = !ToNodeId.IsEmpty() && !ToInput.IsEmpty();
	const bool bHasGraphOutput = !GraphOutput.IsEmpty();

	if (!bHasFrom && !bHasTo && !bHasGraphOutput)
	{
		return MCPError(
			TEXT("Nothing to disconnect. Pass one of: fromNodeId + fromOutput and toNodeId + toInput (drop that one edge); ")
			TEXT("toNodeId + toInput alone (clear whatever drives that input); fromNodeId + fromOutput alone (clear every edge leaving that output); ")
			TEXT("or graphOutput (clear what drives a graph output, which is also how an audio output is cleared - the audio outs are graph outputs named ")
			TEXT("'Out Mono', 'Out Left' and 'Out Right'). audio(metasound_list_connections) reports every edge in these exact field names."));
	}

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;

	// Resolve every handle before disconnecting anything, so a typo in the
	// second endpoint cannot leave the first one already cut.
	FMetaSoundBuilderNodeOutputHandle OutputHandle;
	if (bHasFrom)
	{
		const FMetaSoundNodeHandle FromNode = MSEditNodeFromId(FromNodeId);
		if (!Target.Builder->ContainsNode(FromNode)) return MSEditUnknownNodeError(Target, FromNodeId);
		OutputHandle = Target.Builder->FindNodeOutputByName(FromNode, FName(*FromOutput), R);
		if (!MSEditOk(R))
		{
			return MCPError(FString::Printf(
				TEXT("Output vertex '%s' not found on node '%s'. audio(metasound_list_node_pins) lists a node's vertices by name."),
				*FromOutput, *FromNodeId));
		}
	}

	FMetaSoundBuilderNodeInputHandle InputHandle;
	FString InputLabel;
	if (bHasGraphOutput)
	{
		FName DataType;
		Target.Builder->FindGraphOutputNode(FName(*GraphOutput), DataType, InputHandle, R);
		if (!MSEditOk(R))
		{
			TArray<FString> Names;
			EMetaSoundBuilderResult NamesResult = EMetaSoundBuilderResult::Failed;
			for (const FName& Name : Target.Builder->GetGraphOutputNames(NamesResult))
			{
				Names.Add(Name.ToString());
			}
			return MCPError(FString::Printf(
				TEXT("Graph output '%s' not found in '%s'. Graph outputs: %s"),
				*GraphOutput, *Target.AssetPath,
				Names.Num() > 0 ? *FString::Join(Names, TEXT(", ")) : TEXT("(none)")));
		}
		InputLabel = FString::Printf(TEXT("graphOutput:%s"), *GraphOutput);
	}
	else if (bHasTo)
	{
		const FMetaSoundNodeHandle ToNode = MSEditNodeFromId(ToNodeId);
		if (!Target.Builder->ContainsNode(ToNode)) return MSEditUnknownNodeError(Target, ToNodeId);
		InputHandle = Target.Builder->FindNodeInputByName(ToNode, FName(*ToInput), R);
		if (!MSEditOk(R))
		{
			return MCPError(FString::Printf(
				TEXT("Input vertex '%s' not found on node '%s'. audio(metasound_list_node_pins) lists a node's vertices by name."),
				*ToInput, *ToNodeId));
		}
		InputLabel = FString::Printf(TEXT("%s:%s"), *ToNodeId, *ToInput);
	}

	const bool bHasInput = bHasTo || bHasGraphOutput;

	// Idempotency: report whether there was anything connected to cut.
	bool bWasConnected = false;
	FString Form;
	if (bHasFrom && bHasInput)
	{
		Form = TEXT("edge");
		bWasConnected = Target.Builder->NodesAreConnected(OutputHandle, InputHandle);
	}
	else if (bHasInput)
	{
		Form = TEXT("input");
		bWasConnected = Target.Builder->NodeInputIsConnected(InputHandle);
	}
	else
	{
		Form = TEXT("output");
		bWasConnected = Target.Builder->NodeOutputIsConnected(OutputHandle);
	}

	if (!bWasConnected)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDisconnected"), true);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("form"), Form);
		Noop->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Noop->SetStringField(TEXT("source"), Target.Source);
		Noop->SetStringField(TEXT("note"), TEXT("Nothing was connected there; nothing was disconnected."));
		return MCPResult(Noop);
	}

	R = EMetaSoundBuilderResult::Failed;
	if (Form == TEXT("edge"))
	{
		Target.Builder->DisconnectNodes(OutputHandle, InputHandle, R);
	}
	else if (Form == TEXT("input"))
	{
		Target.Builder->DisconnectNodeInput(InputHandle, R);
	}
	else
	{
		Target.Builder->DisconnectNodeOutput(OutputHandle, R);
	}

	if (!MSEditOk(R))
	{
		return MCPError(FString::Printf(
			TEXT("The MetaSound builder refused the '%s' disconnect in '%s'."), *Form, *Target.AssetPath));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDisconnected"), false);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetStringField(TEXT("form"), Form);
	Result->SetStringField(TEXT("fromNodeId"), FromNodeId);
	Result->SetStringField(TEXT("fromOutput"), FromOutput);
	Result->SetStringField(TEXT("toNodeId"), ToNodeId);
	Result->SetStringField(TEXT("toInput"), ToInput);
	Result->SetStringField(TEXT("graphOutput"), GraphOutput);
	MSEditFinish(Result, Target);

	// Only the single-edge form has an exact inverse. The two sweep forms cut an
	// unknown number of edges and the builder does not report which.
	if (Form == TEXT("edge"))
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Payload->SetStringField(TEXT("fromNodeId"), FromNodeId);
		Payload->SetStringField(TEXT("fromOutput"), FromOutput);
		if (bHasGraphOutput)
		{
			Payload->SetStringField(TEXT("graphOutput"), GraphOutput);
			MCPSetRollback(Result, TEXT("metasound_connect_graph_output"), Payload);
		}
		else
		{
			Payload->SetStringField(TEXT("toNodeId"), ToNodeId);
			Payload->SetStringField(TEXT("toInput"), ToInput);
			MCPSetRollback(Result, TEXT("metasound_connect"), Payload);
		}
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		// A rollback is still emitted, because a flow with none is
		// unrecoverable, but it restores one edge and the sweep cut several.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Payload->SetStringField(TEXT("fromNodeId"), FromNodeId);
		Payload->SetStringField(TEXT("fromOutput"), FromOutput);
		Payload->SetStringField(TEXT("toNodeId"), ToNodeId);
		Payload->SetStringField(TEXT("toInput"), ToInput);
		MCPSetRollback(Result, TEXT("metasound_connect"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The '") + Form + TEXT("' form clears EVERY edge on that vertex and the builder does not report which ones, so the rollback cannot restore them. ")
			TEXT("Record them with audio(metasound_list_connections) first, or use the single-edge form (all four of fromNodeId, fromOutput, toNodeId, toInput), whose inverse is exact."));
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// metasound_remove_member
//
// The inverse of metasound_add_input and metasound_add_output, plus graph
// variables, which had an add path through metasound_author and no removal at
// all. One action because all three are name-addressed members of the same
// graph and splitting them would triple the parameter surface for nothing.
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundRemoveMember(const TSharedPtr<FJsonObject>& Params)
{
	FString MemberKind;
	if (auto Err = RequireString(Params, TEXT("memberKind"), MemberKind)) return Err;
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	const FString Kind = MemberKind.ToLower();
	if (Kind != TEXT("input") && Kind != TEXT("output") && Kind != TEXT("variable"))
	{
		return MCPError(FString::Printf(
			TEXT("memberKind '%s' is not valid. Use 'input' (the inverse of metasound_add_input), 'output' (the inverse of metasound_add_output), or 'variable'."),
			*MemberKind));
	}

	FMSEditTarget Target;
	TSharedPtr<FJsonValue> Error;
	if (!MSEditResolve(Params, Target, Error)) return Error;

	// Present? The answer is both the idempotency check and, for inputs and
	// outputs, where the data type for the rollback comes from.
	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	bool bPresent = false;
	FString DataType;
	TArray<FString> Known;
	// Only the variable branch walks the document, so only it has a page to name.
	const FMetasoundFrontendGraph* VariablePage = nullptr;
	bool bVariablePageFellBack = false;

	if (Kind == TEXT("input"))
	{
		EMetaSoundBuilderResult NamesResult = EMetaSoundBuilderResult::Failed;
		for (const FName& Existing : Target.Builder->GetGraphInputNames(NamesResult))
		{
			Known.Add(Existing.ToString());
			if (Existing == FName(*Name)) bPresent = true;
		}
		if (bPresent)
		{
			FName TypeName;
			FMetaSoundBuilderNodeOutputHandle Handle;
			Target.Builder->FindGraphInputNode(FName(*Name), TypeName, Handle, R);
			if (MSEditOk(R)) DataType = TypeName.ToString();
		}
	}
	else if (Kind == TEXT("output"))
	{
		EMetaSoundBuilderResult NamesResult = EMetaSoundBuilderResult::Failed;
		for (const FName& Existing : Target.Builder->GetGraphOutputNames(NamesResult))
		{
			Known.Add(Existing.ToString());
			if (Existing == FName(*Name)) bPresent = true;
		}
		if (bPresent)
		{
			FName TypeName;
			FMetaSoundBuilderNodeInputHandle Handle;
			Target.Builder->FindGraphOutputNode(FName(*Name), TypeName, Handle, R);
			if (MSEditOk(R)) DataType = TypeName.ToString();
		}
	}
	else
	{
		const FMetasoundFrontendDocument* Doc = MSEditDocument(Target);
		bool bFellBackFromDefaultPage = false;
		const FMetasoundFrontendGraph* Graph = MSEditFindGraph(Doc, &bFellBackFromDefaultPage);
		if (Doc && !Graph)
		{
			// Variables live on a page, so with no page there is no honest answer
			// to "is it present": reporting alreadyDeleted here would tell a
			// caller the variable was removed from a document that holds nothing.
			return MSEditNoGraphPagesError(Target);
		}
		if (Graph)
		{
			for (const FMetasoundFrontendVariable& Variable : Graph->Variables)
			{
				Known.Add(Variable.Name.ToString());
				if (Variable.Name == FName(*Name))
				{
					bPresent = true;
					DataType = Variable.TypeName.ToString();
				}
			}
			VariablePage = Graph;
			bVariablePageFellBack = bFellBackFromDefaultPage;
		}
	}

	if (!bPresent)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Noop->SetStringField(TEXT("source"), Target.Source);
		Noop->SetStringField(TEXT("memberKind"), Kind);
		Noop->SetStringField(TEXT("name"), Name);
		Noop->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No graph %s named '%s'. Present: %s"),
			*Kind, *Name,
			Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("(none)")));
		return MCPResult(Noop);
	}

	R = EMetaSoundBuilderResult::Failed;
	if (Kind == TEXT("input")) Target.Builder->RemoveGraphInput(FName(*Name), R);
	else if (Kind == TEXT("output")) Target.Builder->RemoveGraphOutput(FName(*Name), R);
	else Target.Builder->RemoveGraphVariable(FName(*Name), R);

	if (!MSEditOk(R))
	{
		return MCPError(FString::Printf(
			TEXT("The MetaSound builder refused to remove graph %s '%s' from '%s'."), *Kind, *Name, *Target.AssetPath));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("memberKind"), Kind);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("dataType"), DataType);
	MSEditStampPage(Result, VariablePage, bVariablePageFellBack);
	MSEditFinish(Result, Target);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
	Payload->SetStringField(TEXT("name"), Name);
	Payload->SetStringField(TEXT("dataType"), DataType);
	if (Kind == TEXT("input"))
	{
		MCPSetRollback(Result, TEXT("metasound_add_graph_input"), Payload);
	}
	else if (Kind == TEXT("output"))
	{
		MCPSetRollback(Result, TEXT("metasound_add_graph_output"), Payload);
	}
	else
	{
		// No add_graph_variable action ships, so the honest inverse is the one
		// call that can put a variable back: re-author the graph.
		MCPSetRollback(Result, TEXT("metasound_author"), Payload);
	}
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), Kind == TEXT("variable")
		? TEXT("There is no add-variable action, so this rollback cannot restore the variable. Re-author the graph, or record it with audio(metasound_list_variables) before removing.")
		: TEXT("The member is restored with its data type and NO default value, and every edge it drove was cut with it. Read those with audio(metasound_list_connections) before removing if they matter."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// metasound_rename_member
//
// Not a property write: renaming a graph input or output rewires the template
// nodes that stand in for it inside the graph, which is why the builder owns
// the operation and a direct write to the document would leave dangling
// references.
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundRenameMember(const TSharedPtr<FJsonObject>& Params)
{
	FString MemberKind;
	if (auto Err = RequireString(Params, TEXT("memberKind"), MemberKind)) return Err;
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString NewName;
	if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;

	const FString Kind = MemberKind.ToLower();
	if (Kind != TEXT("input") && Kind != TEXT("output"))
	{
		return MCPError(FString::Printf(
			TEXT("memberKind '%s' cannot be renamed. Use 'input' or 'output'. Graph variables have no rename on the builder; remove and re-author instead."),
			*MemberKind));
	}

	FMSEditTarget Target;
	TSharedPtr<FJsonValue> Error;
	if (!MSEditResolve(Params, Target, Error)) return Error;

	// Validate both names before writing: renaming onto an existing name would
	// collide, and renaming something absent would report a success that did
	// nothing.
	EMetaSoundBuilderResult NamesResult = EMetaSoundBuilderResult::Failed;
	TArray<FName> Existing = (Kind == TEXT("input"))
		? Target.Builder->GetGraphInputNames(NamesResult)
		: Target.Builder->GetGraphOutputNames(NamesResult);

	TArray<FString> Known;
	bool bHasSource = false;
	bool bHasTarget = false;
	for (const FName& N : Existing)
	{
		Known.Add(N.ToString());
		if (N == FName(*Name)) bHasSource = true;
		if (N == FName(*NewName)) bHasTarget = true;
	}

	if (!bHasSource)
	{
		if (bHasTarget)
		{
			// Already renamed: a replayed step is a success that says so.
			auto Done = MCPSuccess();
			MCPSetExisted(Done);
			Done->SetBoolField(TEXT("unchanged"), true);
			Done->SetStringField(TEXT("assetPath"), Target.AssetPath);
			Done->SetStringField(TEXT("source"), Target.Source);
			Done->SetStringField(TEXT("memberKind"), Kind);
			Done->SetStringField(TEXT("name"), Name);
			Done->SetStringField(TEXT("newName"), NewName);
			Done->SetStringField(TEXT("note"), TEXT("The new name is already present and the old one is gone, so the rename had already been applied."));
			TSharedPtr<FJsonObject> BackPayload = MakeShared<FJsonObject>();
			BackPayload->SetStringField(TEXT("assetPath"), Target.AssetPath);
			BackPayload->SetStringField(TEXT("memberKind"), Kind);
			BackPayload->SetStringField(TEXT("name"), NewName);
			BackPayload->SetStringField(TEXT("newName"), Name);
			MCPSetRollback(Done, TEXT("metasound_rename_member"), BackPayload);
			return MCPResult(Done);
		}
		return MCPError(FString::Printf(
			TEXT("No graph %s named '%s' in '%s'. Present: %s"),
			*Kind, *Name, *Target.AssetPath,
			Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("(none)")));
	}
	if (bHasTarget)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' already names a graph %s in '%s', so the rename would collide. Graph %ss: %s"),
			*NewName, *Kind, *Target.AssetPath, *Kind, *FString::Join(Known, TEXT(", "))));
	}

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	if (Kind == TEXT("input"))
	{
		Target.Builder->SetGraphInputName(FName(*Name), FName(*NewName), R);
	}
	else
	{
		Target.Builder->SetGraphOutputName(FName(*Name), FName(*NewName), R);
	}
	if (!MSEditOk(R))
	{
		return MCPError(FString::Printf(
			TEXT("The MetaSound builder refused to rename graph %s '%s' to '%s'."), *Kind, *Name, *NewName));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetStringField(TEXT("memberKind"), Kind);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("newName"), NewName);
	MSEditFinish(Result, Target);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
	Payload->SetStringField(TEXT("memberKind"), Kind);
	Payload->SetStringField(TEXT("name"), NewName);
	Payload->SetStringField(TEXT("newName"), Name);
	MCPSetRollback(Result, TEXT("metasound_rename_member"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// soundcue_remove_node
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAudioHandlers::SoundCueRemoveNode(const TSharedPtr<FJsonObject>& Params)
{
	FString CuePath;
	if (auto Err = RequireStringAlt(Params, TEXT("cuePath"), TEXT("assetPath"), CuePath)) return Err;
	FString NodeId;
	if (auto Err = RequireString(Params, TEXT("nodeId"), NodeId)) return Err;

	USoundCue* Cue = Cast<USoundCue>(MCPLoadAssetObject(CuePath));
	if (!Cue) return MCPError(FString::Printf(TEXT("SoundCue not found: %s"), *CuePath));

	USoundNode* Node = MSEditFindCueNode(Cue, NodeId);
	if (!Node)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("cuePath"), CuePath);
		Noop->SetStringField(TEXT("nodeId"), NodeId);
		Noop->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No node '%s' in this cue. Nodes: %s"), *NodeId, *MSEditCueNodeIds(Cue)));
		return MCPResult(Noop);
	}

	const FString NodeType = Node->GetClass()->GetName();
	const bool bWasRoot = (Cue->FirstNode == Node);

	// Detach from every parent before removing, or the tree keeps a null child
	// slot that the cue evaluates as silence.
	TArray<TSharedPtr<FJsonValue>> DetachedFrom;
#if WITH_EDITORONLY_DATA
	for (USoundNode* Candidate : Cue->AllNodes)
	{
		if (!Candidate || Candidate == Node) continue;
		for (int32 i = Candidate->ChildNodes.Num() - 1; i >= 0; --i)
		{
			if (Candidate->ChildNodes[i] != Node) continue;
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("parentNodeId"), Candidate->GetName());
			O->SetNumberField(TEXT("childIndex"), i);
			DetachedFrom.Add(MakeShared<FJsonValueObject>(O));
			Candidate->RemoveChildNode(i);
		}
	}
#endif

	// Orphan the node's own children rather than deleting them: they are
	// separate nodes the caller may still want, and a cascade nobody asked for
	// is the harder failure to recover from.
	TArray<TSharedPtr<FJsonValue>> OrphanedChildren;
	for (USoundNode* Child : Node->ChildNodes)
	{
		if (Child) OrphanedChildren.Add(MakeShared<FJsonValueString>(Child->GetName()));
	}
	Node->ChildNodes.Empty();

	if (bWasRoot)
	{
		Cue->FirstNode = nullptr;
	}

#if WITH_EDITORONLY_DATA
	Cue->AllNodes.Remove(Node);
	// The paired editor graph node has to go with it, or the cue graph shows a
	// node the audio tree no longer has.
	if (UEdGraphNode* GraphNode = Node->GetGraphNode())
	{
		if (UEdGraph* Graph = GraphNode->GetGraph())
		{
			Graph->RemoveNode(GraphNode);
		}
	}
#endif

	MSEditSaveCue(Cue, CuePath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("cuePath"), CuePath);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetStringField(TEXT("nodeType"), NodeType);
	Result->SetBoolField(TEXT("wasRoot"), bWasRoot);
	Result->SetArrayField(TEXT("detachedFrom"), DetachedFrom);
	Result->SetArrayField(TEXT("orphanedChildren"), OrphanedChildren);
	Result->SetStringField(TEXT("root"), Cue->FirstNode ? Cue->FirstNode->GetName() : FString());
	if (bWasRoot)
	{
		Result->SetStringField(TEXT("warning"),
			TEXT("That node was the cue root, so this cue now plays nothing. Set a new root with audio(cue_connect) and no parentNodeId."));
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("cuePath"), CuePath);
	Payload->SetStringField(TEXT("nodeType"), NodeType);
	MCPSetRollback(Result, TEXT("soundcue_add_node"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("soundcue_add_node restores a DEFAULT node of the same type under a NEW node id. Its property values, its wave assignment, its parent link and its ")
		TEXT("children are not restored; detachedFrom and orphanedChildren list what has to be re-wired with audio(cue_connect)."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// soundcue_disconnect
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAudioHandlers::SoundCueDisconnect(const TSharedPtr<FJsonObject>& Params)
{
	FString CuePath;
	if (auto Err = RequireStringAlt(Params, TEXT("cuePath"), TEXT("assetPath"), CuePath)) return Err;

	USoundCue* Cue = Cast<USoundCue>(MCPLoadAssetObject(CuePath));
	if (!Cue) return MCPError(FString::Printf(TEXT("SoundCue not found: %s"), *CuePath));

	const FString ChildNodeId = OptionalString(Params, TEXT("childNodeId"));
	const FString ParentNodeId = OptionalString(Params, TEXT("parentNodeId"));
	const bool bClearRoot = OptionalBool(Params, TEXT("clearRoot"), false);

	if (ChildNodeId.IsEmpty() && !bClearRoot)
	{
		return MCPError(FString::Printf(
			TEXT("Nothing to disconnect. Pass 'childNodeId' (detach that child from every parent, or from 'parentNodeId' alone), or clearRoot=true to unset the cue root. Nodes in this cue: %s"),
			*MSEditCueNodeIds(Cue)));
	}

	USoundNode* Child = nullptr;
	if (!ChildNodeId.IsEmpty())
	{
		Child = MSEditFindCueNode(Cue, ChildNodeId);
		if (!Child)
		{
			return MCPError(FString::Printf(
				TEXT("Child node '%s' not found in this cue. Nodes: %s"), *ChildNodeId, *MSEditCueNodeIds(Cue)));
		}
	}

	USoundNode* Parent = nullptr;
	if (!ParentNodeId.IsEmpty())
	{
		Parent = MSEditFindCueNode(Cue, ParentNodeId);
		if (!Parent)
		{
			return MCPError(FString::Printf(
				TEXT("Parent node '%s' not found in this cue. Nodes: %s"), *ParentNodeId, *MSEditCueNodeIds(Cue)));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Detached;
	bool bClearedRoot = false;

	if (bClearRoot && Cue->FirstNode && (!Child || Cue->FirstNode == Child))
	{
		Cue->FirstNode = nullptr;
		bClearedRoot = true;
	}

	if (Child)
	{
#if WITH_EDITORONLY_DATA
		for (USoundNode* Candidate : Cue->AllNodes)
		{
			if (!Candidate || Candidate == Child) continue;
			if (Parent && Candidate != Parent) continue;
			for (int32 i = Candidate->ChildNodes.Num() - 1; i >= 0; --i)
			{
				if (Candidate->ChildNodes[i] != Child) continue;
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("parentNodeId"), Candidate->GetName());
				O->SetNumberField(TEXT("childIndex"), i);
				Detached.Add(MakeShared<FJsonValueObject>(O));
				Candidate->RemoveChildNode(i);
			}
		}
#endif
	}

	if (Detached.Num() == 0 && !bClearedRoot)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDisconnected"), true);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("cuePath"), CuePath);
		Noop->SetStringField(TEXT("childNodeId"), ChildNodeId);
		Noop->SetStringField(TEXT("parentNodeId"), ParentNodeId);
		Noop->SetStringField(TEXT("note"), TEXT("That link was not present; nothing was disconnected."));
		return MCPResult(Noop);
	}

	MSEditSaveCue(Cue, CuePath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDisconnected"), false);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetStringField(TEXT("cuePath"), CuePath);
	Result->SetStringField(TEXT("childNodeId"), ChildNodeId);
	Result->SetArrayField(TEXT("detached"), Detached);
	Result->SetBoolField(TEXT("clearedRoot"), bClearedRoot);
	Result->SetStringField(TEXT("root"), Cue->FirstNode ? Cue->FirstNode->GetName() : FString());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("cuePath"), CuePath);
	Payload->SetStringField(TEXT("childNodeId"), ChildNodeId);
	if (Detached.Num() > 0)
	{
		const TSharedPtr<FJsonObject> First = Detached[0]->AsObject();
		Payload->SetStringField(TEXT("parentNodeId"), First->GetStringField(TEXT("parentNodeId")));
		Payload->SetNumberField(TEXT("childIndex"), First->GetNumberField(TEXT("childIndex")));
	}
	MCPSetRollback(Result, TEXT("soundcue_connect"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), Detached.Num() > 1 || bClearedRoot);
	if (Detached.Num() > 1 || bClearedRoot)
	{
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The rollback restores ONE parent link (the first in 'detached'). Any further links, and a cleared root, have to be replayed from 'detached' with audio(cue_connect)."));
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_sound_class_parent
//
// Not a property write, for the same reason set_submix_parent is not: a sound
// class hierarchy is stored on BOTH ends. ParentClass on the child and
// ChildClasses on the parent, plus a removal from the OLD parent's list.
// USoundClass::SetParentClass is the call that keeps all three consistent, and
// it also refuses a cycle. Writing ParentClass alone produces a class the audio
// engine walks up from and the mixer never walks down to.
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAudioHandlers::SetSoundClassParent(const TSharedPtr<FJsonObject>& Params)
{
	FString SoundClassPath;
	if (auto Err = RequireStringAlt(Params, TEXT("soundClassPath"), TEXT("assetPath"), SoundClassPath)) return Err;

	USoundClass* SoundClass = Cast<USoundClass>(MCPLoadAssetObject(SoundClassPath));
	if (!SoundClass)
	{
		return MCPError(FString::Printf(TEXT("SoundClass not found: %s"), *SoundClassPath));
	}

	const FString ParentPath = OptionalString(Params, TEXT("parentPath"));
	USoundClass* NewParent = nullptr;
	if (!ParentPath.IsEmpty())
	{
		NewParent = Cast<USoundClass>(MCPLoadAssetObject(ParentPath));
		if (!NewParent)
		{
			return MCPError(FString::Printf(
				TEXT("Parent SoundClass not found: %s. Pass an empty parentPath to detach '%s' to the root instead."),
				*ParentPath, *SoundClass->GetName()));
		}
		if (NewParent == SoundClass)
		{
			return MCPError(TEXT("A sound class cannot be its own parent."));
		}
	}

#if WITH_EDITOR
	// A cycle would make the mixer's volume walk never terminate. RecurseCheckChild
	// answers whether the proposed parent is already below this class.
	if (NewParent && SoundClass->RecurseCheckChild(NewParent))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is already a descendant of '%s', so parenting one to the other would make a cycle."),
			*NewParent->GetName(), *SoundClass->GetName()));
	}
#endif

	USoundClass* OldParent = SoundClass->ParentClass;
	const FString OldParentPath = OldParent ? OldParent->GetPathName() : FString();

	auto AttachRollback = [&SoundClassPath, &OldParentPath](TSharedPtr<FJsonObject> Res)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("soundClassPath"), SoundClassPath);
		Payload->SetStringField(TEXT("parentPath"), OldParentPath);
		MCPSetRollback(Res, TEXT("set_sound_class_parent"), Payload);
		Res->SetBoolField(TEXT("rollbackLossy"), false);
	};

	if (OldParent == NewParent)
	{
		auto Unchanged = MCPSuccess();
		MCPSetExisted(Unchanged);
		Unchanged->SetBoolField(TEXT("unchanged"), true);
		Unchanged->SetStringField(TEXT("soundClassPath"), SoundClassPath);
		Unchanged->SetStringField(TEXT("parentPath"), ParentPath);
		AttachRollback(Unchanged);
		return MCPResult(Unchanged);
	}

#if WITH_EDITOR
	SoundClass->SetParentClass(NewParent);
#else
	return MCPError(TEXT("Sound class reparenting requires an editor build."));
#endif

	SoundClass->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(SoundClass, /*bOnlyIfIsDirty*/ true);
	if (OldParent)
	{
		OldParent->MarkPackageDirty();
		UEditorAssetLibrary::SaveLoadedAsset(OldParent, /*bOnlyIfIsDirty*/ true);
	}
	if (NewParent)
	{
		NewParent->MarkPackageDirty();
		UEditorAssetLibrary::SaveLoadedAsset(NewParent, /*bOnlyIfIsDirty*/ true);
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetStringField(TEXT("soundClassPath"), SoundClassPath);
	Result->SetStringField(TEXT("parentPath"), ParentPath);
	Result->SetStringField(TEXT("previousParentPath"), OldParentPath);
	// Read the other end back rather than asserting the write took.
	Result->SetBoolField(TEXT("listedOnParent"),
		NewParent ? NewParent->ChildClasses.Contains(SoundClass) : false);
	AttachRollback(Result);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// read_sound_routing
//
// Six actions assign routing onto a sound and nothing read it back, so an agent
// could wire a whole mix and had no way to see where a sound actually goes.
// This walks both chains a sound sits in - its sound class up to the root, and
// its submix up to the master - and reports the problems that make a correctly
// authored mix silent anyway.
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAudioHandlers::ReadSoundRouting(const TSharedPtr<FJsonObject>& Params)
{
	FString SoundPath;
	if (auto Err = RequireStringAlt(Params, TEXT("soundPath"), TEXT("assetPath"), SoundPath)) return Err;

	UObject* Asset = MCPLoadAssetObject(SoundPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Sound not found: %s"), *SoundPath));
	}
	USoundBase* Sound = Cast<USoundBase>(Asset);
	if (!Sound)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a %s, not a sound. audio(read_sound_routing) reads USoundBase assets: SoundWave, SoundCue, MetaSoundSource."),
			*SoundPath, *Asset->GetClass()->GetName()));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("soundPath"), Sound->GetPathName());
	Result->SetStringField(TEXT("soundClass"), Sound->GetClass()->GetName());
	Result->SetNumberField(TEXT("durationSeconds"), Sound->Duration);
	Result->SetNumberField(TEXT("priority"), Sound->Priority);

	TArray<TSharedPtr<FJsonValue>> Problems;
	auto Problem = [&Problems](const FString& Text)
	{
		Problems.Add(MakeShared<FJsonValueString>(Text));
	};

	// A fact the caller needs but which does not break routing. It is kept off
	// `problems` deliberately, because `routable` is asserted on that array
	// being empty and a correctly routed sound must not read as broken.
	TArray<TSharedPtr<FJsonValue>> Notes;
	auto Note = [&Notes](const FString& Text)
	{
		Notes.Add(MakeShared<FJsonValueString>(Text));
	};

	// ── Sound class chain, child to root ────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> ClassChain;
	if (USoundClass* SC = Sound->SoundClassObject)
	{
		Result->SetStringField(TEXT("soundClassPath"), SC->GetPathName());
		USoundClass* Walk = SC;
		int32 Guard = 0;
		while (Walk && Guard++ < 64)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), Walk->GetName());
			O->SetStringField(TEXT("objectPath"), Walk->GetPathName());
			O->SetNumberField(TEXT("volume"), Walk->Properties.Volume);
			O->SetNumberField(TEXT("pitch"), Walk->Properties.Pitch);
			ClassChain.Add(MakeShared<FJsonValueObject>(O));
			if (FMath::IsNearlyZero(Walk->Properties.Volume))
			{
				Problem(FString::Printf(
					TEXT("Sound class '%s' has Volume 0, so everything under it is silent. Fix with asset(set_property, assetPath='%s', propertyName='Properties.Volume')."),
					*Walk->GetName(), *Walk->GetPathName()));
			}
			Walk = Walk->ParentClass;
		}
		if (Guard >= 64)
		{
			Problem(TEXT("The sound class parent chain is longer than 64 links, which means it contains a cycle. Repair it with audio(set_sound_class_parent)."));
		}
	}
	else
	{
		Problem(FString::Printf(
			TEXT("No sound class is assigned, so this sound is not affected by any sound mix. Assign one with audio(set_sound_class, soundPath='%s', soundClassPath=the class)."),
			*SoundPath));
	}
	Result->SetArrayField(TEXT("soundClassChain"), ClassChain);

	// ── Submix chain, sound to master ───────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> SubmixChain;
	if (USoundSubmixBase* Base = Sound->SoundSubmixObject)
	{
		Result->SetStringField(TEXT("submixPath"), Base->GetPathName());
		USoundSubmixBase* Walk = Base;
		int32 Guard = 0;
		while (Walk && Guard++ < 64)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), Walk->GetName());
			O->SetStringField(TEXT("objectPath"), Walk->GetPathName());
			USoundSubmix* Full = Cast<USoundSubmix>(Walk);
			if (Full)
			{
				O->SetNumberField(TEXT("effectChainLength"), Full->SubmixEffectChain.Num());
				TArray<TSharedPtr<FJsonValue>> Effects;
				for (USoundEffectSubmixPreset* Preset : Full->SubmixEffectChain)
				{
					Effects.Add(MakeShared<FJsonValueString>(
						Preset ? Preset->GetPathName() : FString(TEXT("(empty slot)"))));
					if (!Preset)
					{
						Problem(FString::Printf(
							TEXT("Submix '%s' has an empty slot in its effect chain. Rewrite the chain with asset(set_property, assetPath='%s', propertyName='SubmixEffectChain')."),
							*Walk->GetName(), *Walk->GetPathName()));
					}
				}
				O->SetArrayField(TEXT("effectChain"), Effects);
			}
			SubmixChain.Add(MakeShared<FJsonValueObject>(O));
			// Only the parented submix families continue the walk. An endpoint
			// submix has no parent by design and ends the chain.
			USoundSubmixWithParentBase* Parented = Cast<USoundSubmixWithParentBase>(Walk);
			Walk = Parented ? Parented->ParentSubmix : nullptr;
		}
		if (Guard >= 64)
		{
			Problem(TEXT("The submix parent chain is longer than 64 links, which means it contains a cycle. Repair it with audio(set_submix_parent)."));
		}
	}
	else
	{
		Result->SetStringField(TEXT("submixNote"),
			TEXT("No base submix is set, so this sound routes to the project's master submix. That is the normal default, not a fault."));
	}
	Result->SetArrayField(TEXT("submixChain"), SubmixChain);

	// ── Submix sends ────────────────────────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> Sends;
	TArray<FString> SeenSends;
	for (const FSoundSubmixSendInfo& Send : Sound->SoundSubmixSends)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		const FString Target = Send.SoundSubmix ? Send.SoundSubmix->GetPathName() : FString();
		O->SetStringField(TEXT("submixPath"), Target);
		O->SetNumberField(TEXT("sendLevel"), Send.SendLevel);
		Sends.Add(MakeShared<FJsonValueObject>(O));

		if (!Send.SoundSubmix)
		{
			Problem(TEXT("A submix send has no submix, so it does nothing. Rewrite the array with asset(set_property, propertyName='SoundSubmixSends')."));
		}
		else if (SeenSends.Contains(Target))
		{
			Problem(FString::Printf(
				TEXT("Two sends target '%s'. add_sound_submix_send appends without checking, so a replayed call duplicates; rewrite the array with asset(set_property, assetPath='%s', propertyName='SoundSubmixSends')."),
				*Send.SoundSubmix->GetName(), *SoundPath));
		}
		else
		{
			SeenSends.Add(Target);
		}

		if (Send.SoundSubmix && FMath::IsNearlyZero(Send.SendLevel))
		{
			Problem(FString::Printf(
				TEXT("The send to '%s' is at level 0, so nothing reaches it."), *Send.SoundSubmix->GetName()));
		}
	}
	Result->SetArrayField(TEXT("submixSends"), Sends);

	// ── Attenuation ─────────────────────────────────────────────────────
	if (USoundAttenuation* Atten = Sound->AttenuationSettings)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("objectPath"), Atten->GetPathName());
		O->SetBoolField(TEXT("attenuate"), Atten->Attenuation.bAttenuate != 0);
		O->SetBoolField(TEXT("spatialize"), Atten->Attenuation.bSpatialize != 0);
		O->SetBoolField(TEXT("enableOcclusion"), Atten->Attenuation.bEnableOcclusion != 0);
		O->SetNumberField(TEXT("falloffDistance"), Atten->Attenuation.FalloffDistance);
		Result->SetObjectField(TEXT("attenuation"), O);

		if (!Atten->Attenuation.bAttenuate && Atten->Attenuation.FalloffDistance > 0.0f)
		{
			Problem(FString::Printf(
				TEXT("Attenuation '%s' has a falloff distance of %.1f but bAttenuate is off, so distance has no effect. Set it with asset(set_property, assetPath='%s', propertyName='Attenuation.bAttenuate')."),
				*Atten->GetName(), Atten->Attenuation.FalloffDistance, *Atten->GetPathName()));
		}
		if (Atten->Attenuation.bAttenuate && FMath::IsNearlyZero(Atten->Attenuation.FalloffDistance))
		{
			Problem(FString::Printf(
				TEXT("Attenuation '%s' attenuates over a falloff distance of 0, so the sound is inaudible past its inner radius."), *Atten->GetName()));
		}
	}
	else
	{
		Result->SetStringField(TEXT("attenuationNote"),
			TEXT("No attenuation asset, so this sound plays 2D at full volume regardless of listener distance."));
	}

	// ── Concurrency ─────────────────────────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> Concurrencies;
	for (USoundConcurrency* Conc : Sound->ConcurrencySet)
	{
		if (!Conc) continue;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("objectPath"), Conc->GetPathName());

		// MaxCount has TWO values and reading the public field alone reports the
		// wrong one for exactly the assets where it matters. When
		// bEnableMaxCountPlatformScaling is on, the field is EditCondition'd off
		// in the details panel and the count in force comes from the private
		// PlatformMaxCount (an FPerPlatformInt) resolved for this platform.
		// GetMaxCount() is the accessor that answers with the resolved value,
		// and IsMaxCountPlatformScalingEnabled() reports which of the two is
		// live (Sound/SoundConcurrency.h: MaxCount public, the flag and
		// PlatformMaxCount private, both accessors ENGINE_API).
		//
		// Both numbers are reported rather than one silently replacing the
		// other: `maxCount` keeps meaning the authored field, and
		// `effectiveMaxCount` is what the voice limiter actually applies.
		const int32 AuthoredMaxCount = Conc->Concurrency.MaxCount;
		const int32 EffectiveMaxCount = Conc->Concurrency.GetMaxCount();
		const bool bPlatformScaled = Conc->Concurrency.IsMaxCountPlatformScalingEnabled();
		O->SetNumberField(TEXT("maxCount"), AuthoredMaxCount);
		O->SetNumberField(TEXT("effectiveMaxCount"), EffectiveMaxCount);
		O->SetBoolField(TEXT("maxCountPlatformScaling"), bPlatformScaled);
		O->SetStringField(TEXT("maxCountSource"),
			bPlatformScaled ? TEXT("PlatformMaxCount") : TEXT("MaxCount"));
		O->SetBoolField(TEXT("limitToOwner"), Conc->Concurrency.bLimitToOwner != 0);
		// VolumeScale is private on FSoundConcurrencySettings; GetVolumeScale()
		// is the public accessor (Sound/SoundConcurrency.h).
		O->SetNumberField(TEXT("volumeScale"), Conc->Concurrency.GetVolumeScale());
		Concurrencies.Add(MakeShared<FJsonValueObject>(O));

		// The property a caller has to write to change the count in force is
		// not the same property in both modes, so the fix named here follows
		// which one is live.
		const FString CountProperty = bPlatformScaled
			? TEXT("Concurrency.PlatformMaxCount")
			: TEXT("Concurrency.MaxCount");

		if (EffectiveMaxCount <= 0)
		{
			const FString ScalingClause = bPlatformScaled
				? FString::Printf(
					TEXT(" MaxCount platform scaling is ON, so the authored MaxCount of %d is ignored and the count comes from PlatformMaxCount for this platform."),
					AuthoredMaxCount)
				: FString();
			Problem(FString::Printf(
				TEXT("Concurrency '%s' has an effective MaxCount of %d, so no voice of this sound can ever start.%s Set it with asset(set_property, assetPath='%s', propertyName='%s')."),
				*Conc->GetName(), EffectiveMaxCount, *ScalingClause, *Conc->GetPathName(), *CountProperty));
		}
		else if (bPlatformScaled && EffectiveMaxCount != AuthoredMaxCount)
		{
			// Not a failure, but the authored number is the one a caller reads
			// off the details panel and it is not the one being enforced.
			Note(FString::Printf(
				TEXT("Concurrency '%s' authors MaxCount %d but enforces %d, because MaxCount platform scaling is on and PlatformMaxCount answers for this platform. Change the enforced count with asset(set_property, assetPath='%s', propertyName='Concurrency.PlatformMaxCount'), or turn scaling off with propertyName='Concurrency.bEnableMaxCountPlatformScaling' to make MaxCount authoritative again."),
				*Conc->GetName(), AuthoredMaxCount, EffectiveMaxCount, *Conc->GetPathName()));
		}
	}
	Result->SetArrayField(TEXT("concurrencies"), Concurrencies);
	Result->SetBoolField(TEXT("overrideConcurrency"), Sound->bOverrideConcurrency != 0);
	if (Sound->bOverrideConcurrency && Concurrencies.Num() > 0)
	{
		Problem(FString::Printf(
			TEXT("bOverrideConcurrency is set, so the %d assigned concurrency asset(s) are ignored in favour of ConcurrencyOverrides on the sound itself."),
			Concurrencies.Num()));
	}

	Result->SetArrayField(TEXT("problems"), Problems);
	Result->SetNumberField(TEXT("problemCount"), Problems.Num());
	Result->SetArrayField(TEXT("notes"), Notes);
	Result->SetNumberField(TEXT("noteCount"), Notes.Num());
	Result->SetBoolField(TEXT("routable"), Problems.Num() == 0);
	return MCPResult(Result);
}
