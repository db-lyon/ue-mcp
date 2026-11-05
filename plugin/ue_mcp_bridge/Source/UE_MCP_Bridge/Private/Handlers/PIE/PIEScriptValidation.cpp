#include "Handlers/PIE/PIEScriptValidation.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Handlers/PIE/PIEObserver.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Handlers/PIE/PIEStepActions.h"
#include "Handlers/PIE/PIEActorPuppet.h"
#include "InputAction.h"
#include "HandlerClassResolve.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"

namespace MCPPIE
{
	namespace
	{
		const TCHAR* const DraftCheckBoneChannels[] = {
			TEXT("x"), TEXT("y"), TEXT("z"), TEXT("pitch"), TEXT("yaw"), TEXT("roll"), TEXT("scalex"), TEXT("scaley"), TEXT("scalez")
		};

		const TCHAR* const DraftCheckOps[] = {
			TEXT("decreasing"), TEXT("increasing"), TEXT("eq"), TEXT("gt"), TEXT("lt"), TEXT("visible"), TEXT("exists")
		};

		FScriptStepIssue DraftCheckIssue(int32 Step, const FString& Field, const FString& Message, const TArray<FString>& Candidates = TArray<FString>())
		{
			FScriptStepIssue Issue;
			Issue.Step = Step;
			Issue.Field = Field;
			Issue.Message = Message;
			Issue.Candidates = Candidates;
			return Issue;
		}

		// Edit distance between two lower-cased strings.
		int32 DraftCheckDistance(const FString& A, const FString& B)
		{
			const int32 N = A.Len();
			const int32 M = B.Len();
			TArray<int32> Prev;
			TArray<int32> Cur;
			Prev.SetNumUninitialized(M + 1);
			Cur.SetNumUninitialized(M + 1);
			for (int32 J = 0; J <= M; ++J) Prev[J] = J;
			for (int32 I = 1; I <= N; ++I)
			{
				Cur[0] = I;
				for (int32 J = 1; J <= M; ++J)
				{
					const int32 Cost = A[I - 1] == B[J - 1] ? 0 : 1;
					Cur[J] = FMath::Min3(Prev[J] + 1, Cur[J - 1] + 1, Prev[J - 1] + Cost);
				}
				Swap(Prev, Cur);
			}
			return Prev[M];
		}

		FString DraftCheckJoin(const FString& Prefix, const FString& Name)
		{
			return Prefix.IsEmpty() ? Name : Prefix + TEXT(".") + Name;
		}

		TArray<FString> DraftCheckPrefixed(const FString& Prefix, const TArray<FString>& Names)
		{
			TArray<FString> Out;
			for (const FString& N : Names) Out.Add(DraftCheckJoin(Prefix, N));
			return Out;
		}

		bool DraftCheckEnum(const UEnum* Enum, FScriptValueInfo& Out)
		{
			if (!Enum) return false;
			Out.Kind = EScriptValueKind::Enum;
			Out.TypeName = Enum->GetName();
			const int32 Num = Enum->NumEnums();
			for (int32 I = 0; I < Num; ++I)
			{
				const FString Name = Enum->GetNameStringByIndex(I);
				// The generated _MAX entry is not a value a property holds.
				if (I == Num - 1 && Name.EndsWith(TEXT("MAX"))) continue;
				Out.EnumValues.Add(TPair<FString, int64>(Name, Enum->GetValueByIndex(I)));
			}
			return true;
		}

		// The leaf types the observer samples to a double, mirroring its reflection walk.
		bool DraftCheckLeaf(const FProperty* P, FScriptValueInfo& Out)
		{
			auto Integer = [&Out](const TCHAR* Type, double Lo, double Hi)
			{
				Out.Kind = EScriptValueKind::Integer;
				Out.TypeName = Type;
				Out.Min = Lo;
				Out.Max = Hi;
				return true;
			};
			if (CastField<FFloatProperty>(P)) { Out.Kind = EScriptValueKind::Number; Out.TypeName = TEXT("float"); return true; }
			if (CastField<FDoubleProperty>(P)) { Out.Kind = EScriptValueKind::Number; Out.TypeName = TEXT("double"); return true; }
			if (CastField<FIntProperty>(P)) return Integer(TEXT("int32"), MIN_int32, MAX_int32);
			if (CastField<FInt64Property>(P)) return Integer(TEXT("int64"), static_cast<double>(MIN_int64), static_cast<double>(MAX_int64));
			if (CastField<FInt16Property>(P)) return Integer(TEXT("int16"), MIN_int16, MAX_int16);
			if (CastField<FInt8Property>(P)) return Integer(TEXT("int8"), MIN_int8, MAX_int8);
			if (CastField<FUInt32Property>(P)) return Integer(TEXT("uint32"), 0.0, MAX_uint32);
			if (CastField<FUInt16Property>(P)) return Integer(TEXT("uint16"), 0.0, MAX_uint16);
			if (const FByteProperty* B = CastField<FByteProperty>(P))
			{
				if (B->Enum) return DraftCheckEnum(B->Enum, Out);
				return Integer(TEXT("uint8"), 0.0, MAX_uint8);
			}
			if (CastField<FBoolProperty>(P)) { Out.Kind = EScriptValueKind::Boolean; Out.TypeName = TEXT("bool"); return true; }
			if (const FEnumProperty* E = CastField<FEnumProperty>(P))
			{
				const FNumericProperty* Under = E->GetUnderlyingProperty();
				if (CastField<FByteProperty>(Under) || CastField<FIntProperty>(Under)) return DraftCheckEnum(E->GetEnum(), Out);
			}
			return false;
		}

		// Property names on Struct a path can continue with: readable leaves, plus structs and objects unless bLeavesOnly.
		void DraftCheckFieldNames(const UStruct* Struct, bool bLeavesOnly, TArray<FString>& Out)
		{
			if (!Struct) return;
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				FScriptValueInfo Ignored;
				const bool bLeaf = DraftCheckLeaf(*It, Ignored);
				const bool bBranch = CastField<FStructProperty>(*It) || CastField<FObjectProperty>(*It);
				if (bLeaf || (!bLeavesOnly && bBranch)) Out.AddUnique(It->GetName());
			}
		}

		struct FDraftComponent
		{
			FString Name;
			const UObject* Template = nullptr;
			UClass* Class = nullptr;
		};

