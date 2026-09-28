#pragma once

// The F6 component-template add as a Blueprint edit: one component of a fixed
// class on a Blueprint's construction script, idempotent by class, committed
// through the F1 Blueprint traits. add_state_tree_component,
// add_smart_object_component and add_ability_system_component are this with
// their class and default name. Shared by the Gameplay and GAS handlers.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "Families/MCPWorldSpawn.h"
#include "Handlers/Blueprint/BlueprintEditTraits.h"

namespace MCPBlueprintComponent
{
	/**
	 * Add a ClassPath component named componentName (DefaultName when the call
	 * has none) to the Blueprint at blueprintPath, unless one of that class is
	 * already there. Returns blueprintPath, componentName, componentClass,
	 * created/existed and the remove_component rollback, compiled and saved.
	 */
	inline TSharedPtr<FJsonValue> AddOfClass(const TSharedPtr<FJsonObject>& Params, const TCHAR* Action, const TCHAR* ClassPath, const TCHAR* DefaultName, const TCHAR* PluginHint)
	{
		FString BPPath;
		if (auto Err = RequireString(Params, TEXT("blueprintPath"), BPPath)) return Err;
		const FString Name = OptionalString(Params, TEXT("componentName"), DefaultName);

		FMCPEditScope Edit(Params, Action, TEXT("blueprintPath"), MCPBlueprint::PlainEdit());
		// A missing asset keeps this category's load error.
		if (auto Err = Edit.Open<UBlueprint>()) return LoadAssetByPath<UBlueprint>(BPPath) ? Err : MCPAssetLoadError(BPPath, TEXT("UBlueprint"));
		UBlueprint* BP = Edit.Target<UBlueprint>();

		UClass* CompClass = FindObject<UClass>(nullptr, ClassPath);
		if (!CompClass)
		{
			FString ClassName(ClassPath);
			int32 Dot = INDEX_NONE;
			if (ClassName.FindLastChar(TEXT('.'), Dot)) ClassName.RightChopInline(Dot + 1);
			return MCPError(FString::Printf(TEXT("%s not found. %s"), *ClassName, PluginHint));
		}
		if (auto Err = MCPWorldSpawn::RequireConstructionScript(BP)) return Err;

		if (USCS_Node* Existing = MCPWorldSpawn::FindTemplateOfClass(BP, CompClass))
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			MCPWorldSpawn::DescribeTemplate(Existed, BPPath, Existing);
			Existed->RemoveField(TEXT("rollback"));
			return MCPResult(Existed);
		}

		if (auto Err = Edit.Begin()) return Err;
		USCS_Node* NewNode = MCPWorldSpawn::AddTemplate(BP, CompClass, FName(*Name));
		if (!NewNode)
		{
			Edit.Cancel();
			return MCPError(FString::Printf(TEXT("Failed to create the %s construction-script node."), *CompClass->GetName()));
		}
		Edit.Commit();

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		MCPWorldSpawn::DescribeTemplate(Result, BPPath, NewNode);
		return Edit.Finish(Result);
	}
}
