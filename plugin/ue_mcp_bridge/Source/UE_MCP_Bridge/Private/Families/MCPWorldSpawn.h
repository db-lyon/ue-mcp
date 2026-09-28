#pragma once

// F6 World (plans/domain-design.md 3.3): put one actor into a world. Every
// spawn takes place_actor's params (label, onConflict, location, rotation,
// scale, world, pieInstance) plus its own, and returns one shape. The scope
// resolves the world by the F7 rule, answers a rerun with the actor already
// carrying the label (or refuses it under onConflict=error), spawns inside
// one undo record, applies the label, folder and transform, destroys the
// actor again when a configure step refuses, and Finish writes: created,
// actorLabel, actorPath, actorName, actorClass, location, rotation, scale,
// world and a delete_actor rollback that replays against the same world.
//
//   FMCPSpawn Spawn(Params, { TEXT("Light") });
//   if (auto Err = Spawn.Open()) return Err;           // world, then the label rerun
//   ALight* Light = Spawn.Spawn<ALight>(Class);
//   if (!Light) return Spawn.Failed();
//   ...configure; on a refusal: return Spawn.Abandon(Err);
//   TSharedPtr<FJsonObject> Result = Spawn.Finish();
//
// Component templates and instance components have their own helpers below.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Families/MCPRuntime.h"

/** What one spawn declares. */
struct FMCPSpawnSpec
{
	/** The type as the existed probe and errors name it, such as "Light". */
	const TCHAR* TypeLabel = TEXT("Actor");
	/** Whether a label already in use answers existed; off for replays that spawn under reused labels. */
	bool bLabelCheck = true;
	/** The param naming the outliner folder, or null when the action has none. */
	const TCHAR* FolderParam = TEXT("folderPath");
};

class FMCPSpawn
{
public:
	FMCPSpawn(const TSharedPtr<FJsonObject>& InParams, const FMCPSpawnSpec& InSpec)
		: Params(InParams)
		, Spec(InSpec)
	{
		// Every shared param is read up front, so none reads as ignored (#1057).
		ActorLabel = OptionalString(Params, TEXT("label"));
		OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
		if (Spec.FolderParam) Folder = OptionalString(Params, Spec.FolderParam);
		Location = OptionalVec3(Params, TEXT("location"));
		Rotation = OptionalRotator(Params, TEXT("rotation"));
		bHasScale = HasParam(Params, TEXT("scale"));
		Scale = OptionalVec3(Params, TEXT("scale"), FVector::OneVector);
		Where = FMCPRuntimeWorld::Resolve(Params, TEXT("editor"));
	}

	~FMCPSpawn() { Transaction.Reset(); }

	FMCPSpawn(const FMCPSpawn&) = delete;
	FMCPSpawn& operator=(const FMCPSpawn&) = delete;

	/** The world refusal, then the label rerun (existed, or the onConflict refusal). Null to go ahead. */
	TSharedPtr<FJsonValue> Open() const
	{
		if (auto Err = Where.Require(FString::Printf(TEXT("World not available for scope '%s'"), *Where.Scope))) return Err;
		if (!Spec.bLabelCheck) return nullptr;
		return MCPCheckActorLabelExists(Where.World, ActorLabel, OnConflict, Spec.TypeLabel);
	}

	UWorld* World() const { return Where.World; }
	const FMCPRuntimeWorld& Runtime() const { return Where; }
	const FString& Label() const { return ActorLabel; }
	const FVector& RequestedLocation() const { return Location; }
	const FRotator& RequestedRotation() const { return Rotation; }
	bool HasScale() const { return bHasScale; }
	const FVector& RequestedScale() const { return Scale; }

	/** The requested transform: location and rotation, and scale when the caller sent one. */
	FTransform Transform() const
	{
		return FTransform(Rotation, Location, bHasScale ? Scale : FVector::OneVector);
	}

	/**
	 * Spawn Class at the requested transform (or At) inside the scope's undo
	 * record, then label and file it. Null when the world refused.
	 */
	template <typename T = AActor>
	T* Spawn(UClass* Class, const FActorSpawnParameters& SpawnParams = FActorSpawnParameters())
	{
		return SpawnAt<T>(Class, Transform(), SpawnParams);
	}

