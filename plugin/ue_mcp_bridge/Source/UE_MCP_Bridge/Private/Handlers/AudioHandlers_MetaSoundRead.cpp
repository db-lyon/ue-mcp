// MetaSound graph introspection for the audio category.
//
// The authoring half (AudioHandlers_MetaSound.cpp) can build a graph but never
// reads one back, so an agent can write a MetaSound and has no way to verify or
// iterate on it. These handlers close that loop: read the document, list its
// connections and variables, search and inspect nodes, and diagnose the graph.
//
// Identity contract - the single thing that makes this half useful. Every node
// reported here carries "nodeId", the string form of FMetasoundFrontendNode's
// FGuid, which is exactly what metasound_add_node returns and exactly what
// metasound_connect / metasound_connect_graph_input / metasound_connect_graph_output
// / metasound_connect_audio_out / metasound_set_input_default parse back through
// their NodeFromId(). Vertices are reported by name, which is what those same
// actions take as fromOutput / toInput / inputName. A connection therefore comes
// back already shaped as the argument list of the write action that would
// recreate it.
//
// Document source. There is one document, and it is the asset's own
// RootMetasoundDocument: the authoring actions attach their builder to the asset
// through the document builder registry rather than to a detached transient
// object, so what a builder has written is already in the asset in memory and
// metasound_build only persists it. So this reads the asset and separately
// reports whether a builder is attached, via "hasActiveBuilder", which is the
// fact a caller actually needs (it says whether unsaved edits may be in flight).
//
// The lookup used to be UMetaSoundBuilderSubsystem::FindSourceBuilder(assetPath),
// and that could never have matched. That subsystem's map is keyed by a caller-
// chosen BuilderName and holds only builders passed to RegisterBuilder; its own
// header says "the builder manually registered ... with the provided custom
// name". Nothing here ever registered one, so the lookup missed every time and
// every read silently reported source "asset". IDocumentBuilderRegistry is the
// registry that is actually keyed by the MetaSound.

#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"

#include "MetasoundBuilderSubsystem.h"
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

namespace
{
	// ── Resolution ────────────────────────────────────────────────────────

	/** A resolved MetaSound document plus the page graph the caller asked for. */
	struct FMSReadTarget
	{
		const FMetasoundFrontendDocument* Doc = nullptr;
		const FMetasoundFrontendGraph* Graph = nullptr;
		/** A builder is attached to this asset's document, so edits may be unsaved. */
		bool bHasBuilder = false;
		FString AssetPath;
		FString Source;                             // "builder" | "asset"
		FString PageId;
		/** The document had no page under the default id, so the first page it
		 *  does hold was read instead. Reported, because a caller comparing two
		 *  reads has to know they may be looking at different pages. */
		bool bFellBackFromDefaultPage = false;
	};

	/** Every MetaSound asset in the project, so a bad path can name the good ones. */
	TArray<FString> MSReadKnownMetaSounds(int32 Limit)
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

	/** "No MetaSound there" told as a next call rather than as a dead end. */
	TSharedPtr<FJsonValue> MSReadNoAssetError(const FString& AssetPath)
	{
		const TArray<FString> Known = MSReadKnownMetaSounds(20);
		FString Msg = FString::Printf(
			TEXT("No MetaSound found at '%s'. "), *AssetPath);

		if (Known.Num() == 0)
		{
			Msg += TEXT("This project has no MetaSound assets at all: create one with ")
				   TEXT("audio(metasound_author) or audio(create_metasound), then read it back.");
		}
		else
		{
			Msg += FString::Printf(TEXT("MetaSounds in this project (%d): %s. "),
				Known.Num(), *FString::Join(Known, TEXT(", ")));
			Msg += TEXT("Pass one of those as assetPath, or call asset(list) / asset(search) for the full set.");
		}
		return MCPError(Msg);
	}

