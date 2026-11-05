#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Templates/Function.h"
#include "Handlers/PIE/PIEScriptRunner.h"

class UClass;
class UObject;

/**
 * Draft-time checks for test scripts: a step the observer could never read, or
 * whose op and target cannot hold for the value's type. pie_script_validate
 * reports them; pie_script_create and pie_script_update save anyway and return
 * them as warnings, refusing only what cannot be written (bBlocksSave).
 *
 * Values resolve the way the observer resolves them at run time: a profile
 * tracked value (exact path or last segment), else a dotted reflection path on
 * the player pawn (head may name a component), sub:<Subsystem>.<path>, or
 * bone:/bonecs:<bone>.<channel>. exists names an actor in the level, or a class
 * the game mode spawns; visible names a widget class or a widget in a Widget
 * Blueprint. A step with resolve_in_pie is not resolved.
 *
 * The checks are plain functions over a context of resolvers, so the
 * automation tests drive them without a level.
 */
namespace MCPPIE
{
	// One reason a script cannot be checked. Step is INDEX_NONE for the script as a whole.
	struct FScriptStepIssue
	{
		int32 Step = INDEX_NONE;
		// The step field at fault, such as expect.value, or a script param. Empty for the whole step.
		FString Field;
		FString Message;
		TArray<FString> Candidates;
		// The script cannot be written with this issue (a step that does not parse, a missing map).
		// Every other issue is a warning: script_create and script_update save with it.
		bool bBlocksSave = false;
	};

	enum class EScriptValueKind : uint8 { Number, Integer, Boolean, Enum };

	// What the observer would read: the value's type as the checks see it.
	struct FScriptValueInfo
	{
		EScriptValueKind Kind = EScriptValueKind::Number;
		// The C++ type, for messages: float, int32, bool, EMovementMode.
		FString TypeName;
		// Integer only: the representable range.
		double Min = 0.0;
		double Max = 0.0;
		// Enum only: name and value of each entry.
		TArray<TPair<FString, int64>> EnumValues;
	};

	struct FScriptLookup
	{
		bool bFound = false;
		FScriptValueInfo Info;
		// Why it was not found, when it was not.
		FString Message;
		TArray<FString> Candidates;
	};

	struct FScriptValidationContext
	{
		// The script's profile tracked value paths; an expectation naming one (or its last segment) reads it.
		TArray<FString> ProfileValuePaths;
		TFunction<FScriptLookup(const FString& Path)> ResolveValue;
		TFunction<FScriptLookup(const FString& ActorId)> ResolveActor;
		TFunction<FScriptLookup(const FString& Widget)> ResolveWidget;
		// Arrange and act: the Input Action an input entry drives, and the actor class a spawn names.
		TFunction<FScriptLookup(const FString& ActionPath)> ResolveInputAction;
		TFunction<FScriptLookup(const FString& ClassPath)> ResolveActorClass;
	};

	// The Pool entries closest to Needle, best first, ignoring case.
	TArray<FString> RankScriptCandidates(const FString& Needle, const TArray<FString>& Pool, int32 MaxResults = 5);

	// Parses every step, collecting one issue per step that does not parse. Out holds a
	// default step at each failed index, so indices line up with the input.
	bool ParseScriptStepsJsonIssues(const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FScriptStepDef>& Out,
		TArray<FScriptStepIssue>& OutIssues);

	// Resolves each expectation and checks its op and target against the value's type, and each
	// arrange and act entry's actor, actor class and Input Action (unless the step resolves in PIE).
	// Without bManualOnly, a script needs at least one auto step with an expect.
	// SkipSteps are steps that failed to parse: they are not checked, and the auto-step rule is not applied.
	void ValidateScriptSteps(const TArray<FScriptStepDef>& Steps, bool bManualOnly, const FScriptValidationContext& Ctx,
		TArray<FScriptStepIssue>& OutIssues, const TSet<int32>& SkipSteps = TSet<int32>());

	// Static resolution of a dotted reflection path on RootClass, mirroring the observer's walk.
	// Sample is an instance or the CDO; it supplies the concrete class behind object properties.
	FScriptLookup ResolveScriptPathOnClass(UClass* RootClass, const UObject* Sample, const FString& Path);

	// Resolvers for the editor's current level and the given profile (empty for none).
	FScriptValidationContext MakeEditorScriptValidationContext(const FString& ProfilePath);

	// What a script can name for the editor's current level: the player pawn's readable value paths
	// (with their kind and type), level actor labels, classes the game mode spawns, Widget Blueprints
	// under /Game and the profile's tracked values. TargetClass, when given, adds which level actors
	// are of that class and whether it is the player pawn.
	TSharedRef<FJsonObject> ScriptWatchableValuesToJson(const FString& TargetClass, const FString& ProfilePath, int32 Limit);

	TSharedRef<FJsonObject> ScriptIssueToJson(const FScriptStepIssue& Issue);
	TArray<TSharedPtr<FJsonValue>> ScriptIssuesToJson(const TArray<FScriptStepIssue>& Issues);
	// { success: false, errorCode: INVALID_SCRIPT_STEPS, error, errors: [{ step, field, message, candidates? }] }.
	// Lead opens the error text, ahead of the count and the messages.
	TSharedPtr<FJsonObject> ScriptIssuesErrorObject(const TArray<FScriptStepIssue>& Issues,
		const TCHAR* Lead = TEXT("The script cannot be checked as written"));

	// Issues that block a save, and the rest (warnings), each in their original order.
	void SplitScriptIssues(const TArray<FScriptStepIssue>& Issues, TArray<FScriptStepIssue>& OutBlocking,
		TArray<FScriptStepIssue>& OutWarnings);
}
