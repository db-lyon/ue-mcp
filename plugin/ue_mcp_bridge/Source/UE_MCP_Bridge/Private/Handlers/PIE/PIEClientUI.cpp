#include "Handlers/PIE/PIEClientUI.h"

#include "Handlers/PIE/PIERemotePlayers.h"
#include "HandlerWorld.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace MCPPIE::PIEClientUI
{
	namespace
	{
		FString DescribeWorldsForError()
		{
			TArray<FString> Parts;
			if (GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					UWorld* World = Context.World();
					if (!World || (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game))
					{
						continue;
					}
					const UGameInstance* GameInstance = World->GetGameInstance();
					Parts.Add(FString::Printf(TEXT("pie_instance %d (%s, %d local players)"), Context.PIEInstance,
						*DescribePIENetMode(World), GameInstance ? GameInstance->GetNumLocalPlayers() : 0));
				}
			}
			return Parts.Num() ? FString::Join(Parts, TEXT(", ")) : FString(TEXT("none; start PIE first"));
		}

		TSharedPtr<SWidget> FocusedWidget(const FUIPlayer& Player)
		{
			return FSlateApplication::Get().GetUserFocusedWidget(static_cast<uint32>(Player.SlateUserIndex));
		}

		void WriteHandled(const TSharedPtr<FJsonObject>& Into, const TCHAR* Field, bool bSent, bool bHandled)
		{
			if (bSent)
			{
				Into->SetBoolField(Field, bHandled);
			}
			else
			{
				Into->SetField(Field, MakeShared<FJsonValueNull>());
			}
		}
	}

	bool Resolve(int32 PIEInstance, int32 LocalPlayerIndex, FUIPlayer& Out, FString& OutError)
	{
		Out = FUIPlayer();
		if (!FSlateApplication::IsInitialized())
		{
			OutError = TEXT("Slate is not initialised in this process, so there is no UI focus or key routing");
			return false;
		}
		FString Err;
		ULocalPlayer* LocalPlayer = PIERemotePlayers::FindOwnLocalPlayer(PIEInstance, LocalPlayerIndex, Err);
		if (!LocalPlayer)
		{
			OutError = FString::Printf(TEXT("%s. PIE worlds: %s. pie(clients) lists each world's pie_instance and local players."),
				*Err, *DescribeWorldsForError());
			return false;
		}
		UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient;
		TSharedPtr<SViewport> Viewport = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr;
		if (!Viewport.IsValid())
		{
			OutError = FString::Printf(TEXT("%s has no game viewport widget, so it has no UI to focus"),
				*PIERemotePlayers::DescribeLocalPlayer(LocalPlayer));
			return false;
		}

		Out.LocalPlayer = LocalPlayer;
		Out.PlayerController = LocalPlayer->PlayerController;
		Out.World = LocalPlayer->GetWorld();
		const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(Out.World) : nullptr;
		Out.PIEInstance = Context ? Context->PIEInstance : INDEX_NONE;
		Out.LocalPlayerIndex = LocalPlayer->GetLocalPlayerIndex();
		const TSharedPtr<FSlateUser> SlateUser = LocalPlayer->GetSlateUser();
		Out.SlateUserIndex = SlateUser.IsValid() ? SlateUser->GetUserIndex() : FSlateApplication::Get().GetUserIndexForKeyboard();
		Out.Viewport = Viewport;
		Out.Window = ViewportClient->GetWindow();
		Out.Description = PIERemotePlayers::DescribeLocalPlayer(LocalPlayer);
		return true;
	}

	bool IsInsideViewport(const FUIPlayer& Player, const TSharedPtr<SWidget>& Widget)
	{
		const SWidget* Viewport = Player.Viewport.Get();
		int32 Depth = 0;
		for (TSharedPtr<SWidget> Current = Widget; Current.IsValid() && Depth < 256; Current = Current->GetParentWidget(), ++Depth)
		{
			if (Current.Get() == Viewport)
			{
				return true;
			}
		}
		return false;
	}

	FString FindUmgName(const FUIPlayer& Player, const TSharedPtr<SWidget>& Widget)
	{
		if (!Widget.IsValid() || !Player.World)
		{
			return FString();
		}
		TMap<const SWidget*, UWidget*> ByCached;
		for (TObjectIterator<UWidget> It; It; ++It)
		{
			UWidget* Candidate = *It;
			if (!Candidate || Candidate->GetWorld() != Player.World)
			{
				continue;
			}
			if (TSharedPtr<SWidget> Cached = Candidate->GetCachedWidget())
			{
				ByCached.Add(Cached.Get(), Candidate);
			}
		}
		int32 Depth = 0;
		for (TSharedPtr<SWidget> Current = Widget; Current.IsValid() && Depth < 64; Current = Current->GetParentWidget(), ++Depth)
		{
			if (UWidget* const* Found = ByCached.Find(Current.Get()))
			{
				return (*Found)->GetName();
			}
		}
		return FString();
	}

	TSharedPtr<FJsonObject> DescribeFocus(const FUIPlayer& Player)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		const TSharedPtr<SWidget> Focused = FocusedWidget(Player);
		Out->SetNumberField(TEXT("slate_user"), Player.SlateUserIndex);
		Out->SetBoolField(TEXT("has_focus"), Focused.IsValid());
		Out->SetBoolField(TEXT("in_viewport"), Focused.IsValid() && IsInsideViewport(Player, Focused));
		Out->SetStringField(TEXT("slate_type"), Focused.IsValid() ? Focused->GetTypeAsString() : FString());
		Out->SetStringField(TEXT("widget"), FindUmgName(Player, Focused));
		return Out;
	}

	bool FocusViewportIfOutside(const FUIPlayer& Player)
	{
		const TSharedPtr<SWidget> Focused = FocusedWidget(Player);
		if (Focused.IsValid() && IsInsideViewport(Player, Focused))
		{
			return false;
		}
		return FSlateApplication::Get().SetUserFocus(static_cast<uint32>(Player.SlateUserIndex), Player.Viewport, EFocusCause::SetDirectly);
	}

	bool ParseKeyPhase(const FString& Text, EKeyPhase& Out)
	{
		if (Text.IsEmpty() || Text.Equals(TEXT("press"), ESearchCase::IgnoreCase)) { Out = EKeyPhase::Press; return true; }
		if (Text.Equals(TEXT("down"), ESearchCase::IgnoreCase)) { Out = EKeyPhase::Down; return true; }
		if (Text.Equals(TEXT("up"), ESearchCase::IgnoreCase)) { Out = EKeyPhase::Up; return true; }
		return false;
	}

	bool SendKey(const FUIPlayer& Player, const FString& KeyName, EKeyPhase Phase, FKeyResult& Out, FString& OutError)
	{
		const FKey Key(*KeyName);
		if (KeyName.IsEmpty() || !Key.IsValid())
		{
			OutError = FString::Printf(TEXT("'%s' is not a key name. Use an FKey name such as Right, Left, Up, Down, Tab, Enter, ")
				TEXT("SpaceBar, Escape, Gamepad_DPad_Down or Gamepad_FaceButton_Bottom."), *KeyName);
			return false;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		Out.FocusBefore = DescribeFocus(Player);
		Out.bFocusMovedToViewport = FocusViewportIfOutside(Player);

		// The printable character rides on the event so text boxes see it too; navigation keys have none.
		uint32 CharCode = 0;
		if (Key == EKeys::SpaceBar)
		{
			CharCode = TEXT(' ');
		}
		else if (KeyName.Len() == 1 && FChar::IsAlnum(KeyName[0]))
		{
			CharCode = static_cast<uint32>(FChar::ToLower(KeyName[0]));
		}
		const FKeyEvent Event(Key, FModifierKeysState(), static_cast<uint32>(Player.SlateUserIndex), false, CharCode, 0);
		if (Phase != EKeyPhase::Up)
		{
			Out.bDownSent = true;
			Out.bDownHandled = Slate.ProcessKeyDownEvent(Event);
			if (CharCode != 0)
			{
				Slate.ProcessKeyCharEvent(FCharacterEvent(static_cast<TCHAR>(CharCode), FModifierKeysState(), static_cast<uint32>(Player.SlateUserIndex), false));
				++Out.CharsSent;
			}
		}
		if (Phase != EKeyPhase::Down)
		{
			Out.bUpSent = true;
			Out.bUpHandled = Slate.ProcessKeyUpEvent(Event);
		}
		Out.FocusAfter = DescribeFocus(Player);
		return true;
	}

	bool SendText(const FUIPlayer& Player, const FString& Text, FKeyResult& Out, FString& OutError)
	{
		if (Text.IsEmpty())
		{
			OutError = TEXT("text is empty");
			return false;
		}
		Out.FocusBefore = DescribeFocus(Player);
		Out.bFocusMovedToViewport = FocusViewportIfOutside(Player);
		FSlateApplication& Slate = FSlateApplication::Get();
		for (const TCHAR Char : Text)
		{
			Slate.ProcessKeyCharEvent(FCharacterEvent(Char, FModifierKeysState(), static_cast<uint32>(Player.SlateUserIndex), false));
			++Out.CharsSent;
		}
		Out.FocusAfter = DescribeFocus(Player);
		return true;
	}

	void WriteKeyResult(const FUIPlayer& Player, const FKeyResult& R, const TSharedPtr<FJsonObject>& Into)
	{
		Into->SetStringField(TEXT("target"), Player.Description);
		Into->SetNumberField(TEXT("pie_instance"), Player.PIEInstance);
		Into->SetNumberField(TEXT("client_id"), Player.LocalPlayerIndex);
		Into->SetNumberField(TEXT("slate_user"), Player.SlateUserIndex);
		WriteHandled(Into, TEXT("down_handled"), R.bDownSent, R.bDownHandled);
		WriteHandled(Into, TEXT("up_handled"), R.bUpSent, R.bUpHandled);
		Into->SetNumberField(TEXT("chars_sent"), R.CharsSent);
		Into->SetBoolField(TEXT("focus_moved_to_viewport"), R.bFocusMovedToViewport);
		if (R.FocusBefore.IsValid()) Into->SetObjectField(TEXT("focus_before"), R.FocusBefore);
		if (R.FocusAfter.IsValid()) Into->SetObjectField(TEXT("focus_after"), R.FocusAfter);
		const bool bMoved = R.FocusBefore.IsValid() && R.FocusAfter.IsValid()
			&& (R.FocusBefore->GetStringField(TEXT("slate_type")) != R.FocusAfter->GetStringField(TEXT("slate_type"))
				|| R.FocusBefore->GetStringField(TEXT("widget")) != R.FocusAfter->GetStringField(TEXT("widget")));
		Into->SetBoolField(TEXT("focus_changed"), bMoved);
	}
}