	/**
	 * Resolve assetPath to a readable document.
	 *
	 * The document is the asset's, always. The authoring builder is attached to
	 * that same document, so there is no second copy to prefer; what varies is
	 * only whether a builder is currently attached, which is reported rather than
	 * used to pick a source.
	 */
	bool MSReadResolve(const TSharedPtr<FJsonObject>& Params, FMSReadTarget& Out, TSharedPtr<FJsonValue>& OutError)
	{
		if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("metasoundPath"), Out.AssetPath))
		{
			OutError = Err;
			return false;
		}

		UObject* Obj = MCPLoadAssetObject(Out.AssetPath);
		if (!Obj)
		{
			OutError = MSReadNoAssetError(Out.AssetPath);
			return false;
		}

		const IMetaSoundDocumentInterface* DocIface = Cast<IMetaSoundDocumentInterface>(Obj);
		if (!DocIface)
		{
			OutError = MCPError(FString::Printf(
				TEXT("'%s' is a %s, not a MetaSound. audio(metasound_*) reads MetaSoundSource and ")
				TEXT("MetaSoundPatch assets only: call asset(read) for other asset types."),
				*Out.AssetPath, *Obj->GetClass()->GetName()));
			return false;
		}

		// Is a builder attached to this document? This is a read, so it asks and
		// does not attach: FindBuilder, never FindOrBeginBuilding.
		{
			TScriptInterface<IMetaSoundDocumentInterface> DocScriptIface(Obj);
			if (Metasound::Frontend::IDocumentBuilderRegistry* Registry = Metasound::Frontend::IDocumentBuilderRegistry::Get())
			{
				Out.bHasBuilder = Registry->FindBuilder(DocScriptIface) != nullptr;
			}
		}
		Out.Source = Out.bHasBuilder ? TEXT("builder") : TEXT("asset");

		Out.Doc = &DocIface->GetConstDocument();

		// Page selection. A document may hold several paged graphs; the default
		// page is what the builder authors into unless told otherwise.
		const FString WantPage = OptionalString(Params, TEXT("pageId"));
		if (!WantPage.IsEmpty())
		{
			for (const FMetasoundFrontendGraph& Page : Out.Doc->RootGraph.GetConstGraphPages())
			{
				if (Page.PageID.ToString() == WantPage)
				{
					Out.Graph = &Page;
					break;
				}
			}
			if (!Out.Graph)
			{
				TArray<FString> Pages;
				for (const FMetasoundFrontendGraph& Page : Out.Doc->RootGraph.GetConstGraphPages())
				{
					Pages.Add(Page.PageID.ToString());
				}
				OutError = MCPError(FString::Printf(
					TEXT("No graph page '%s' in '%s'. Pages present: %s. Omit pageId to read the default page."),
					*WantPage, *Out.AssetPath,
					Pages.Num() ? *FString::Join(Pages, TEXT(", ")) : TEXT("(none)")));
				return false;
			}
		}
		else
		{
			// NEVER GetConstDefaultGraph() here.
			//
			// FMetasoundFrontendGraphClass::GetConstDefaultGraph() ends in a
			// check() rather than returning null when the document holds no page
			// under Metasound::Frontend::DefaultPageID, and a MetaSoundSource
			// written by audio(metasound_author) is such a document. Calling it
			// took the whole editor down with a fatal assert, from the saved
			// asset as well as from a live builder session, and every one of the
			// seven read actions resolves through here.
			//
			// FindConstGraph returns a pointer and asks the same question
			// safely. A document whose default page is missing but which holds
			// pages under other ids is still readable, so fall back to the first
			// one and say which page was read rather than refusing; only a
			// document with no graph pages at all is an error, and it is
			// reported as one instead of as a crash.
			Out.Graph = Out.Doc->RootGraph.FindConstGraph(Metasound::Frontend::DefaultPageID);
			if (!Out.Graph)
			{
				const TArray<FMetasoundFrontendGraph>& Pages = Out.Doc->RootGraph.GetConstGraphPages();
				if (Pages.Num() == 0)
				{
					// This is a real diagnosis rather than a shrug. A MetaSound
					// initialized by its factory always holds a default graph
					// page plus the UE.Source and output-format interfaces. A
					// document with no pages AND no interfaces is an asset that
					// was constructed without that initialization, which is a
					// shell no read and no write can do anything with. Recreating
					// it is the only fix, so the error says so.
					OutError = MCPError(FString::Printf(
						TEXT("'%s' holds no graph pages at all, so there is nothing to read: its document "
							 "is empty (%d interfaces declared, %d dependencies). A MetaSound is only "
							 "initialized by its own asset factory, so an asset built any other way loads "
							 "as a valid MetaSoundSource with a completely blank document. Recreate it with "
							 "audio(metasound_author) or audio(create_metasound), which go through "
							 "the factory, and delete this one."),
						*Out.AssetPath, Out.Doc->Interfaces.Num(), Out.Doc->Dependencies.Num()));
					return false;
				}
				Out.Graph = &Pages[0];
				Out.bFellBackFromDefaultPage = true;
			}
		}

		Out.PageId = Out.Graph->PageID.ToString();
		return true;
	}

	/** Stamp the provenance fields every read result carries. */
	void MSReadStampSource(const TSharedPtr<FJsonObject>& Res, const FMSReadTarget& T)
	{
		Res->SetStringField(TEXT("path"), T.AssetPath);
		Res->SetStringField(TEXT("source"), T.Source);
		Res->SetStringField(TEXT("pageId"), T.PageId);
		Res->SetBoolField(TEXT("hasActiveBuilder"), T.bHasBuilder);
		Res->SetBoolField(TEXT("readDefaultPage"), !T.bFellBackFromDefaultPage);
		if (T.bFellBackFromDefaultPage)
		{
			// Silence here would let two reads of the same asset disagree with
			// no visible reason, which is worse than the page being unusual.
			Res->SetStringField(TEXT("pageNote"), FString::Printf(
				TEXT("This document holds no page under the default page id, so page '%s' was read "
					 "instead. Pass pageId to choose another one."),
				*T.PageId));
		}
		if (T.bHasBuilder)
		{
			Res->SetStringField(TEXT("sourceNote"),
				TEXT("Read from the asset's document with an authoring builder attached to it. Edits made through the metasound_* write actions are already in what you are reading; audio(metasound_build) saves them to disk."));
		}
		else
		{
			Res->SetStringField(TEXT("sourceNote"),
				TEXT("Read from the asset's document with no authoring builder attached, so nothing is unsaved. The metasound_* write actions attach one on demand, so they work on this asset without a create call."));
		}
	}

	// ── Document walking ──────────────────────────────────────────────────

	/** The class a node instantiates: dependencies first, then subgraphs, then the root graph. */
	const FMetasoundFrontendClass* MSReadFindClass(const FMetasoundFrontendDocument& Doc, const FGuid& ClassID)
	{
		for (const FMetasoundFrontendClass& Dep : Doc.Dependencies)
		{
			if (Dep.ID == ClassID) return &Dep;
		}
		for (const FMetasoundFrontendGraphClass& Sub : Doc.Subgraphs)
		{
			if (Sub.ID == ClassID) return &Sub;
		}
		if (Doc.RootGraph.ID == ClassID) return &Doc.RootGraph;
		return nullptr;
	}

	const FMetasoundFrontendNode* MSReadFindNode(const FMetasoundFrontendGraph& Graph, const FGuid& NodeID)
	{
		for (const FMetasoundFrontendNode& Node : Graph.Nodes)
		{
			if (Node.GetID() == NodeID) return &Node;
		}
		return nullptr;
	}

	const FMetasoundFrontendVertex* MSReadFindVertex(const TArray<FMetasoundFrontendVertex>& Vertices, const FGuid& VertexID)
	{
		for (const FMetasoundFrontendVertex& V : Vertices)
		{
			if (V.VertexID == VertexID) return &V;
		}
		return nullptr;
	}

	EMetasoundFrontendClassType MSReadClassType(const FMetasoundFrontendDocument& Doc, const FMetasoundFrontendNode& Node)
	{
		if (const FMetasoundFrontendClass* Cls = MSReadFindClass(Doc, Node.ClassID))
		{
			return Cls->Metadata.GetType();
		}
		return EMetasoundFrontendClassType::Invalid;
	}

	/** Variable plumbing nodes are wired by the variable actions, not by hand. */
	bool MSReadIsVariableNode(EMetasoundFrontendClassType Type)
	{
		return Type == EMetasoundFrontendClassType::Variable
			|| Type == EMetasoundFrontendClassType::VariableAccessor
			|| Type == EMetasoundFrontendClassType::VariableDeferredAccessor
			|| Type == EMetasoundFrontendClassType::VariableMutator;
	}

	/** A literal type's own name, for saying what a value is when it cannot be rendered. */
	FString MSReadLiteralTypeName(EMetasoundFrontendLiteralType Type)
	{
		switch (Type)
		{
		case EMetasoundFrontendLiteralType::None:         return TEXT("None");
		case EMetasoundFrontendLiteralType::Boolean:      return TEXT("Boolean");
		case EMetasoundFrontendLiteralType::Integer:      return TEXT("Integer");
		case EMetasoundFrontendLiteralType::Float:        return TEXT("Float");
		case EMetasoundFrontendLiteralType::String:       return TEXT("String");
		case EMetasoundFrontendLiteralType::UObject:      return TEXT("UObject");
		case EMetasoundFrontendLiteralType::NoneArray:    return TEXT("NoneArray");
		case EMetasoundFrontendLiteralType::BooleanArray: return TEXT("BooleanArray");
		case EMetasoundFrontendLiteralType::IntegerArray: return TEXT("IntegerArray");
		case EMetasoundFrontendLiteralType::FloatArray:   return TEXT("FloatArray");
		case EMetasoundFrontendLiteralType::StringArray:  return TEXT("StringArray");
		case EMetasoundFrontendLiteralType::UObjectArray: return TEXT("UObjectArray");
		default:                                          return TEXT("Invalid");
		}
	}

	/**
	 * A literal the typed accessors declined, named rather than guessed at.
	 *
	 * The frontend's ToString() is a debug rendering, not a value: handing it
	 * back as a bare JSON string would look like something metasound_author or
	 * metasound_set_input_default would take, and it is not. So the field says
	 * what the value is and marks the text as read-only.
	 */
	TSharedPtr<FJsonValue> MSReadUnrenderableLiteral(const FMetasoundFrontendLiteral& Lit)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("literalType"), MSReadLiteralTypeName(Lit.GetType()));
		O->SetStringField(TEXT("text"), Lit.ToString());
		O->SetStringField(TEXT("note"),
			TEXT("This literal has no JSON form the metasound write actions accept, so it is reported by type "
				 "rather than by value. 'text' is the frontend's own rendering and is for reading only."));
		return MakeShared<FJsonValueObject>(O);
	}

	/** A literal as JSON of its own kind, so a reader can compare it to what it set. */
	TSharedPtr<FJsonValue> MSReadLiteralToJson(const FMetasoundFrontendLiteral& Lit)
	{
		switch (Lit.GetType())
		{
		case EMetasoundFrontendLiteralType::Boolean:
		{
			bool B = false;
			if (Lit.TryGet(B)) return MakeShared<FJsonValueBoolean>(B);
			break;
		}
		case EMetasoundFrontendLiteralType::Integer:
		{
			int32 I = 0;
			if (Lit.TryGet(I)) return MakeShared<FJsonValueNumber>(I);
			break;
		}
		case EMetasoundFrontendLiteralType::Float:
		{
			float F = 0.f;
			if (Lit.TryGet(F)) return MakeShared<FJsonValueNumber>(F);
			break;
		}
		case EMetasoundFrontendLiteralType::String:
		{
			FString S;
			if (Lit.TryGet(S)) return MakeShared<FJsonValueString>(S);
			break;
		}
		case EMetasoundFrontendLiteralType::UObject:
		{
			UObject* Obj = nullptr;
			if (Lit.TryGet(Obj))
			{
				return MakeShared<FJsonValueString>(Obj ? Obj->GetPathName() : FString());
			}
			break;
		}
		case EMetasoundFrontendLiteralType::None:
			return MakeShared<FJsonValueNull>();

		// Arrays come back as JSON arrays of the same element form, which is the
		// shape the write actions take them in, rather than as one packed string.
		case EMetasoundFrontendLiteralType::NoneArray:
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (int32 I = 0; I < Lit.GetArrayNum(); ++I) Out.Add(MakeShared<FJsonValueNull>());
			return MakeShared<FJsonValueArray>(Out);
		}
		case EMetasoundFrontendLiteralType::BooleanArray:
		{
			TArray<bool> V;
			if (Lit.TryGet(V))
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				for (bool B : V) Out.Add(MakeShared<FJsonValueBoolean>(B));
				return MakeShared<FJsonValueArray>(Out);
			}
			break;
		}
		case EMetasoundFrontendLiteralType::IntegerArray:
		{
			TArray<int32> V;
			if (Lit.TryGet(V))
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				for (int32 I : V) Out.Add(MakeShared<FJsonValueNumber>(I));
				return MakeShared<FJsonValueArray>(Out);
			}
			break;
		}
		case EMetasoundFrontendLiteralType::FloatArray:
		{
			TArray<float> V;
			if (Lit.TryGet(V))
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				for (float F : V) Out.Add(MakeShared<FJsonValueNumber>(F));
				return MakeShared<FJsonValueArray>(Out);
			}
			break;
		}
		case EMetasoundFrontendLiteralType::StringArray:
		{
			TArray<FString> V;
			if (Lit.TryGet(V))
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				for (const FString& S : V) Out.Add(MakeShared<FJsonValueString>(S));
				return MakeShared<FJsonValueArray>(Out);
			}
			break;
		}
		case EMetasoundFrontendLiteralType::UObjectArray:
		{
			TArray<UObject*> V;
			if (Lit.TryGet(V))
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				for (UObject* Obj : V) Out.Add(MakeShared<FJsonValueString>(Obj ? Obj->GetPathName() : FString()));
				return MakeShared<FJsonValueArray>(Out);
			}
			break;
		}
		default:
			break;
		}
		// Invalid, and anything a typed accessor declined: named, not guessed at.
		return MSReadUnrenderableLiteral(Lit);
	}

	/**
	 * The literal a GRAPH INPUT carries as its default value.
	 *
	 * It is not on the input node. A graph input node's InputLiterals array is
	 * empty, which is why reading only that array reported no default for an
	 * input that metasound_author had just written one for. The value lives on
	 * the document's root graph CLASS interface, as one entry per page on the
	 * class input of the same name (FMetasoundFrontendClassInput::Defaults), and
	 * UMetaSoundSourceBuilder::AddGraphInputNode / SetGraphInputDefault write it
	 * there.
	 *
	 * Returns null when the input carries no default at all, so a caller can
	 * tell "no default set" from "the default is zero".
	 */
	const FMetasoundFrontendLiteral* MSReadGraphInputDefault(
		const FMetasoundFrontendDocument& Doc,
		const FName InputName,
		const FGuid& PageID)
	{
		for (const FMetasoundFrontendClassInput& In : Doc.RootGraph.GetDefaultInterface().Inputs)
		{
			if (In.Name != InputName) continue;
			if (const FMetasoundFrontendLiteral* Lit = In.FindConstDefault(PageID))
			{
				if (Lit->IsValid()) return Lit;
			}
			// The page being read carries no default of its own. A default held
			// on another page is still what this input was authored with, so
			// report that rather than nothing.
			for (const FMetasoundFrontendClassInputDefault& D : In.GetDefaults())
			{
				if (D.Literal.IsValid()) return &D.Literal;
			}
			return nullptr;
		}
		return nullptr;
	}

	/** The literal set on a node input in this graph, if one is set. */
	const FMetasoundFrontendLiteral* MSReadNodeInputLiteral(const FMetasoundFrontendNode& Node, const FGuid& VertexID)
	{
		for (const FMetasoundFrontendVertexLiteral& VL : Node.InputLiterals)
		{
			if (VL.VertexID == VertexID) return &VL.Value;
		}
		return nullptr;
	}

	/** Class identity in the exact shape metasound_add_node takes it back. */
	TSharedPtr<FJsonObject> MSReadClassJson(const FMetasoundFrontendClass* Cls)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		if (!Cls) return O;
		const FMetasoundFrontendClassName& CN = Cls->Metadata.GetClassName();
		O->SetStringField(TEXT("nodeClassName"), CN.Name.ToString());
		O->SetStringField(TEXT("nodeNamespace"), CN.Namespace.ToString());
		O->SetStringField(TEXT("nodeVariant"), CN.Variant.ToString());
		O->SetNumberField(TEXT("majorVersion"), Cls->Metadata.GetVersion().Major);
		O->SetNumberField(TEXT("minorVersion"), Cls->Metadata.GetVersion().Minor);
		O->SetStringField(TEXT("classType"), LexToString(Cls->Metadata.GetType()));
		return O;
	}

	/** Edge counts per node, computed once per read rather than per node. */
	struct FMSReadDegree
	{
		TMap<FGuid, int32> InDegree;
		TMap<FGuid, int32> OutDegree;
		TSet<FMetasoundFrontendVertexHandle> DrivenInputs;
		TSet<FMetasoundFrontendVertexHandle> DrivingOutputs;
	};

	void MSReadBuildDegrees(const FMetasoundFrontendGraph& Graph, FMSReadDegree& Out)
	{
		for (const FMetasoundFrontendNode& Node : Graph.Nodes)
		{
			Out.InDegree.Add(Node.GetID(), 0);
			Out.OutDegree.Add(Node.GetID(), 0);
		}
		for (const FMetasoundFrontendEdge& Edge : Graph.Edges)
		{
			if (int32* In = Out.InDegree.Find(Edge.ToNodeID)) { (*In)++; }
			if (int32* O = Out.OutDegree.Find(Edge.FromNodeID)) { (*O)++; }
			Out.DrivenInputs.Add(Edge.GetToVertexHandle());
			Out.DrivingOutputs.Add(Edge.GetFromVertexHandle());
		}
	}

	/** One node, addressed the way every write action wants it addressed. */
	TSharedPtr<FJsonObject> MSReadNodeBrief(
		const FMetasoundFrontendDocument& Doc,
		const FMetasoundFrontendNode& Node,
		const FMSReadDegree* Degrees)
	{
		const FMetasoundFrontendClass* Cls = MSReadFindClass(Doc, Node.ClassID);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("nodeId"), Node.GetID().ToString());
		O->SetStringField(TEXT("name"), Node.Name.ToString());
		O->SetObjectField(TEXT("class"), MSReadClassJson(Cls));
		O->SetNumberField(TEXT("inputCount"), Node.Interface.Inputs.Num());
		O->SetNumberField(TEXT("outputCount"), Node.Interface.Outputs.Num());

		const EMetasoundFrontendClassType Type = Cls ? Cls->Metadata.GetType() : EMetasoundFrontendClassType::Invalid;
		// Graph interface nodes are reachable by name through the *_graph_input /
		// *_graph_output write actions, so name the parameter that takes them.
		if (Type == EMetasoundFrontendClassType::Input)
		{
			O->SetStringField(TEXT("graphInput"), Node.Name.ToString());
			O->SetStringField(TEXT("authorEndpoint"), FString::Printf(TEXT("input:%s"), *Node.Name.ToString()));
		}
		else if (Type == EMetasoundFrontendClassType::Output)
		{
			O->SetStringField(TEXT("graphOutput"), Node.Name.ToString());
			O->SetStringField(TEXT("authorEndpoint"), FString::Printf(TEXT("output:%s"), *Node.Name.ToString()));
		}

		if (Degrees)
		{
			const int32* In = Degrees->InDegree.Find(Node.GetID());
			const int32* Out = Degrees->OutDegree.Find(Node.GetID());
			O->SetNumberField(TEXT("incomingConnections"), In ? *In : 0);
			O->SetNumberField(TEXT("outgoingConnections"), Out ? *Out : 0);
		}
		return O;
	}

	/**
	 * One input vertex, carrying its connection state and whatever default is set.
	 *
	 * Two places hold a default and they are not interchangeable. An ordinary
	 * node's per-instance override sits in the node's own InputLiterals; a GRAPH
	 * INPUT node has none, and its value is the graph input's default on the root
	 * graph class interface. Both are reported as "default", because both are
	 * what the caller set and what it gets to set again, and "defaultSource" says
	 * which one it came from so a caller knows whether to write it back with
	 * metasound_set_input_default's nodeId form or its graphInput form.
	 */
	TSharedPtr<FJsonObject> MSReadInputVertexJson(
		const FMetasoundFrontendDocument& Doc,
		const FMetasoundFrontendNode& Node,
		const FMetasoundFrontendVertex& Vertex,
		const FGuid& PageID,
		const FMSReadDegree& Degrees)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Vertex.Name.ToString());
		O->SetStringField(TEXT("dataType"), Vertex.TypeName.ToString());
		O->SetStringField(TEXT("vertexId"), Vertex.VertexID.ToString());

		const FMetasoundFrontendVertexHandle Handle{ Node.GetID(), Vertex.VertexID };
		O->SetBoolField(TEXT("connected"), Degrees.DrivenInputs.Contains(Handle));

		if (const FMetasoundFrontendLiteral* Lit = MSReadNodeInputLiteral(Node, Vertex.VertexID))
		{
			O->SetField(TEXT("default"), MSReadLiteralToJson(*Lit));
			O->SetBoolField(TEXT("defaultIsSet"), true);
			O->SetStringField(TEXT("defaultSource"), TEXT("node"));
			return O;
		}

		if (MSReadClassType(Doc, Node) == EMetasoundFrontendClassType::Input)
		{
			if (const FMetasoundFrontendLiteral* Lit = MSReadGraphInputDefault(Doc, Node.Name, PageID))
			{
				O->SetField(TEXT("default"), MSReadLiteralToJson(*Lit));
				O->SetBoolField(TEXT("defaultIsSet"), true);
				O->SetStringField(TEXT("defaultSource"), TEXT("graphInput"));
				return O;
			}
		}

		O->SetBoolField(TEXT("defaultIsSet"), false);
		return O;
	}

	TSharedPtr<FJsonObject> MSReadOutputVertexJson(
		const FMetasoundFrontendNode& Node,
		const FMetasoundFrontendVertex& Vertex,
		const FMSReadDegree& Degrees)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Vertex.Name.ToString());
		O->SetStringField(TEXT("dataType"), Vertex.TypeName.ToString());
		O->SetStringField(TEXT("vertexId"), Vertex.VertexID.ToString());

		const FMetasoundFrontendVertexHandle Handle{ Node.GetID(), Vertex.VertexID };
		O->SetBoolField(TEXT("connected"), Degrees.DrivingOutputs.Contains(Handle));
		return O;
	}

	/**
	 * One edge, reported as the argument list of the write action that would make
	 * it. fromNodeId / fromOutput / toNodeId / toInput are literally metasound_connect's
	 * parameter names, and graphInput / graphOutput are the *_connect_graph_* ones.
	 */
	TSharedPtr<FJsonObject> MSReadEdgeJson(
		const FMetasoundFrontendDocument& Doc,
		const FMetasoundFrontendGraph& Graph,
		const FMetasoundFrontendEdge& Edge,
		FString& OutProblem)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("fromNodeId"), Edge.FromNodeID.ToString());
		O->SetStringField(TEXT("toNodeId"), Edge.ToNodeID.ToString());

		const FMetasoundFrontendNode* FromNode = MSReadFindNode(Graph, Edge.FromNodeID);
		const FMetasoundFrontendNode* ToNode = MSReadFindNode(Graph, Edge.ToNodeID);

		if (!FromNode || !ToNode)
		{
			O->SetBoolField(TEXT("dangling"), true);
			OutProblem = FString::Printf(
				TEXT("connection references a node that is not in the graph (fromNodeId=%s%s, toNodeId=%s%s): ")
				TEXT("the document is inconsistent, rebuild the graph with audio(metasound_author)"),
				*Edge.FromNodeID.ToString(), FromNode ? TEXT("") : TEXT(" MISSING"),
				*Edge.ToNodeID.ToString(), ToNode ? TEXT("") : TEXT(" MISSING"));
			return O;
		}

		const FMetasoundFrontendVertex* FromVertex = MSReadFindVertex(FromNode->Interface.Outputs, Edge.FromVertexID);
		const FMetasoundFrontendVertex* ToVertex = MSReadFindVertex(ToNode->Interface.Inputs, Edge.ToVertexID);

		O->SetStringField(TEXT("fromNodeName"), FromNode->Name.ToString());
		O->SetStringField(TEXT("toNodeName"), ToNode->Name.ToString());

		if (!FromVertex || !ToVertex)
		{
			O->SetBoolField(TEXT("dangling"), true);
			OutProblem = FString::Printf(
				TEXT("connection from '%s' to '%s' references a vertex that no longer exists on the node: ")
				TEXT("the node class changed under the graph, re-add the node with audio(metasound_add_node)"),
				*FromNode->Name.ToString(), *ToNode->Name.ToString());
			return O;
		}

		O->SetStringField(TEXT("fromOutput"), FromVertex->Name.ToString());
		O->SetStringField(TEXT("toInput"), ToVertex->Name.ToString());
		O->SetStringField(TEXT("fromDataType"), FromVertex->TypeName.ToString());
		O->SetStringField(TEXT("toDataType"), ToVertex->TypeName.ToString());

		const EMetasoundFrontendClassType FromType = MSReadClassType(Doc, *FromNode);
		const EMetasoundFrontendClassType ToType = MSReadClassType(Doc, *ToNode);
		if (FromType == EMetasoundFrontendClassType::Input)
		{
			O->SetStringField(TEXT("graphInput"), FromNode->Name.ToString());
		}
		if (ToType == EMetasoundFrontendClassType::Output)
		{
			O->SetStringField(TEXT("graphOutput"), ToNode->Name.ToString());
		}

		if (FromVertex->TypeName != ToVertex->TypeName)
		{
			O->SetBoolField(TEXT("typeMismatch"), true);
			OutProblem = FString::Printf(
				TEXT("type mismatch: '%s.%s' is %s but '%s.%s' expects %s. ")
				TEXT("Insert a converter node (audio(metasound_list_node_classes)) or re-wire with audio(metasound_connect)"),
				*FromNode->Name.ToString(), *FromVertex->Name.ToString(), *FromVertex->TypeName.ToString(),
				*ToNode->Name.ToString(), *ToVertex->Name.ToString(), *ToVertex->TypeName.ToString());
		}
		return O;
	}

	/** A bad nodeId is only useful if it hands back the ids that would have worked. */
	TSharedPtr<FJsonValue> MSReadBadNodeIdError(const FMSReadTarget& T, const FString& NodeId)
	{
		TArray<FString> Valid;
		for (const FMetasoundFrontendNode& Node : T.Graph->Nodes)
		{
			if (Valid.Num() >= 40) break;
			Valid.Add(FString::Printf(TEXT("%s (%s)"), *Node.GetID().ToString(), *Node.Name.ToString()));
		}

		if (Valid.Num() == 0)
		{
			return MCPError(FString::Printf(
				TEXT("No node '%s' in '%s': the graph has no nodes at all. ")
				TEXT("Add one with audio(metasound_add_node) or stamp a whole graph with audio(metasound_author)."),
				*NodeId, *T.AssetPath));
		}

		return MCPError(FString::Printf(
			TEXT("No node '%s' in '%s' (read from the %s). Valid nodeIds%s: %s. ")
			TEXT("Call audio(metasound_read_document) for the full graph."),
			*NodeId, *T.AssetPath, *T.Source,
			T.Graph->Nodes.Num() > Valid.Num() ? TEXT(" (first 40)") : TEXT(""),
			*FString::Join(Valid, TEXT(", "))));
	}

	/** Resolve a nodeId param against the graph, with the listing error on a miss. */
	const FMetasoundFrontendNode* MSReadRequireNode(
		const TSharedPtr<FJsonObject>& Params,
		const FMSReadTarget& T,
		TSharedPtr<FJsonValue>& OutError)
	{
		FString NodeId;
		if (auto Err = RequireString(Params, TEXT("nodeId"), NodeId))
		{
			OutError = Err;
			return nullptr;
		}

		FGuid Guid;
		if (!FGuid::Parse(NodeId, Guid))
		{
			OutError = MSReadBadNodeIdError(T, NodeId);
			return nullptr;
		}

		const FMetasoundFrontendNode* Node = MSReadFindNode(*T.Graph, Guid);
		if (!Node)
		{
			OutError = MSReadBadNodeIdError(T, NodeId);
			return nullptr;
		}
		return Node;
	}
}

