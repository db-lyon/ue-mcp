// Test-project fixture for the timeline playtest work (#2097): a small arena a
// replay can be measured in. It lives in the test project, never in the bridge
// plugin, because nothing but the tests should carry it.
//
// AMCPProbeArena is the only actor a level needs. On BeginPlay it spawns a
// floor, a physics crate, an AI-controlled chaser and the probe character, and
// hands player 0 the character. The character binds four Input Actions from
// ActionsRoot (created by the probe or the live tests through the bridge):
//
//   IA_ProbeMove      Axis2D. WASD through mapping modifiers, and the left stick.
//   IA_ProbeJump      Boolean.
//   IA_ProbeCharge    Boolean with an action-level Hold trigger: ChargeCount.
//   IA_ProbeThrottle  Axis1D with an action-level Scalar modifier: ThrottleSum.
//
// ChargeCount, ChargeFirstMs and ThrottleSum are what show whether a replay ran
// the action's triggers and modifiers once, twice or not at all.
//
// The autopilot stands in for a person: it presses keys and moves the stick
// through APlayerController::InputKey on a fixed schedule of game time, so its
// input takes the same path as a keyboard and gamepad, mapping modifiers
// included.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Blueprint/UserWidget.h"
#include "MCPProbeArena.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UCLASS()
class AMCPProbeCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AMCPProbeCharacter();

	/** Folder holding IA_ProbeMove, IA_ProbeJump, IA_ProbeCharge, IA_ProbeThrottle and IMC_Probe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe")
	FString ActionsRoot = TEXT("/Game/MCPTest/Probe");

	/** How many times IA_ProbeCharge reached Triggered. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	int32 ChargeCount = 0;

	/** Game ms from the arena's start when IA_ProbeCharge first triggered, or -1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	double ChargeFirstMs = -1.0;

	/** The integral over game time of IA_ProbeThrottle's value while it triggered. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	double ThrottleSum = 0.0;

	/** How many times IA_ProbeJump started. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	int32 JumpCount = 0;

	/** Seconds of game time the autopilot has played, or -1 when it is off. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	double AutopilotSeconds = -1.0;

	/** Start pressing keys and moving the stick on the built-in schedule, as a person would, from StartAtSeconds into it. */
	UFUNCTION(BlueprintCallable, Category = "Probe")
	void StartAutopilot(double StartAtSeconds = 0.0);

	/** Release everything the autopilot holds and stop it. */
	UFUNCTION(BlueprintCallable, Category = "Probe")
	void StopAutopilot();

	/** Press or release one key through the player's input, as a keyboard would. */
	UFUNCTION(BlueprintCallable, Category = "Probe")
	void ProbeKey(FName Key, bool bDown);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;

private:
	void OnMove(const FInputActionValue& Value);
	void OnJumpStarted(const FInputActionValue& Value);
	void OnJumpCompleted(const FInputActionValue& Value);
	void OnCharge(const FInputActionValue& Value);
	void OnThrottle(const FInputActionValue& Value);
	void AddMappingContext();
	void TickAutopilot(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void SetKey(const FKey& Key, bool bDown);
	UInputAction* LoadProbeAction(const TCHAR* Name) const;

	double StartSeconds = 0.0;
	FDelegateHandle PreActorTickHandle;
	TSet<FKey> HeldKeys;
};

/** Moves toward the player pawn every tick and stops within StopDistance. AI-controlled. */
UCLASS()
class AMCPProbeChaser : public ACharacter
{
	GENERATED_BODY()

public:
	AMCPProbeChaser();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe")
	float StopDistance = 200.f;

	virtual void Tick(float DeltaSeconds) override;
};

/** Two buttons in a row; Right moves focus, Enter presses. Clicks counts presses of the first. */
UCLASS()
class UMCPProbeMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Probe")
	int32 Clicks = 0;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void OnFirstClicked();

	UPROPERTY()
	TObjectPtr<class UButton> First;

	UPROPERTY()
	TObjectPtr<class UButton> Second;
};

UCLASS()
class AMCPProbeArena : public AActor
{
	GENERATED_BODY()

public:
	AMCPProbeArena();

	/** Folder the character binds its Input Actions and mapping context from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe")
	FString ActionsRoot = TEXT("/Game/MCPTest/Probe");

	/** Spawn the physics crate in the character's path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe")
	bool bSpawnCrate = true;

	/** Spawn the AI-controlled chaser. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe")
	bool bSpawnChaser = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	TObjectPtr<AMCPProbeCharacter> Character;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	TObjectPtr<AActor> Crate;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	TObjectPtr<AMCPProbeChaser> Chaser;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	TObjectPtr<UMCPProbeMenu> Menu;

	/** Clicks on the menu's first button since it opened, or -1 when it never opened. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	int32 MenuClicks = -1;

	/** Show the menu with UI-only input, its first button focused. */
	UFUNCTION(BlueprintCallable, Category = "Probe")
	void OpenMenu();

	/** Close the menu and return to game-only input. */
	UFUNCTION(BlueprintCallable, Category = "Probe")
	void CloseMenu();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
};