		// The components an actor of ActorClass has at run time, by the name the observer matches.
		void DraftCheckComponents(UClass* ActorClass, const UObject* Sample, TArray<FDraftComponent>& Out)
		{
			auto AddUniqueComponent = [&Out](const FString& Name, const UObject* Template)
			{
				if (!Template || Name.IsEmpty()) return;
				if (Out.ContainsByPredicate([&Name](const FDraftComponent& D) { return D.Name == Name; })) return;
				FDraftComponent D;
				D.Name = Name;
				D.Template = Template;
				D.Class = Template->GetClass();
				Out.Add(D);
			};
			const AActor* Actor = Cast<AActor>(Sample);
			if (Actor)
			{
				for (const UActorComponent* C : Actor->GetComponents())
				{
					if (C) AddUniqueComponent(C->GetName(), C);
				}
				TArray<UObject*> Subobjects;
				const_cast<AActor*>(Actor)->GetDefaultSubobjects(Subobjects);
				for (const UObject* O : Subobjects)
				{
					if (Cast<UActorComponent>(O)) AddUniqueComponent(O->GetName(), O);
				}
			}
			// A class default holds Blueprint components only as construction script templates.
			if (!Actor || Actor->HasAnyFlags(RF_ClassDefaultObject))
			{
				for (UClass* C = ActorClass; C; C = C->GetSuperClass())
				{
					const UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C);
					if (!BPGC || !BPGC->SimpleConstructionScript) continue;
					for (const USCS_Node* Node : BPGC->SimpleConstructionScript->GetAllNodes())
					{
						if (Node && Node->ComponentTemplate) AddUniqueComponent(Node->GetVariableName().ToString(), Node->ComponentTemplate);
					}
				}
			}
		}

		FScriptLookup DraftCheckSubsystem(const FString& Rest)
		{
			FScriptLookup L;
			FString ClassName, PropPath;
			TArray<FString> Names;
			UClass* Found = nullptr;
			const bool bSplit = Rest.Split(TEXT("."), &ClassName, &PropPath);
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* C = *It;
				if (!C->IsChildOf(UGameInstanceSubsystem::StaticClass())
					&& !C->IsChildOf(ULocalPlayerSubsystem::StaticClass())
					&& !C->IsChildOf(UWorldSubsystem::StaticClass())) continue;
				if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
				const FString N = C->GetName();
				if (N.StartsWith(TEXT("SKEL_")) || N.StartsWith(TEXT("REINST_")) || N.StartsWith(TEXT("TRASH_"))) continue;
				Names.AddUnique(N);
				if (bSplit && !Found && (N == ClassName || N == (FString(TEXT("U")) + ClassName) || C->GetPathName() == ClassName)) Found = C;
			}
			if (!bSplit || PropPath.IsEmpty())
			{
				L.Message = TEXT("a sub: value is sub:<Subsystem>.<property path>");
				return L;
			}
			if (!Found)
			{
				L.Message = FString::Printf(TEXT("no game instance, local player or world subsystem class is named '%s'"), *ClassName);
				for (const FString& N : RankScriptCandidates(ClassName, Names)) L.Candidates.Add(FString::Printf(TEXT("sub:%s.%s"), *N, *PropPath));
				return L;
			}
			L = ResolveScriptPathOnClass(Found, Found->GetDefaultObject(), PropPath);
			L.Candidates = DraftCheckPrefixed(FString::Printf(TEXT("sub:%s"), *Found->GetName()), L.Candidates);
			return L;
		}

		struct FDraftPawn
		{
			TWeakObjectPtr<UClass> Class;
			TWeakObjectPtr<const UObject> Sample;
			FString Source;
			// Classes the game mode spawns, which exists can name.
			TArray<TWeakObjectPtr<UClass>> Spawned;
		};

		FDraftPawn DraftCheckFindPawn(UWorld* World)
		{
			FDraftPawn P;
			UClass* GameModeClass = nullptr;
			if (World)
			{
				if (const AWorldSettings* WS = World->GetWorldSettings()) GameModeClass = WS->DefaultGameMode.Get();
			}
			if (!GameModeClass && GConfig)
			{
				FString Path;
				GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), Path, GEngineIni);
				if (!Path.IsEmpty()) GameModeClass = LoadClass<AGameModeBase>(nullptr, *Path);
			}
			if (!GameModeClass) GameModeClass = AGameModeBase::StaticClass();
			const AGameModeBase* GM = GameModeClass->GetDefaultObject<AGameModeBase>();
			if (GM)
			{
				P.Spawned.Add(TWeakObjectPtr<UClass>(GameModeClass));
				P.Spawned.Add(TWeakObjectPtr<UClass>(GM->DefaultPawnClass.Get()));
				P.Spawned.Add(TWeakObjectPtr<UClass>(GM->PlayerControllerClass.Get()));
				P.Spawned.Add(TWeakObjectPtr<UClass>(GM->GameStateClass.Get()));
				P.Spawned.Add(TWeakObjectPtr<UClass>(GM->PlayerStateClass.Get()));
				P.Spawned.Add(TWeakObjectPtr<UClass>(GM->HUDClass.Get()));
			}
			if (World)
			{
				for (TActorIterator<APawn> It(World); It; ++It)
				{
					if (It->AutoPossessPlayer == EAutoReceiveInput::Player0)
					{
						P.Class = It->GetClass();
						P.Sample = *It;
						P.Source = FString::Printf(TEXT("the pawn %s in the level, which player 0 possesses"), *It->GetActorLabel());
						return P;
					}
				}
			}
			if (GM && GM->DefaultPawnClass.Get())
			{
				P.Class = GM->DefaultPawnClass.Get();
				P.Sample = P.Class->GetDefaultObject();
				P.Source = FString::Printf(TEXT("%s, the DefaultPawnClass of %s"), *P.Class->GetName(), *GameModeClass->GetName());
			}
			return P;
		}

		FScriptLookup DraftCheckBone(const FString& Prefix, const FString& Spec, const FDraftPawn& Pawn)
		{
			FScriptLookup L;
			TArray<FString> Channels;
			for (const TCHAR* C : DraftCheckBoneChannels) Channels.Add(C);
			FString Bone, Channel;
			if (!Spec.Split(TEXT("."), &Bone, &Channel, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				L.Message = FString::Printf(TEXT("a %s value is %s<bone or socket>.<channel>, channel one of %s"), *Prefix, *Prefix, *FString::Join(Channels, TEXT(", ")));
				return L;
			}
			Bone.TrimStartAndEndInline();
			Channel = Channel.TrimStartAndEnd().ToLower();
			if (!Channels.Contains(Channel))
			{
				L.Message = FString::Printf(TEXT("channel '%s' is not one of %s"), *Channel, *FString::Join(Channels, TEXT(", ")));
				for (const FString& C : RankScriptCandidates(Channel, Channels)) L.Candidates.Add(Prefix + Bone + TEXT(".") + C);
				return L;
			}
			if (!Pawn.Class.IsValid())
			{
				L.Message = TEXT("there is no player pawn to read a bone from");
				return L;
			}
			TArray<FDraftComponent> Components;
			DraftCheckComponents(Pawn.Class.Get(), Pawn.Sample.Get(), Components);
			const USkeletalMeshComponent* MeshComp = nullptr;
			for (const FDraftComponent& C : Components)
			{
				MeshComp = Cast<USkeletalMeshComponent>(C.Template);
				if (MeshComp) break;
			}
			if (!MeshComp)
			{
				L.Message = FString::Printf(TEXT("the player pawn (%s) has no skeletal mesh component"), *Pawn.Source);
				return L;
			}
			const USkeletalMesh* Mesh = MeshComp->GetSkeletalMeshAsset();
			if (!Mesh)
			{
				L.Message = FString::Printf(TEXT("the player pawn's skeletal mesh component %s has no mesh set before play"), *MeshComp->GetName());
				return L;
			}
			const FName BoneName(*Bone);
			if (Mesh->GetRefSkeleton().FindBoneIndex(BoneName) != INDEX_NONE || Mesh->FindSocket(BoneName))
			{
				L.bFound = true;
				L.Info.Kind = EScriptValueKind::Number;
				L.Info.TypeName = TEXT("float");
				return L;
			}
			TArray<FString> Names;
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			for (int32 I = 0; I < Ref.GetNum(); ++I) Names.AddUnique(Ref.GetBoneName(I).ToString());
			for (const USkeletalMeshSocket* S : Mesh->GetActiveSocketList())
			{
				if (S) Names.AddUnique(S->SocketName.ToString());
			}
			L.Message = FString::Printf(TEXT("%s has no bone or socket named '%s'"), *Mesh->GetName(), *Bone);
			for (const FString& N : RankScriptCandidates(Bone, Names)) L.Candidates.Add(Prefix + N + TEXT(".") + Channel);
			return L;
		}

		FScriptLookup DraftCheckActor(UWorld* World, const FDraftPawn& Pawn, const FString& Id)
		{
			FScriptLookup L;
			L.Info.Kind = EScriptValueKind::Boolean;
			if (World && FindActorById(World, Id))
			{
				L.bFound = true;
				return L;
			}
			TArray<FString> Names;
			for (const TWeakObjectPtr<UClass>& Weak : Pawn.Spawned)
			{
				const UClass* C = Weak.Get();
				if (!C) continue;
				if (C->GetName() == Id || C->GetPathName() == Id)
				{
					L.bFound = true;
					return L;
				}
				Names.AddUnique(C->GetName());
			}
			if (World)
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					Names.AddUnique(It->GetActorLabel());
				}
			}
			L.Message = FString::Printf(TEXT("no actor in the level %s has the label, name or class '%s', and the game mode spawns no class of that name"),
				World ? *World->GetMapName() : TEXT("(none)"), *Id);
			L.Candidates = RankScriptCandidates(Id, Names);
			return L;
		}

		FScriptLookup DraftCheckWidget(const FString& Spec)
		{
			FScriptLookup L;
			L.Info.Kind = EScriptValueKind::Boolean;
			if (MCPResolveClassOfType(Spec, UWidget::StaticClass(), /*bAllowLoad=*/true))
			{
				L.bFound = true;
				return L;
			}
			IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			TArray<FAssetData> Assets;
			AR.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Assets, true);
			TArray<FString> Names;
			for (const FAssetData& A : Assets)
			{
				const FString Name = A.AssetName.ToString();
				if (Name.Equals(Spec, ESearchCase::IgnoreCase))
				{
					L.bFound = true;
					return L;
				}
				Names.AddUnique(Name);
			}
			// A widget object name: the observer matches it against every widget in the world.
			for (const FAssetData& A : Assets)
			{
				if (!A.PackageName.ToString().StartsWith(TEXT("/Game"))) continue;
				const UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(A.GetAsset());
				if (!WBP || !WBP->WidgetTree) continue;
				bool bMatch = false;
				WBP->WidgetTree->ForEachWidget([&](UWidget* W)
				{
					if (!W) return;
					Names.AddUnique(W->GetName());
					if (W->GetName().Equals(Spec, ESearchCase::IgnoreCase)) bMatch = true;
				});
				if (bMatch)
				{
					L.bFound = true;
					return L;
				}
			}
			L.Message = FString::Printf(TEXT("no widget class or Widget Blueprint is named '%s', and no Widget Blueprint under /Game holds a widget of that name"), *Spec);
			L.Candidates = RankScriptCandidates(Spec, Names);
			return L;
		}

		FString DraftCheckTargetText(const FScriptExpect& E)
		{
			if (E.bTargetIsBool) return E.Target != 0.0 ? TEXT("true") : TEXT("false");
			return FString::SanitizeFloat(E.Target);
		}

		// The op and target against the type of the value they read.
		void DraftCheckOpForValue(int32 Step, const FScriptExpect& E, const FString& Path, const FScriptValueInfo& V, TArray<FScriptStepIssue>& Out)
		{
			const FString Op = ScriptExpectOpToString(E.Op);
			const FString Where = FString::Printf(TEXT("steps[%d]"), Step);
			switch (V.Kind)
			{
			case EScriptValueKind::Boolean:
				if (E.Op != EScriptExpectOp::Eq)
				{
					Out.Add(DraftCheckIssue(Step, TEXT("expect.op"), FString::Printf(
						TEXT("%s.expect.op: '%s' is a bool, and %s compares numbers; use eq with target true or false"), *Where, *Path, *Op), { TEXT("eq") }));
				}
				else if (!E.bTargetIsBool && E.Target != 0.0 && E.Target != 1.0)
				{
					Out.Add(DraftCheckIssue(Step, TEXT("expect.target"), FString::Printf(
						TEXT("%s.expect.target: '%s' is a bool, so eq takes target true or false, not %s"), *Where, *Path, *DraftCheckTargetText(E)), { TEXT("true"), TEXT("false") }));
				}
				return;
			case EScriptValueKind::Enum:
			{
				TArray<FString> Values;
				for (const TPair<FString, int64>& EV : V.EnumValues) Values.Add(FString::Printf(TEXT("%lld (%s)"), EV.Value, *EV.Key));
				if (E.Op != EScriptExpectOp::Eq)
				{
					Out.Add(DraftCheckIssue(Step, TEXT("expect.op"), FString::Printf(
						TEXT("%s.expect.op: '%s' is the enum %s, which only eq compares, with a target that is one of its values"), *Where, *Path, *V.TypeName), { TEXT("eq") }));
					return;
				}
				const bool bIsValue = !E.bTargetIsBool && V.EnumValues.ContainsByPredicate([&E](const TPair<FString, int64>& EV)
				{
					return static_cast<double>(EV.Value) == E.Target;
				});
				if (!bIsValue)
				{
					Out.Add(DraftCheckIssue(Step, TEXT("expect.target"), FString::Printf(
						TEXT("%s.expect.target: %s is not a value of the enum %s that '%s' holds"), *Where, *DraftCheckTargetText(E), *V.TypeName, *Path), Values));
				}
				return;
			}
			case EScriptValueKind::Number:
			case EScriptValueKind::Integer:
				if (E.bHasTarget && E.bTargetIsBool)
				{
					Out.Add(DraftCheckIssue(Step, TEXT("expect.target"), FString::Printf(
						TEXT("%s.expect.target: '%s' is a %s, so %s takes a number target, not %s"), *Where, *Path, *V.TypeName, *Op, *DraftCheckTargetText(E))));
					return;
				}
				if (V.Kind != EScriptValueKind::Integer || !E.bHasTarget) return;
				if (E.Op == EScriptExpectOp::Eq && E.Target != FMath::RoundToDouble(E.Target))
				{
					Out.Add(DraftCheckIssue(Step, TEXT("expect.target"), FString::Printf(
						TEXT("%s.expect.target: '%s' is a %s and never equals the fractional target %s"), *Where, *Path, *V.TypeName, *DraftCheckTargetText(E))));
				}
				else if (E.Op == EScriptExpectOp::Eq && (E.Target < V.Min || E.Target > V.Max))
				{
					Out.Add(DraftCheckIssue(Step, TEXT("expect.target"), FString::Printf(
						TEXT("%s.expect.target: '%s' is a %s, which holds %s to %s, so it never equals %s"), *Where, *Path, *V.TypeName,
						*FString::SanitizeFloat(V.Min), *FString::SanitizeFloat(V.Max), *DraftCheckTargetText(E))));
				}
				else if (E.Op == EScriptExpectOp::Gt && E.Target >= V.Max)
				{
					Out.Add(DraftCheckIssue(Step, TEXT("expect.target"), FString::Printf(
						TEXT("%s.expect.target: '%s' is a %s and is never greater than %s"), *Where, *Path, *V.TypeName, *DraftCheckTargetText(E))));
				}
				else if (E.Op == EScriptExpectOp::Lt && E.Target <= V.Min)
				{
					Out.Add(DraftCheckIssue(Step, TEXT("expect.target"), FString::Printf(
						TEXT("%s.expect.target: '%s' is a %s and is never less than %s"), *Where, *Path, *V.TypeName, *DraftCheckTargetText(E))));
				}
				return;
			}
		}
	}

	TArray<FString> RankScriptCandidates(const FString& Needle, const TArray<FString>& Pool, int32 MaxResults)
	{
		struct FScored { int32 Score; FString Name; };
		const FString Lower = Needle.ToLower();
		TArray<FScored> Scored;
		for (const FString& Name : Pool)
		{
			if (Name.IsEmpty()) continue;
			const FString L = Name.ToLower();
			// A name that contains the needle, or that the needle contains, ranks ahead of edit distance alone.
			const bool bContains = !Lower.IsEmpty() && (L.Contains(Lower) || (L.Len() >= 3 && Lower.Contains(L)));
			Scored.Add({ DraftCheckDistance(Lower, L) - (bContains ? 1000 : 0), Name });
		}
		Scored.Sort([](const FScored& A, const FScored& B)
		{
			if (A.Score != B.Score) return A.Score < B.Score;
			if (A.Name.Len() != B.Name.Len()) return A.Name.Len() < B.Name.Len();
			return A.Name < B.Name;
		});
		TArray<FString> Out;
		for (const FScored& S : Scored)
		{
			if (Out.Num() >= MaxResults) break;
			Out.AddUnique(S.Name);
		}
		return Out;
	}

	bool ParseScriptStepsJsonIssues(const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FScriptStepDef>& Out,
		TArray<FScriptStepIssue>& OutIssues)
	{
		Out.Reset();
		if (Values.Num() == 0)
		{
			FScriptStepIssue Issue = DraftCheckIssue(INDEX_NONE, TEXT("steps"), TEXT("steps must contain at least one step"));
			Issue.bBlocksSave = true;
			OutIssues.Add(MoveTemp(Issue));
			return false;
		}
		bool bOk = true;
		for (int32 I = 0; I < Values.Num(); ++I)
		{
			FScriptStepDef Def;
			FString Err, Field;
			if (ParseScriptStepJson(Values[I], I, Def, Err, &Field))
			{
				Out.Add(MoveTemp(Def));
				continue;
			}
			bOk = false;
			FScriptStepIssue Issue = DraftCheckIssue(I, Field, Err);
			Issue.bBlocksSave = true;
			if (Field == TEXT("judge"))
			{
				Issue.Candidates = { TEXT("auto"), TEXT("manual") };
			}
			else if (Field == TEXT("expect.op"))
			{
				const TSharedPtr<FJsonObject>* StepObj = nullptr;
				const TSharedPtr<FJsonObject>* ExpObj = nullptr;
				FString Op;
				TArray<FString> Ops;
				for (const TCHAR* O : DraftCheckOps) Ops.Add(O);
				if (Values[I].IsValid() && Values[I]->TryGetObject(StepObj) && (*StepObj)->TryGetObjectField(TEXT("expect"), ExpObj)
					&& (*ExpObj)->TryGetStringField(TEXT("op"), Op) && !Op.IsEmpty())
				{
					Issue.Candidates = RankScriptCandidates(Op, Ops, 3);
				}
				else
				{
					Issue.Candidates = Ops;
				}
			}
			OutIssues.Add(MoveTemp(Issue));
			Out.Add(FScriptStepDef());
		}
		return bOk;
	}

	namespace
	{
		void DraftCheckLookup(int32 Step, const FString& Field, const FString& What, const FString& Name,
			const TFunction<FScriptLookup(const FString&)>& Resolve, const FString& Hint, TArray<FScriptStepIssue>& Out)
		{
			const FScriptLookup L = Resolve ? Resolve(Name) : FScriptLookup();
			if (L.bFound) return;
			const FString Why = L.Message.IsEmpty() ? FString(TEXT("nothing to resolve it against")) : L.Message;
			Out.Add(DraftCheckIssue(Step, Field, FString::Printf(TEXT("steps[%d].%s: %s '%s' cannot be found: %s.%s"),
				Step, *Field, *What, *Name, *Why, *Hint), L.Candidates));
		}

		// The actors, actor classes and Input Actions a step's arrange and act name, resolved the way the run will.
		void DraftCheckArrangeAndAct(int32 Step, const FScriptStepDef& S, const FScriptValidationContext& Ctx,
			const FString& InPIEHint, TArray<FScriptStepIssue>& Out)
		{
			// Assets resolve without PIE; actors may only exist once it runs.
			for (int32 J = 0; J < S.Arrange.Num(); ++J)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!S.Arrange[J].IsValid() || !S.Arrange[J]->TryGetObject(O) || !O) continue;
				FString Kind, Name;
				(*O)->TryGetStringField(TEXT("kind"), Kind);
				Kind = Kind.ToLower();
				const FString Where = FString::Printf(TEXT("arrange[%d]"), J);
				if (Kind == TEXT("spawn") && (*O)->TryGetStringField(TEXT("class"), Name))
				{
					DraftCheckLookup(Step, Where + TEXT(".class"), TEXT("actor class"), Name, Ctx.ResolveActorClass, FString(), Out);
				}
				else if ((Kind == TEXT("set") || Kind == TEXT("call")) && !S.bResolveInPIE && (*O)->TryGetStringField(TEXT("target"), Name))
				{
					DraftCheckLookup(Step, Where + TEXT(".target"), TEXT("actor"), Name, Ctx.ResolveActor, InPIEHint, Out);
				}
			}
			for (int32 J = 0; J < S.Act.Num(); ++J)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				FStepAction A;
				FString Err;
				if (!S.Act[J].IsValid() || !S.Act[J]->TryGetObject(O) || !O || !ParseStepAction(*O, A, Err)) continue;
				const FString Where = FString::Printf(TEXT("act[%d].%s"), J, *A.Verb);
				if (A.UsesInputAction())
				{
					DraftCheckLookup(Step, Where + TEXT(".action"), TEXT("Input Action"), A.ActionPath, Ctx.ResolveInputAction, FString(), Out);
				}
				else if (A.Kind == FStepAction::EKind::Place && !A.At.IsEmpty() && !S.bResolveInPIE)
				{
					DraftCheckLookup(Step, Where + TEXT(".at"), TEXT("actor"), A.At, Ctx.ResolveActor, InPIEHint, Out);
				}
			}
		}
	}

	void ValidateScriptSteps(const TArray<FScriptStepDef>& Steps, bool bManualOnly, const FScriptValidationContext& Ctx,
		TArray<FScriptStepIssue>& OutIssues, const TSet<int32>& SkipSteps)
	{
		const FString InPIEHint = TEXT(" If it only exists once PIE is running, set resolve_in_pie: true on the step.");
		int32 Checkable = 0;
		for (int32 I = 0; I < Steps.Num(); ++I)
		{
			if (SkipSteps.Contains(I)) continue;
			const FScriptStepDef& S = Steps[I];
			DraftCheckArrangeAndAct(I, S, Ctx, InPIEHint, OutIssues);
			if (S.Judge != EScriptJudge::Auto || !S.bHasExpect) continue;
			++Checkable;
			const FScriptExpect& E = S.Expect;
			const FString Op = ScriptExpectOpToString(E.Op);
			const FString Where = FString::Printf(TEXT("steps[%d]"), I);

			if (IsScriptBooleanOp(E.Op))
			{
				if (E.bHasTarget && !E.bTargetIsBool && E.Target != 0.0 && E.Target != 1.0)
				{
					OutIssues.Add(DraftCheckIssue(I, TEXT("expect.target"), FString::Printf(
						TEXT("%s.expect.target: %s takes target true or false, not %s"), *Where, *Op, *DraftCheckTargetText(E)), { TEXT("true"), TEXT("false") }));
				}
				if (S.bResolveInPIE) continue;
				const bool bVisible = E.Op == EScriptExpectOp::Visible;
				const TFunction<FScriptLookup(const FString&)>& Resolve = bVisible ? Ctx.ResolveWidget : Ctx.ResolveActor;
				const FScriptLookup L = Resolve ? Resolve(E.Value) : FScriptLookup();
				if (!L.bFound)
				{
					const FString Why = L.Message.IsEmpty() ? FString(TEXT("nothing to resolve it against")) : L.Message;
					OutIssues.Add(DraftCheckIssue(I, TEXT("expect.value"), FString::Printf(
						TEXT("%s.expect.value: %s '%s' cannot be checked: %s.%s"), *Where, bVisible ? TEXT("widget") : TEXT("actor"), *E.Value, *Why, *InPIEHint),
						L.Candidates));
				}
				continue;
			}

			// A value resolved in PIE has no type yet; the parser already checked its target shape.
			if (S.bResolveInPIE) continue;

			// The observer reads a profile tracked value first: the exact path, then a last-segment match.
			FString Path = E.Value;
			bool bFromProfile = false;
			for (const FString& P : Ctx.ProfileValuePaths)
			{
				if (P.Equals(E.Value, ESearchCase::IgnoreCase)) { Path = P; bFromProfile = true; break; }
			}
			if (!bFromProfile)
			{
				for (const FString& P : Ctx.ProfileValuePaths)
				{
					if (TrackedValuePathMatches(P, E.Value)) { Path = P; bFromProfile = true; break; }
				}
			}

			const FScriptLookup L = Ctx.ResolveValue ? Ctx.ResolveValue(Path) : FScriptLookup();
			if (!L.bFound)
			{
				const FString Why = L.Message.IsEmpty() ? FString(TEXT("nothing to resolve it against")) : L.Message;
				const FString Msg = bFromProfile
					? FString::Printf(TEXT("%s.expect.value: '%s' reads the profile value '%s', which cannot be read: %s.%s"), *Where, *E.Value, *Path, *Why, *InPIEHint)
					: FString::Printf(TEXT("%s.expect.value: '%s' cannot be read: %s.%s"), *Where, *E.Value, *Why, *InPIEHint);
				TArray<FString> Candidates = RankScriptCandidates(E.Value, Ctx.ProfileValuePaths, 2);
				for (const FString& C : L.Candidates)
				{
					if (Candidates.Num() >= 5) break;
					Candidates.AddUnique(C);
				}
				OutIssues.Add(DraftCheckIssue(I, TEXT("expect.value"), Msg, Candidates));
				continue;
			}
			DraftCheckOpForValue(I, E, Path, L.Info, OutIssues);
		}

		if (!bManualOnly && SkipSteps.Num() == 0 && Checkable == 0)
		{
			OutIssues.Add(DraftCheckIssue(INDEX_NONE, TEXT("steps"),
				TEXT("The script has no auto step with an expect, so the observer checks nothing in it. Add one, or pass manual_only: true for a script judged only by verdicts."),
				{ TEXT("manual_only") }));
		}
	}

	FScriptLookup ResolveScriptPathOnClass(UClass* RootClass, const UObject* Sample, const FString& Path)
	{
		FScriptLookup L;
		if (!RootClass)
		{
			L.Message = TEXT("there is no class to read it from");
			return L;
		}
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 0)
		{
			L.Message = TEXT("the path is empty");
			return L;
		}

		const bool bActorRoot = RootClass->IsChildOf(AActor::StaticClass());
		TArray<FDraftComponent> Components;
		bool bComponentsRead = false;
		auto ReadComponents = [&]()
		{
			if (!bComponentsRead) DraftCheckComponents(RootClass, Sample, Components);
			bComponentsRead = true;
		};
		auto FindComponent = [&](const FString& Name) -> const FDraftComponent*
		{
			ReadComponents();
			return Components.FindByPredicate([&Name](const FDraftComponent& D) { return D.Name == Name; });
		};

		const UStruct* Struct = RootClass;
		const void* Container = Sample;
		FString Walked;
		// Set when the walk continues from a property's declared class because no instance holds a value.
		FString DeclaredOnly;

		for (int32 I = 0; I < Parts.Num(); ++I)
		{
			const FString& Part = Parts[I];
			const bool bLast = I == Parts.Num() - 1;
			const FString Here = DraftCheckJoin(Walked, Part);
			const FProperty* Seg = Struct->FindPropertyByName(FName(*Part));

			// The observer takes a bare component name at the head when no property has it.
			if (!Seg && I == 0 && bActorRoot)
			{
				if (const FDraftComponent* C = FindComponent(Part))
				{
					if (bLast)
					{
						L.Message = FString::Printf(TEXT("%s is a component (%s), not a number; name one of its properties"), *Part, *C->Class->GetName());
						TArray<FString> Leaves;
						DraftCheckFieldNames(C->Class, true, Leaves);
						L.Candidates = DraftCheckPrefixed(Part, RankScriptCandidates(Part, Leaves));
						return L;
					}
					Struct = C->Class;
					Container = C->Template;
					Walked = Here;
					continue;
				}
			}

			if (!Seg)
			{
				TArray<FString> Names;
				DraftCheckFieldNames(Struct, bLast, Names);
				if (I == 0 && bActorRoot)
				{
					ReadComponents();
					for (const FDraftComponent& C : Components) Names.AddUnique(C.Name);
				}
				L.Message = Walked.IsEmpty()
					? FString::Printf(TEXT("%s has no property%s named %s"), *Struct->GetName(), bActorRoot ? TEXT(" or component") : TEXT(""), *Part)
					: FString::Printf(TEXT("%s (%s) has no property named %s"), *Walked, *Struct->GetName(), *Part);
				if (!DeclaredOnly.IsEmpty())
				{
					L.Message += FString::Printf(TEXT(" (%s is declared as %s and holds nothing before play; a subclass assigned at run time may have it)"), *DeclaredOnly, *Struct->GetName());
				}
				L.Candidates = DraftCheckPrefixed(Walked, RankScriptCandidates(Part, Names));
				return L;
			}

			const void* ValuePtr = Container ? Seg->ContainerPtrToValuePtr<void>(Container) : nullptr;
			if (bLast)
			{
				if (DraftCheckLeaf(Seg, L.Info))
				{
					L.bFound = true;
					return L;
				}
				L.Message = FString::Printf(TEXT("%s is a %s, which the observer cannot read as a number"), *Here, *Seg->GetCPPType());
				TArray<FString> Leaves;
				if (const FStructProperty* SP = CastField<FStructProperty>(Seg))
				{
					DraftCheckFieldNames(SP->Struct, true, Leaves);
				}
				else if (const FObjectProperty* OP = CastField<FObjectProperty>(Seg))
				{
					const UObject* Sub = ValuePtr ? OP->GetObjectPropertyValue(ValuePtr) : nullptr;
					DraftCheckFieldNames(Sub ? Sub->GetClass() : static_cast<UClass*>(OP->PropertyClass), true, Leaves);
				}
				if (Leaves.Num() > 5) Leaves.SetNum(5);
				L.Candidates = DraftCheckPrefixed(Here, Leaves);
				return L;
			}

			if (const FStructProperty* SP = CastField<FStructProperty>(Seg))
			{
				Struct = SP->Struct;
				Container = ValuePtr;
			}
			else if (const FObjectProperty* OP = CastField<FObjectProperty>(Seg))
			{
				const UObject* Sub = ValuePtr ? OP->GetObjectPropertyValue(ValuePtr) : nullptr;
				const FDraftComponent* C = (!Sub && I == 0 && bActorRoot) ? FindComponent(Part) : nullptr;
				if (Sub)
				{
					Struct = Sub->GetClass();
					Container = Sub;
				}
				else if (C)
				{
					// A Blueprint component variable is empty on the class default; its template stands in.
					Struct = C->Class;
					Container = C->Template;
				}
				else
				{
					Struct = static_cast<UClass*>(OP->PropertyClass);
					Container = nullptr;
					DeclaredOnly = Here;
				}
				if (!Struct)
				{
					L.Message = FString::Printf(TEXT("%s has no class to continue into"), *Here);
					return L;
				}
			}
			else
			{
				L.Message = FString::Printf(TEXT("%s is a %s; only struct and object properties lead further"), *Here, *Seg->GetCPPType());
				return L;
			}
			Walked = Here;
		}
		return L;
	}

	FScriptValidationContext MakeEditorScriptValidationContext(const FString& ProfilePath)
	{
		FScriptValidationContext Ctx;
		if (!ProfilePath.IsEmpty())
		{
			if (const UMCPPIEObservationProfile* Profile = LoadObject<UMCPPIEObservationProfile>(nullptr, *ProfilePath))
			{
				for (const FMCPPIETrackedValueEntry& E : Profile->TrackedValues)
				{
					if (!E.Path.IsEmpty()) Ctx.ProfileValuePaths.Add(E.Path);
				}
			}
		}

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		const FDraftPawn Pawn = DraftCheckFindPawn(World);

		Ctx.ResolveValue = [Pawn](const FString& Path) -> FScriptLookup
		{
			if (Path.StartsWith(TEXT("sub:"))) return DraftCheckSubsystem(Path.RightChop(4));
			if (Path.StartsWith(TEXT("bone:"))) return DraftCheckBone(TEXT("bone:"), Path.RightChop(5), Pawn);
			if (Path.StartsWith(TEXT("bonecs:"))) return DraftCheckBone(TEXT("bonecs:"), Path.RightChop(7), Pawn);
			if (!Pawn.Class.IsValid())
			{
				FScriptLookup L;
				L.Message = TEXT("there is no player pawn to read it from: no pawn in the level is possessed by player 0, and the game mode has no DefaultPawnClass");
				return L;
			}
			FScriptLookup L = ResolveScriptPathOnClass(Pawn.Class.Get(), Pawn.Sample.Get(), Path);
			if (!L.bFound) L.Message += FString::Printf(TEXT(" (the observer reads it from the player pawn, %s)"), *Pawn.Source);
			return L;
		};
		Ctx.ResolveActor = [WeakWorld, Pawn](const FString& Id) -> FScriptLookup
		{
			return DraftCheckActor(WeakWorld.Get(), Pawn, Id);
		};
		Ctx.ResolveWidget = [](const FString& Widget) -> FScriptLookup
		{
			return DraftCheckWidget(Widget);
		};
		Ctx.ResolveInputAction = [](const FString& Path) -> FScriptLookup
		{
			FScriptLookup L;
			if (Cast<UInputAction>(FSoftObjectPath(Path).TryLoad()))
			{
				L.bFound = true;
				return L;
			}
			TArray<FString> Paths;
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			TArray<FAssetData> Assets;
			ARM.Get().GetAssetsByClass(UInputAction::StaticClass()->GetClassPathName(), Assets, true);
			for (const FAssetData& A : Assets) Paths.Add(A.GetSoftObjectPath().ToString());
			L.Message = TEXT("no Input Action loads from that path (an object path such as /Game/Input/IA_Jump.IA_Jump)");
			L.Candidates = RankScriptCandidates(Path, Paths);
			return L;
		};
		Ctx.ResolveActorClass = [](const FString& Path) -> FScriptLookup
		{
			FScriptLookup L;
			FString Err;
			L.bFound = FPIEActorPuppet::ResolveActorClass(Path, Err) != nullptr;
			if (!L.bFound) L.Message = Err;
			return L;
		};
		return Ctx;
	}

	namespace
	{
		const TCHAR* DraftCheckKindName(EScriptValueKind Kind)
		{
			switch (Kind)
			{
			case EScriptValueKind::Number: return TEXT("number");
			case EScriptValueKind::Integer: return TEXT("integer");
			case EScriptValueKind::Boolean: return TEXT("boolean");
			case EScriptValueKind::Enum: return TEXT("enum");
			}
			return TEXT("number");
		}

		TSharedRef<FJsonObject> DraftCheckValueToJson(const FString& Path, const FScriptValueInfo& V)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), Path);
			O->SetStringField(TEXT("kind"), DraftCheckKindName(V.Kind));
			O->SetStringField(TEXT("type"), V.TypeName);
			if (V.Kind == EScriptValueKind::Enum)
			{
				TArray<TSharedPtr<FJsonValue>> Values;
				for (const TPair<FString, int64>& EV : V.EnumValues)
				{
					TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
					E->SetStringField(TEXT("name"), EV.Key);
					E->SetNumberField(TEXT("value"), static_cast<double>(EV.Value));
					Values.Add(MakeShared<FJsonValueObject>(E));
				}
				O->SetArrayField(TEXT("values"), Values);
			}
			return O;
		}

		// Readable paths under Struct, the way the observer walks them: every leaf first, then one
		// level into structs and objects (the instance standing in for the declared class when there is one),
		// so a limit cuts nested fields before it cuts a value the class declares itself.
		void DraftCheckEnumerate(const UStruct* Struct, const void* Container, const FString& Prefix, int32 Depth,
			int32 Limit, TArray<TSharedPtr<FJsonValue>>& Out)
		{
			if (!Struct) return;
			for (TFieldIterator<FProperty> It(Struct); It && Out.Num() < Limit; ++It)
			{
				FScriptValueInfo Info;
				if (!It->HasAnyPropertyFlags(CPF_Deprecated) && DraftCheckLeaf(*It, Info))
				{
					Out.Add(MakeShared<FJsonValueObject>(DraftCheckValueToJson(DraftCheckJoin(Prefix, It->GetName()), Info)));
				}
			}
			if (Depth <= 0) return;
			for (TFieldIterator<FProperty> It(Struct); It && Out.Num() < Limit; ++It)
			{
				const FProperty* P = *It;
				if (P->HasAnyPropertyFlags(CPF_Deprecated)) continue;
				const FString Path = DraftCheckJoin(Prefix, P->GetName());
				const void* ValuePtr = Container ? P->ContainerPtrToValuePtr<void>(Container) : nullptr;
				if (const FStructProperty* SP = CastField<FStructProperty>(P))
				{
					DraftCheckEnumerate(SP->Struct, ValuePtr, Path, Depth - 1, Limit, Out);
				}
				else if (const FObjectProperty* OP = CastField<FObjectProperty>(P))
				{
					// Only objects the class default already holds, such as default subobject components.
					const UObject* Sub = ValuePtr ? OP->GetObjectPropertyValue(ValuePtr) : nullptr;
					if (Sub && Sub->IsA<UActorComponent>()) DraftCheckEnumerate(Sub->GetClass(), Sub, Path, Depth - 1, Limit, Out);
				}
			}
		}
	}

	TSharedRef<FJsonObject> ScriptWatchableValuesToJson(const FString& TargetClass, const FString& ProfilePath, int32 Limit)
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		Limit = FMath::Clamp(Limit, 1, 5000);
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const FDraftPawn Pawn = DraftCheckFindPawn(World);

		TSharedRef<FJsonObject> PawnObj = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Values;
		if (UClass* PawnClass = Pawn.Class.Get())
		{
			PawnObj->SetStringField(TEXT("class"), PawnClass->GetPathName());
			PawnObj->SetStringField(TEXT("source"), Pawn.Source);
			DraftCheckEnumerate(PawnClass, Pawn.Sample.Get(), FString(), 1, Limit, Values);
			TArray<FDraftComponent> Components;
			DraftCheckComponents(PawnClass, Pawn.Sample.Get(), Components);
			TArray<TSharedPtr<FJsonValue>> ComponentNames;
			for (const FDraftComponent& C : Components)
			{
				ComponentNames.Add(MakeShared<FJsonValueString>(C.Name));
				// A component the class holds only by name (a Blueprint component, or a native one
				// with no property) is read through the observer's head-segment fallback.
				if (!PawnClass->FindPropertyByName(FName(*C.Name)))
				{
					DraftCheckEnumerate(C.Class, C.Template, C.Name, 0, Limit, Values);
				}
			}
			PawnObj->SetArrayField(TEXT("components"), ComponentNames);
		}
		R->SetObjectField(TEXT("pawn"), PawnObj);
		R->SetArrayField(TEXT("values"), Values);
		R->SetBoolField(TEXT("truncated"), Values.Num() >= Limit);

		TArray<TSharedPtr<FJsonValue>> ProfileValues;
		if (!ProfilePath.IsEmpty())
		{
			if (const UMCPPIEObservationProfile* Profile = LoadObject<UMCPPIEObservationProfile>(nullptr, *ProfilePath))
			{
				for (const FMCPPIETrackedValueEntry& E : Profile->TrackedValues)
				{
					if (!E.Path.IsEmpty()) ProfileValues.Add(MakeShared<FJsonValueString>(E.Path));
				}
			}
		}
		R->SetArrayField(TEXT("profile_values"), ProfileValues);

		TArray<TSharedPtr<FJsonValue>> Actors;
		UClass* Target = TargetClass.IsEmpty() ? nullptr : MCPResolveClassOfType(TargetClass, AActor::StaticClass(), /*bAllowLoad=*/true);
		TArray<TSharedPtr<FJsonValue>> TargetActors;
		if (World)
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (Actors.Num() < Limit) Actors.Add(MakeShared<FJsonValueString>(It->GetActorLabel()));
				if (Target && It->IsA(Target)) TargetActors.Add(MakeShared<FJsonValueString>(It->GetActorLabel()));
			}
		}
		R->SetArrayField(TEXT("actors"), Actors);
		TArray<TSharedPtr<FJsonValue>> Spawned;
		for (const TWeakObjectPtr<UClass>& Weak : Pawn.Spawned)
		{
			if (const UClass* C = Weak.Get()) Spawned.Add(MakeShared<FJsonValueString>(C->GetName()));
		}
		R->SetArrayField(TEXT("spawned_classes"), Spawned);

		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		AR.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Assets, true);
		TArray<TSharedPtr<FJsonValue>> Widgets;
		for (const FAssetData& A : Assets)
		{
			if (Widgets.Num() >= Limit) break;
			if (A.PackageName.ToString().StartsWith(TEXT("/Game"))) Widgets.Add(MakeShared<FJsonValueString>(A.AssetName.ToString()));
		}
		R->SetArrayField(TEXT("widgets"), Widgets);

		if (!TargetClass.IsEmpty())
		{
			TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
			T->SetStringField(TEXT("requested"), TargetClass);
			T->SetBoolField(TEXT("found"), Target != nullptr);
			if (Target)
			{
				T->SetStringField(TEXT("class"), Target->GetPathName());
				const UClass* PawnClass = Pawn.Class.Get();
				T->SetBoolField(TEXT("is_player_pawn"), PawnClass && PawnClass->IsChildOf(Target));
				T->SetArrayField(TEXT("actors"), TargetActors);
			}
			R->SetObjectField(TEXT("target"), T);
		}
		return R;
	}

	TSharedRef<FJsonObject> ScriptIssueToJson(const FScriptStepIssue& Issue)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		if (Issue.Step == INDEX_NONE) O->SetField(TEXT("step"), MakeShared<FJsonValueNull>());
		else O->SetNumberField(TEXT("step"), Issue.Step);
		O->SetStringField(TEXT("field"), Issue.Field);
		O->SetStringField(TEXT("message"), Issue.Message);
		if (Issue.Candidates.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> C;
			for (const FString& S : Issue.Candidates) C.Add(MakeShared<FJsonValueString>(S));
			O->SetArrayField(TEXT("candidates"), C);
		}
		return O;
	}

	TArray<TSharedPtr<FJsonValue>> ScriptIssuesToJson(const TArray<FScriptStepIssue>& Issues)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FScriptStepIssue& Issue : Issues) Out.Add(MakeShared<FJsonValueObject>(ScriptIssueToJson(Issue)));
		return Out;
	}

	TSharedPtr<FJsonObject> ScriptIssuesErrorObject(const TArray<FScriptStepIssue>& Issues, const TCHAR* Lead)
	{
		TArray<FString> Lines;
		for (const FScriptStepIssue& Issue : Issues) Lines.Add(Issue.Message);
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("errorCode"), TEXT("INVALID_SCRIPT_STEPS"));
		Obj->SetStringField(TEXT("error"), FString::Printf(TEXT("%s (%d problem%s): %s"),
			Lead, Issues.Num(), Issues.Num() == 1 ? TEXT("") : TEXT("s"), *FString::Join(Lines, TEXT(" | "))));
		Obj->SetArrayField(TEXT("errors"), ScriptIssuesToJson(Issues));
		return Obj;
	}

	void SplitScriptIssues(const TArray<FScriptStepIssue>& Issues, TArray<FScriptStepIssue>& OutBlocking,
		TArray<FScriptStepIssue>& OutWarnings)
	{
		for (const FScriptStepIssue& Issue : Issues) (Issue.bBlocksSave ? OutBlocking : OutWarnings).Add(Issue);
	}
}
