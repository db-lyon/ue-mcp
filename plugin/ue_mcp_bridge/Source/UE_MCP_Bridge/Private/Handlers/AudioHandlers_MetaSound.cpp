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
}

TSharedPtr<FJsonValue> FAudioHandlers::CreateMetaSoundSource(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/MetaSounds"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const FString Format = OptionalString(Params, TEXT("format"), TEXT("mono")).ToLower();
	const bool bOneShot = OptionalBool(Params, TEXT("oneShot"), true);

	UClass* MetaSoundSourceClass = FindObject<UClass>(nullptr, TEXT("/Script/MetasoundEngine.MetaSoundSource"));
	if (!MetaSoundSourceClass)
	{
		return MCPError(TEXT("MetaSoundSource class not found. Enable the MetaSound plugin."));
	}

	// Persistent asset shell (idempotent). The builder overwrites it on build.
	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("MetaSoundSource"), MetaSoundSourceClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	const FString AssetPath = Created.Asset->GetPathName();

	UMetaSoundBuilderSubsystem* Sub = BuilderSubsystem();
	if (!Sub)
	{
		return MCPError(TEXT("MetaSound Builder subsystem unavailable."));
	}

	FMSSession Session;
	Session.bOneShot = bOneShot;
	EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
	const EMetaSoundOutputAudioFormat OutFormat =
		(Format == TEXT("stereo")) ? EMetaSoundOutputAudioFormat::Stereo : EMetaSoundOutputAudioFormat::Mono;

	UMetaSoundSourceBuilder* Builder = Sub->CreateSourceBuilder(
		FName(*AssetPath),
		Session.OnPlay,
		Session.OnFinished,
		Session.AudioOuts,
		Result,
		OutFormat,
		bOneShot);

	if (!Builder || !Ok(Result))
	{
		return MCPError(TEXT("Failed to create MetaSound source builder."));
	}
	Session.Builder = Builder;

	// Write the (currently empty but interface-valid) document into the asset so it
	// is a loadable, silent MetaSound until authored further.
	if (UMetaSoundSource* Source = Cast<UMetaSoundSource>(Created.Asset))
	{
		TScriptInterface<IMetaSoundDocumentInterface> DocIface(Source);
		Builder->BuildAndOverwriteMetaSound(DocIface, /*bForceUniqueClassName*/ false);
		UEditorAssetLibrary::SaveAsset(AssetPath);
	}

	GMetaSoundSessions.Add(AssetPath, Session);

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("name"), Name);
	Res->SetStringField(TEXT("format"), Format);
	Res->SetBoolField(TEXT("oneShot"), bOneShot);
	Res->SetNumberField(TEXT("audioOutputs"), Session.AudioOuts.Num());
	Res->SetStringField(TEXT("note"), TEXT("Builder session active. Author with metasound_* actions, then metasound_build to persist."));
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
