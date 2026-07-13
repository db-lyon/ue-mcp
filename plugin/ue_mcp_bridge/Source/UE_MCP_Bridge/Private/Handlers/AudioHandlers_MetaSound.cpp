// MetaSound graph authoring for the audio category.
//
// Builds a MetaSoundSource end-to-end through the MetaSound Builder subsystem:
// create -> add nodes -> add graph inputs/outputs -> connect vertices / audio out
// -> set input defaults -> build to the asset.
//
// Session model: a live UMetaSoundSourceBuilder is created by create_metasound and
// held (with its OnPlay / OnFinished / audio-out vertex handles) keyed by the
// asset path for the life of the editor session. Authoring actions look that
// session up; metasound_build overwrites the persistent asset with the builder's
// document and saves. Authoring is therefore session-scoped: create, author, and
// build within one editor run.

#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"

#include "MetasoundBuilderSubsystem.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundSource.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendDocument.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"

namespace
{
	/** A live builder plus the interface vertex handles CreateSourceBuilder returns. */
	struct FMSSession
	{
		TWeakObjectPtr<UMetaSoundSourceBuilder> Builder;
		FMetaSoundBuilderNodeOutputHandle OnPlay;
		FMetaSoundBuilderNodeInputHandle OnFinished;
		TArray<FMetaSoundBuilderNodeInputHandle> AudioOuts;
		bool bOneShot = true;
	};

	/** Session builders keyed by MetaSound asset object path. Editor-session lived. */
	static TMap<FString, FMSSession> GMetaSoundSessions;

	UMetaSoundBuilderSubsystem* BuilderSubsystem()
	{
		return UMetaSoundBuilderSubsystem::Get();
	}

	FMSSession* FindSession(const FString& AssetPath)
	{
		FMSSession* S = GMetaSoundSessions.Find(AssetPath);
		if (S && S->Builder.IsValid())
		{
			return S;
		}
		return nullptr;
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
	 * Create (idempotently) the MetaSoundSource asset shell and open a source
	 * builder session for it. On success returns the stored session and sets
	 * OutAssetPath. On failure returns nullptr and sets OutEarly (error / existed).
	 * Shared by create_metasound and the one-shot metasound_author.
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

		auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("MetaSoundSource"), Cls, nullptr);
		if (Created.EarlyReturn) { OutEarly = Created.EarlyReturn; return nullptr; }
		OutAssetPath = Created.Asset->GetPathName();

		UMetaSoundBuilderSubsystem* Sub = BuilderSubsystem();
		if (!Sub) { OutEarly = MCPError(TEXT("MetaSound Builder subsystem unavailable.")); return nullptr; }

		FMSSession Session;
		Session.bOneShot = bOneShot;
		EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
		const EMetaSoundOutputAudioFormat Fmt =
			(Format == TEXT("stereo")) ? EMetaSoundOutputAudioFormat::Stereo : EMetaSoundOutputAudioFormat::Mono;

		UMetaSoundSourceBuilder* Builder = Sub->CreateSourceBuilder(
			FName(*OutAssetPath), Session.OnPlay, Session.OnFinished, Session.AudioOuts, R, Fmt, bOneShot);
		if (!Builder || !Ok(R)) { OutEarly = MCPError(TEXT("Failed to create MetaSound source builder.")); return nullptr; }
		Session.Builder = Builder;

		return &GMetaSoundSessions.Add(OutAssetPath, Session);
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

