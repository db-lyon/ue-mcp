// MetaSound graph authoring for the audio category.
//
// Builds a MetaSoundSource end-to-end through the MetaSound Builder subsystem:
// create -> add nodes -> add graph inputs/outputs -> connect vertices / audio out
// -> set input defaults -> build to the asset.
//
// Builder model - and the two engine facts that decide it.
//
// 1. A MetaSoundSource cannot be created by a bare NewObject. Its document lives
//    in the UPROPERTY RootMetasoundDocument, and a default-constructed one holds
//    no interfaces, no dependencies and no graph pages at all. The asset loads,
//    reports as a MetaSoundSource and is completely empty. Only
//    UMetaSoundSourceFactory runs the initialization that installs UE.Source,
//    UE.Source.OneShot and the output-format interface and mints the default
//    graph page, so every asset here is created through that factory. It is
//    resolved by class path rather than linked, because it lives in the
//    MetasoundEditor module and this plugin does not depend on it.
//
// 2. UMetaSoundBuilderBase::BuildAndOverwriteMetaSound CANNOT write to an asset.
//    Its own header says so: "Not permissible to overwrite MetaSound asset, only
//    transient MetaSound". It returns no result and reports no failure, so the
//    old create-a-transient-builder-then-overwrite shape reported success on
//    every call and left the asset's document empty forever.
//
// So the builder is attached to the ASSET's own document, through
// Metasound::Engine::FDocumentBuilderRegistry::FindOrBeginBuilding - the same
// route the MetaSound editor itself uses to open an asset, and the one the
// removal/disconnect actions in AudioHandlers_Depth.cpp already take. Every
// authoring call therefore writes straight into the document that
// audio(metasound_read_document) reads and that the package serializes, and the
// node ids handed out by metasound_add_node are the ids a read hands back.
//
// One consequence worth stating: authoring is no longer session-scoped. A
// MetaSound this editor run did not create can be edited, because the session is
// attached on demand rather than remembered from a create call. metasound_build
// now means "persist the document to disk", not "flush a detached builder".

#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"

#include "Factories/Factory.h"

#include "MetasoundBuilderSubsystem.h"
#include "MetasoundBuilderBase.h"
// MetasoundDocumentBuilderRegistry.h, and never MetasoundFrontendDocumentBuilderRegistry.h:
// the frontend header ships on 5.8 only. This MetasoundEngine header reaches
// Metasound::Frontend::IDocumentBuilderRegistry on every supported engine, by
// including the frontend header on 5.8 and MetasoundDocumentInterface.h, where
// 5.7 and earlier declare the class, on all of them.
#include "MetasoundDocumentBuilderRegistry.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundSource.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendDocument.h"
#include "Interfaces/MetasoundFrontendSourceInterface.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"

namespace
{
	/** A builder attached to a MetaSoundSource asset, plus the interface vertex
	 *  handles the source and output-format interfaces put on its graph. */
	struct FMSSession
	{
		TWeakObjectPtr<UMetaSoundSourceBuilder> Builder;
		FMetaSoundBuilderNodeOutputHandle OnPlay;
		FMetaSoundBuilderNodeInputHandle OnFinished;
		TArray<FMetaSoundBuilderNodeInputHandle> AudioOuts;
		/** The graph-output vertex name of each entry in AudioOuts, at the same
		 *  index. An audio output IS a graph output ("Out Mono", "Out Left",
		 *  "Out Right", ...), and audio(metasound_disconnect) addresses it by
		 *  that name, so the name is what a rollback for
		 *  metasound_connect_audio_out has to carry. Filled in the same loop as
		 *  AudioOuts so the two can never drift apart. */
		TArray<FName> AudioOutNames;
		bool bOneShot = true;
	};

	/** Sessions keyed by MetaSound asset object path. Editor-session lived, and a
	 *  cache rather than a gate: a miss re-attaches instead of refusing. */
	static TMap<FString, FMSSession> GMetaSoundSessions;

	bool Ok(EMetaSoundBuilderResult R);

	/**
	 * The MetaSoundSource factory, resolved by class path.
	 *
	 * UMetaSoundSourceFactory is in the MetasoundEditor module, which this plugin
	 * does not link (and must not, since it is editor-only). Its FactoryCreateNew
	 * is what runs UMetaSoundEditorSubsystem::InitAsset, and that is the ONLY
	 * route that produces a MetaSoundSource with interfaces and a graph page.
	 * Creating one without it yields an asset whose document is empty, which
	 * every read then correctly reports as having nothing to read.
	 */
	UFactory* MSAuthorSourceFactory()
	{
		UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/MetasoundEditor.MetaSoundSourceFactory"));
		if (!FactoryClass)
		{
			return nullptr;
		}
		return NewObject<UFactory>(GetTransientPackage(), FactoryClass);
	}

	/**
	 * The builder that edits this asset's own document, attached if it is not
	 * already. Mirrors MSEditResolve in AudioHandlers_Depth.cpp, which is the
	 * proven path: an edit made through this builder lands in the asset's
	 * RootMetasoundDocument immediately, with no separate flush step.
	 */
	UMetaSoundSourceBuilder* MSAuthorAssetBuilder(UMetaSoundSource* Source, TSharedPtr<FJsonValue>& OutError)
	{
#if WITH_EDITORONLY_DATA
		using namespace Metasound::Frontend;
		if (!Source)
		{
			return nullptr;
		}
		IDocumentBuilderRegistry* Registry = IDocumentBuilderRegistry::Get();
		if (!Registry)
		{
			OutError = MCPError(TEXT("The MetaSound document builder registry is not available, so no MetaSound can be authored. Enable the MetaSound plugin."));
			return nullptr;
		}
		Metasound::Engine::FDocumentBuilderRegistry& EngineRegistry =
			static_cast<Metasound::Engine::FDocumentBuilderRegistry&>(*Registry);
		return &EngineRegistry.FindOrBeginBuilding<UMetaSoundSourceBuilder>(*Source);
#else
		OutError = MCPError(TEXT("MetaSound authoring requires an editor build."));
		return nullptr;
#endif
	}