// ── metasound_read_document ───────────────────────────────────────────────
//
// The whole graph in one call: document version, declared interfaces, graph
// inputs and outputs with their defaults, variables, every node and every edge.
// This is the verification counterpart to metasound_author.
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundReadDocument(const TSharedPtr<FJsonObject>& Params)
{
	FMSReadTarget T;
	TSharedPtr<FJsonValue> Error;
	if (!MSReadResolve(Params, T, Error)) return Error;

	const bool bIncludeNodes = OptionalBool(Params, TEXT("includeNodes"), true);
	const bool bIncludeConnections = OptionalBool(Params, TEXT("includeConnections"), true);

	FMSReadDegree Degrees;
	MSReadBuildDegrees(*T.Graph, Degrees);

	auto Res = MCPSuccess();
	MSReadStampSource(Res, T);

	// Document identity.
	Res->SetStringField(TEXT("documentVersion"), T.Doc->Metadata.Version.ToString());
	{
		const FMetasoundFrontendClassName& RootName = T.Doc->RootGraph.Metadata.GetClassName();
		Res->SetStringField(TEXT("graphClassName"), RootName.ToString());
	}

	TArray<TSharedPtr<FJsonValue>> Interfaces;
	for (const FMetasoundFrontendVersion& Iface : T.Doc->Interfaces)
	{
		Interfaces.Add(MakeShared<FJsonValueString>(Iface.ToString()));
	}
	Res->SetArrayField(TEXT("interfaces"), Interfaces);

	TArray<TSharedPtr<FJsonValue>> Pages;
	for (const FMetasoundFrontendGraph& Page : T.Doc->RootGraph.GetConstGraphPages())
	{
		Pages.Add(MakeShared<FJsonValueString>(Page.PageID.ToString()));
	}
	Res->SetArrayField(TEXT("pages"), Pages);

	// Graph interface: the input and output nodes, split out from the node list
	// because they are what a caller connects to by name.
	TArray<TSharedPtr<FJsonValue>> GraphInputs, GraphOutputs, Nodes;
	for (const FMetasoundFrontendNode& Node : T.Graph->Nodes)
	{
		const EMetasoundFrontendClassType Type = MSReadClassType(*T.Doc, Node);
		TSharedPtr<FJsonObject> Brief = MSReadNodeBrief(*T.Doc, Node, &Degrees);

		if (Type == EMetasoundFrontendClassType::Input)
		{
			// An input node's single output vertex carries the graph input's data type.
			if (Node.Interface.Outputs.Num() > 0)
			{
				Brief->SetStringField(TEXT("dataType"), Node.Interface.Outputs[0].TypeName.ToString());
			}
			// The default the caller authored, in the same JSON form
			// metasound_author's inputs[].default took it, so a read of a graph
			// input is the argument list that would declare it again. It is read
			// off the root graph class interface: an input node's own
			// InputLiterals array is empty, and reading only that reported no
			// default for a value the write had stored correctly.
			if (const FMetasoundFrontendLiteral* Lit = MSReadGraphInputDefault(*T.Doc, Node.Name, T.Graph->PageID))
			{
				Brief->SetField(TEXT("default"), MSReadLiteralToJson(*Lit));
				Brief->SetBoolField(TEXT("defaultIsSet"), true);
			}
			else
			{
				Brief->SetBoolField(TEXT("defaultIsSet"), false);
			}
			GraphInputs.Add(MakeShared<FJsonValueObject>(Brief));
		}
		else if (Type == EMetasoundFrontendClassType::Output)
		{
			if (Node.Interface.Inputs.Num() > 0)
			{
				Brief->SetStringField(TEXT("dataType"), Node.Interface.Inputs[0].TypeName.ToString());
			}
			GraphOutputs.Add(MakeShared<FJsonValueObject>(Brief));
		}
		else if (bIncludeNodes)
		{
			Nodes.Add(MakeShared<FJsonValueObject>(Brief));
		}
	}
	Res->SetArrayField(TEXT("graphInputs"), GraphInputs);
	Res->SetArrayField(TEXT("graphOutputs"), GraphOutputs);
	if (bIncludeNodes)
	{
		Res->SetArrayField(TEXT("nodes"), Nodes);
	}
	Res->SetNumberField(TEXT("nodeCount"), T.Graph->Nodes.Num());

	// Variables.
	TArray<TSharedPtr<FJsonValue>> Variables;
	for (const FMetasoundFrontendVariable& Var : T.Graph->Variables)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Var.Name.ToString());
		O->SetStringField(TEXT("dataType"), Var.TypeName.ToString());
		Variables.Add(MakeShared<FJsonValueObject>(O));
	}
	Res->SetArrayField(TEXT("variables"), Variables);

	// Connections.
	if (bIncludeConnections)
	{
		TArray<TSharedPtr<FJsonValue>> Conns;
		for (const FMetasoundFrontendEdge& Edge : T.Graph->Edges)
		{
			FString Ignored;
			Conns.Add(MakeShared<FJsonValueObject>(MSReadEdgeJson(*T.Doc, *T.Graph, Edge, Ignored)));
		}
		Res->SetArrayField(TEXT("connections"), Conns);
	}
	Res->SetNumberField(TEXT("connectionCount"), T.Graph->Edges.Num());

	Res->SetStringField(TEXT("note"),
		TEXT("nodeId is what metasound_connect / metasound_set_input_default take back verbatim. ")
		TEXT("Call audio(metasound_validate) for the problems in this graph."));
	return MCPResult(Res);
}

