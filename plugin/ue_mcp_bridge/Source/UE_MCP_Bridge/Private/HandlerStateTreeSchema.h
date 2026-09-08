// Shared StateTree schema resolution and editor-data attachment.
//
// A UStateTree whose EditorData is null, or whose editor data carries no
// schema, cannot compile. UStateTreeEditingSubsystem::CompileStateTree logs
// "The state tree '<path>' does not have a schema." and stops, and every
// consumer afterwards reports the asset as failed to link, so the asset is
// dead the moment it is written.
//
// Two actions have to stand the same editor data up in exactly the same way:
// gameplay(create_state_tree), which makes a new tree, and
// statetree(set_schema), which repairs a tree that arrived without one (the
// generic asset(create_asset_by_class) route writes exactly that shape). The
// module is a unity build, so this lives in a header rather than being copied
// into both translation units.
#pragma once

#include "CoreMinimal.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeSchema.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HandlerUtils.h"

namespace MCPStateTreeSchema
{
	/** The gameplay schemas, in the order a caller who named none would want
	 *  them. Both ship in the GameplayStateTree plugin, which is off by
	 *  default, so neither is guaranteed to exist in a given project. */
	inline const TArray<FString>& PreferredSchemaPaths()
	{
		static const TArray<FString> Paths = {
			TEXT("/Script/GameplayStateTreeModule.StateTreeComponentSchema"),
			TEXT("/Script/GameplayStateTreeModule.StateTreeAIComponentSchema"),
		};
		return Paths;
	}

	/** Load the modules that own the preferred schemas, when the project has
	 *  them at all. ModuleExists is asked first because LoadModule on a module
	 *  no enabled plugin provides warns, and "the plugin is off" is a normal
	 *  state here rather than a fault. */
	inline void LoadSchemaModules()
	{
		static const TCHAR* ModuleNames[] = { TEXT("GameplayStateTreeModule") };
		for (const TCHAR* ModuleName : ModuleNames)
		{
			if (FModuleManager::Get().ModuleExists(ModuleName) &&
				!FModuleManager::Get().IsModuleLoaded(FName(ModuleName)))
			{
				FModuleManager::Get().LoadModule(FName(ModuleName));
			}
		}
	}

	/** Every concrete schema class a tree could actually use, sorted by path.
	 *  Sorted because the pick has to be the same on two runs of the same
	 *  editor: TObjectIterator order is load order, so an unsorted "first hit"
	 *  changes with which plugins happened to load first. Test-suite schemas
	 *  are excluded; they exist only to be compiled against in engine tests. */
	inline TArray<UClass*> ConcreteSchemas()
	{
		TArray<UClass*> Out;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (!Candidate->IsChildOf(UStateTreeSchema::StaticClass())) continue;
			if (Candidate == UStateTreeSchema::StaticClass()) continue;
			if (Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
			if (Candidate->GetName().Contains(TEXT("Test"))) continue;
			Out.Add(Candidate);
		}
		Out.Sort([](const UClass& A, const UClass& B)
		{
			return A.GetPathName() < B.GetPathName();
		});
		return Out;
	}

