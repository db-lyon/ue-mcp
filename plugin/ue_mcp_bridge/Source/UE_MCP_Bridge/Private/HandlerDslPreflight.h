#pragma once

// What a Graph DSL rewrite of a Blueprint would delete, answered before it runs
// (#1908). Shared by epic(call_tool), which refuses a destructive
// BlueprintDslToolset Write or Edit unless the caller opts in, and by
// blueprint(preflight_dsl_write), which reports the same thing without writing.
//
// Epic's Write rebuilds each graph it touches from the text. It keeps only what
// a statement reaches, so a loose node (an impure chain with no entry before it,
// a pure node whose output nothing reads, a stray reroute) is deleted, and so is
// every comment box the text does not carry as a '; text' run. A collapsed graph
// inside a rewritten entry is flattened for good. Read never shows loose nodes,
// so a caller cannot see the loss coming from the text alone.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MCPDslPreflight
{
	inline const TCHAR* DslToolset() { return TEXT("BlueprintToolsets.BlueprintDslToolset"); }

	/** True for the DSL tools that rewrite graphs: Write and Edit. ToolName is
	 *  bare or qualified with its toolset. */
	inline bool IsRewriteTool(const FString& Toolset, const FString& ToolName, bool& bOutIsEdit)
	{
		bOutIsEdit = false;
		if (!Toolset.Equals(DslToolset(), ESearchCase::IgnoreCase)) return false;
		FString Bare = ToolName;
		const FString Prefix = FString(DslToolset()) + TEXT(".");
		if (Bare.StartsWith(Prefix, ESearchCase::IgnoreCase)) Bare.RightChopInline(Prefix.Len());
		if (Bare.Equals(TEXT("Write"), ESearchCase::IgnoreCase)) return true;
		if (Bare.Equals(TEXT("Edit"), ESearchCase::IgnoreCase))
		{
			bOutIsEdit = true;
			return true;
		}
		return false;
	}

	/** One top-level `(event ...)` or `(fn ...)` form the text names. */
	struct FEntry
	{
		FString Kind;
		FString Name;
		bool bTombstone = false;
	};

	/** What the scanner found in a DSL text: its entries and its '; text' runs. */
	struct FScan
	{
		TArray<FEntry> Entries;
		/** Every comment line's text, trimmed and joined by single spaces. */
		FString CommentText;
	};

	inline FString CollapseWhitespace(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		bool bSpace = false;
		for (TCHAR C : In)
		{
			if (FChar::IsWhitespace(C))
			{
				bSpace = !Out.IsEmpty();
				continue;
			}
			if (bSpace) Out.AppendChar(TEXT(' '));
			bSpace = false;
			Out.AppendChar(C);
		}
		return Out;
	}

	/** Read the token at I: a `quoted` name or a bare run up to whitespace or a paren. */
	inline FString ReadName(const FString& Text, int32& I)
	{
		const int32 Len = Text.Len();
		while (I < Len && FChar::IsWhitespace(Text[I])) ++I;
		if (I >= Len) return FString();
		if (Text[I] == TEXT('`'))
		{
			const int32 Start = ++I;
			while (I < Len && Text[I] != TEXT('`')) ++I;
			const FString Name = Text.Mid(Start, I - Start);
			if (I < Len) ++I;
			return Name;
		}
		const int32 Start = I;
		while (I < Len && !FChar::IsWhitespace(Text[I]) && Text[I] != TEXT('(') && Text[I] != TEXT(')')) ++I;
		return Text.Mid(Start, I - Start);
	}

	/** Scan DSL text for entry heads and comment runs. Strings, quoted names and
	 *  comments are skipped so a paren inside them does not move the depth. An
	 *  entry is recognised at any depth, so a fragment (an Edit's newString) that
	 *  carries a head still names its entry; a tombstone is `:!delete` directly
	 *  inside the head's form. */
	inline FScan Scan(const FString& Text)
	{
		FScan Out;
		TArray<FString> Comments;
		const int32 Len = Text.Len();
		int32 Depth = 0;
		// Depth of the entry form currently open, and its index in Entries.
		int32 EntryDepth = -1;
		int32 EntryIndex = INDEX_NONE;
		int32 I = 0;
		while (I < Len)
		{
			const TCHAR C = Text[I];
			if (C == TEXT(';'))
			{
				int32 End = I + 1;
				while (End < Len && Text[End] != TEXT('\n') && Text[End] != TEXT('\r')) ++End;
				const FString Line = Text.Mid(I + 1, End - I - 1).TrimStartAndEnd();
				if (!Line.IsEmpty()) Comments.Add(Line);
				I = End;
				continue;
			}
			if (C == TEXT('"'))
			{
				++I;
				while (I < Len && Text[I] != TEXT('"'))
				{
					if (Text[I] == TEXT('\\')) ++I;
					++I;
				}
				++I;
				continue;
			}
			if (C == TEXT('`'))
			{
				++I;
				while (I < Len && Text[I] != TEXT('`')) ++I;
				++I;
				continue;
			}
			if (C == TEXT('('))
			{
				++Depth;
				int32 J = I + 1;
				const FString Head = ReadName(Text, J);
				if (Head == TEXT("event") || Head == TEXT("fn"))
				{
					FEntry Entry;
					Entry.Kind = Head;
					Entry.Name = ReadName(Text, J);
					EntryIndex = Out.Entries.Add(Entry);
					EntryDepth = Depth;
					I = J;
					continue;
				}
				++I;
				continue;
			}
			if (C == TEXT(')'))
			{
				if (Depth == EntryDepth)
				{
					EntryDepth = -1;
					EntryIndex = INDEX_NONE;
				}
				--Depth;
				++I;
				continue;
			}
			if (C == TEXT(':') && EntryIndex != INDEX_NONE && Depth == EntryDepth
				&& Text.Mid(I, 8).Equals(TEXT(":!delete"), ESearchCase::CaseSensitive))
			{
				Out.Entries[EntryIndex].bTombstone = true;
				I += 8;
				continue;
			}
			++I;
		}
		Out.CommentText = CollapseWhitespace(FString::Join(Comments, TEXT(" ")));
		return Out;
	}

	/** The member an entry name refers to: the last `.` segment, so
	 *  `self.ReceiveBeginPlay` and `SomeInterface.OnThing` read as their member. */
	inline FString MemberOf(const FString& EntryName)
	{
		int32 Dot = INDEX_NONE;
		return EntryName.FindLastChar(TEXT('.'), Dot) ? EntryName.Mid(Dot + 1) : EntryName;
	}

	inline bool IsConstructionScript(const FEntry& Entry)
	{
		return Entry.Kind == TEXT("fn") && MemberOf(Entry.Name).Equals(UEdGraphSchema_K2::FN_UserConstructionScript.ToString(), ESearchCase::IgnoreCase);
	}

	inline UEdGraph* FindGraphNamed(UBlueprint* Blueprint, const FString& Name)
	{
		auto Search = [&Name](const TArray<TObjectPtr<UEdGraph>>& Graphs) -> UEdGraph*
		{
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph && Graph->GetName().Equals(Name, ESearchCase::IgnoreCase)) return Graph;
			}
			return nullptr;
		};
		if (UEdGraph* Found = Search(Blueprint->UbergraphPages)) return Found;
		return Search(Blueprint->FunctionGraphs);
	}

	/** The event graph holding an event entry, or nullptr when no event node in
	 *  any event graph answers to the name (a new custom event, an input event). */
	inline UEdGraph* FindEventGraphFor(UBlueprint* Blueprint, const FString& EntryName)
	{
		const FString Member = MemberOf(EntryName);
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				const UK2Node_Event* Event = Cast<UK2Node_Event>(Node);
				if (!Event) continue;
				if (const UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(Event))
				{
					if (Custom->CustomFunctionName.ToString().Equals(EntryName, ESearchCase::IgnoreCase)
						|| Custom->CustomFunctionName.ToString().Equals(Member, ESearchCase::IgnoreCase))
					{
						return Graph;
					}
					continue;
				}
				if (const UK2Node_ComponentBoundEvent* Bound = Cast<UK2Node_ComponentBoundEvent>(Event))
				{
					if (Bound->DelegatePropertyName.ToString().Equals(Member, ESearchCase::IgnoreCase)) return Graph;
					continue;
				}
				if (Event->EventReference.GetMemberName().ToString().Equals(Member, ESearchCase::IgnoreCase)) return Graph;
			}
		}
		return nullptr;
	}

	inline bool HasExecPin(const UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) return true;
		}
		return false;
	}

	/** Nodes some entry reaches: forward along exec wires from every node with an
	 *  exec output and no exec input (events, function entries, input events),
	 *  then every node without exec pins whose output feeds a reached node. */
	inline TSet<const UEdGraphNode*> ReachedNodes(const UEdGraph* Graph)
	{
		TSet<const UEdGraphNode*> Reached;
		TArray<const UEdGraphNode*> Queue;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || Node->IsA<UEdGraphNode_Comment>()) continue;
			if (HasExecPin(Node, EGPD_Output) && !HasExecPin(Node, EGPD_Input))
			{
				Reached.Add(Node);
				Queue.Add(Node);
			}
		}
		while (Queue.Num() > 0)
		{
			const UEdGraphNode* Node = Queue.Pop();
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					const UEdGraphNode* Next = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
					if (Next && !Reached.Contains(Next))
					{
						Reached.Add(Next);
						Queue.Add(Next);
					}
				}
			}
		}
		// Pure nodes, to a fixed point: a pure chain feeding a reached node is used.
		for (bool bGrew = true; bGrew;)
		{
			bGrew = false;
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node || Reached.Contains(Node) || Node->IsA<UEdGraphNode_Comment>()) continue;
				if (HasExecPin(Node, EGPD_Input) || HasExecPin(Node, EGPD_Output)) continue;
				bool bFeeds = false;
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Output) continue;
					for (const UEdGraphPin* Linked : Pin->LinkedTo)
					{
						const UEdGraphNode* Next = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
						if (Next && Reached.Contains(Next))
						{
							bFeeds = true;
							break;
						}
					}
					if (bFeeds) break;
				}
				if (bFeeds)
				{
					Reached.Add(Node);
					bGrew = true;
				}
			}
		}
		return Reached;
	}

	/** True when the comment box's area holds a reached node, which is the only
	 *  kind of box Read can write back as a '; text' run. */
	inline bool CoversReachedNode(const UEdGraphNode_Comment* Comment, const TSet<const UEdGraphNode*>& Reached)
	{
		const int32 Left = Comment->NodePosX;
		const int32 Top = Comment->NodePosY;
		const int32 Right = Left + Comment->NodeWidth;
		const int32 Bottom = Top + Comment->NodeHeight;
		for (const UEdGraphNode* Node : Reached)
		{
			if (Node->NodePosX >= Left && Node->NodePosX <= Right && Node->NodePosY >= Top && Node->NodePosY <= Bottom) return true;
		}
		return false;
	}

	inline TSharedPtr<FJsonObject> NodeRow(const UEdGraph* Graph, const UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("graph"), Graph->GetName());
		Row->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
		Row->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
		Row->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		return Row;
	}

	/** What rewriting a Blueprint with Text would delete. bIsEdit reads Text as
	 *  the replacement strings of an Edit, whose comment runs live in the session
	 *  text rather than in the edit, so a comment box counts as lost only when
	 *  Read could not have written it (it covers no reached node). */
	inline TSharedPtr<FJsonObject> Analyze(UBlueprint* Blueprint, const FString& Text, const FString& Scope, bool bIsEdit)
	{
		const FScan Found = Scan(Text);
		TSharedPtr<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("mode"), bIsEdit ? TEXT("edit") : TEXT("write"));
		Report->SetStringField(TEXT("scope"), Scope);

		// The construction script cannot be brought back by any Epic tool once
		// tombstoned: AddFunctionGraph of the name makes an ordinary function.
		bool bConstructionTombstone = false;
		for (const FEntry& Entry : Found.Entries)
		{
			if (Entry.bTombstone && IsConstructionScript(Entry)) bConstructionTombstone = true;
		}
		Report->SetBoolField(TEXT("constructionScriptTombstone"), bConstructionTombstone);

		// Which graphs the rewrite touches. A scope names one; an empty scope
		// touches the graph of every entry the text names, and every event and
		// function graph when the text names none it can place.
		TArray<UEdGraph*> Targets;
		TArray<TSharedPtr<FJsonValue>> Unplaced;
		auto AddAllEventGraphs = [&Targets, Blueprint]()
		{
			for (UEdGraph* Graph : Blueprint->UbergraphPages) if (Graph) Targets.AddUnique(Graph);
		};
		FString ScopeGraph = Scope;
		int32 Slash = INDEX_NONE;
		if (ScopeGraph.FindChar(TEXT('/'), Slash)) ScopeGraph.LeftInline(Slash);
		ScopeGraph.TrimStartAndEndInline();
		if (!ScopeGraph.IsEmpty())
		{
			if (UEdGraph* Graph = FindGraphNamed(Blueprint, ScopeGraph)) Targets.Add(Graph);
		}
		else
		{
			for (const FEntry& Entry : Found.Entries)
			{
				if (Entry.bTombstone) continue;
				if (Entry.Kind == TEXT("fn"))
				{
					if (UEdGraph* Graph = FindGraphNamed(Blueprint, MemberOf(Entry.Name))) Targets.AddUnique(Graph);
					else if (UEdGraph* Full = FindGraphNamed(Blueprint, Entry.Name)) Targets.AddUnique(Full);
					continue;
				}
				if (UEdGraph* Graph = FindEventGraphFor(Blueprint, Entry.Name))
				{
					Targets.AddUnique(Graph);
					continue;
				}
				Unplaced.Add(MakeShared<FJsonValueString>(Entry.Name));
				AddAllEventGraphs();
			}
			if (Found.Entries.Num() == 0)
			{
				AddAllEventGraphs();
				for (UEdGraph* Graph : Blueprint->FunctionGraphs) if (Graph) Targets.AddUnique(Graph);
			}
		}
		if (Unplaced.Num() > 0) Report->SetArrayField(TEXT("unplacedEntries"), Unplaced);

		const FString DslComments = Found.CommentText;
		TArray<TSharedPtr<FJsonValue>> GraphNames, Loose, Comments, Collapsed;
		for (UEdGraph* Graph : Targets)
		{
			GraphNames.Add(MakeShared<FJsonValueString>(Graph->GetName()));
			const TSet<const UEdGraphNode*> Reached = ReachedNodes(Graph);
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
				{
					const FString BoxText = CollapseWhitespace(Comment->NodeComment);
					const bool bKept = bIsEdit
						? CoversReachedNode(Comment, Reached)
						: (!BoxText.IsEmpty() && DslComments.Contains(BoxText, ESearchCase::CaseSensitive));
					if (!bKept)
					{
						TSharedPtr<FJsonObject> Row = NodeRow(Graph, Node);
						Row->SetStringField(TEXT("text"), Comment->NodeComment);
						Comments.Add(MakeShared<FJsonValueObject>(Row));
					}
					continue;
				}
				if (!Reached.Contains(Node))
				{
					Loose.Add(MakeShared<FJsonValueObject>(NodeRow(Graph, Node)));
					continue;
				}
				if (Node->IsA<UK2Node_Composite>())
				{
					Collapsed.Add(MakeShared<FJsonValueObject>(NodeRow(Graph, Node)));
				}
			}
		}
		Report->SetArrayField(TEXT("graphs"), GraphNames);
		Report->SetArrayField(TEXT("looseNodes"), Loose);
		Report->SetArrayField(TEXT("commentBoxes"), Comments);
		Report->SetArrayField(TEXT("collapsedGraphs"), Collapsed);
		const bool bDestructive = Loose.Num() + Comments.Num() + Collapsed.Num() > 0;
		Report->SetBoolField(TEXT("destructive"), bDestructive);
		return Report;
	}

	/** How to go on from a destructive preflight: opt in, or keep what would go. */
	inline FString WayForward()
	{
		return TEXT("Pass allowDestructive=true to apply the rewrite and accept these losses. To keep them instead: wire each loose node "
			"into an entry's exec chain, or remove it on purpose with blueprint(delete_node); write each comment box as a '; text' "
			"line above the statements it covers; and leave an entry holding a collapsed graph out of the document, or narrow scope "
			"to another graph. blueprint(preflight_dsl_write) runs this check without writing.");
	}

	inline FString ConstructionScriptWayForward()
	{
		return TEXT("A (fn UserConstructionScript :!delete) tombstone removes the construction script for good: no Epic tool brings it "
			"back, and adding a function of that name makes an ordinary function. It is refused whatever allowDestructive says. "
			"To empty the construction script, write (fn `UserConstructionScript`) with no statements instead.");
	}

	/** One field of a JSON object read case-insensitively, as the engine reads tool input. */
	inline TSharedPtr<FJsonValue> FieldIgnoringCase(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		if (!Object.IsValid()) return nullptr;
		for (const auto& Entry : Object->Values)
		{
			const TPair<FString, TSharedPtr<FJsonValue>> Pair(Entry.Key, Entry.Value);
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase)) return Pair.Value;
		}
		return nullptr;
	}

	inline FString StringFieldIgnoringCase(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		const TSharedPtr<FJsonValue> Value = FieldIgnoringCase(Object, Name);
		FString Out;
		if (Value.IsValid()) Value->TryGetString(Out);
		return Out;
	}

	/** The text a Write or Edit input carries: Write's dsl, or every Edit
	 *  replacement's oldString and newString joined by newlines. */
	inline FString InputText(const TSharedPtr<FJsonObject>& Input, bool bIsEdit)
	{
		if (!bIsEdit) return StringFieldIgnoringCase(Input, TEXT("dsl"));
		const TSharedPtr<FJsonValue> Edits = FieldIgnoringCase(Input, TEXT("edits"));
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Edits.IsValid() || !Edits->TryGetArray(Rows) || !Rows) return FString();
		TArray<FString> Parts;
		for (const TSharedPtr<FJsonValue>& Row : *Rows)
		{
			const TSharedPtr<FJsonObject>* Edit = nullptr;
			if (!Row.IsValid() || !Row->TryGetObject(Edit) || !Edit) continue;
			Parts.Add(StringFieldIgnoringCase(*Edit, TEXT("oldString")));
			Parts.Add(StringFieldIgnoringCase(*Edit, TEXT("newString")));
		}
		return FString::Join(Parts, TEXT("\n"));
	}

	/** epic(call_tool)'s guard for a DSL Write or Edit. Returns the refusal to
	 *  send, or nullptr when the call may go ahead: the input does not parse or
	 *  names no Blueprint (the tool reports that itself), nothing would be lost,
	 *  or the caller passed allowDestructive. */
	inline TSharedPtr<FJsonValue> GuardCall(const FString& Toolset, const FString& ToolName, const FString& InputJson, bool bAllowDestructive)
	{
		bool bIsEdit = false;
		if (!IsRewriteTool(Toolset, ToolName, bIsEdit)) return nullptr;

		TSharedPtr<FJsonObject> Input;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InputJson);
		if (!FJsonSerializer::Deserialize(Reader, Input) || !Input.IsValid()) return nullptr;
		const FString AssetPath = StringFieldIgnoringCase(Input, TEXT("assetPath"));
		UBlueprint* Blueprint = Cast<UBlueprint>(MCPLoadAssetObject(AssetPath));
		if (!Blueprint) return nullptr;

		const TSharedPtr<FJsonObject> Report = Analyze(Blueprint, InputText(Input, bIsEdit), StringFieldIgnoringCase(Input, TEXT("scope")), bIsEdit);
		Report->SetStringField(TEXT("assetPath"), AssetPath);
		if (Report->GetBoolField(TEXT("constructionScriptTombstone")))
		{
			TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(
				TEXT("Refused before %s.%s ran: the document deletes the construction script of '%s'. %s Nothing was changed."),
				*Toolset, bIsEdit ? TEXT("Edit") : TEXT("Write"), *AssetPath, *ConstructionScriptWayForward()));
			Error->SetStringField(TEXT("reason"), TEXT("construction_script_tombstone"));
			Error->SetObjectField(TEXT("preflight"), Report);
			return MakeShared<FJsonValueObject>(Error);
		}
		if (!Report->GetBoolField(TEXT("destructive")) || bAllowDestructive) return nullptr;

		TSharedPtr<FJsonObject> Error = MCPErrorObject(FString::Printf(
			TEXT("Refused before %s.%s ran: rewriting '%s' would delete %d loose node(s), %d comment box(es) and flatten %d collapsed graph(s) "
			     "that the text does not carry; preflight lists each one. %s Nothing was changed."),
			*Toolset, bIsEdit ? TEXT("Edit") : TEXT("Write"), *AssetPath,
			Report->GetArrayField(TEXT("looseNodes")).Num(), Report->GetArrayField(TEXT("commentBoxes")).Num(),
			Report->GetArrayField(TEXT("collapsedGraphs")).Num(), *WayForward()));
		Error->SetStringField(TEXT("reason"), TEXT("dsl_rewrite_destructive"));
		Error->SetObjectField(TEXT("preflight"), Report);
		return MakeShared<FJsonValueObject>(Error);
	}
}
