// Effective collision reads (#925). See BlueprintHandlers_Collision.h for why
// the existing property dumps cannot answer the question these two do.
//
// The whole file reads. Nothing here writes a package, spawns anything, or
// touches the open level.

#include "BlueprintHandlers_Collision.h"

#include "BlueprintHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
// The container has one byte per serialisable channel and ECC_GameTraceChannel50
// is the last of them, so the response table is exactly this wide. ECC_MAX is
// larger because it counts the deprecated non-serialised entry past the end.
constexpr int32 CollisionChannelCount = 64;

/** "Block" rather than "ECR_Block". The enum's DisplayName metadata says the
 *  same thing, but only in an editor build with metadata loaded, and a result
 *  field that silently degrades to an ordinal is worse than a switch. */
const TCHAR* CollisionResponseName(ECollisionResponse Response)
{
	switch (Response)
	{
	case ECR_Ignore:  return TEXT("Ignore");
	case ECR_Overlap: return TEXT("Overlap");
	case ECR_Block:   return TEXT("Block");
	default:          return TEXT("Unknown");
	}
}

/** The spelling the Collision details panel uses, for the same reason. */
const TCHAR* CollisionEnabledName(ECollisionEnabled::Type Enabled)
{
	switch (Enabled)
	{
	case ECollisionEnabled::NoCollision:      return TEXT("NoCollision");
	case ECollisionEnabled::QueryOnly:        return TEXT("QueryOnly");
	case ECollisionEnabled::PhysicsOnly:      return TEXT("PhysicsOnly");
	case ECollisionEnabled::QueryAndPhysics:  return TEXT("QueryAndPhysics");
	case ECollisionEnabled::ProbeOnly:        return TEXT("ProbeOnly");
	case ECollisionEnabled::QueryAndProbe:    return TEXT("QueryAndProbe");
	default:                                  return TEXT("Unknown");
	}
}

/** The C++ enumerator for a container index: ECC_Camera for 4. Stable across
 *  projects, unlike the display name, so a caller can key on it. */
FString CollisionChannelEnumName(int32 Index)
{
	if (Index >= 0 && Index < 8)
	{
		static const TCHAR* const Fixed[8] = {
			TEXT("ECC_WorldStatic"), TEXT("ECC_WorldDynamic"), TEXT("ECC_Pawn"),
			TEXT("ECC_Visibility"),  TEXT("ECC_Camera"),       TEXT("ECC_PhysicsBody"),
			TEXT("ECC_Vehicle"),     TEXT("ECC_Destructible")
		};
		return Fixed[Index];
	}
	if (Index >= 8 && Index < 14)
	{
		return FString::Printf(TEXT("ECC_EngineTraceChannel%d"), Index - 7);
	}
	if (Index >= 14 && Index < CollisionChannelCount)
	{
		return FString::Printf(TEXT("ECC_GameTraceChannel%d"), Index - 13);
	}
	return FString::Printf(TEXT("ECC_Unknown%d"), Index);
}

/** The name a channel carries when the project has not renamed it. Comparing
 *  the configured name against this is what tells a project's own channel
 *  ("Weapon") apart from an unused slot ("GameTraceChannel7"). */
FString CollisionChannelDefaultName(int32 Index)
{
	FString EnumName = CollisionChannelEnumName(Index);
	EnumName.RemoveFromStart(TEXT("ECC_"));
	return EnumName;
}

/** One channel, described once so the component read and the profile read
 *  cannot disagree about what a channel is called. */
struct FMCPCollisionChannelInfo
{
	int32 Index = 0;
	ECollisionChannel Channel = ECC_WorldStatic;
	FString EnumName;
	FString DisplayName;
	bool bCustomNamed = false;
	bool bTraceChannel = false;
	bool bObjectChannel = false;
};

/** Build the channel table from the project's own collision settings, so a
 *  custom trace or object channel appears under the name the project gave it
 *  rather than as GameTraceChannel<n>. */
