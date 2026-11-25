#include "MCPProbeArena.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// One entry of the autopilot's schedule: a key held over [Start, End) seconds of
	// game time, repeated every Period seconds.
	struct FProbeKeySpan
	{
		const TCHAR* Key;
		double Start;
		double End;
	};

	constexpr double ProbePeriod = 15.0;

	const FProbeKeySpan ProbeKeySpans[] = {
		{ TEXT("W"), 0.5, 2.5 },
		{ TEXT("D"), 2.0, 3.5 },
		{ TEXT("SpaceBar"), 3.0, 3.1 },
		{ TEXT("Q"), 4.0, 4.8 },
		{ TEXT("E"), 5.0, 6.5 },
		{ TEXT("SpaceBar"), 9.0, 9.1 },
		{ TEXT("S"), 12.0, 13.5 },
		{ TEXT("A"), 13.0, 14.0 },
	};

	// The stick circles over [6.5, 11.5): an analog input whose value changes every frame.
	constexpr double StickStart = 6.5;
	constexpr double StickEnd = 11.5;

	// Spawned actors are named, so a recording's track_actors and a script's values can name them.
	AStaticMeshActor* SpawnBox(UWorld* World, const TCHAR* Name, const FVector& Location, const FVector& Scale, bool bPhysics)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		FActorSpawnParameters Params;
		Params.Name = MakeUniqueObjectName(World->PersistentLevel, AStaticMeshActor::StaticClass(), FName(Name));
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Box = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, Params);
		if (!Box)
		{
			return nullptr;
		}
#if WITH_EDITOR
		Box->SetActorLabel(Name);
#endif
		UStaticMeshComponent* Mesh = Box->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Box->SetActorScale3D(Scale);
		if (bPhysics)
		{
			Mesh->SetSimulatePhysics(true);
		}
		return Box;
	}
}

// ── Character ──────────────────────────────────────────────────────────

AMCPProbeCharacter::AMCPProbeCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
}

UInputAction* AMCPProbeCharacter::LoadProbeAction(const TCHAR* Name) const
{
	return LoadObject<UInputAction>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *ActionsRoot, Name, Name));
}

void AMCPProbeCharacter::BeginPlay()
{
	Super::BeginPlay();
	StartSeconds = GetWorld()->GetTimeSeconds();
}

void AMCPProbeCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	StopAutopilot();
	Super::EndPlay(Reason);
}

void AMCPProbeCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	AddMappingContext();
}

void AMCPProbeCharacter::AddMappingContext()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Sub = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *FString::Printf(TEXT("%s/IMC_Probe.IMC_Probe"), *ActionsRoot));
	if (Sub && IMC)
	{
		Sub->AddMappingContext(IMC, 0);
	}
}

void AMCPProbeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		return;
	}
	if (UInputAction* Move = LoadProbeAction(TEXT("IA_ProbeMove")))
	{
		EIC->BindAction(Move, ETriggerEvent::Triggered, this, &AMCPProbeCharacter::OnMove);
	}
	if (UInputAction* JumpAction = LoadProbeAction(TEXT("IA_ProbeJump")))
	{
		EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &AMCPProbeCharacter::OnJumpStarted);
		EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &AMCPProbeCharacter::OnJumpCompleted);
	}
	if (UInputAction* Charge = LoadProbeAction(TEXT("IA_ProbeCharge")))
	{
		EIC->BindAction(Charge, ETriggerEvent::Triggered, this, &AMCPProbeCharacter::OnCharge);
	}
	if (UInputAction* Throttle = LoadProbeAction(TEXT("IA_ProbeThrottle")))
	{
		EIC->BindAction(Throttle, ETriggerEvent::Triggered, this, &AMCPProbeCharacter::OnThrottle);
	}
}

void AMCPProbeCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	AddMovementInput(FVector::ForwardVector, V.Y);
	AddMovementInput(FVector::RightVector, V.X);
}

void AMCPProbeCharacter::OnJumpStarted(const FInputActionValue&)
{
	++JumpCount;
	Jump();
}

void AMCPProbeCharacter::OnJumpCompleted(const FInputActionValue&)
{
	StopJumping();
}

void AMCPProbeCharacter::OnCharge(const FInputActionValue&)
{
	++ChargeCount;
	if (ChargeFirstMs < 0.0)
	{
		ChargeFirstMs = (GetWorld()->GetTimeSeconds() - StartSeconds) * 1000.0;
	}
}

void AMCPProbeCharacter::OnThrottle(const FInputActionValue& Value)
{
	ThrottleSum += static_cast<double>(Value.Get<float>()) * GetWorld()->GetDeltaSeconds();
}

void AMCPProbeCharacter::StartAutopilot(double StartAtSeconds)
{
	if (PreActorTickHandle.IsValid())
	{
		return;
	}
	AutopilotSeconds = FMath::Max(0.0, StartAtSeconds);
	// Before the actors tick, so a key set here is in the player's input when the controller processes it this frame.
	PreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(this, &AMCPProbeCharacter::TickAutopilot);
}

void AMCPProbeCharacter::StopAutopilot()
{
	if (PreActorTickHandle.IsValid())
	{
		FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
		PreActorTickHandle.Reset();
	}
	for (const FKey& Key : HeldKeys.Array())
	{
		SetKey(Key, false);
	}
	HeldKeys.Reset();
	AutopilotSeconds = -1.0;
}

void AMCPProbeCharacter::ProbeKey(FName Key, bool bDown)
{
	SetKey(FKey(Key), bDown);
}

void AMCPProbeCharacter::SetKey(const FKey& Key, bool bDown)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || bDown == HeldKeys.Contains(Key))
	{
		return;
	}
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	if (bDown)
	{
		HeldKeys.Add(Key);
	}
	else
	{
		HeldKeys.Remove(Key);
	}
}