	// Write the (empty but interface-valid) document into the asset so it is a
	// loadable, silent MetaSound until authored further.
	if (UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath)))
	{
		TScriptInterface<IMetaSoundDocumentInterface> DocIface(Source);
		S->Builder->BuildAndOverwriteMetaSound(DocIface, /*bForceUniqueClassName*/ false);
		UEditorAssetLibrary::SaveAsset(AssetPath);
	}

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetBoolField(TEXT("oneShot"), S->bOneShot);
	Res->SetNumberField(TEXT("audioOutputs"), S->AudioOuts.Num());
	Res->SetStringField(TEXT("note"), TEXT("Builder session active. Author with metasound_* actions (or use metasound_author to stamp a whole graph), then metasound_build."));
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

	// 5. Build + save.
	if (UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath)))
	{
		TScriptInterface<IMetaSoundDocumentInterface> DocIface(Source);
		B->BuildAndOverwriteMetaSound(DocIface, false);
		UEditorAssetLibrary::SaveAsset(AssetPath);
	}

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
	if (!S) return MCPError(TEXT("No active MetaSound builder for this asset. Call create_metasound in this editor session first."));

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
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundAddGraphInput(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, Name, DataType;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	if (auto Err = RequireString(Params, TEXT("dataType"), DataType)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MCPError(TEXT("No active MetaSound builder for this asset. Call create_metasound first."));

	FMetasoundFrontendLiteral Default = MakeLiteral(Params->TryGetField(TEXT("defaultValue")), DataType);
	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	S->Builder->AddGraphInputNode(FName(*Name), FName(*DataType), Default, Result, /*bIsConstructorInput*/ false);
	if (!Ok(Result)) return MCPError(FString::Printf(TEXT("Failed to add graph input '%s' (%s)."), *Name, *DataType));

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("input"), Name);
	Res->SetStringField(TEXT("dataType"), DataType);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundAddGraphOutput(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, Name, DataType;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	if (auto Err = RequireString(Params, TEXT("dataType"), DataType)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MCPError(TEXT("No active MetaSound builder for this asset. Call create_metasound first."));

	FMetasoundFrontendLiteral Empty;
	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	S->Builder->AddGraphOutputNode(FName(*Name), FName(*DataType), Empty, Result, /*bIsConstructorOutput*/ false);
	if (!Ok(Result)) return MCPError(FString::Printf(TEXT("Failed to add graph output '%s' (%s)."), *Name, *DataType));

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("output"), Name);
	Res->SetStringField(TEXT("dataType"), DataType);
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
	if (!S) return MCPError(TEXT("No active MetaSound builder for this asset. Call create_metasound first."));

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
	if (!S) return MCPError(TEXT("No active MetaSound builder for this asset. Call create_metasound first."));

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	S->Builder->ConnectGraphInputToNode(FName(*GraphInput), NodeFromId(ToNodeId), FName(*ToInput), R);
	if (!Ok(R)) return MCPError(TEXT("Failed to connect graph input to node."));

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
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
	if (!S) return MCPError(TEXT("No active MetaSound builder for this asset. Call create_metasound first."));

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	S->Builder->ConnectNodeToGraphOutput(NodeFromId(FromNodeId), FName(*FromOutput), FName(*GraphOutput), R);
	if (!Ok(R)) return MCPError(TEXT("Failed to connect node output to graph output."));

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundConnectAudioOut(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, FromNodeId, FromOutput;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("fromNodeId"), FromNodeId)) return Err;
	if (auto Err = RequireString(Params, TEXT("fromOutput"), FromOutput)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MCPError(TEXT("No active MetaSound builder for this asset. Call create_metasound first."));

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
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundSetInputDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MCPError(TEXT("No active MetaSound builder for this asset. Call create_metasound first."));

	const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));
	const FString TypeHint = OptionalString(Params, TEXT("dataType"));
	FMetasoundFrontendLiteral Lit = MakeLiteral(Value, TypeHint);
	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;

	FString GraphInput;
	if (Params->TryGetStringField(TEXT("graphInput"), GraphInput) && !GraphInput.IsEmpty())
	{
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
		S->Builder->SetNodeInputDefault(In, Lit, R);
		if (!Ok(R)) return MCPError(TEXT("Failed to set node input default (type mismatch?)."));
	}

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundBuild(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FMSSession* S = FindSession(AssetPath);
	if (!S) return MCPError(TEXT("No active MetaSound builder for this asset. Call create_metasound first."));

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) return MCPError(FString::Printf(TEXT("MetaSoundSource not found at %s."), *AssetPath));

	TScriptInterface<IMetaSoundDocumentInterface> DocIface(Source);
	S->Builder->BuildAndOverwriteMetaSound(DocIface, /*bForceUniqueClassName*/ false);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("note"), TEXT("Builder document written to the asset and saved."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundGetGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	FMSSession* S = FindSession(AssetPath);
	Res->SetBoolField(TEXT("hasActiveBuilder"), S != nullptr);
	if (S)
	{
		Res->SetNumberField(TEXT("audioOutputs"), S->AudioOuts.Num());
		Res->SetBoolField(TEXT("oneShot"), S->bOneShot);
	}
	if (UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath)))
	{
		Res->SetBoolField(TEXT("assetExists"), true);
	}
	Res->SetStringField(TEXT("note"), TEXT("Node/edge enumeration follows in a later pass; author via the builder session."));
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