// ── metasound_list_connections ────────────────────────────────────────────
//
// Every edge as a rewireable argument list, optionally narrowed to one node.
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundListConnections(const TSharedPtr<FJsonObject>& Params)
{
	FMSReadTarget T;
	TSharedPtr<FJsonValue> Error;
	if (!MSReadResolve(Params, T, Error)) return Error;

	// Optional narrowing to one node, either end or a named end.
	FGuid FilterNode;
	bool bFilter = false;
	FString NodeId;
	if (Params->TryGetStringField(TEXT("nodeId"), NodeId) && !NodeId.IsEmpty())
	{
		const FMetasoundFrontendNode* Node = MSReadRequireNode(Params, T, Error);
		if (!Node) return Error;
		FilterNode = Node->GetID();
		bFilter = true;
	}
	const FString Direction = OptionalString(Params, TEXT("direction"), TEXT("both")).ToLower();
	const FString DataTypeFilter = OptionalString(Params, TEXT("dataType"));

	TArray<TSharedPtr<FJsonValue>> Conns;
	int32 Problems = 0;
	for (const FMetasoundFrontendEdge& Edge : T.Graph->Edges)
	{
		if (bFilter)
		{
			const bool bFrom = Edge.FromNodeID == FilterNode;
			const bool bTo = Edge.ToNodeID == FilterNode;
			if (Direction == TEXT("out") && !bFrom) continue;
			if (Direction == TEXT("in") && !bTo) continue;
			if (Direction != TEXT("in") && Direction != TEXT("out") && !bFrom && !bTo) continue;
		}

		FString Problem;
		TSharedPtr<FJsonObject> O = MSReadEdgeJson(*T.Doc, *T.Graph, Edge, Problem);
		if (!Problem.IsEmpty()) Problems++;

		if (!DataTypeFilter.IsEmpty())
		{
			FString FromType;
			O->TryGetStringField(TEXT("fromDataType"), FromType);
			if (!FromType.Equals(DataTypeFilter, ESearchCase::IgnoreCase)) continue;
		}
		Conns.Add(MakeShared<FJsonValueObject>(O));
	}

	auto Res = MCPSuccess();
	MSReadStampSource(Res, T);
	Res->SetArrayField(TEXT("connections"), Conns);
	Res->SetNumberField(TEXT("count"), Conns.Num());
	Res->SetNumberField(TEXT("totalInGraph"), T.Graph->Edges.Num());
	Res->SetNumberField(TEXT("malformed"), Problems);
	Res->SetStringField(TEXT("note"),
		TEXT("Each entry names metasound_connect's parameters exactly: fromNodeId, fromOutput, toNodeId, toInput."));
	return MCPResult(Res);
}

