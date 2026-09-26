// Coverage for #925.
//
// blueprint(read_component_properties) exports BodyInstance as text, and that
// text carries CollisionProfileName plus the ResponseArray of overrides that
// DIFFER from the profile. A response inherited from the profile is not in it,
// so "does this capsule block ECC_Camera" was unanswerable from the dump, and
// editor(get_object_properties) has the same limit.
//
// These assertions run against ACharacter's own capsule, which every engine
// ships with the Pawn profile, so they check the exact thing the issue asked
// for: an EFFECTIVE response that the component itself never overrides and that
// therefore only exists in the profile. Nothing is created, loaded from the
// attached project, or written.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers_Collision.h"

#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> CollisionResponseObject(const TSharedPtr<FJsonValue>& Response)
	{
		return (Response.IsValid() && Response->Type == EJson::Object)
			? Response->AsObject()
			: TSharedPtr<FJsonObject>();
	}

	bool CollisionResponseBool(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field, bool bDefault = false)
	{
		const TSharedPtr<FJsonObject> Obj = CollisionResponseObject(Response);
		if (!Obj.IsValid()) return bDefault;
		bool bValue = bDefault;
		Obj->TryGetBoolField(Field, bValue);
		return bValue;
	}

	FString CollisionResponseString(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject> Obj = CollisionResponseObject(Response);
		if (!Obj.IsValid()) return FString();
		FString Value;
		Obj->TryGetStringField(Field, Value);
		return Value;
	}

	/** Pull one channel's entry out of a `responses` array by its enumName,
	 *  which is the identifier that does not move when a project renames a
	 *  channel. Returns an unset pointer when the channel is not listed. */
	TSharedPtr<FJsonObject> FindChannelEntry(const TSharedPtr<FJsonValue>& Response, const TCHAR* EnumName)
	{
		const TSharedPtr<FJsonObject> Obj = CollisionResponseObject(Response);
		if (!Obj.IsValid()) return TSharedPtr<FJsonObject>();

		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Obj->TryGetArrayField(TEXT("responses"), Entries) || !Entries)
		{
			return TSharedPtr<FJsonObject>();
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Entries)
		{
			if (!Entry.IsValid() || Entry->Type != EJson::Object) continue;
			const TSharedPtr<FJsonObject> EntryObj = Entry->AsObject();
			FString Name;
			if (EntryObj->TryGetStringField(TEXT("enumName"), Name) && Name == EnumName)
			{
				return EntryObj;
			}
		}
		return TSharedPtr<FJsonObject>();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPResolveCollisionProfileTest,
	"UE.MCP.Project.ResolveCollisionProfile.ReportsPerChannelResponses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPResolveCollisionProfileTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FCollisionQueryHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("resolve_collision_profile is registered"),
		Registry.HasHandler(TEXT("resolve_collision_profile")))) return false;

	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("profileName"), TEXT("Pawn"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("resolve_collision_profile"), Params);
		TestTrue(FString::Printf(TEXT("the Pawn profile resolves (%s)"),
			*CollisionResponseString(Response, TEXT("error"))),
			CollisionResponseBool(Response, TEXT("success")));
		TestEqual(TEXT("the profile answers under its own name"),
			CollisionResponseString(Response, TEXT("profileName")), FString(TEXT("Pawn")));
		TestFalse(TEXT("the profile names its collision mode"),
			CollisionResponseString(Response, TEXT("collisionEnabled")).IsEmpty());

		// The eight engine channels are always reported, which is the set a
		// caller asking about Camera or Visibility means.
		const TSharedPtr<FJsonObject> Camera = FindChannelEntry(Response, TEXT("ECC_Camera"));
		if (TestTrue(TEXT("the Camera channel is listed"), Camera.IsValid()))
		{
			FString CameraResponse;
			Camera->TryGetStringField(TEXT("response"), CameraResponse);
			TestTrue(TEXT("the Camera response is one of the three collision responses"),
				CameraResponse == TEXT("Block") || CameraResponse == TEXT("Overlap") || CameraResponse == TEXT("Ignore"));

			double ChannelIndex = -1.0;
			Camera->TryGetNumberField(TEXT("channelIndex"), ChannelIndex);
			TestEqual(TEXT("the Camera channel reports its container index"), ChannelIndex, 4.0);
		}
	}

	// One channel asked for, one channel back.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("profileName"), TEXT("Pawn"));
		Params->SetStringField(TEXT("channel"), TEXT("ECC_Camera"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("resolve_collision_profile"), Params);
		TestTrue(TEXT("a single-channel query succeeds"), CollisionResponseBool(Response, TEXT("success")));

		const TSharedPtr<FJsonObject> Obj = CollisionResponseObject(Response);
		if (TestTrue(TEXT("the single-channel query answered"), Obj.IsValid()))
		{
			double Count = -1.0;
			Obj->TryGetNumberField(TEXT("responseCount"), Count);
			TestEqual(TEXT("exactly one channel comes back"), Count, 1.0);
			TestTrue(TEXT("the one channel is lifted to the top level"), Obj->HasField(TEXT("channel")));
		}
	}

	// A miss names what does exist rather than answering with nothing.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("profileName"), TEXT("NoSuchProfileNameAnywhere"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("resolve_collision_profile"), Params);
		TestFalse(TEXT("an unknown profile is not a success"),
			CollisionResponseBool(Response, TEXT("success"), true));
		TestEqual(TEXT("an unknown profile says why"),
			CollisionResponseString(Response, TEXT("reason")), FString(TEXT("profile_not_found")));

		const TSharedPtr<FJsonObject> Obj = CollisionResponseObject(Response);
		if (Obj.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Known = nullptr;
			if (TestTrue(TEXT("an unknown profile lists the known ones"),
				Obj->TryGetArrayField(TEXT("knownProfiles"), Known) && Known != nullptr))
			{
				TestTrue(TEXT("the project defines at least one profile"), Known->Num() > 0);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPGetComponentCollisionTest,
	"UE.MCP.Blueprint.GetComponentCollision.ReportsTheEffectiveResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPGetComponentCollisionTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FCollisionQueryHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("get_component_collision is registered"),
		Registry.HasHandler(TEXT("get_component_collision")))) return false;

	const FString CapsuleName = ACharacter::CapsuleComponentName.ToString();

	// The native-class route. This is the shape #925 was reported from:
	// checking whether a C++ constructor's collision call reached the CDO.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), TEXT("/Script/Engine.Character"));
		Params->SetStringField(TEXT("componentName"), CapsuleName);

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("get_component_collision"), Params);
		if (!TestTrue(FString::Printf(TEXT("the Character CDO capsule resolves (%s)"),
			*CollisionResponseString(Response, TEXT("error"))),
			CollisionResponseBool(Response, TEXT("success")))) return false;

		TestEqual(TEXT("the answer says it came from a class rather than a Blueprint"),
			CollisionResponseString(Response, TEXT("source")), FString(TEXT("class")));
		TestEqual(TEXT("the capsule's profile is Pawn"),
			CollisionResponseString(Response, TEXT("profileName")), FString(TEXT("Pawn")));
		TestTrue(TEXT("the profile resolved to a real profile"),
			CollisionResponseBool(Response, TEXT("profileResolved")));
		TestFalse(TEXT("the answer names the collision mode"),
			CollisionResponseString(Response, TEXT("collisionEnabled")).IsEmpty());

		// The assertion the issue exists for. ACharacter's capsule never
		// overrides Camera, so the only place its answer can come from is the
		// Pawn profile, and the text dump the old actions produce does not
		// contain it at all.
		const TSharedPtr<FJsonObject> Camera = FindChannelEntry(Response, TEXT("ECC_Camera"));
		if (TestTrue(TEXT("the Camera channel is reported"), Camera.IsValid()))
		{
			FString Effective;
			Camera->TryGetStringField(TEXT("response"), Effective);
			FString FromProfile;
			Camera->TryGetStringField(TEXT("profileResponse"), FromProfile);

			TestFalse(TEXT("the effective Camera response is reported"), Effective.IsEmpty());
			TestEqual(TEXT("an inherited response equals the profile's own"), Effective, FromProfile);

			bool bOverrides = true;
			Camera->TryGetBoolField(TEXT("overridesProfile"), bOverrides);
			TestFalse(TEXT("an inherited response is not reported as an override"), bOverrides);

			// And it agrees with the engine's own evaluator, which is the
			// definition of "effective".
			if (UClass* CharacterClass = ACharacter::StaticClass())
			{
				if (ACharacter* CDO = Cast<ACharacter>(CharacterClass->GetDefaultObject()))
				{
					if (UCapsuleComponent* Capsule = CDO->GetCapsuleComponent())
					{
						const ECollisionResponse Engine = Capsule->GetCollisionResponseToChannel(ECC_Camera);
						const FString EngineName =
							Engine == ECR_Block ? TEXT("Block") :
							Engine == ECR_Overlap ? TEXT("Overlap") :
							Engine == ECR_Ignore ? TEXT("Ignore") : TEXT("Unknown");
						TestEqual(TEXT("the reported response is what the engine evaluator returns"),
							Effective, EngineName);
					}
				}
			}
		}
	}

	// A channel name the project does not have is an error naming the ones it
	// does, not an empty response.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), TEXT("/Script/Engine.Character"));
		Params->SetStringField(TEXT("componentName"), CapsuleName);
		Params->SetStringField(TEXT("channel"), TEXT("NoSuchChannelName"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("get_component_collision"), Params);
		TestFalse(TEXT("an unknown channel is not a success"),
			CollisionResponseBool(Response, TEXT("success"), true));
		TestTrue(TEXT("an unknown channel lists the known ones"),
			CollisionResponseString(Response, TEXT("error")).Contains(TEXT("Camera")));
	}

	// A component with no BodyInstance says so rather than answering with an
	// empty channel list.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), TEXT("/Script/Engine.Character"));
		Params->SetStringField(TEXT("componentName"), ACharacter::CharacterMovementComponentName.ToString());

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("get_component_collision"), Params);
		TestFalse(TEXT("a non-primitive component is not a success"),
			CollisionResponseBool(Response, TEXT("success"), true));
		TestTrue(TEXT("a non-primitive component says it carries no collision"),
			CollisionResponseString(Response, TEXT("error")).Contains(TEXT("carries no collision")));
	}

	// A component name that does not exist lists what does.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), TEXT("/Script/Engine.Character"));
		Params->SetStringField(TEXT("componentName"), TEXT("NoSuchComponent"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("get_component_collision"), Params);
		TestFalse(TEXT("a missing component is not a success"),
			CollisionResponseBool(Response, TEXT("success"), true));
		TestTrue(TEXT("a missing component lists the available ones"),
			CollisionResponseString(Response, TEXT("error")).Contains(CapsuleName));
	}

	return true;
}

#endif