	/**
	 * Read OnPlay / OnFinished / the audio outs back off the graph.
	 *
	 * CreateSourceBuilder used to hand these out; an asset builder does not,
	 * because the interfaces already put the nodes on the graph. The audio-out
	 * vertex names are taken from the builder's own format info rather than
	 * spelled out, so "Out Mono" versus "Out Left"/"Out Right" versus the 5.1 and
	 * 7.1 orders come from the engine's table and the channel ORDER is the
	 * engine's, which is what makes audioOut:<n> mean the nth channel.
	 */
	void MSAuthorFillInterfaceHandles(UMetaSoundSourceBuilder& B, FMSSession& Session)
	{
		using namespace Metasound::Frontend;

		EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
		FName DataType;

		B.FindGraphInputNode(SourceInterface::Inputs::OnPlay, DataType, Session.OnPlay, R);
		B.FindGraphOutputNode(SourceOneShotInterface::Outputs::OnFinished, DataType, Session.OnFinished, R);

		Session.AudioOuts.Reset();
		Session.AudioOutNames.Reset();
		if (const Metasound::Engine::FOutputAudioFormatInfoPair* FormatInfo = B.FindOutputAudioFormatInfo())
		{
			for (const Metasound::FVertexName& VertexName : FormatInfo->Value.OutputVertexChannelOrder)
			{
				FMetaSoundBuilderNodeInputHandle Handle;
				B.FindGraphOutputNode(VertexName, DataType, Handle, R);
				if (Ok(R))
				{
					Session.AudioOuts.Add(Handle);
					Session.AudioOutNames.Add(VertexName);
				}
			}
		}

		Session.bOneShot = B.InterfaceIsDeclared(SourceOneShotInterface::GetVersion().Name);
	}