// ── metasound_list_variables ──────────────────────────────────────────────
//
// Graph variables with their initial literal and the node ids that set and read
// them, so a caller can follow a variable to the wiring that uses it.
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundListVariables(const TSharedPtr<FJsonObject>& Params)
{
	FMSReadTarget T;
	TSharedPtr<FJsonValue> Error;
	if (!MSReadResolve(Params, T, Error)) return Error;

	const FString Filter = OptionalString(Params, TEXT("filter")).ToLower();

	TArray<TSharedPtr<FJsonValue>> Vars;
	for (const FMetasoundFrontendVariable& Var : T.Graph->Variables)
	{
		const FString Name = Var.Name.ToString();
		if (!Filter.IsEmpty() && !Name.ToLower().Contains(Filter)) continue;

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Name);
		O->SetStringField(TEXT("dataType"), Var.TypeName.ToString());
		O->SetField(TEXT("default"), MSReadLiteralToJson(Var.Literal));
		O->SetStringField(TEXT("variableNodeId"), Var.VariableNodeID.ToString());
		O->SetStringField(TEXT("setterNodeId"), Var.MutatorNodeID.ToString());

		TArray<TSharedPtr<FJsonValue>> Getters;
		for (const FGuid& Id : Var.AccessorNodeIDs)
		{
			Getters.Add(MakeShared<FJsonValueString>(Id.ToString()));
		}
		for (const FGuid& Id : Var.DeferredAccessorNodeIDs)
		{
			Getters.Add(MakeShared<FJsonValueString>(Id.ToString()));
		}
		O->SetArrayField(TEXT("getterNodeIds"), Getters);
		O->SetBoolField(TEXT("hasSetter"), Var.MutatorNodeID.IsValid());
		O->SetNumberField(TEXT("getterCount"), Getters.Num());
		Vars.Add(MakeShared<FJsonValueObject>(O));
	}

	auto Res = MCPSuccess();
	MSReadStampSource(Res, T);
	Res->SetArrayField(TEXT("variables"), Vars);
	Res->SetNumberField(TEXT("count"), Vars.Num());
	if (Vars.Num() == 0 && Filter.IsEmpty())
	{
		Res->SetStringField(TEXT("note"),
			TEXT("This graph declares no variables. Variables are graph-scoped values written once and read from several places; a graph without them is normal."));
	}
	else
	{
		Res->SetStringField(TEXT("note"),
			TEXT("setterNodeId / getterNodeIds are node ids: pass them to audio(metasound_inspect_node) to see what drives and reads the variable."));
	}
	return MCPResult(Res);
}

