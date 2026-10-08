// Helpers shared by the PCG handler files. The module is a unity build, so a
// helper used by more than one PCGHandlers_*.cpp lives here, not as copies.
#pragma once

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "PCGGraph.h"
#include "PCGNode.h"

namespace MCPPCG
{
	/** A node by engine name. UPCGGraph::GetNodes() leaves out the graph's input and output nodes, so they are checked too (#1324). */
	inline UPCGNode* FindNode(UPCGGraph* Graph, const FString& Name)
	{
		if (!Graph || Name.IsEmpty()) return nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node->GetName() == Name) return Node;
		}
		if (UPCGNode* In = Graph->GetInputNode(); In && In->GetName() == Name) return In;
		if (UPCGNode* Out = Graph->GetOutputNode(); Out && Out->GetName() == Name) return Out;
		return nullptr;
	}

	/** Whether Node is the graph's input or output node, which the graph owns and no handler may remove. */
	inline bool IsGraphIONode(const UPCGGraph* Graph, const UPCGNode* Node)
	{
		return Graph && Node && (Node == Graph->GetInputNode() || Node == Graph->GetOutputNode());
	}

	/** " - did you mean 'DefaultInputNode'?" for the names callers guess for the input/output nodes (#239), else empty. */
	inline FString IONodeNameHint(const UPCGGraph* Graph, const FString& Name)
	{
		if (!Graph) return FString();
		auto Hint = [](const UPCGNode* Node)
		{
			return Node ? FString::Printf(TEXT(" - did you mean '%s'?"), *Node->GetName()) : FString();
		};
		for (const TCHAR* Guess : { TEXT("Input"), TEXT("InputNode"), TEXT("GraphInput") })
		{
			if (Name.Equals(Guess, ESearchCase::IgnoreCase)) return Hint(Graph->GetInputNode());
		}
		for (const TCHAR* Guess : { TEXT("Output"), TEXT("OutputNode"), TEXT("GraphOutput") })
		{
			if (Name.Equals(Guess, ESearchCase::IgnoreCase)) return Hint(Graph->GetOutputNode());
		}
		return FString();
	}

	/** "<Role> not found: <name>", with the input/output hint when the name is a guess at one. */
	inline TSharedPtr<FJsonValue> NodeNotFoundError(const UPCGGraph* Graph, const FString& Name, const TCHAR* Role = TEXT("Node"))
	{
		const FString Hint = IONodeNameHint(Graph, Name);
		return MCPError(FString::Printf(TEXT("%s not found: %s%s"), Role, *Name,
			Hint.IsEmpty() ? TEXT(". read_graph lists the node names.") : *Hint));
	}
}