TArray<FMCPCollisionChannelInfo> BuildCollisionChannelTable()
{
	TArray<FMCPCollisionChannelInfo> Table;

	UCollisionProfile* Profile = UCollisionProfile::Get();
	Table.Reserve(CollisionChannelCount);
	for (int32 Index = 0; Index < CollisionChannelCount; ++Index)
	{
		FMCPCollisionChannelInfo Info;
		Info.Index = Index;
		Info.Channel = static_cast<ECollisionChannel>(Index);
		Info.EnumName = CollisionChannelEnumName(Index);

		const FString DefaultName = CollisionChannelDefaultName(Index);
		Info.DisplayName = DefaultName;
		if (Profile)
		{
			const FName Configured = Profile->ReturnChannelNameFromContainerIndex(Index);
			if (!Configured.IsNone())
			{
				Info.DisplayName = Configured.ToString();
			}
			Info.bTraceChannel = Profile->ConvertToTraceType(Info.Channel) != TraceTypeQuery_MAX;
			Info.bObjectChannel = Profile->ConvertToObjectType(Info.Channel) != ObjectTypeQuery_MAX;
		}
		Info.bCustomNamed = !Info.DisplayName.Equals(DefaultName, ESearchCase::CaseSensitive);
		Table.Add(Info);
	}
	return Table;
}

/** Which channels a response list carries by default.
 *
 *  All 64 would bury the answer in fifty unused GameTraceChannel slots, so the
 *  default is the eight engine channels every project has plus every channel
 *  the project actually configured. includeAllChannels=true asks for the rest. */
bool ShouldReportChannel(const FMCPCollisionChannelInfo& Info, bool bIncludeAll)
{
	if (bIncludeAll) return true;
	if (Info.Index < 8) return true;
	return Info.bCustomNamed;
}

/** Match a caller's `channel` argument against the table. Accepts the
 *  configured display name ("Weapon"), the C++ enumerator ("ECC_Camera"), the
 *  bare enumerator ("Camera") and the container index as a string. Returns
 *  INDEX_NONE on a miss. */
int32 FindCollisionChannel(const TArray<FMCPCollisionChannelInfo>& Table, const FString& Spec)
{
	if (Spec.IsEmpty()) return INDEX_NONE;

	if (Spec.IsNumeric())
	{
		const int32 AsIndex = FCString::Atoi(*Spec);
		return (AsIndex >= 0 && AsIndex < Table.Num()) ? AsIndex : INDEX_NONE;
	}

	for (const FMCPCollisionChannelInfo& Info : Table)
	{
		if (Info.DisplayName.Equals(Spec, ESearchCase::IgnoreCase)) return Info.Index;
		if (Info.EnumName.Equals(Spec, ESearchCase::IgnoreCase)) return Info.Index;
	}
	return INDEX_NONE;
}

/** The names a failed channel lookup should suggest: everything a caller could
 *  legitimately have meant, without the unused slots. */
FString DescribeKnownChannels(const TArray<FMCPCollisionChannelInfo>& Table)
{
	TArray<FString> Names;
	for (const FMCPCollisionChannelInfo& Info : Table)
	{
		if (ShouldReportChannel(Info, /*bIncludeAll=*/false))
		{
			Names.Add(Info.DisplayName);
		}
	}
	return FString::Join(Names, TEXT(", "));
}

void WriteChannelIdentity(const TSharedPtr<FJsonObject>& Out, const FMCPCollisionChannelInfo& Info)
{
	Out->SetStringField(TEXT("channel"), Info.DisplayName);
	Out->SetStringField(TEXT("enumName"), Info.EnumName);
	Out->SetNumberField(TEXT("channelIndex"), Info.Index);
	Out->SetBoolField(TEXT("isTraceChannel"), Info.bTraceChannel);
	Out->SetBoolField(TEXT("isObjectChannel"), Info.bObjectChannel);
	Out->SetBoolField(TEXT("isCustomNamed"), Info.bCustomNamed);
}

/** Look a profile up by name, tolerating a renamed profile through the
 *  engine's own redirect table. */
bool LookUpProfileTemplate(const FString& ProfileName, FCollisionResponseTemplate& OutTemplate, FString& OutResolvedName)
{
	OutResolvedName = ProfileName;
	UCollisionProfile* Profile = UCollisionProfile::Get();
	if (!Profile) return false;

	FName Name(*ProfileName);
	if (const FName* Redirect = Profile->LookForProfileRedirect(Name))
	{
		Name = *Redirect;
		OutResolvedName = Name.ToString();
	}
	return Profile->GetProfileTemplate(Name, OutTemplate);
}