	template <typename T = AActor>
	T* SpawnAt(UClass* Class, const FTransform& At, const FActorSpawnParameters& SpawnParams = FActorSpawnParameters())
	{
		if (!Where.World || !Class) return nullptr;
		if (!Transaction.IsValid() && !Where.World->IsGameWorld())
		{
			Transaction = MakeUnique<FScopedTransaction>(TEXT("UE_MCP"),
				FText::FromString(FString::Printf(TEXT("Spawn %s"), Spec.TypeLabel)), nullptr);
		}
		T* Spawned = Where.World->SpawnActor<T>(Class, At, SpawnParams);
		Actor = Spawned;
		if (!Spawned) return nullptr;
		if (!ActorLabel.IsEmpty()) Spawned->SetActorLabel(ActorLabel);
		if (!Folder.IsEmpty()) Spawned->SetFolderPath(*Folder);
		return Spawned;
	}

	/** Re-apply the caller's scale, for a configure step (a brush build) that resets it. */
	void ApplyScale()
	{
		if (Actor && bHasScale) Actor->SetActorScale3D(Scale);
	}

	/** Adopt an actor the handler spawned another way, so Abandon and Finish cover it. */
	void Adopt(AActor* InActor) { Actor = InActor; }

	AActor* Spawned() const { return Actor; }

	/** The refusal for a spawn the world did not make. */
	TSharedPtr<FJsonValue> Failed() const
	{
		return MCPError(FString::Printf(TEXT("Failed to spawn %s"), Spec.TypeLabel));
	}

	/** Destroy the half-made actor, drop the undo record and hand back Err. */
	TSharedPtr<FJsonValue> Abandon(const TSharedPtr<FJsonValue>& Err)
	{
		if (Actor && Where.World) Where.World->DestroyActor(Actor);
		Actor = nullptr;
		if (Transaction.IsValid())
		{
			Transaction->Cancel();
			Transaction.Reset();
		}
		return Err;
	}

	/** The spawn shape. The handler adds its own keys to the returned object. */
	TSharedPtr<FJsonObject> Finish()
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		MCPSetCreated(Result);
		if (!Actor) return Result;
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Result->SetStringField(TEXT("actorName"), Actor->GetName());
		Result->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
		Result->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Actor->GetActorLocation()));
		Result->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Actor->GetActorRotation()));
		Result->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Actor->GetActorScale3D()));
		Result->SetStringField(TEXT("world"), Where.IsLive() ? TEXT("pie") : TEXT("editor"));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Where.Echo(Payload);
		MCPSetRollback(Result, TEXT("delete_actor"), Payload);
		return Result;
	}

	/**
	 * The params every spawn takes, after the action's own. An own param of the
	 * same name wins, so an action can word one for its type. PieType follows
	 * the category, whose one key has one type.
	 */
	static TArray<FMCPParamSpec> ParamsWith(std::initializer_list<FMCPParamSpec> Own, EMCPParamType PieType = EMCPParamType::Integer)
	{
		using EType = EMCPParamType;
		TArray<FMCPParamSpec> Out(Own);
		auto Add = [&Out](FMCPParamSpec Spec)
		{
			for (const FMCPParamSpec& Existing : Out)
			{
				if (Existing.Name == Spec.Name) return;
			}
			Out.Add(MoveTemp(Spec));
		};
		Add(MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label; an existing actor with this label is reported rather than duplicated")));
		Add(MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When the label or name is taken: skip (default) | error")));
		Add(MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("World location")));
		Add(MCPParam::Optional(TEXT("rotation"), EType::Rotator, TEXT("World rotation")));
		Add(MCPParam::Optional(TEXT("scale"), EType::Vec3, TEXT("Actor scale")));
		Add(MCPParam::Optional(TEXT("folderPath"), EType::String, TEXT("World Outliner folder")));
		Add(MCPParam::Optional(TEXT("world"), EType::String, TEXT("World scope: editor (default) | pie")));
		Add(MCPParam::Optional(TEXT("pieInstance"), PieType, TEXT("Which PIE world when world is pie: 0 = server or primary, 1..N = clients")));
		return Out;
	}

private:
	TSharedPtr<FJsonObject> Params;
	FMCPSpawnSpec Spec;
	FMCPRuntimeWorld Where;
	FString ActorLabel;
	FString OnConflict;
	FString Folder;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Scale = FVector::OneVector;
	bool bHasScale = false;
	AActor* Actor = nullptr;
	TUniquePtr<FScopedTransaction> Transaction;
};