// ── metasound_search_nodes ────────────────────────────────────────────────
//
// Find node instances inside one graph by name, class, or the data types they
// carry. This is the "which node was that" step between reading a document and
// acting on a node.
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundSearchNodes(const TSharedPtr<FJsonObject>& Params)
{
	FMSReadTarget T;
	TSharedPtr<FJsonValue> Error;
	if (!MSReadResolve(Params, T, Error)) return Error;

	const FString Query = OptionalString(Params, TEXT("query")).ToLower();
	const FString DataType = OptionalString(Params, TEXT("dataType"));
	const FString ClassTypeFilter = OptionalString(Params, TEXT("classType")).ToLower();
	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 100), 1, 1000);

	FMSReadDegree Degrees;
	MSReadBuildDegrees(*T.Graph, Degrees);

	TArray<TSharedPtr<FJsonValue>> Matches;
	int32 Total = 0;
	for (const FMetasoundFrontendNode& Node : T.Graph->Nodes)
	{
		const FMetasoundFrontendClass* Cls = MSReadFindClass(*T.Doc, Node.ClassID);
		const EMetasoundFrontendClassType Type = Cls ? Cls->Metadata.GetType() : EMetasoundFrontendClassType::Invalid;

		if (!ClassTypeFilter.IsEmpty())
		{
			const FString TypeStr = FString(LexToString(Type)).ToLower();
			if (!TypeStr.Contains(ClassTypeFilter)) continue;
		}

		if (!Query.IsEmpty())
		{
			bool bHit = Node.Name.ToString().ToLower().Contains(Query);
			if (!bHit && Cls)
			{
				const FMetasoundFrontendClassName& CN = Cls->Metadata.GetClassName();
				bHit = CN.Name.ToString().ToLower().Contains(Query)
					|| CN.Namespace.ToString().ToLower().Contains(Query)
					|| CN.Variant.ToString().ToLower().Contains(Query);
			}
			if (!bHit) continue;
		}

		if (!DataType.IsEmpty())
		{
			bool bHit = false;
			for (const FMetasoundFrontendVertex& V : Node.Interface.Inputs)
			{
				if (V.TypeName.ToString().Equals(DataType, ESearchCase::IgnoreCase)) { bHit = true; break; }
			}
			if (!bHit)
			{
				for (const FMetasoundFrontendVertex& V : Node.Interface.Outputs)
				{
					if (V.TypeName.ToString().Equals(DataType, ESearchCase::IgnoreCase)) { bHit = true; break; }
				}
			}
			if (!bHit) continue;
		}

		Total++;
		if (Matches.Num() < Limit)
		{
			Matches.Add(MakeShared<FJsonValueObject>(MSReadNodeBrief(*T.Doc, Node, &Degrees)));
		}
	}

	auto Res = MCPSuccess();
	MSReadStampSource(Res, T);
	Res->SetArrayField(TEXT("nodes"), Matches);
	Res->SetNumberField(TEXT("count"), Matches.Num());
	Res->SetNumberField(TEXT("matched"), Total);
	Res->SetNumberField(TEXT("searched"), T.Graph->Nodes.Num());
	if (Total == 0)
	{
		Res->SetStringField(TEXT("note"), FString::Printf(
			TEXT("Nothing matched in the %d nodes of '%s'. Drop the filters, or call audio(metasound_read_document) to see the whole graph. ")
			TEXT("This searches inside one graph: for classes you could add, call audio(metasound_list_node_classes)."),
			T.Graph->Nodes.Num(), *T.AssetPath));
	}
	return MCPResult(Res);
}

