// Per-player Enhanced Input runtime state (#778).
//
// The other half of #778: there was no way to ask "does player X in PIE
// instance N currently have mapping context Y applied?", which is what you need
// to verify input-authority gating across a multiplayer PIE session. The
// applied contexts live on the player's UEnhancedPlayerInput, reachable from
// the local player's Enhanced Input subsystem.
//
// Translation-unit partition of FGameplayHandlers; registration lives in
// GameplayHandlers.cpp.

#include "GameplayHandlers.h"

#include "HandlerUtils.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Applied mapping contexts for one player controller, or none. */
	TSharedPtr<FJsonObject> DescribePlayer(APlayerController* PC, int32 PlayerIndex, bool bIncludeActions)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("playerIndex"), PlayerIndex);
		Entry->SetStringField(TEXT("playerController"), PC ? PC->GetName() : FString());
		if (!PC) return Entry;

		Entry->SetBoolField(TEXT("isLocalController"), PC->IsLocalController());
		if (APawn* Pawn = PC->GetPawn())
		{
			Entry->SetStringField(TEXT("pawn"), Pawn->GetName());
			Entry->SetStringField(TEXT("pawnClass"), Pawn->GetClass()->GetName());
		}

		ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
		if (!LocalPlayer)
		{
			// A remote controller on the server has no local player, so it has
			// no Enhanced Input subsystem. Say so rather than reporting an
			// empty context list, which would read as "nothing is applied".
			Entry->SetStringField(TEXT("note"),
				TEXT("No LocalPlayer (remote controller); Enhanced Input state lives on the owning client."));
			return Entry;
		}

		UEnhancedInputLocalPlayerSubsystem* Subsystem =
			LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		if (!Subsystem)
		{
			Entry->SetStringField(TEXT("note"), TEXT("Enhanced Input subsystem not active for this local player"));
			return Entry;
		}

		UEnhancedPlayerInput* PlayerInput = Subsystem->GetPlayerInput();
		if (!PlayerInput)
		{
			Entry->SetStringField(TEXT("note"), TEXT("Local player has no UEnhancedPlayerInput yet"));
			return Entry;
		}

		// GetAppliedInputContextData() is protected (only the subsystem
		// interface is a friend), but AppliedInputContextData is a UPROPERTY -
		// and FProperty reflection ignores C++ access specifiers. Read the map
		// through reflection rather than giving up on enumeration and forcing
		// the caller to already know which context to ask about.
		TArray<TSharedPtr<FJsonValue>> Contexts;
		FMapProperty* MapProp = CastField<FMapProperty>(
			PlayerInput->GetClass()->FindPropertyByName(TEXT("AppliedInputContextData")));
		if (!MapProp)
		{
			Entry->SetStringField(TEXT("note"),
				TEXT("AppliedInputContextData not found by reflection on this engine version"));
			return Entry;
		}

		FScriptMapHelper MapHelper(MapProp, MapProp->ContainerPtrToValuePtr<void>(PlayerInput));
		FObjectPropertyBase* KeyProp = CastField<FObjectPropertyBase>(MapProp->KeyProp);
		FStructProperty* ValueProp = CastField<FStructProperty>(MapProp->ValueProp);

		for (FScriptMapHelper::FIterator It = MapHelper.CreateIterator(); It; ++It)
		{
			const UInputMappingContext* Context = KeyProp
				? Cast<UInputMappingContext>(KeyProp->GetObjectPropertyValue(MapHelper.GetKeyPtr(It)))
				: nullptr;
			if (!Context) continue;

			int32 Priority = 0;
			int32 RegistrationCount = 0;
			if (ValueProp)
			{
				void* ValueAddr = MapHelper.GetValuePtr(It);
				if (FIntProperty* P = CastField<FIntProperty>(ValueProp->Struct->FindPropertyByName(TEXT("Priority"))))
				{
					Priority = P->GetPropertyValue_InContainer(ValueAddr);
				}
				if (FIntProperty* R = CastField<FIntProperty>(ValueProp->Struct->FindPropertyByName(TEXT("RegistrationCount"))))
				{
					RegistrationCount = R->GetPropertyValue_InContainer(ValueAddr);
				}
			}

			TSharedPtr<FJsonObject> ContextObj = MakeShared<FJsonObject>();
			ContextObj->SetStringField(TEXT("name"), Context->GetName());
			ContextObj->SetStringField(TEXT("path"), Context->GetPathName());
			ContextObj->SetNumberField(TEXT("priority"), Priority);
			ContextObj->SetNumberField(TEXT("registrationCount"), RegistrationCount);
			ContextObj->SetNumberField(TEXT("mappingCount"), Context->GetMappings().Num());

			if (bIncludeActions)
			{
				TArray<TSharedPtr<FJsonValue>> Actions;
				TSet<FString> Seen;
				for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
				{
					if (!Mapping.Action) continue;
					const FString ActionName = Mapping.Action->GetName();
					if (Seen.Contains(ActionName)) continue;
					Seen.Add(ActionName);
					TSharedPtr<FJsonObject> ActionObj = MakeShared<FJsonObject>();
					ActionObj->SetStringField(TEXT("action"), ActionName);
					ActionObj->SetStringField(TEXT("actionPath"), Mapping.Action->GetPathName());
					ActionObj->SetStringField(TEXT("key"), Mapping.Key.ToString());
					Actions.Add(MakeShared<FJsonValueObject>(ActionObj));
				}
				ContextObj->SetArrayField(TEXT("actions"), Actions);
			}
			Contexts.Add(MakeShared<FJsonValueObject>(ContextObj));
		}

		// Highest priority first: that is the order Enhanced Input resolves in,
		// so a caller debugging a shadowed binding reads it top-down.
		Contexts.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
		{
			double PA = 0.0, PB = 0.0;
			A->AsObject()->TryGetNumberField(TEXT("priority"), PA);
			B->AsObject()->TryGetNumberField(TEXT("priority"), PB);
			return PA > PB;
		});

		Entry->SetArrayField(TEXT("mappingContexts"), Contexts);
		Entry->SetNumberField(TEXT("mappingContextCount"), Contexts.Num());
		return Entry;
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetInputMappingContexts(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEngine) return MCPError(TEXT("Engine not available"));

	const bool bIncludeActions = OptionalBool(Params, TEXT("includeActions"), false);
	const FString ContextFilter = OptionalString(Params, TEXT("mappingContext"));

	int32 RequestedInstance = INDEX_NONE;
	double RawInstance = 0.0;
	const bool bHasInstance = TryGetNumberParam(Params, TEXT("pieInstance"), RawInstance);
	if (bHasInstance) RequestedInstance = FMath::RoundToInt(RawInstance);

	int32 RequestedPlayer = INDEX_NONE;
	double RawPlayer = 0.0;
	if (TryGetNumberParam(Params, TEXT("playerIndex"), RawPlayer)) RequestedPlayer = FMath::RoundToInt(RawPlayer);

	TArray<TSharedPtr<FJsonValue>> Worlds;
	int32 WorldsSeen = 0;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) continue;
		if (bHasInstance && Ctx.PIEInstance != RequestedInstance) continue;
		UWorld* World = Ctx.World();
		if (!World) continue;
		++WorldsSeen;

		TSharedPtr<FJsonObject> WorldObj = MakeShared<FJsonObject>();
		WorldObj->SetNumberField(TEXT("pieInstance"), Ctx.PIEInstance);
		WorldObj->SetStringField(TEXT("worldPath"), World->GetPathName());
		WorldObj->SetStringField(TEXT("netMode"), DescribePIENetMode(World));

		TArray<TSharedPtr<FJsonValue>> Players;
		int32 PlayerIndex = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++PlayerIndex)
		{
			if (RequestedPlayer != INDEX_NONE && PlayerIndex != RequestedPlayer) continue;
			APlayerController* PC = It->Get();
			TSharedPtr<FJsonObject> Entry = DescribePlayer(PC, PlayerIndex, bIncludeActions);

			// When asking about one specific context, answer the yes/no
			// question directly instead of making the caller scan the list.
			if (!ContextFilter.IsEmpty())
			{
				bool bApplied = false;
				int32 Priority = 0;
				const TArray<TSharedPtr<FJsonValue>>* Contexts = nullptr;
				if (Entry->TryGetArrayField(TEXT("mappingContexts"), Contexts) && Contexts)
				{
					for (const TSharedPtr<FJsonValue>& Value : *Contexts)
					{
						const TSharedPtr<FJsonObject> Obj = Value->AsObject();
						FString Name, Path;
						Obj->TryGetStringField(TEXT("name"), Name);
						Obj->TryGetStringField(TEXT("path"), Path);
						if (Name.Equals(ContextFilter, ESearchCase::IgnoreCase) ||
							Path.Equals(ContextFilter, ESearchCase::IgnoreCase) ||
							Path.Contains(ContextFilter, ESearchCase::IgnoreCase))
						{
							bApplied = true;
							double P = 0.0;
							Obj->TryGetNumberField(TEXT("priority"), P);
							Priority = (int32)P;
							break;
						}
					}
				}
				Entry->SetBoolField(TEXT("hasRequestedContext"), bApplied);
				if (bApplied) Entry->SetNumberField(TEXT("requestedContextPriority"), Priority);
			}

			Players.Add(MakeShared<FJsonValueObject>(Entry));
		}
		WorldObj->SetArrayField(TEXT("players"), Players);
		// Reported = how many are in this response; total = how many the world
		// actually has, which differs whenever playerIndex filtered the list.
		WorldObj->SetNumberField(TEXT("playerCount"), Players.Num());
		WorldObj->SetNumberField(TEXT("totalPlayerCount"), World->GetNumPlayerControllers());
		Worlds.Add(MakeShared<FJsonValueObject>(WorldObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("worlds"), Worlds);
	Result->SetNumberField(TEXT("worldCount"), Worlds.Num());
	if (!ContextFilter.IsEmpty()) Result->SetStringField(TEXT("mappingContext"), ContextFilter);
	if (WorldsSeen == 0)
	{
		Result->SetStringField(TEXT("note"), bHasInstance
			? FString::Printf(TEXT("No running PIE world with instance %d. Use editor(list_pie_instances)."), RequestedInstance)
			: TEXT("PIE is not running. Start it with editor(play_in_editor)."));
	}
	return MCPResult(Result);
}
