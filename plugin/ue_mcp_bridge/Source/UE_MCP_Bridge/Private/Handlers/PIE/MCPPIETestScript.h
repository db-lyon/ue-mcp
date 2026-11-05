#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCPPIETestScript.generated.h"

UENUM(BlueprintType)
enum class EMCPPIEStepJudge : uint8
{
	Auto UMETA(ToolTip="Evaluated each frame against the observed values."),
	Manual UMETA(ToolTip="A person (or the calling agent) gives the verdict.")
};

UENUM(BlueprintType)
enum class EMCPPIEExpectOp : uint8
{
	Decreasing UMETA(ToolTip="Value drops below its value at step start."),
	Increasing UMETA(ToolTip="Value rises above its value at step start."),
	Eq UMETA(DisplayName="Equal", ToolTip="Value equals Target (small epsilon)."),
	Gt UMETA(DisplayName="Greater Than", ToolTip="Value is greater than Target."),
	Lt UMETA(DisplayName="Less Than", ToolTip="Value is less than Target."),
	Visible UMETA(ToolTip="Widget is on screen. Target 0 means not visible."),
	Exists UMETA(ToolTip="Actor resolves in the PIE world. Target 0 means gone.")
};

USTRUCT(BlueprintType)
struct FMCPPIEStepExpect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Expect",
		meta=(ToolTip="Tracked value path (or its last segment), actor id for Exists, widget class or name for Visible."))
	FString Value;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Expect",
		meta=(ToolTip="Comparison the observed value must satisfy."))
	EMCPPIEExpectOp Op = EMCPPIEExpectOp::Eq;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Expect",
		meta=(ToolTip="Whether Target is set. Required for Eq, Gt and Lt; Visible and Exists default to true."))
	bool bHasTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Expect",
		meta=(EditCondition="bHasTarget", ToolTip="Target number. For Visible and Exists, 0 means false and anything else true."))
	double Target = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Expect",
		meta=(ClampMin="0", ToolTip="Milliseconds of game time the condition has to hold within. 0 uses the script default."))
	int32 WithinMs = 0;
};

USTRUCT(BlueprintType)
struct FMCPPIETestStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step",
		meta=(MultiLine="true", ToolTip="What the tester does in this step."))
	FString Instruction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step",
		meta=(ToolTip="Auto steps are judged from observed values; manual steps wait for a verdict."))
	EMCPPIEStepJudge Judge = EMCPPIEStepJudge::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step",
		meta=(ToolTip="Whether this step carries an expectation. An auto step without one passes as soon as it starts."))
	bool bHasExpect = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step",
		meta=(EditCondition="bHasExpect", ToolTip="The condition an auto step waits for."))
	FMCPPIEStepExpect Expect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step",
		meta=(EditCondition="Judge == EMCPPIEStepJudge::Manual", ToolTip="Whether the calling agent may give this manual step's verdict. Off means only a person can."))
	bool bAgentJudgeable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step",
		meta=(ToolTip="The expected value, and the actors arrange and act name, only resolve once PIE runs, such as a spawned actor, so authoring does not check them against the level."))
	bool bResolveInPIE = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step",
		meta=(MultiLine="true", ToolTip="JSON array of arrange entries run when the step starts: {kind: spawn|set|call, ...}, as a scenario's arrange."))
	FString ArrangeJson;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step",
		meta=(MultiLine="true", ToolTip="JSON array of step actions run after arrange, before the expectation is watched: tape, press, hold, inject, console, place, capture, wait_ms, as pie(uat_run) takes them."))
	FString ActJson;
};

UCLASS(BlueprintType)
class UMCPPIETestScript : public UDataAsset
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Script",
		meta=(MultiLine="true", ToolTip="What this script tests."))
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Script",
		meta=(ToolTip="Observation profile asset path. Optional: the values the steps reference are observed without one."))
	FString Profile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AssetRegistrySearchable, Category="Script",
		meta=(ToolTip="Level the script plays in, e.g. /Game/Maps/L_Arena. Optional: a run opens it before PIE; empty plays whatever level is open."))
	FString Map;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Script",
		meta=(TitleProperty="Instruction", ToolTip="Steps, run in order."))
	TArray<FMCPPIETestStep> Steps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Script",
		meta=(ClampMin="1", ToolTip="Default time window (ms of game time) for an auto step whose expectation sets none."))
	int32 DefaultWithinMs = 5000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Script",
		meta=(ToolTip="Every step is judged by a verdict. Without it a script needs at least one auto step with an expectation."))
	bool bManualOnly = false;
};