// ── metasound_inspect_node ────────────────────────────────────────────────
//
// One node in full: class identity, every vertex with its type, default and
// connection state, and the edges on both sides named by node.
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundInspectNode(const TSharedPtr<FJsonObject>& Params)
{
	FMSReadTarget T;
	TSharedPtr<FJsonValue> Error;
	if (!MSReadResolve(Params, T, Error)) return Error;

	const FMetasoundFrontendNode* Node = MSReadRequireNode(Params, T, Error);
	if (!Node) return Error;

	FMSReadDegree Degrees;
	MSReadBuildDegrees(*T.Graph, Degrees);

	auto Res = MCPSuccess();
	MSReadStampSource(Res, T);

	TSharedPtr<FJsonObject> Brief = MSReadNodeBrief(*T.Doc, *Node, &Degrees);
	for (const auto& Pair : Brief->Values)
	{
		Res->SetField(Pair.Key, Pair.Value);
	}

	TArray<TSharedPtr<FJsonValue>> Inputs, Outputs, Environment;
	for (const FMetasoundFrontendVertex& V : Node->Interface.Inputs)
	{
		Inputs.Add(MakeShared<FJsonValueObject>(MSReadInputVertexJson(*T.Doc, *Node, V, T.Graph->PageID, Degrees)));
	}
	for (const FMetasoundFrontendVertex& V : Node->Interface.Outputs)
	{
		Outputs.Add(MakeShared<FJsonValueObject>(MSReadOutputVertexJson(*Node, V, Degrees)));
	}
	for (const FMetasoundFrontendVertex& V : Node->Interface.Environment)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), V.Name.ToString());
		O->SetStringField(TEXT("dataType"), V.TypeName.ToString());
		Environment.Add(MakeShared<FJsonValueObject>(O));
	}
	Res->SetArrayField(TEXT("inputs"), Inputs);
	Res->SetArrayField(TEXT("outputs"), Outputs);
	Res->SetArrayField(TEXT("environment"), Environment);

	TArray<TSharedPtr<FJsonValue>> Incoming, Outgoing;
	for (const FMetasoundFrontendEdge& Edge : T.Graph->Edges)
	{
		if (Edge.ToNodeID != Node->GetID() && Edge.FromNodeID != Node->GetID()) continue;
		FString Ignored;
		TSharedPtr<FJsonObject> O = MSReadEdgeJson(*T.Doc, *T.Graph, Edge, Ignored);
		if (Edge.ToNodeID == Node->GetID()) Incoming.Add(MakeShared<FJsonValueObject>(O));
		if (Edge.FromNodeID == Node->GetID()) Outgoing.Add(MakeShared<FJsonValueObject>(O));
	}
	Res->SetArrayField(TEXT("incoming"), Incoming);
	Res->SetArrayField(TEXT("outgoing"), Outgoing);

	Res->SetStringField(TEXT("note"),
		TEXT("Set an unconnected input with audio(metasound_set_input_default) using this nodeId plus the input's name as inputName; ")
		TEXT("wire one with audio(metasound_connect) using the same nodeId as toNodeId and the name as toInput."));
	return MCPResult(Res);
}

// ── metasound_list_node_pins ──────────────────────────────────────────────
//
// The lean vertex listing: what a node accepts and produces, with types and
// connection state, for picking the vertex names the write actions want.
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundListNodePins(const TSharedPtr<FJsonObject>& Params)
{
	FMSReadTarget T;
	TSharedPtr<FJsonValue> Error;
	if (!MSReadResolve(Params, T, Error)) return Error;

	const FMetasoundFrontendNode* Node = MSReadRequireNode(Params, T, Error);
	if (!Node) return Error;

	const FString DataType = OptionalString(Params, TEXT("dataType"));
	const FString Direction = OptionalString(Params, TEXT("direction"), TEXT("both")).ToLower();

	FMSReadDegree Degrees;
	MSReadBuildDegrees(*T.Graph, Degrees);

	auto Matches = [&DataType](const FMetasoundFrontendVertex& V)
	{
		return DataType.IsEmpty() || V.TypeName.ToString().Equals(DataType, ESearchCase::IgnoreCase);
	};

	TArray<TSharedPtr<FJsonValue>> Inputs, Outputs;
	int32 UnconnectedInputs = 0;
	if (Direction != TEXT("outputs"))
	{
		for (const FMetasoundFrontendVertex& V : Node->Interface.Inputs)
		{
			if (!Matches(V)) continue;
			TSharedPtr<FJsonObject> O = MSReadInputVertexJson(*T.Doc, *Node, V, T.Graph->PageID, Degrees);
			bool bConnected = false;
			O->TryGetBoolField(TEXT("connected"), bConnected);
			if (!bConnected) UnconnectedInputs++;
			Inputs.Add(MakeShared<FJsonValueObject>(O));
		}
	}
	if (Direction != TEXT("inputs"))
	{
		for (const FMetasoundFrontendVertex& V : Node->Interface.Outputs)
		{
			if (!Matches(V)) continue;
			Outputs.Add(MakeShared<FJsonValueObject>(MSReadOutputVertexJson(*Node, V, Degrees)));
		}
	}

	auto Res = MCPSuccess();
	MSReadStampSource(Res, T);
	Res->SetStringField(TEXT("nodeId"), Node->GetID().ToString());
	Res->SetStringField(TEXT("nodeName"), Node->Name.ToString());
	Res->SetObjectField(TEXT("class"), MSReadClassJson(MSReadFindClass(*T.Doc, Node->ClassID)));
	Res->SetArrayField(TEXT("inputs"), Inputs);
	Res->SetArrayField(TEXT("outputs"), Outputs);
	Res->SetNumberField(TEXT("inputCount"), Inputs.Num());
	Res->SetNumberField(TEXT("outputCount"), Outputs.Num());
	Res->SetNumberField(TEXT("unconnectedInputs"), UnconnectedInputs);
	Res->SetStringField(TEXT("note"),
		TEXT("The 'name' of a pin is the fromOutput / toInput / inputName argument the metasound write actions take."));
	return MCPResult(Res);
}