	inline TArray<TSharedPtr<FJsonValue>> ConcreteSchemaPathsJson()
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (UClass* Candidate : ConcreteSchemas())
		{
			Out.Add(MakeShared<FJsonValueString>(Candidate->GetPathName()));
		}
		return Out;
	}

	/** What Resolve() decided, and why, so the caller is told which schema its
	 *  asset ended up with instead of having to read the asset back. */
	struct FResolution
	{
		UClass* SchemaClass = nullptr;
		FString Requested;
		/** requested | preferred | fallback */
		FString Source;
		FString Note;
	};

	/** Resolve the schema to attach. A named schema either resolves or fails:
	 *  quietly substituting something else for a name the caller chose would
	 *  hand back an asset that is not the one asked for. With no name, the
	 *  gameplay schemas win, and only when the project has none of them does
	 *  an arbitrary concrete schema get used - said out loud in Note, because
	 *  "some schema" and "the component schema" are not interchangeable. */
	inline FResolution Resolve(const FString& Requested)
	{
		LoadSchemaModules();

		FResolution Res;
		Res.Requested = Requested;

		if (!Requested.IsEmpty())
		{
			Res.SchemaClass = MCPResolveClassOfType(Requested, UStateTreeSchema::StaticClass());
			Res.Source = TEXT("requested");
			return Res;
		}

		// Non-loading: the owning module was force-loaded above if this project
		// has it at all, so a miss here means "the plugin is off" rather than
		// "not loaded yet" - and asking to LOAD an absent /Script path logs a
		// warning for a condition that is normal.
		for (const FString& Path : PreferredSchemaPaths())
		{
			if (UClass* Found = MCPResolveClassOfType(Path, UStateTreeSchema::StaticClass(), /*bAllowLoad*/ false))
			{
				Res.SchemaClass = Found;
				Res.Source = TEXT("preferred");
				return Res;
			}
		}

		const TArray<UClass*> Candidates = ConcreteSchemas();
		if (Candidates.Num() > 0)
		{
			Res.SchemaClass = Candidates[0];
			Res.Source = TEXT("fallback");
			Res.Note = FString::Printf(
				TEXT("Neither gameplay StateTree schema is present in this project, so '%s' was used and the tree ")
				TEXT("compiles against that instead. StateTreeComponentSchema and StateTreeAIComponentSchema ship in ")
				TEXT("the GameplayStateTree plugin, which is disabled by default: enable it with ")
				TEXT("project(enable_plugin, pluginName=\"GameplayStateTree\"), restart the editor, then pass ")
				TEXT("schema=\"/Script/GameplayStateTreeModule.StateTreeComponentSchema\"."),
				*Candidates[0]->GetPathName());
		}
		return Res;
	}

	/** "No schema resolved", with the list of schemas that do exist, so the
	 *  caller can name one without a second round trip. */
	inline TSharedPtr<FJsonValue> UnresolvedError(const FResolution& Res)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("reason"), TEXT("statetree_schema_unavailable"));
		Obj->SetStringField(TEXT("requested"), Res.Requested);
		Obj->SetArrayField(TEXT("availableSchemas"), ConcreteSchemaPathsJson());
		if (!Res.Requested.IsEmpty())
		{
			Obj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("StateTree schema class not found: '%s'. Pass 'schema' as a /Script/<Module>.<SchemaClass> path. ")
				TEXT("availableSchemas lists the concrete schemas loaded in this editor."),
				*Res.Requested));
		}
		else
		{
			Obj->SetStringField(TEXT("error"),
				TEXT("This editor has no concrete UStateTreeSchema class, and a StateTree without a schema cannot ")
				TEXT("compile. Enable the GameplayStateTree plugin with ")
				TEXT("project(enable_plugin, pluginName=\"GameplayStateTree\") and restart the editor, which brings in ")
				TEXT("StateTreeComponentSchema and StateTreeAIComponentSchema."));
		}
		return MakeShared<FJsonValueObject>(Obj);
	}

	/** Stand up the editor data a StateTree needs to be authorable and
	 *  compilable: the UStateTreeEditorData itself, its schema, and a root
	 *  state to hang states off. Existing editor data is kept - only the
	 *  schema is replaced - so repairing a tree does not discard its states. */
	struct FAttachOutcome
	{
		bool bCreatedEditorData = false;
		bool bCreatedRootState = false;
		FString PreviousSchemaPath;
		bool bSchemaChanged = false;
	};

	inline FAttachOutcome AttachSchema(UStateTree* StateTree, UClass* SchemaClass)
	{
		FAttachOutcome Out;
		check(StateTree && SchemaClass);

		UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
		if (!EditorData)
		{
			EditorData = NewObject<UStateTreeEditorData>(
				StateTree, UStateTreeEditorData::StaticClass(), FName(), RF_Transactional);
			StateTree->EditorData = EditorData;
			Out.bCreatedEditorData = true;
		}

		if (EditorData->Schema)
		{
			Out.PreviousSchemaPath = EditorData->Schema->GetClass()->GetPathName();
		}
		if (!EditorData->Schema || EditorData->Schema->GetClass() != SchemaClass)
		{
			EditorData->Schema = NewObject<UStateTreeSchema>(EditorData, SchemaClass, FName(), RF_Transactional);
			Out.bSchemaChanged = true;
		}

		if (EditorData->SubTrees.Num() == 0)
		{
			EditorData->AddRootState();
			Out.bCreatedRootState = true;
		}

		return Out;
	}
}