	/** Is a builder already attached to this MetaSound's document? Asks only:
	 *  FindBuilder, never FindOrBeginBuilding, so a read stays a read. */
	bool MSAuthorHasBuilder(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}
		TScriptInterface<IMetaSoundDocumentInterface> DocIface(Asset);
		if (Metasound::Frontend::IDocumentBuilderRegistry* Registry = Metasound::Frontend::IDocumentBuilderRegistry::Get())
		{
			return Registry->FindBuilder(DocIface) != nullptr;
		}
		return false;
	}

	/** Attach a session to an already-created MetaSoundSource asset. */
	FMSSession* MSAuthorOpenOnAsset(UMetaSoundSource* Source, const FString& AssetPath, TSharedPtr<FJsonValue>& OutError)
	{
		UMetaSoundSourceBuilder* B = MSAuthorAssetBuilder(Source, OutError);
		if (!B)
		{
			if (!OutError.IsValid())
			{
				OutError = MCPError(FString::Printf(
					TEXT("Could not open a MetaSound builder for '%s'."), *AssetPath));
			}
			return nullptr;
		}

		FMSSession Session;
		Session.Builder = B;
		MSAuthorFillInterfaceHandles(*B, Session);
		return &GMetaSoundSessions.Add(AssetPath, Session);
	}

	/**
	 * The builder session for this asset, attaching one if none is cached.
	 *
	 * This used to be a lookup that failed when create_metasound had not run in
	 * this editor session, which made every authoring action refuse on a
	 * MetaSound already on disk. The builder registry answers for any asset, so
	 * the cache is now only a cache.
	 */
	FMSSession* FindSession(const FString& AssetPath)
	{
		if (FMSSession* S = GMetaSoundSessions.Find(AssetPath))
		{
			if (S->Builder.IsValid())
			{
				return S;
			}
			GMetaSoundSessions.Remove(AssetPath);
		}

		UMetaSoundSource* Source = Cast<UMetaSoundSource>(MCPLoadAssetObject(AssetPath));
		if (!Source)
		{
			return nullptr;
		}

		// The cache is keyed by the resolved object path; a caller may pass the
		// package form of the same asset.
		const FString Resolved = Source->GetPathName();
		if (FMSSession* S = GMetaSoundSessions.Find(Resolved))
		{
			if (S->Builder.IsValid())
			{
				return S;
			}
			GMetaSoundSessions.Remove(Resolved);
		}

		TSharedPtr<FJsonValue> Ignored;
		return MSAuthorOpenOnAsset(Source, Resolved, Ignored);
	}

	/** "No builder for that path", told as the reason rather than as a refusal. */
	TSharedPtr<FJsonValue> MSAuthorNoSessionError(const FString& AssetPath)
	{
		UObject* Asset = MCPLoadAssetObject(AssetPath);
		if (!Asset)
		{
			return MCPAssetNotFoundError(AssetPath, TEXT("MetaSoundSource"));
		}
		if (!Cast<UMetaSoundSource>(Asset))
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is a %s, not a MetaSoundSource. The audio(metasound_*) authoring actions build sources; ")
				TEXT("audio(metasound_read_document) reads any MetaSound document."),
				*AssetPath, *Asset->GetClass()->GetName()));
		}
		return MCPError(FString::Printf(
			TEXT("Could not open a MetaSound builder for '%s'. The MetaSound plugin must be enabled and this must be an editor build."),
			*AssetPath));
	}

	/** Persist the asset's document. Every authoring entry point ends here. */
	void MSAuthorSave(const FString& AssetPath)
	{
		if (UObject* Asset = MCPLoadAssetObject(AssetPath))
		{
			Asset->MarkPackageDirty();
			UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty*/ false);
		}
	}

	FMetaSoundNodeHandle NodeFromId(const FString& Id)
	{
		FMetaSoundNodeHandle H;
		FGuid::Parse(Id, H.NodeID);
		return H;
	}

	/** Build a frontend literal from a JSON value, honoring an optional type hint. */
	FMetasoundFrontendLiteral MakeLiteral(const TSharedPtr<FJsonValue>& V, const FString& TypeHint)
	{
		FMetasoundFrontendLiteral Lit;
		if (!V.IsValid())
		{
			return Lit;
		}

		const FString Hint = TypeHint.ToLower();
		if (Hint == TEXT("int32") || Hint == TEXT("int"))
		{
			Lit.Set((int32)V->AsNumber());
			return Lit;
		}
		if (Hint == TEXT("bool"))
		{
			Lit.Set(V->AsBool());
			return Lit;
		}
		if (Hint == TEXT("string"))
		{
			Lit.Set(V->AsString());
			return Lit;
		}
		if (Hint == TEXT("float"))
		{
			Lit.Set((float)V->AsNumber());
			return Lit;
		}

		// No hint: infer from the JSON value kind.
		switch (V->Type)
		{
		case EJson::Boolean: Lit.Set(V->AsBool()); break;
		case EJson::Number:  Lit.Set((float)V->AsNumber()); break;
		case EJson::String:  Lit.Set(V->AsString()); break;
		default: break;
		}
		return Lit;
	}

	bool Ok(EMetaSoundBuilderResult R) { return R == EMetaSoundBuilderResult::Succeeded; }

	/**
	 * A frontend literal back out as the (value, dataType) pair MakeLiteral
	 * above turns into exactly that literal again. This is what lets
	 * metasound_set_input_default emit a rollback that restores the previous
	 * default rather than guessing at one.
	 *
	 * Only the four scalar kinds round-trip, because they are the only ones
	 * MakeLiteral can build. Everything else (None, UObject, every array kind)
	 * returns false, and the caller says so instead of emitting a rollback that
	 * would write a different value than the one that was there.
	 */
	bool MSAuthorLiteralToJson(const FMetasoundFrontendLiteral& Lit, TSharedPtr<FJsonValue>& OutValue, FString& OutTypeHint)
	{
		switch (Lit.GetType())
		{
		case EMetasoundFrontendLiteralType::Boolean:
		{
			bool B = false;
			if (!Lit.TryGet(B)) return false;
			OutValue = MakeShared<FJsonValueBoolean>(B);
			OutTypeHint = TEXT("bool");
			return true;
		}
		case EMetasoundFrontendLiteralType::Integer:
		{
			int32 I = 0;
			if (!Lit.TryGet(I)) return false;
			OutValue = MakeShared<FJsonValueNumber>((double)I);
			OutTypeHint = TEXT("int32");
			return true;
		}
		case EMetasoundFrontendLiteralType::Float:
		{
			float F = 0.0f;
			if (!Lit.TryGet(F)) return false;
			OutValue = MakeShared<FJsonValueNumber>((double)F);
			OutTypeHint = TEXT("float");
			return true;
		}
		case EMetasoundFrontendLiteralType::String:
		{
			FString S;
			if (!Lit.TryGet(S)) return false;
			OutValue = MakeShared<FJsonValueString>(S);
			OutTypeHint = TEXT("string");
			return true;
		}
		default:
			return false;
		}
	}

	/**
	 * Create (idempotently) the MetaSoundSource asset and attach a builder to its
	 * own document. On success returns the stored session and sets OutAssetPath.
	 * On failure returns nullptr and sets OutEarly (error / existed).
	 * Shared by create_metasound_source and the one-shot metasound_author.
	 */
	FMSSession* OpenBuilderSession(const TSharedPtr<FJsonObject>& Params, FString& OutAssetPath, TSharedPtr<FJsonValue>& OutEarly)
	{
		FString Name;
		if (auto Err = RequireString(Params, TEXT("name"), Name)) { OutEarly = Err; return nullptr; }

		const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/MetaSounds"));
		const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
		const FString Format = OptionalString(Params, TEXT("format"), TEXT("mono")).ToLower();
		const bool bOneShot = OptionalBool(Params, TEXT("oneShot"), true);

		UClass* Cls = FindObject<UClass>(nullptr, TEXT("/Script/MetasoundEngine.MetaSoundSource"));
		if (!Cls) { OutEarly = MCPError(TEXT("MetaSoundSource class not found. Enable the MetaSound plugin.")); return nullptr; }

		// The factory is not optional. Without it the asset is a shell whose
		// document holds no interfaces and no graph pages, which is exactly the
		// state that makes every read report there is nothing to read.
		UFactory* Factory = MSAuthorSourceFactory();
		if (!Factory)
		{
			OutEarly = MCPError(
				TEXT("UMetaSoundSourceFactory was not found, so a MetaSoundSource cannot be initialized. ")
				TEXT("It lives in the MetasoundEditor module: this needs an editor build with the MetaSound plugin enabled."));
			return nullptr;
		}

		auto Created = MCPCreateAssetIdempotent<UMetaSoundSource>(Name, PackagePath, OnConflict, TEXT("MetaSoundSource"), Cls, Factory);
		if (Created.EarlyReturn) { OutEarly = Created.EarlyReturn; return nullptr; }
		OutAssetPath = Created.Asset->GetPathName();

		FMSSession* Session = MSAuthorOpenOnAsset(Created.Asset, OutAssetPath, OutEarly);
		if (!Session)
		{
			if (!OutEarly.IsValid()) { OutEarly = MSAuthorNoSessionError(OutAssetPath); }
			return nullptr;
		}

		UMetaSoundSourceBuilder* B = Session->Builder.Get();
		EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;

		// Format and one-shot are asked for by the caller, and the factory always
		// initializes a mono one-shot source, so both are applied rather than
		// assumed. The vertex handles are re-read afterwards because changing
		// either replaces the interface nodes they point at.
		const EMetaSoundOutputAudioFormat Fmt =
			(Format == TEXT("stereo")) ? EMetaSoundOutputAudioFormat::Stereo : EMetaSoundOutputAudioFormat::Mono;
		B->SetFormat(Fmt, R);

		const FName OneShotInterface = Metasound::Frontend::SourceOneShotInterface::GetVersion().Name;
		if (bOneShot && !B->InterfaceIsDeclared(OneShotInterface))
		{
			B->AddInterface(OneShotInterface, R);
		}
		else if (!bOneShot && B->InterfaceIsDeclared(OneShotInterface))
		{
			B->RemoveInterface(OneShotInterface, R);
		}

		MSAuthorFillInterfaceHandles(*B, *Session);
		return Session;
	}

	/** Split "prefixOrNodeId:vertex" on the first ':'. */
	bool SplitEndpoint(const FString& Endpoint, FString& OutHead, FString& OutTail)
	{
		int32 Idx;
		if (!Endpoint.FindChar(TEXT(':'), Idx)) { OutHead = Endpoint; OutTail.Empty(); return false; }
		OutHead = Endpoint.Left(Idx);
		OutTail = Endpoint.Mid(Idx + 1);
		return true;
	}
}