/** Every profile the project has, for a lookup that missed. */
TArray<FString> ListProfileNames()
{
	TArray<FString> Names;
	UCollisionProfile* Profile = UCollisionProfile::Get();
	if (!Profile) return Names;
	for (int32 Index = 0; Index < Profile->GetNumOfProfiles(); ++Index)
	{
		if (const FCollisionResponseTemplate* Template = Profile->GetProfileByIndex(Index))
		{
			Names.AddUnique(Template->Name.ToString());
		}
	}
	return Names;
}

/** Collect every component on an Actor CDO, which is where a native C++
 *  constructor's CreateDefaultSubobject lands. This is the case #925 was
 *  reported from: verifying that a rebuilt constructor's
 *  SetCollisionResponseToChannel actually reached the CDO. */
UActorComponent* FindComponentOnActorCDO(
	AActor* CDO,
	const FString& ComponentName,
	TArray<FString>& OutAvailable)
{
	if (!CDO) return nullptr;

	UActorComponent* Found = nullptr;
	for (UActorComponent* Component : CDO->GetComponents())
	{
		if (!Component) continue;
		const FString Name = Component->GetName();
		OutAvailable.AddUnique(Name);
		if (!Found && Name.Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			Found = Component;
		}
	}
	return Found;
}
}

void FCollisionQueryHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("blueprint"));
	Registry.RegisterHandler(TEXT("get_component_collision"), &GetComponentCollision);
	Registry.RegisterHandler(TEXT("resolve_collision_profile"), &ResolveCollisionProfile);
}