namespace MCPWorldSpawn
{
	/** A component class from a short name, a /Script path or a Blueprint class path. Null when none resolves. */
	inline UClass* ResolveComponentClass(const FString& Spec)
	{
		UClass* Class = nullptr;
		if (Spec.Contains(TEXT("/")) || Spec.Contains(TEXT(".")))
		{
			Class = LoadObject<UClass>(nullptr, *Spec);
		}
		if (!Class) Class = MCPResolveClass(Spec);
		if (!Class) Class = LoadObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + Spec));
		return Class;
	}

	/** Blueprint's construction-script node whose template is a Class, or null. */
	inline USCS_Node* FindTemplateOfClass(UBlueprint* Blueprint, UClass* Class)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript || !Class) return nullptr;
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf(Class)) return Node;
		}
		return nullptr;
	}

	/** The refusal for a Blueprint with no construction script to take a template, or nullptr. */
	inline TSharedPtr<FJsonValue> RequireConstructionScript(UBlueprint* Blueprint)
	{
		if (Blueprint && Blueprint->SimpleConstructionScript) return nullptr;
		return MCPError(TEXT("Blueprint has no SimpleConstructionScript (not an Actor Blueprint?)"));
	}

	/**
	 * Add a component template of Class named Name to Blueprint's construction
	 * script, under Parent or as a root node. Returns the node, or null when
	 * the script made none. Report the node's variable name: the script
	 * uniquifies a name already taken.
	 */
	inline USCS_Node* AddTemplate(UBlueprint* Blueprint, UClass* Class, FName Name, USCS_Node* Parent = nullptr)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript || !Class) return nullptr;
		USimpleConstructionScript* Script = Blueprint->SimpleConstructionScript;
		Script->Modify();
		USCS_Node* Node = Script->CreateNode(Class, Name);
		if (!Node) return nullptr;
		if (Parent) Parent->AddChildNode(Node);
		else Script->AddNode(Node);
		return Node;
	}

	/** The component template result keys and remove_component rollback every adder shares. */
	inline void DescribeTemplate(const TSharedPtr<FJsonObject>& Result, const FString& BlueprintPath, const USCS_Node* Node)
	{
		const FString ComponentName = Node ? Node->GetVariableName().ToString() : FString();
		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetStringField(TEXT("componentName"), ComponentName);
		if (Node && Node->ComponentClass) Result->SetStringField(TEXT("componentClass"), Node->ComponentClass->GetName());
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), BlueprintPath);
		Payload->SetStringField(TEXT("componentName"), ComponentName);
		MCPSetRollback(Result, TEXT("remove_component"), Payload);
	}

	/** Add an instance component of Class named Name to Actor, under its root when it is a scene component (or as the root when it has none). */
	inline UActorComponent* AddInstanceComponent(AActor* Actor, UClass* Class, FName Name)
	{
		if (!Actor || !Class) return nullptr;
		Actor->Modify();
		UActorComponent* Component = NewObject<UActorComponent>(Actor, Class, Name, RF_Transactional);
		if (!Component) return nullptr;
		USceneComponent* Scene = Cast<USceneComponent>(Component);
		if (Scene && Actor->GetRootComponent()) Scene->SetupAttachment(Actor->GetRootComponent());
		Component->RegisterComponent();
		if (Scene && !Actor->GetRootComponent()) Actor->SetRootComponent(Scene);
		Actor->AddInstanceComponent(Component);
		return Component;
	}
}