// ── metasound_validate ────────────────────────────────────────────────────
//
// Read plus diagnose, in the shape gameplay(read_eqs_query) established:
// problems[] as sentences naming the offending node and the call that fixes it,
// and runnable as the single verdict. A MetaSound that builds silently is the
// failure mode this exists to catch.
TSharedPtr<FJsonValue> FAudioHandlers::MetaSoundValidate(const TSharedPtr<FJsonObject>& Params)
{
	FMSReadTarget T;
	TSharedPtr<FJsonValue> Error;
	if (!MSReadResolve(Params, T, Error)) return Error;

	FMSReadDegree Degrees;
	MSReadBuildDegrees(*T.Graph, Degrees);

	TArray<FString> Problems;

	// 1. An empty graph is silent by construction.
	if (T.Graph->Nodes.Num() == 0)
	{
		Problems.Add(TEXT("the graph has no nodes at all: stamp one with audio(metasound_author), ")
					 TEXT("or add nodes with audio(metasound_add_node)"));
	}

	// 2. Malformed and mistyped edges. MSReadEdgeJson writes the sentence.
	for (const FMetasoundFrontendEdge& Edge : T.Graph->Edges)
	{
		FString Problem;
		MSReadEdgeJson(*T.Doc, *T.Graph, Edge, Problem);
		if (!Problem.IsEmpty()) Problems.Add(Problem);
	}

	// 3. Per-node structure.
	int32 UnconnectedGraphOutputs = 0;
	for (const FMetasoundFrontendNode& Node : T.Graph->Nodes)
	{
		const EMetasoundFrontendClassType Type = MSReadClassType(*T.Doc, Node);
		const FString NodeId = Node.GetID().ToString();
		const FString NodeName = Node.Name.ToString();
		const int32* InDeg = Degrees.InDegree.Find(Node.GetID());
		const int32* OutDeg = Degrees.OutDegree.Find(Node.GetID());
		const int32 In = InDeg ? *InDeg : 0;
		const int32 Out = OutDeg ? *OutDeg : 0;

		// 3a. A graph output nobody drives is the classic silent MetaSound: the
		//     asset builds, plays, and emits nothing.
		if (Type == EMetasoundFrontendClassType::Output)
		{
			if (In == 0)
			{
				UnconnectedGraphOutputs++;
				Problems.Add(FString::Printf(
					TEXT("graph output '%s' is never driven, so it outputs silence/zero: connect a node to it with ")
					TEXT("audio(metasound_connect_graph_output, fromNodeId=<node>, fromOutput=<vertex>, graphOutput='%s')"),
					*NodeName, *NodeName));
			}
			continue;
		}

		// 3b. A graph input nothing reads is dead surface area, not a build error.
		if (Type == EMetasoundFrontendClassType::Input)
		{
			if (Out == 0)
			{
				Problems.Add(FString::Printf(
					TEXT("graph input '%s' is exposed but nothing in the graph reads it, so setting it does nothing: ")
					TEXT("wire it with audio(metasound_connect_graph_input, graphInput='%s', toNodeId=<node>, toInput=<vertex>)"),
					*NodeName, *NodeName));
			}
			continue;
		}

		// Variable plumbing nodes are wired by the variable actions, not by hand.
		if (MSReadIsVariableNode(Type)) continue;

		// 3c. Orphan: wired to nothing on either side.
		if (In == 0 && Out == 0)
		{
			Problems.Add(FString::Printf(
				TEXT("node '%s' (nodeId %s) is orphaned - nothing feeds it and nothing reads it, so it does not run: ")
				TEXT("wire it with audio(metasound_connect) or drop it"),
				*NodeName, *NodeId));
			continue;
		}

		// 3d. Dead end: it computes something no one consumes.
		if (Out == 0 && Node.Interface.Outputs.Num() > 0)
		{
			Problems.Add(FString::Printf(
				TEXT("node '%s' (nodeId %s) produces output that nothing consumes: connect it onward with ")
				TEXT("audio(metasound_connect, fromNodeId='%s', ...) or to the source with audio(metasound_connect_audio_out)"),
				*NodeName, *NodeId, *NodeId));
		}

		// 3e. A required input left dangling. Trigger and Audio carry no useful
		//     literal, so an unconnected one with no default is genuinely broken;
		//     other types fall back to a class default and are fine.
		for (const FMetasoundFrontendVertex& V : Node.Interface.Inputs)
		{
			const FMetasoundFrontendVertexHandle Handle{ Node.GetID(), V.VertexID };
			if (Degrees.DrivenInputs.Contains(Handle)) continue;
			if (MSReadNodeInputLiteral(Node, V.VertexID)) continue;

			const FString TypeName = V.TypeName.ToString();
			if (TypeName == TEXT("Trigger"))
			{
				Problems.Add(FString::Printf(
					TEXT("node '%s' (nodeId %s) input '%s' is a Trigger with nothing connected, so it never fires: ")
					TEXT("connect a trigger source with audio(metasound_connect, toNodeId='%s', toInput='%s')"),
					*NodeName, *NodeId, *V.Name.ToString(), *NodeId, *V.Name.ToString()));
			}
			else if (TypeName == TEXT("Audio"))
			{
				Problems.Add(FString::Printf(
					TEXT("node '%s' (nodeId %s) input '%s' takes Audio and is unconnected, so it processes silence: ")
					TEXT("connect a source with audio(metasound_connect, toNodeId='%s', toInput='%s')"),
					*NodeName, *NodeId, *V.Name.ToString(), *NodeId, *V.Name.ToString()));
			}
		}
	}

	// 4. A source with no graph outputs at all cannot be heard.
	if (T.Graph->Nodes.Num() > 0)
	{
		int32 OutputNodes = 0;
		for (const FMetasoundFrontendNode& Node : T.Graph->Nodes)
		{
			if (MSReadClassType(*T.Doc, Node) == EMetasoundFrontendClassType::Output) OutputNodes++;
		}
		if (OutputNodes == 0)
		{
			Problems.Add(TEXT("the graph declares no outputs, so nothing leaves it: add one with ")
						 TEXT("audio(metasound_add_output), or recreate the source with audio(create_metasound) ")
						 TEXT("which installs the audio output interface"));
		}
	}

	// 5. A variable written and never read (or read and never written).
	for (const FMetasoundFrontendVariable& Var : T.Graph->Variables)
	{
		const int32 Getters = Var.AccessorNodeIDs.Num() + Var.DeferredAccessorNodeIDs.Num();
		if (!Var.MutatorNodeID.IsValid() && Getters > 0)
		{
			Problems.Add(FString::Printf(
				TEXT("variable '%s' is read by %d node(s) but never set, so it always holds its initial value: ")
				TEXT("add a setter or fold the value into the reading node's default with audio(metasound_set_input_default)"),
				*Var.Name.ToString(), Getters));
		}
		else if (Var.MutatorNodeID.IsValid() && Getters == 0)
		{
			Problems.Add(FString::Printf(
				TEXT("variable '%s' is set but never read, so the write is discarded: read it somewhere or drop the setter (nodeId %s)"),
				*Var.Name.ToString(), *Var.MutatorNodeID.ToString()));
		}
	}

	TArray<TSharedPtr<FJsonValue>> ProblemList;
	for (const FString& P : Problems) ProblemList.Add(MakeShared<FJsonValueString>(P));

	auto Res = MCPSuccess();
	MSReadStampSource(Res, T);
	Res->SetArrayField(TEXT("problems"), ProblemList);
	Res->SetBoolField(TEXT("runnable"), Problems.Num() == 0);
	Res->SetNumberField(TEXT("nodeCount"), T.Graph->Nodes.Num());
	Res->SetNumberField(TEXT("connectionCount"), T.Graph->Edges.Num());
	Res->SetNumberField(TEXT("unconnectedGraphOutputs"), UnconnectedGraphOutputs);
	Res->SetStringField(TEXT("note"),
		Problems.Num() == 0
			? TEXT("Every node is wired, every graph output is driven, and no edge crosses data types.")
			: TEXT("Each problem names the node and the call that fixes it. Re-run after fixing; call metasound_build to persist builder edits."));
	return MCPResult(Res);
}