TSharedPtr<FJsonValue> FCollisionQueryHandlers::GetComponentCollision(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	const FString RequestedChannel = OptionalString(Params, TEXT("channel"));
	const bool bIncludeAll = OptionalBool(Params, TEXT("includeAllChannels"), false);

	// Two ways in, because the two things a caller wants to verify live in
	// different places: a Blueprint's component template, and a native class's
	// CDO component. A Blueprint path resolves as an asset; anything else is
	// tried as a class.
	UActorComponent* Component = nullptr;
	TArray<FString> Available;
	bool bInherited = false;
	FString Source;
	FString OwnerPath;

	if (UBlueprint* Blueprint = Cast<UBlueprint>(MCPLoadAssetObject(AssetPath)))
	{
		Component = ResolveComponentTemplate(
			Blueprint, ComponentName, /*bForWrite=*/false, bInherited, Available);
		Source = TEXT("blueprint");
		OwnerPath = Blueprint->GetPathName();
	}
	else
	{
		UClass* Class = MCPResolveClass(AssetPath);
		if (!Class)
		{
			return MCPClassNotFoundError(AssetPath, TEXT("assetPath"));
		}
		if (!Class->IsChildOf(AActor::StaticClass()))
		{
			return MCPClassUnusableError(AssetPath, Class, TEXT("wrong_base"),
				TEXT("it is not an Actor class, so it has no component to read collision from."));
		}
		AActor* CDO = Cast<AActor>(Class->GetDefaultObject());
		Component = FindComponentOnActorCDO(CDO, ComponentName, Available);
		Source = TEXT("class");
		OwnerPath = Class->GetPathName();
	}

	if (!Component)
	{
		Available.Sort([](const FString& A, const FString& B) { return A < B; });
		return MCPError(FString::Printf(
			TEXT("Component '%s' not found on '%s'. Available: [%s]"),
			*ComponentName, *AssetPath, *FString::Join(Available, TEXT(", "))));
	}

	UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
	if (!Primitive)
	{
		// A SceneComponent or a bare ActorComponent has no BodyInstance at all,
		// and saying "not a PrimitiveComponent" is the difference between an
		// answerable question and an empty response.
		return MCPError(FString::Printf(
			TEXT("Component '%s' on '%s' is a %s, which carries no collision. Only PrimitiveComponent ")
				TEXT("subclasses (capsules, boxes, spheres, meshes) have a collision profile and per-channel responses."),
			*ComponentName, *AssetPath, *Component->GetClass()->GetName()));
	}

	const TArray<FMCPCollisionChannelInfo> Table = BuildCollisionChannelTable();

	int32 SingleChannel = INDEX_NONE;
	if (!RequestedChannel.IsEmpty())
	{
		SingleChannel = FindCollisionChannel(Table, RequestedChannel);
		if (SingleChannel == INDEX_NONE)
		{
			return MCPError(FString::Printf(
				TEXT("Unknown collision channel '%s'. Pass a configured channel name, a C++ enumerator ")
					TEXT("(ECC_Camera), or a container index. Known channels: %s"),
				*RequestedChannel, *DescribeKnownChannels(Table)));
		}
	}

	const FName ProfileName = Primitive->GetCollisionProfileName();
	FCollisionResponseTemplate ProfileTemplate;
	FString ResolvedProfileName;
	const bool bProfileResolved =
		LookUpProfileTemplate(ProfileName.ToString(), ProfileTemplate, ResolvedProfileName);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("resolvedPath"), OwnerPath);
	Result->SetStringField(TEXT("source"), Source);
	Result->SetStringField(TEXT("componentName"), Component->GetName());
	Result->SetStringField(TEXT("componentClass"), Component->GetClass()->GetName());
	Result->SetBoolField(TEXT("inherited"), bInherited);

	Result->SetStringField(TEXT("collisionEnabled"), CollisionEnabledName(Primitive->GetCollisionEnabled()));
	Result->SetBoolField(TEXT("collisionEnabledForQueries"), Primitive->IsQueryCollisionEnabled());
	Result->SetBoolField(TEXT("collisionEnabledForPhysics"), Primitive->IsPhysicsCollisionEnabled());

	const ECollisionChannel ObjectType = Primitive->GetCollisionObjectType();
	const int32 ObjectTypeIndex = static_cast<int32>(ObjectType);
	Result->SetStringField(TEXT("objectType"),
		Table.IsValidIndex(ObjectTypeIndex) ? Table[ObjectTypeIndex].DisplayName : CollisionChannelEnumName(ObjectTypeIndex));
	Result->SetStringField(TEXT("objectTypeEnumName"), CollisionChannelEnumName(ObjectTypeIndex));
	Result->SetNumberField(TEXT("objectTypeIndex"), ObjectTypeIndex);

	Result->SetStringField(TEXT("profileName"), ProfileName.ToString());
	Result->SetBoolField(TEXT("profileResolved"), bProfileResolved);
	if (bProfileResolved && !ResolvedProfileName.Equals(ProfileName.ToString(), ESearchCase::CaseSensitive))
	{
		Result->SetStringField(TEXT("profileRedirectedTo"), ResolvedProfileName);
	}

	// The effective response per channel. GetCollisionResponseToChannel is the
	// evaluated answer: the profile's response with the component's own
	// overrides already folded in. The profile's own response rides alongside
	// so a caller can see WHERE the answer came from, which is what the
	// ResponseArray text dump could never show for an inherited response.
	TArray<TSharedPtr<FJsonValue>> Responses;
	for (const FMCPCollisionChannelInfo& Info : Table)
	{
		if (SingleChannel != INDEX_NONE && Info.Index != SingleChannel) continue;
		if (SingleChannel == INDEX_NONE && !ShouldReportChannel(Info, bIncludeAll)) continue;

		const ECollisionResponse Effective = Primitive->GetCollisionResponseToChannel(Info.Channel);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		WriteChannelIdentity(Entry, Info);
		Entry->SetStringField(TEXT("response"), CollisionResponseName(Effective));
		if (bProfileResolved)
		{
			const ECollisionResponse FromProfile = ProfileTemplate.ResponseToChannels.GetResponse(Info.Channel);
			Entry->SetStringField(TEXT("profileResponse"), CollisionResponseName(FromProfile));
			Entry->SetBoolField(TEXT("overridesProfile"), Effective != FromProfile);
		}
		Responses.Add(MakeShared<FJsonValueObject>(Entry));
	}

	if (SingleChannel != INDEX_NONE && Responses.Num() > 0)
	{
		// One channel asked for, one answer at the top level as well as in the
		// list, so the caller does not have to index into an array of one.
		Result->SetObjectField(TEXT("channel"), Responses[0]->AsObject());
	}
	Result->SetArrayField(TEXT("responses"), Responses);
	Result->SetNumberField(TEXT("responseCount"), Responses.Num());
	Result->SetBoolField(TEXT("includeAllChannels"), bIncludeAll);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FCollisionQueryHandlers::ResolveCollisionProfile(const TSharedPtr<FJsonObject>& Params)
{
	FString ProfileName;
	if (auto Err = RequireString(Params, TEXT("profileName"), ProfileName)) return Err;

	const FString RequestedChannel = OptionalString(Params, TEXT("channel"));
	const bool bIncludeAll = OptionalBool(Params, TEXT("includeAllChannels"), false);

	if (!UCollisionProfile::Get())
	{
		return MCPError(TEXT("Collision settings are not available in this process."));
	}

	FCollisionResponseTemplate Template;
	FString ResolvedProfileName;
	if (!LookUpProfileTemplate(ProfileName, Template, ResolvedProfileName))
	{
		TArray<FString> Known = ListProfileNames();
		Known.Sort([](const FString& A, const FString& B) { return A < B; });

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("No collision profile named '%s'. The project defines: %s"),
			*ProfileName, *FString::Join(Known, TEXT(", "))));
		Obj->SetStringField(TEXT("profileName"), ProfileName);
		Obj->SetStringField(TEXT("reason"), TEXT("profile_not_found"));
		TArray<TSharedPtr<FJsonValue>> KnownJson;
		for (const FString& Name : Known) KnownJson.Add(MakeShared<FJsonValueString>(Name));
		Obj->SetArrayField(TEXT("knownProfiles"), KnownJson);
		return MakeShared<FJsonValueObject>(Obj);
	}

	const TArray<FMCPCollisionChannelInfo> Table = BuildCollisionChannelTable();

	int32 SingleChannel = INDEX_NONE;
	if (!RequestedChannel.IsEmpty())
	{
		SingleChannel = FindCollisionChannel(Table, RequestedChannel);
		if (SingleChannel == INDEX_NONE)
		{
			return MCPError(FString::Printf(
				TEXT("Unknown collision channel '%s'. Pass a configured channel name, a C++ enumerator ")
					TEXT("(ECC_Camera), or a container index. Known channels: %s"),
				*RequestedChannel, *DescribeKnownChannels(Table)));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("profileName"), Template.Name.ToString());
	Result->SetStringField(TEXT("requestedProfileName"), ProfileName);
	if (!ResolvedProfileName.Equals(ProfileName, ESearchCase::CaseSensitive))
	{
		Result->SetStringField(TEXT("profileRedirectedTo"), ResolvedProfileName);
	}
	Result->SetStringField(TEXT("collisionEnabled"), CollisionEnabledName(Template.CollisionEnabled));

	const int32 ObjectTypeIndex = static_cast<int32>(Template.ObjectType.GetValue());
	Result->SetStringField(TEXT("objectType"),
		Table.IsValidIndex(ObjectTypeIndex) ? Table[ObjectTypeIndex].DisplayName : CollisionChannelEnumName(ObjectTypeIndex));
	Result->SetStringField(TEXT("objectTypeEnumName"), CollisionChannelEnumName(ObjectTypeIndex));
	Result->SetStringField(TEXT("objectTypeName"), Template.ObjectTypeName.ToString());
	Result->SetNumberField(TEXT("objectTypeIndex"), ObjectTypeIndex);
	Result->SetBoolField(TEXT("canModify"), Template.bCanModify);
#if WITH_EDITORONLY_DATA
	Result->SetStringField(TEXT("helpMessage"), Template.HelpMessage);
#endif

	TArray<TSharedPtr<FJsonValue>> Responses;
	for (const FMCPCollisionChannelInfo& Info : Table)
	{
		if (SingleChannel != INDEX_NONE && Info.Index != SingleChannel) continue;
		if (SingleChannel == INDEX_NONE && !ShouldReportChannel(Info, bIncludeAll)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		WriteChannelIdentity(Entry, Info);
		Entry->SetStringField(TEXT("response"),
			CollisionResponseName(Template.ResponseToChannels.GetResponse(Info.Channel)));
		Responses.Add(MakeShared<FJsonValueObject>(Entry));
	}

	if (SingleChannel != INDEX_NONE && Responses.Num() > 0)
	{
		Result->SetObjectField(TEXT("channel"), Responses[0]->AsObject());
	}
	Result->SetArrayField(TEXT("responses"), Responses);
	Result->SetNumberField(TEXT("responseCount"), Responses.Num());
	Result->SetBoolField(TEXT("includeAllChannels"), bIncludeAll);
	return MCPResult(Result);
}
