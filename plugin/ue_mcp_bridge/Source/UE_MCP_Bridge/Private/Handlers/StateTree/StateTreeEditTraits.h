#pragma once

// What a StateTree asset declares to the F1 edit scope: how it is opened, how
// an edit settles, how it compiles, what it saves and how the result says so.
// Shared by every StateTree translation unit, which a unity build may merge.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "MCPEngineCompat.h"
#include "Families/MCPEditScope.h"

#if UE_MCP_HAS_5_5_API

#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"

namespace MCPStateTree
{
	inline FString Guid(const FGuid& Value)
	{
		return Value.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	inline FGuid ParseGuid(const FString& Text)
	{
		FGuid Out;
		FGuid::Parse(Text, Out);
		return Out;
	}

	inline UStateTreeEditorData* EditorData(const UStateTree* Tree)
	{
		return Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
	}

	// #833: a tree with no editor data reads like a broken action unless the
	// message names the one call that repairs it.
	inline FString MissingEditorDataMessage(const FString& AssetPath)
	{
		return FString::Printf(
			TEXT("StateTree '%s' has no editor data, so it has no schema, no states, and cannot compile. ")
			TEXT("A tree created through the generic asset(create_asset_by_class) route lands in this state. ")
			TEXT("Repair it with statetree(set_schema, assetPath=\"%s\"), which attaches the editor data, a schema ")
			TEXT("and a root state, or create trees with gameplay(create_state_tree), which does that up front."),
			*AssetPath, *AssetPath);
	}

	/** The default open check: every authoring action needs editor data. */
	inline TSharedPtr<FJsonValue> NeedEditorData(UStateTree* Tree, const FString& AssetPath)
	{
		return EditorData(Tree) ? nullptr : MCPError(MissingEditorDataMessage(AssetPath));
	}

	/** Compile and validate need a schema too: without one the compiler stops with an empty log. */
	inline TSharedPtr<FJsonValue> NeedSchema(UStateTree* Tree, const FString& AssetPath)
	{
		if (auto Err = NeedEditorData(Tree, AssetPath)) return Err;
		if (EditorData(Tree)->Schema) return nullptr;
		return MCPError(FString::Printf(
			TEXT("StateTree '%s' has no schema, and the compiler cannot build a tree without one ")
			TEXT("(it logs \"does not have a schema\" and stops). Attach one with ")
			TEXT("statetree(set_schema, assetPath=\"%s\")."),
			*AssetPath, *AssetPath));
	}

	/** Compile, writing compiled, errors and warnings from the compiler log. */
	inline bool CompileWithLog(UStateTree* Tree, const TSharedPtr<FJsonObject>& Out)
	{
		FStateTreeCompilerLog Log;
		const bool bSuccess = UStateTreeEditingSubsystem::CompileStateTree(Tree, Log);
		Out->SetBoolField(TEXT("compiled"), bSuccess);

		TArray<TSharedPtr<FJsonValue>> Errors;
		TArray<TSharedPtr<FJsonValue>> Warnings;
#if UE_MCP_HAS_STATETREE_COMPILER_TOKENIZED_MESSAGES
		for (const TSharedRef<FTokenizedMessage>& Message : Log.ToTokenizedMessages())
		{
			const FString Text = Message->ToText().ToString();
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				Errors.Add(MakeShared<FJsonValueString>(Text));
			}
			else if (Message->GetSeverity() == EMessageSeverity::Warning
				|| Message->GetSeverity() == EMessageSeverity::PerformanceWarning)
			{
				Warnings.Add(MakeShared<FJsonValueString>(Text));
			}
		}
#else
		if (!bSuccess)
		{
			Errors.Add(MakeShared<FJsonValueString>(TEXT("CompileStateTree returned failure; detailed compiler diagnostics are not exposed by the UE 5.5 FStateTreeCompilerLog API.")));
		}
#endif
		Out->SetArrayField(TEXT("errors"), Errors);
		Out->SetArrayField(TEXT("warnings"), Warnings);
		return bSuccess;
	}

	/** Value writes, bindings and the palette do not settle; node and state structure does. */
	inline FMCPCommitPolicy ValueEdit() { return FMCPCommitPolicy::Edit(/*bInSettle=*/false); }
	inline FMCPCommitPolicy StructureEdit() { return FMCPCommitPolicy::Edit(/*bInSettle=*/true); }
}

template <>
struct TMCPEditTraits<UStateTree>
{
	static const TCHAR* TypeName() { return TEXT("StateTree"); }

	static TSharedPtr<FJsonValue> CheckOpen(UStateTree* Tree, const FString& AssetPath)
	{
		return MCPStateTree::NeedEditorData(Tree, AssetPath);
	}

	/** The editor's own fixups after a structural change. */
	static void Settle(UStateTree* Tree)
	{
		UStateTreeEditingSubsystem::ValidateStateTree(Tree);
	}

	static bool Compile(UStateTree* Tree, const TSharedPtr<FJsonObject>& Out)
	{
		return MCPStateTree::CompileWithLog(Tree, Out);
	}

	static void SaveSet(UStateTree* Tree, TArray<UObject*>& Out)
	{
		Out.Add(Tree);
	}

	static constexpr EMCPResultDialect Dialect = EMCPResultDialect::SavedSaveError;
};

#endif // UE_MCP_HAS_5_5_API