TSharedPtr<FJsonValue> FAudioHandlers::CreateMetaSoundSource(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	TSharedPtr<FJsonValue> Early;
	FMSSession* S = OpenBuilderSession(Params, AssetPath, Early);
	if (!S) return Early;

	// The builder writes into the asset's own document, so the asset is already
	// a loadable, interface-valid, silent MetaSound. Only the save is left.
	MSAuthorSave(AssetPath);

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetBoolField(TEXT("oneShot"), S->bOneShot);
	Res->SetNumberField(TEXT("audioOutputs"), S->AudioOuts.Num());
	Res->SetStringField(TEXT("note"), TEXT("Builder attached to the asset's own document. Author with metasound_* actions (or use metasound_author to stamp a whole graph); metasound_build saves. audio(metasound_read_document) reads back what has been authored so far."));
	MCPSetDeleteAssetRollback(Res, AssetPath);
	return MCPResult(Res);
}

// One-shot declarative authoring: stamp an entire MetaSound graph from a single
// JSON spec, so an agent describes the whole system in one call instead of
// dozens of add_node/connect round-trips.
//
//   name, packagePath?, format?, oneShot?, onConflict?
//   inputs:      [ { name, dataType, default? } ]
//   outputs:     [ { name, dataType } ]
//   nodes:       [ { id, class, namespace?, variant?, majorVersion?, inputs?: {vertex: value} } ]
//   connections: [ { from, to } ]   endpoints are "nodeId:vertex", or the special
//                                    heads  input:<name>, output:<name>, audioOut:<channel>
//
// Every element reports its own success so a partial spec surfaces exactly what
// failed rather than an opaque error. The document is built and saved at the end.
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundAuthor(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	TSharedPtr<FJsonValue> Early;
	FMSSession* S = OpenBuilderSession(Params, AssetPath, Early);
	if (!S) return Early;

	UMetaSoundSourceBuilder* B = S->Builder.Get();
	EMetaSoundBuilderResult R;
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

	// 1. Graph inputs.
	const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
	if (Params->TryGetArrayField(TEXT("inputs"), Inputs) && Inputs)
	{
		for (const TSharedPtr<FJsonValue>& E : *Inputs)
		{
			const TSharedPtr<FJsonObject> O = E->AsObject();
			if (!O.IsValid()) continue;
			const FString IName = O->GetStringField(TEXT("name"));
			const FString DType = O->GetStringField(TEXT("dataType"));
			FMetasoundFrontendLiteral Def = MakeLiteral(O->TryGetField(TEXT("default")), DType);
			B->AddGraphInputNode(FName(*IName), FName(*DType), Def, R, false);
			if (!Ok(R)) Errors++;
			Note(TEXT("input"), IName, Ok(R), Ok(R) ? TEXT("") : TEXT("add failed"));
		}
	}

	// 2. Graph outputs.
	const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
	if (Params->TryGetArrayField(TEXT("outputs"), Outputs) && Outputs)
	{
		for (const TSharedPtr<FJsonValue>& E : *Outputs)
		{
			const TSharedPtr<FJsonObject> O = E->AsObject();
			if (!O.IsValid()) continue;
			const FString OName = O->GetStringField(TEXT("name"));
			const FString DType = O->GetStringField(TEXT("dataType"));
			FMetasoundFrontendLiteral Empty;
			B->AddGraphOutputNode(FName(*OName), FName(*DType), Empty, R, false);
			if (!Ok(R)) Errors++;
			Note(TEXT("output"), OName, Ok(R), Ok(R) ? TEXT("") : TEXT("add failed"));
		}
	}

	// 3. Nodes (build a localId -> handle map), plus per-node input defaults.
	TMap<FString, FMetaSoundNodeHandle> NodeMap;
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (Params->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
	{
		for (const TSharedPtr<FJsonValue>& E : *Nodes)
		{
			const TSharedPtr<FJsonObject> O = E->AsObject();
			if (!O.IsValid()) continue;
			const FString Id = O->GetStringField(TEXT("id"));
			const FString ClassName = O->GetStringField(TEXT("class"));
			const FString Ns = O->HasField(TEXT("namespace")) ? O->GetStringField(TEXT("namespace")) : TEXT("UE");
			const FString Variant = O->HasField(TEXT("variant")) ? O->GetStringField(TEXT("variant")) : FString();
			double MajD; const int32 Major = O->TryGetNumberField(TEXT("majorVersion"), MajD) ? (int32)MajD : 1;

			FMetasoundFrontendClassName FC = Variant.IsEmpty()
				? FMetasoundFrontendClassName(FName(*Ns), FName(*ClassName))
				: FMetasoundFrontendClassName(FName(*Ns), FName(*ClassName), FName(*Variant));
			FMetaSoundNodeHandle Node = B->AddNodeByClassName(FC, R, Major);
			if (!Ok(R) || !Node.IsSet())
			{
				Errors++;
				Note(TEXT("node"), Id, false, FString::Printf(TEXT("add failed for %s.%s"), *Ns, *ClassName));
				continue;
			}
			NodeMap.Add(Id, Node);
			Note(TEXT("node"), Id, true, TEXT(""));

			// Per-node input defaults.
			const TSharedPtr<FJsonObject>* NInputs = nullptr;
			if (O->TryGetObjectField(TEXT("inputs"), NInputs) && NInputs)
			{
				for (const auto& Pair : (*NInputs)->Values)
				{
					FMetaSoundBuilderNodeInputHandle In = B->FindNodeInputByName(Node, FName(*Pair.Key), R);
					if (!Ok(R)) { Errors++; Note(TEXT("default"), Id + TEXT(":") + Pair.Key, false, TEXT("no such input")); continue; }
					FMetasoundFrontendLiteral Lit = MakeLiteral(Pair.Value, FString());
					B->SetNodeInputDefault(In, Lit, R);
					Note(TEXT("default"), Id + TEXT(":") + Pair.Key, Ok(R), Ok(R) ? TEXT("") : TEXT("set failed"));
				}
			}
		}
	}

	// Endpoint resolvers. Source ("from") -> output handle; dest ("to") -> input handle.
	auto ResolveOut = [&](const FString& Ep, FMetaSoundBuilderNodeOutputHandle& Out) -> bool
	{
		FString Head, Tail; SplitEndpoint(Ep, Head, Tail);
		if (Head == TEXT("input"))
		{
			FName DT; FMetaSoundNodeHandle N = B->FindGraphInputNode(FName(*Tail), DT, Out, R);
			return Ok(R);
		}
		FMetaSoundNodeHandle* N = NodeMap.Find(Head);
		if (!N) return false;
		Out = B->FindNodeOutputByName(*N, FName(*Tail), R);
		return Ok(R);
	};
	auto ResolveIn = [&](const FString& Ep, FMetaSoundBuilderNodeInputHandle& In) -> bool
	{
		FString Head, Tail; SplitEndpoint(Ep, Head, Tail);
		if (Head == TEXT("output"))
		{
			FName DT; FMetaSoundNodeHandle N = B->FindGraphOutputNode(FName(*Tail), DT, In, R);
			return Ok(R);
		}
		if (Head == TEXT("audioOut"))
		{
			const int32 Ch = FCString::Atoi(*Tail);
			if (!S->AudioOuts.IsValidIndex(Ch)) return false;
			In = S->AudioOuts[Ch];
			return true;
		}
		FMetaSoundNodeHandle* N = NodeMap.Find(Head);
		if (!N) return false;
		In = B->FindNodeInputByName(*N, FName(*Tail), R);
		return Ok(R);
	};

	// 4. Connections.
	const TArray<TSharedPtr<FJsonValue>>* Conns = nullptr;
	if (Params->TryGetArrayField(TEXT("connections"), Conns) && Conns)
	{
		for (const TSharedPtr<FJsonValue>& E : *Conns)
		{
			const TSharedPtr<FJsonObject> O = E->AsObject();
			if (!O.IsValid()) continue;
			const FString From = O->GetStringField(TEXT("from"));
			const FString To = O->GetStringField(TEXT("to"));
			const FString Ref = From + TEXT(" -> ") + To;

			FMetaSoundBuilderNodeOutputHandle Out;
			FMetaSoundBuilderNodeInputHandle In;
			if (!ResolveOut(From, Out)) { Errors++; Note(TEXT("connection"), Ref, false, TEXT("bad source endpoint")); continue; }
			if (!ResolveIn(To, In))     { Errors++; Note(TEXT("connection"), Ref, false, TEXT("bad dest endpoint")); continue; }
			B->ConnectNodes(Out, In, R);
			if (!Ok(R)) Errors++;
			Note(TEXT("connection"), Ref, Ok(R), Ok(R) ? TEXT("") : TEXT("connect failed (type mismatch?)"));
		}
	}

	// 5. Save. Every step above wrote into the asset's own document, so there is
	//    no separate build to run.
	MSAuthorSave(AssetPath);

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetNumberField(TEXT("nodes"), NodeMap.Num());
	Res->SetNumberField(TEXT("errors"), Errors);
	Res->SetArrayField(TEXT("elements"), Diag);
	Res->SetBoolField(TEXT("built"), true);
	MCPSetDeleteAssetRollback(Res, AssetPath);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundAddNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("nodeClassName"), ClassName)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MSAuthorNoSessionError(AssetPath);

	const FString Namespace = OptionalString(Params, TEXT("nodeNamespace"), TEXT("UE"));
	const FString Variant = OptionalString(Params, TEXT("nodeVariant"));
	const int32 Major = (int32)OptionalNumber(Params, TEXT("majorVersion"), 1);

	FMetasoundFrontendClassName FrontendClass = Variant.IsEmpty()
		? FMetasoundFrontendClassName(FName(*Namespace), FName(*ClassName))
		: FMetasoundFrontendClassName(FName(*Namespace), FName(*ClassName), FName(*Variant));

	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	FMetaSoundNodeHandle Node = S->Builder->AddNodeByClassName(FrontendClass, Result, Major);
	if (!Ok(Result) || !Node.IsSet())
	{
		return MCPError(FString::Printf(TEXT("Failed to add node '%s.%s' (v%d). Check the class name/namespace/variant."), *Namespace, *ClassName, Major));
	}

	// Report vertex counts so the agent can sanity-check the node's shape. Connect
	// by vertex name (from the node's documentation / list_node_classes notes).
	EMetaSoundBuilderResult VResult = EMetaSoundBuilderResult::Failed;
	const int32 NumInputs = S->Builder->FindNodeInputs(Node, VResult).Num();
	const int32 NumOutputs = S->Builder->FindNodeOutputs(Node, VResult).Num();

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("nodeId"), Node.NodeID.ToString());
	Res->SetNumberField(TEXT("inputCount"), NumInputs);
	Res->SetNumberField(TEXT("outputCount"), NumOutputs);

	// The node was created by this call and nothing is wired to it yet, so
	// removing it is an exact inverse. metasound_remove_node's default
	// removeUnusedDependencies also drops the class dependency this add
	// introduced, which is the other half of what was written.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("nodeId"), Node.NodeID.ToString());
	MCPSetRollback(Res, TEXT("metasound_remove_node"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundAddGraphInput(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, Name, DataType;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	if (auto Err = RequireString(Params, TEXT("dataType"), DataType)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MSAuthorNoSessionError(AssetPath);

	FMetasoundFrontendLiteral Default = MakeLiteral(Params->TryGetField(TEXT("defaultValue")), DataType);
	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	S->Builder->AddGraphInputNode(FName(*Name), FName(*DataType), Default, Result, /*bIsConstructorInput*/ false);
	if (!Ok(Result)) return MCPError(FString::Printf(TEXT("Failed to add graph input '%s' (%s)."), *Name, *DataType));

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("input"), Name);
	Res->SetStringField(TEXT("dataType"), DataType);

	// metasound_remove_member with memberKind 'input' is the declared inverse of
	// adding a graph input. Nothing is connected to a member this call just
	// created, so the removal takes no edges with it.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("memberKind"), TEXT("input"));
	Payload->SetStringField(TEXT("name"), Name);
	MCPSetRollback(Res, TEXT("metasound_remove_member"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundAddGraphOutput(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, Name, DataType;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	if (auto Err = RequireString(Params, TEXT("dataType"), DataType)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MSAuthorNoSessionError(AssetPath);

	FMetasoundFrontendLiteral Empty;
	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	S->Builder->AddGraphOutputNode(FName(*Name), FName(*DataType), Empty, Result, /*bIsConstructorOutput*/ false);
	if (!Ok(Result)) return MCPError(FString::Printf(TEXT("Failed to add graph output '%s' (%s)."), *Name, *DataType));

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("output"), Name);
	Res->SetStringField(TEXT("dataType"), DataType);

	// Same shape as the graph-input add: memberKind 'output' is the declared
	// inverse, and a member this call just created drives no edges yet.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("memberKind"), TEXT("output"));
	Payload->SetStringField(TEXT("name"), Name);
	MCPSetRollback(Res, TEXT("metasound_remove_member"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundConnect(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, FromNodeId, FromOutput, ToNodeId, ToInput;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("fromNodeId"), FromNodeId)) return Err;
	if (auto Err = RequireString(Params, TEXT("fromOutput"), FromOutput)) return Err;
	if (auto Err = RequireString(Params, TEXT("toNodeId"), ToNodeId)) return Err;
	if (auto Err = RequireString(Params, TEXT("toInput"), ToInput)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MSAuthorNoSessionError(AssetPath);

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	const FMetaSoundNodeHandle FromNode = NodeFromId(FromNodeId);
	const FMetaSoundNodeHandle ToNode = NodeFromId(ToNodeId);

	FMetaSoundBuilderNodeOutputHandle Out = S->Builder->FindNodeOutputByName(FromNode, FName(*FromOutput), R);
	if (!Ok(R)) return MCPError(FString::Printf(TEXT("Output vertex '%s' not found on source node."), *FromOutput));
	FMetaSoundBuilderNodeInputHandle In = S->Builder->FindNodeInputByName(ToNode, FName(*ToInput), R);
	if (!Ok(R)) return MCPError(FString::Printf(TEXT("Input vertex '%s' not found on destination node."), *ToInput));

	S->Builder->ConnectNodes(Out, In, R);
	if (!Ok(R)) return MCPError(TEXT("Connection failed (type mismatch or vertex already connected)."));

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);

	// All four of fromNodeId/fromOutput/toNodeId/toInput is metasound_disconnect's
	// single-edge form, which is the only one of its four forms with an exact
	// inverse: it drops this edge and no other.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("fromNodeId"), FromNodeId);
	Payload->SetStringField(TEXT("fromOutput"), FromOutput);
	Payload->SetStringField(TEXT("toNodeId"), ToNodeId);
	Payload->SetStringField(TEXT("toInput"), ToInput);
	MCPSetRollback(Res, TEXT("metasound_disconnect"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundConnectGraphInput(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, GraphInput, ToNodeId, ToInput;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("graphInput"), GraphInput)) return Err;
	if (auto Err = RequireString(Params, TEXT("toNodeId"), ToNodeId)) return Err;
	if (auto Err = RequireString(Params, TEXT("toInput"), ToInput)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MSAuthorNoSessionError(AssetPath);

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	S->Builder->ConnectGraphInputToNode(FName(*GraphInput), NodeFromId(ToNodeId), FName(*ToInput), R);
	if (!Ok(R)) return MCPError(TEXT("Failed to connect graph input to node."));

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);

	// toNodeId + toInput alone is metasound_disconnect's 'input' form: it clears
	// whatever drives that input vertex. A node input vertex holds at most one
	// incoming edge (ConnectNodes refuses a second), so the one edge it clears
	// is the one this call just made.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("toNodeId"), ToNodeId);
	Payload->SetStringField(TEXT("toInput"), ToInput);
	MCPSetRollback(Res, TEXT("metasound_disconnect"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundConnectGraphOutput(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, FromNodeId, FromOutput, GraphOutput;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("fromNodeId"), FromNodeId)) return Err;
	if (auto Err = RequireString(Params, TEXT("fromOutput"), FromOutput)) return Err;
	if (auto Err = RequireString(Params, TEXT("graphOutput"), GraphOutput)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MSAuthorNoSessionError(AssetPath);

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	S->Builder->ConnectNodeToGraphOutput(NodeFromId(FromNodeId), FName(*FromOutput), FName(*GraphOutput), R);
	if (!Ok(R)) return MCPError(TEXT("Failed to connect node output to graph output."));

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);

	// fromNodeId + fromOutput + graphOutput is metasound_disconnect's single-edge
	// form with the destination named as a graph output, so it cuts this edge
	// and leaves every other edge on both vertices alone.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("fromNodeId"), FromNodeId);
	Payload->SetStringField(TEXT("fromOutput"), FromOutput);
	Payload->SetStringField(TEXT("graphOutput"), GraphOutput);
	MCPSetRollback(Res, TEXT("metasound_disconnect"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundConnectAudioOut(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, FromNodeId, FromOutput;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("fromNodeId"), FromNodeId)) return Err;
	if (auto Err = RequireString(Params, TEXT("fromOutput"), FromOutput)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MSAuthorNoSessionError(AssetPath);

	const int32 Channel = (int32)OptionalNumber(Params, TEXT("channel"), 0);
	if (!S->AudioOuts.IsValidIndex(Channel))
	{
		return MCPError(FString::Printf(TEXT("Audio output channel %d out of range (source has %d)."), Channel, S->AudioOuts.Num()));
	}

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	FMetaSoundBuilderNodeOutputHandle Out = S->Builder->FindNodeOutputByName(NodeFromId(FromNodeId), FName(*FromOutput), R);
	if (!Ok(R)) return MCPError(FString::Printf(TEXT("Output vertex '%s' not found on node."), *FromOutput));

	S->Builder->ConnectNodes(Out, S->AudioOuts[Channel], R);
	if (!Ok(R)) return MCPError(TEXT("Failed to connect to audio output (type must be Audio)."));

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetNumberField(TEXT("channel"), Channel);

	// An audio output is a graph output, so metasound_disconnect addresses it by
	// vertex name rather than by channel index. The name comes from the same
	// engine channel-order table that produced the handle, at the same index, so
	// it names the vertex this call actually wrote to.
	if (S->AudioOutNames.IsValidIndex(Channel))
	{
		const FString GraphOutput = S->AudioOutNames[Channel].ToString();
		Res->SetStringField(TEXT("graphOutput"), GraphOutput);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("fromNodeId"), FromNodeId);
		Payload->SetStringField(TEXT("fromOutput"), FromOutput);
		Payload->SetStringField(TEXT("graphOutput"), GraphOutput);
		MCPSetRollback(Res, TEXT("metasound_disconnect"), Payload);
		Res->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Res->SetBoolField(TEXT("rollbackPossible"), false);
		Res->SetStringField(TEXT("rollbackNote"),
			TEXT("The vertex name of audio channel ") + FString::FromInt(Channel) +
			TEXT(" could not be read back off the builder's output-format table, and audio(metasound_disconnect) ")
			TEXT("addresses an audio output by graph-output name rather than by channel index, so no rollback is offered. ")
			TEXT("audio(metasound_list_connections) reports the graph output this edge landed on."));
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundSetInputDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MSAuthorNoSessionError(AssetPath);

	const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));
	const FString TypeHint = OptionalString(Params, TEXT("dataType"));
	FMetasoundFrontendLiteral Lit = MakeLiteral(Value, TypeHint);
	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;

	// The previous default, read BEFORE the write, is the rollback payload. Only
	// the scalar literal kinds can be expressed as this action's own (value,
	// dataType) pair, so anything else is reported as having no rollback rather
	// than given one that would write a different value.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	bool bCapturedPrevious = false;

	FString GraphInput;
	if (Params->TryGetStringField(TEXT("graphInput"), GraphInput) && !GraphInput.IsEmpty())
	{
		EMetaSoundBuilderResult PrevResult = EMetaSoundBuilderResult::Failed;
		const FMetasoundFrontendLiteral Previous = S->Builder->GetGraphInputDefault(FName(*GraphInput), PrevResult);
		TSharedPtr<FJsonValue> PrevValue;
		FString PrevHint;
		if (Ok(PrevResult) && MSAuthorLiteralToJson(Previous, PrevValue, PrevHint))
		{
			Payload->SetStringField(TEXT("assetPath"), AssetPath);
			Payload->SetStringField(TEXT("graphInput"), GraphInput);
			Payload->SetField(TEXT("value"), PrevValue);
			Payload->SetStringField(TEXT("dataType"), PrevHint);
			bCapturedPrevious = true;
		}

		S->Builder->SetGraphInputDefault(FName(*GraphInput), Lit, R);
		if (!Ok(R)) return MCPError(FString::Printf(TEXT("Failed to set default on graph input '%s'."), *GraphInput));
	}
	else
	{
		FString NodeId, InputName;
		if (auto Err = RequireString(Params, TEXT("nodeId"), NodeId)) return Err;
		if (auto Err = RequireString(Params, TEXT("inputName"), InputName)) return Err;
		FMetaSoundBuilderNodeInputHandle In = S->Builder->FindNodeInputByName(NodeFromId(NodeId), FName(*InputName), R);
		if (!Ok(R)) return MCPError(FString::Printf(TEXT("Input vertex '%s' not found on node."), *InputName));

		EMetaSoundBuilderResult PrevResult = EMetaSoundBuilderResult::Failed;
		const FMetasoundFrontendLiteral Previous = S->Builder->GetNodeInputDefault(In, PrevResult);
		TSharedPtr<FJsonValue> PrevValue;
		FString PrevHint;
		if (Ok(PrevResult) && MSAuthorLiteralToJson(Previous, PrevValue, PrevHint))
		{
			Payload->SetStringField(TEXT("assetPath"), AssetPath);
			Payload->SetStringField(TEXT("nodeId"), NodeId);
			Payload->SetStringField(TEXT("inputName"), InputName);
			Payload->SetField(TEXT("value"), PrevValue);
			Payload->SetStringField(TEXT("dataType"), PrevHint);
			bCapturedPrevious = true;
		}

		S->Builder->SetNodeInputDefault(In, Lit, R);
		if (!Ok(R)) return MCPError(TEXT("Failed to set node input default (type mismatch?)."));
	}

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	if (bCapturedPrevious)
	{
		MCPSetRollback(Res, TEXT("metasound_set_input_default"), Payload);
		Res->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Res->SetBoolField(TEXT("rollbackPossible"), false);
		Res->SetStringField(TEXT("rollbackNote"),
			TEXT("The previous default could not be expressed as this action's (value, dataType) pair: it was unset, or an object ")
			TEXT("or array literal, and metasound_set_input_default builds bool, int32, float and string literals only. Writing any of ")
			TEXT("those back would set a different value than the one that was there, so no rollback is offered. ")
			TEXT("audio(metasound_read_document) reports the defaults that are on the graph."));
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundBuild(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	// The builder edits the asset's own document, so there is nothing to flush:
	// build means persist. Resolving the session first is still the right shape,
	// because it is what reports a path that names no MetaSoundSource.
	FMSSession* S = FindSession(AssetPath);
	if (!S) return MSAuthorNoSessionError(AssetPath);

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(MCPLoadAssetObject(AssetPath));
	if (!Source) return MCPAssetNotFoundError(AssetPath, TEXT("MetaSoundSource"));

	MSAuthorSave(Source->GetPathName());

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), Source->GetPathName());
	Res->SetStringField(TEXT("note"),
		TEXT("Document saved to the asset. Authoring writes into the asset's own document as it goes, so this persists it to disk rather than flushing a separate builder."));
	// A save has no inverse. The bytes that were on disk before it are gone, and
	// the document it wrote is the same one every earlier authoring call had
	// already edited in memory, so there is nothing this action alone put there
	// to take back out. The authoring calls carry their own rollbacks.
	Res->SetBoolField(TEXT("rollbackPossible"), false);
	Res->SetStringField(TEXT("rollbackNote"),
		TEXT("metasound_build persists the asset's document to disk. A write to disk cannot be withdrawn, and the graph edits it ")
		TEXT("saved were made by the metasound_* authoring calls, each of which emits its own inverse. Roll those back and save again."));
	return MCPResult(Res);
}

// ── metasound_get_graph ───────────────────────────────────────────────────
//
// SUPERSEDED for graph contents, and kept because removing a shipped action is
// a surface break. It predates the read half of this category and reports only
// the state of the in-editor builder SESSION: whether one is open on this
// asset, how many audio outputs create_metasound wired into it, and whether it
// was created as a one-shot.
//
// The graph itself - nodes, pins, connections, variables, defaults and
// problems - is read by the seven actions in AudioHandlers_MetaSoundRead.cpp:
// metasound_read_document, metasound_list_connections, metasound_list_variables,
// metasound_search_nodes, metasound_inspect_node, metasound_list_node_pins and
// metasound_validate. Those also say whether they read the live builder or the
// saved asset, so the one fact this action still puts plainly is whether THIS
// editor run holds an authoring session on the asset.
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundGetGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	// A path naming nothing used to come back success: true with the
	// assetExists field simply absent, so "here is the state of your MetaSound"
	// and "there is no such asset" were the same answer. Both failure shapes
	// are errors now, and both name the path.
	UObject* Asset = MCPLoadAssetObject(AssetPath);
	if (!Asset)
	{
		return MCPAssetNotFoundError(AssetPath, TEXT("MetaSound"));
	}

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(Asset);
	if (!Source)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a %s, not a MetaSoundSource, so it can have no builder session. ")
			TEXT("audio(metasound_read_document) reads any MetaSound document, a MetaSoundPatch included."),
			*AssetPath, *Asset->GetClass()->GetName()));
	}

	// This is a READ, so it asks whether a builder is attached and never attaches
	// one. FindSession would attach on demand, which is right for the write
	// actions and wrong here.
	const bool bHasBuilder = MSAuthorHasBuilder(Source);
	const FMSSession* S = GMetaSoundSessions.Find(Source->GetPathName());
	if (S && !S->Builder.IsValid()) { S = nullptr; }

	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("assetPath"), Source->GetPathName());
	Res->SetStringField(TEXT("assetClass"), Source->GetClass()->GetName());
	Res->SetBoolField(TEXT("assetExists"), true);
	Res->SetBoolField(TEXT("hasActiveBuilder"), bHasBuilder);
	if (S)
	{
		Res->SetNumberField(TEXT("audioOutputs"), S->AudioOuts.Num());
		Res->SetBoolField(TEXT("oneShot"), S->bOneShot);
	}

	Res->SetStringField(TEXT("supersededBy"), TEXT("metasound_read_document"));
	Res->SetStringField(TEXT("note"), bHasBuilder
		? TEXT("Builder state only. A builder is attached to this asset's own document, so the metasound_* ")
		  TEXT("write actions are editing exactly what audio(metasound_read_document) reads, and ")
		  TEXT("audio(metasound_build) saves it to disk. For nodes, pins, connections, variables, defaults ")
		  TEXT("and problems call metasound_read_document, metasound_list_connections, ")
		  TEXT("metasound_inspect_node, metasound_list_node_pins, metasound_search_nodes, ")
		  TEXT("metasound_list_variables or metasound_validate.")
		: TEXT("Builder state only. No builder is attached to this asset yet, so nothing is unsaved; the ")
		  TEXT("metasound_* write actions attach one on demand and do not need a create call first. For ")
		  TEXT("nodes, pins, connections, variables, defaults and problems call metasound_read_document, ")
		  TEXT("metasound_list_connections, metasound_inspect_node, metasound_list_node_pins, ")
		  TEXT("metasound_search_nodes, metasound_list_variables or metasound_validate."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundListNodeClasses(const TSharedPtr<FJsonObject>& Params)
{
	// A curated set of commonly used standard MetaSound node classes (UE namespace),
	// with the vertex names an agent needs to wire them. Reference for add_node.
	struct FNodeRef { const TCHAR* Name; const TCHAR* Variant; const TCHAR* Note; };
	static const FNodeRef Common[] = {
		{ TEXT("Sine"),           TEXT("Audio"), TEXT("Sine oscillator. In: Frequency. Out: Audio") },
		{ TEXT("Saw"),            TEXT("Audio"), TEXT("Saw oscillator. In: Frequency. Out: Audio") },
		{ TEXT("Square"),         TEXT("Audio"), TEXT("Square oscillator. In: Frequency. Out: Audio") },
		{ TEXT("Triangle"),       TEXT("Audio"), TEXT("Triangle oscillator. In: Frequency. Out: Audio") },
		{ TEXT("Noise"),          TEXT("Audio"), TEXT("Noise generator. Out: Audio") },
		{ TEXT("Mono Mixer"),     TEXT("Audio"), TEXT("Sum mono audio inputs. Out: Audio") },
		{ TEXT("Stereo Mixer"),   TEXT("Audio"), TEXT("Sum stereo audio inputs.") },
		{ TEXT("Gain"),           TEXT("Audio"), TEXT("Apply gain. In: In, Gain. Out: Out") },
		{ TEXT("AD Envelope"),    TEXT("Audio"), TEXT("Attack/decay envelope.") },
		{ TEXT("ADSR Envelope"),  TEXT("Audio"), TEXT("ADSR envelope.") },
		{ TEXT("Wave Player"),    TEXT(""),      TEXT("Play a USoundWave. In: Wave Asset, Play (trigger).") },
		{ TEXT("Ladder Filter"),  TEXT(""),      TEXT("Resonant lowpass filter.") },
		{ TEXT("Biquad Filter"),  TEXT(""),      TEXT("Biquad filter.") },
		{ TEXT("Delay"),          TEXT("Audio"), TEXT("Audio delay.") },
		{ TEXT("Trigger Repeat"), TEXT(""),      TEXT("Periodic trigger.") },
	};

	FString Filter = OptionalString(Params, TEXT("filter")).ToLower();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FNodeRef& N : Common)
	{
		const FString NameStr = N.Name;
		if (!Filter.IsEmpty() && !NameStr.ToLower().Contains(Filter)) continue;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), NameStr);
		O->SetStringField(TEXT("namespace"), TEXT("UE"));
		O->SetStringField(TEXT("variant"), N.Variant);
		O->SetStringField(TEXT("note"), N.Note);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}

	auto Res = MCPSuccess();
	Res->SetArrayField(TEXT("nodeClasses"), Arr);
	Res->SetNumberField(TEXT("count"), Arr.Num());
	Res->SetStringField(TEXT("note"), TEXT("Curated common set. Use add_node with name + namespace 'UE' + variant. Any registered class name also works."));
	return MCPResult(Res);
}