void AMCPProbeCharacter::TickAutopilot(UWorld* World, ELevelTick, float DeltaSeconds)
{
	if (World != GetWorld() || AutopilotSeconds < 0.0)
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}
	AutopilotSeconds += DeltaSeconds;
	const double T = FMath::Fmod(AutopilotSeconds, ProbePeriod);

	TSet<FKey> Wanted;
	for (const FProbeKeySpan& Span : ProbeKeySpans)
	{
		if (T >= Span.Start && T < Span.End)
		{
			Wanted.Add(FKey(Span.Key));
		}
	}
	for (const FProbeKeySpan& Span : ProbeKeySpans)
	{
		const FKey Key(Span.Key);
		SetKey(Key, Wanted.Contains(Key));
	}

	// Analog axes are read once per frame, so the stick is fed every frame it is off centre.
	if (T >= StickStart && T < StickEnd)
	{
		const double Phase = 2.0 * PI * (T - StickStart) / (StickEnd - StickStart);
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_LeftX, IE_Axis, static_cast<float>(0.8 * FMath::Sin(Phase))));
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_LeftY, IE_Axis, static_cast<float>(0.8 * FMath::Cos(Phase))));
	}
}

// ── Chaser ─────────────────────────────────────────────────────────────

AMCPProbeChaser::AMCPProbeChaser()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	GetCharacterMovement()->MaxWalkSpeed = 300.f;
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void AMCPProbeChaser::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player || Player == this)
	{
		return;
	}
	FVector ToPlayer = Player->GetActorLocation() - GetActorLocation();
	ToPlayer.Z = 0.0;
	if (ToPlayer.Size() > StopDistance)
	{
		AddMovementInput(ToPlayer.GetSafeNormal(), 1.f);
	}
}

// ── Menu ───────────────────────────────────────────────────────────────

TSharedRef<SWidget> UMCPProbeMenu::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
		WidgetTree->RootWidget = Row;
		First = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("First"));
		Second = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Second"));
		for (UButton* Button : { First.Get(), Second.Get() })
		{
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Label->SetText(FText::FromString(Button == First ? TEXT("First") : TEXT("Second")));
			Button->AddChild(Label);
			Row->AddChildToHorizontalBox(Button);
		}
	}
	return Super::RebuildWidget();
}

void UMCPProbeMenu::NativeConstruct()
{
	Super::NativeConstruct();
	if (First)
	{
		First->OnClicked.AddUniqueDynamic(this, &UMCPProbeMenu::OnFirstClicked);
	}
}

void UMCPProbeMenu::OnFirstClicked()
{
	++Clicks;
}

// ── Arena ──────────────────────────────────────────────────────────────

AMCPProbeArena::AMCPProbeArena()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMCPProbeArena::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	const FVector Origin = GetActorLocation();
	SpawnBox(World, TEXT("ProbeFloor"), Origin + FVector(0.0, 0.0, -50.0), FVector(100.0, 100.0, 1.0), false);
	if (bSpawnCrate)
	{
		Crate = SpawnBox(World, TEXT("ProbeCrate"), Origin + FVector(500.0, 0.0, 40.0), FVector(0.6, 0.6, 0.6), true);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.Name = MakeUniqueObjectName(World->PersistentLevel, AMCPProbeCharacter::StaticClass(), TEXT("ProbeCharacter"));
	// Deferred, so the character knows where its actions are before it binds them.
	Character = World->SpawnActorDeferred<AMCPProbeCharacter>(AMCPProbeCharacter::StaticClass(),
		FTransform(Origin + FVector(0.0, 0.0, 100.0)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (Character)
	{
		Character->ActionsRoot = ActionsRoot;
		Character->FinishSpawning(FTransform(Origin + FVector(0.0, 0.0, 100.0)));
	}
	if (bSpawnChaser)
	{
		Params.Name = MakeUniqueObjectName(World->PersistentLevel, AMCPProbeChaser::StaticClass(), TEXT("ProbeChaser"));
		Chaser = World->SpawnActor<AMCPProbeChaser>(Origin + FVector(-600.0, 300.0, 100.0), FRotator::ZeroRotator, Params);
	}
#if WITH_EDITOR
	if (Character) Character->SetActorLabel(TEXT("ProbeCharacter"));
	if (Chaser) Chaser->SetActorLabel(TEXT("ProbeChaser"));
#endif

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		APawn* Previous = PC->GetPawn();
		if (Character)
		{
			PC->Possess(Character);
			PC->SetControlRotation(FRotator::ZeroRotator);
		}
		if (Previous && Previous != Character)
		{
			Previous->Destroy();
		}
	}
}

void AMCPProbeArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Menu)
	{
		MenuClicks = Menu->Clicks;
	}
}

void AMCPProbeArena::OpenMenu()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	if (!Menu)
	{
		Menu = CreateWidget<UMCPProbeMenu>(PC, UMCPProbeMenu::StaticClass());
	}
	if (!Menu)
	{
		return;
	}
	Menu->Clicks = 0;
	MenuClicks = 0;
	if (!Menu->IsInViewport())
	{
		Menu->AddToViewport();
	}
	// The first button takes the player's focus, so Enter presses it.
	UButton* FirstButton = Cast<UButton>(Menu->WidgetTree->FindWidget(TEXT("First")));
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(FirstButton ? FirstButton->TakeWidget() : Menu->TakeWidget());
	PC->SetInputMode(Mode);
	if (FirstButton)
	{
		FirstButton->SetUserFocus(PC);
	}
}

void AMCPProbeArena::CloseMenu()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Menu)
	{
		Menu->RemoveFromParent();
	}
	if (PC)
	{
		PC->SetInputMode(FInputModeGameOnly());
	}
}
