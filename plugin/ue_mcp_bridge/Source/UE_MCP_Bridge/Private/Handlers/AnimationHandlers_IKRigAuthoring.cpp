// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationHandlers.h"
#include "HandlerUtils.h"

#if UE_MCP_HAS_5_8_API
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Rig/IKRigDataTypes.h"
#include "Rig/IKRigDefinition.h"
#include "Rig/IKRigSkeleton.h"
#include "Rig/Solvers/IKRigFullBodyIK.h"
#include "RigEditor/IKRigAutoCharacterizer.h"
#include "RigEditor/IKRigController.h"
#include "ScopedTransaction.h"
#include "StructUtils/InstancedStruct.h"
#endif

namespace UE_MCP_IKRigAuthoring
{
static TSharedPtr<FJsonValue> Error(
	const FString& Code,
	const FString& Message,
	const TOptional<bool>& RollbackSucceeded = TOptional<bool>())
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("errorCode"), Code);
	Result->SetStringField(TEXT("error"), Message);
	Result->SetBoolField(TEXT("rollbackSafe"), !RollbackSucceeded.IsSet() || RollbackSucceeded.GetValue());
	if (RollbackSucceeded.IsSet())
	{
		Result->SetBoolField(TEXT("rollbackAttempted"), true);
		Result->SetBoolField(TEXT("rollbackSucceeded"), RollbackSucceeded.GetValue());
	}
	return MCPResult(Result);
}

#if UE_MCP_HAS_5_8_API

constexpr int32 MaxChains = 256;
constexpr int32 MaxGoals = 256;
constexpr int32 MaxExclusions = 2048;

struct FChainRequest
{
	FName Name;
	FName StartBone;
	FName EndBone;
	FName Goal;
};

struct FGoalRequest
{
	FName Name;
	FName Bone;
	TOptional<float> PositionAlpha;
	TOptional<float> RotationAlpha;
	TOptional<int32> ChainDepth;
	TOptional<float> StrengthAlpha;
	TOptional<float> PullChainAlpha;
	TOptional<float> PinRotation;
};

struct FFullBodyRequest
{
	bool bPresent = false;
	TOptional<int32> SolverIndex;
	FName RootBone;
	TOptional<bool> bEnabled;
	TArray<FGoalRequest> Goals;
};

struct FExclusionRequest
{
	FName Bone;
	bool bExcluded = false;
};

static bool ReadRequiredString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& Out,
	FString& OutError)
{
	if (!Object.IsValid() || !Object->TryGetStringField(Field, Out))
	{
		OutError = FString::Printf(TEXT("'%s' must be a string"), Field);
		return false;
	}
	Out.TrimStartAndEndInline();
	if (Out.IsEmpty())
	{
		OutError = FString::Printf(TEXT("'%s' must not be empty"), Field);
		return false;
	}
	return true;
}

static bool ReadOptionalUnitFloat(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TOptional<float>& Out,
	FString& OutError)
{
	if (!Object->HasField(Field))
	{
		return true;
	}
	double Value = 0.0;
	if (!Object->TryGetNumberField(Field, Value) || !FMath::IsFinite(Value) || Value < 0.0 || Value > 1.0)
	{
		OutError = FString::Printf(TEXT("'%s' must be a finite number in [0, 1]"), Field);
		return false;
	}
	Out = static_cast<float>(Value);
	return true;
}

static bool ReadOptionalNonNegativeInteger(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TOptional<int32>& Out,
	FString& OutError)
{
	if (!Object->HasField(Field))
	{
		return true;
	}
	double Value = 0.0;
	if (!Object->TryGetNumberField(Field, Value) || !FMath::IsFinite(Value) ||
		Value < 0.0 || Value > static_cast<double>(MAX_int32) || Value != FMath::TruncToDouble(Value))
	{
		OutError = FString::Printf(TEXT("'%s' must be a non-negative integer"), Field);
		return false;
	}
	Out = static_cast<int32>(Value);
	return true;
}

static bool ReadOptionalBool(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TOptional<bool>& Out,
	FString& OutError)
{
	if (!Object->HasField(Field))
	{
		return true;
	}
	bool Value = false;
	if (!Object->TryGetBoolField(Field, Value))
	{
		OutError = FString::Printf(TEXT("'%s' must be a boolean"), Field);
		return false;
	}
	Out = Value;
	return true;
}

static bool IsFullBodySolver(const UIKRigController* Controller, const int32 SolverIndex)
{
	FInstancedStruct* SolverStruct = Controller->GetSolverStructAtIndex(SolverIndex);
	return SolverStruct && SolverStruct->GetScriptStruct() == FIKRigFullBodyIKSolver::StaticStruct();
}

static FIKRigFullBodyIKSolver* GetFullBodySolver(const UIKRigController* Controller, const int32 SolverIndex)
{
	return IsFullBodySolver(Controller, SolverIndex)
		? static_cast<FIKRigFullBodyIKSolver*>(Controller->GetSolverAtIndex(SolverIndex))
		: nullptr;
}

static bool GoalNameIsSafe(const FString& Name)
{
	FString Sanitized = Name;
	UIKRigController::SanitizeGoalName(Sanitized);
	return Sanitized == Name && !FName(*Name).IsNone();
}

#endif
}

TSharedPtr<FJsonValue> FAnimationHandlers::ConfigureIKRig(const TSharedPtr<FJsonObject>& Params)
{
#if !UE_MCP_HAS_5_8_API
	return UE_MCP_IKRigAuthoring::Error(
		TEXT("unsupported_engine_version"),
		TEXT("IK Rig authoring requires Unreal Engine 5.8 or newer"));
#else
	using namespace UE_MCP_IKRigAuthoring;
	using UE_MCP_IKRigAuthoring::Error;

	if (!Params.IsValid())
	{
		return Error(TEXT("invalid_params"), TEXT("Parameters are required"));
	}

	FString ParseError;
	FString RigPath;
	if (!ReadRequiredString(Params, TEXT("rigPath"), RigPath, ParseError))
	{
		return Error(TEXT("invalid_params"), ParseError);
	}
	if (MCPIsProtectedAssetPath(RigPath))
	{
		return Error(TEXT("protected_asset"), FString::Printf(TEXT("Protected asset cannot be modified: %s"), *RigPath));
	}

	FString AutoSetup;
	if (Params->HasField(TEXT("autoSetup")))
	{
		if (!Params->TryGetStringField(TEXT("autoSetup"), AutoSetup) ||
			(AutoSetup != TEXT("retarget") && AutoSetup != TEXT("full_body")))
		{
			return Error(TEXT("invalid_params"), TEXT("'autoSetup' must be 'retarget' or 'full_body'"));
		}
	}
	const bool bAutoRetarget = AutoSetup == TEXT("retarget") || AutoSetup == TEXT("full_body");
	const bool bAutoFullBody = AutoSetup == TEXT("full_body");

	TOptional<FName> RetargetRoot;
	if (Params->HasField(TEXT("retargetRoot")))
	{
		FString Value;
		if (!ReadRequiredString(Params, TEXT("retargetRoot"), Value, ParseError))
		{
			return Error(TEXT("invalid_params"), ParseError);
		}
		RetargetRoot = FName(*Value);
	}

	TOptional<FName> RootMotionBone;
	if (Params->HasField(TEXT("rootMotionBone")))
	{
		FString Value;
		if (!ReadRequiredString(Params, TEXT("rootMotionBone"), Value, ParseError))
		{
			return Error(TEXT("invalid_params"), ParseError);
		}
		RootMotionBone = FName(*Value);
	}

	TArray<FChainRequest> Chains;
	TSet<FName> RequestedChainNames;
	if (Params->HasField(TEXT("chains")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(TEXT("chains"), Values) || !Values || Values->Num() > MaxChains)
		{
			return Error(TEXT("invalid_params"), FString::Printf(TEXT("'chains' must be an array with at most %d entries"), MaxChains));
		}
		for (int32 Index = 0; Index < Values->Num(); ++Index)
		{
			if (!(*Values)[Index].IsValid() || (*Values)[Index]->Type != EJson::Object)
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("chains[%d] must be an object"), Index));
			}
			const TSharedPtr<FJsonObject> Object = (*Values)[Index]->AsObject();
			FString Name;
			FString StartBone;
			FString EndBone;
			if (!ReadRequiredString(Object, TEXT("name"), Name, ParseError) ||
				!ReadRequiredString(Object, TEXT("startBone"), StartBone, ParseError) ||
				!ReadRequiredString(Object, TEXT("endBone"), EndBone, ParseError))
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("chains[%d]: %s"), Index, *ParseError));
			}
			FString Goal;
			if (Object->HasField(TEXT("goal")) && !Object->TryGetStringField(TEXT("goal"), Goal))
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("chains[%d].goal must be a string"), Index));
			}
			Goal.TrimStartAndEndInline();
			FChainRequest Request{FName(*Name), FName(*StartBone), FName(*EndBone), Goal.IsEmpty() ? NAME_None : FName(*Goal)};
			if (Request.Name.IsNone() || RequestedChainNames.Contains(Request.Name))
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("chains[%d].name must be non-empty and unique"), Index));
			}
			RequestedChainNames.Add(Request.Name);
			Chains.Add(Request);
		}
	}

	FFullBodyRequest FullBody;
	if (Params->HasField(TEXT("fullBodyIK")))
	{
		const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
		if (!Params->TryGetObjectField(TEXT("fullBodyIK"), ObjectPtr) || !ObjectPtr || !ObjectPtr->IsValid())
		{
			return Error(TEXT("invalid_params"), TEXT("'fullBodyIK' must be an object"));
		}
		const TSharedPtr<FJsonObject> Object = *ObjectPtr;
		FullBody.bPresent = true;
		FString RootBone;
		if (!ReadRequiredString(Object, TEXT("rootBone"), RootBone, ParseError))
		{
			return Error(TEXT("invalid_params"), FString::Printf(TEXT("fullBodyIK: %s"), *ParseError));
		}
		FullBody.RootBone = FName(*RootBone);
		if (!ReadOptionalNonNegativeInteger(Object, TEXT("solverIndex"), FullBody.SolverIndex, ParseError) ||
			!ReadOptionalBool(Object, TEXT("enabled"), FullBody.bEnabled, ParseError))
		{
			return Error(TEXT("invalid_params"), FString::Printf(TEXT("fullBodyIK: %s"), *ParseError));
		}

		const TArray<TSharedPtr<FJsonValue>>* GoalValues = nullptr;
		if (!Object->TryGetArrayField(TEXT("goals"), GoalValues) || !GoalValues || GoalValues->Num() > MaxGoals)
		{
			return Error(TEXT("invalid_params"), FString::Printf(TEXT("fullBodyIK.goals must be an array with at most %d entries"), MaxGoals));
		}
		TSet<FName> RequestedGoalNames;
		for (int32 Index = 0; Index < GoalValues->Num(); ++Index)
		{
			if (!(*GoalValues)[Index].IsValid() || (*GoalValues)[Index]->Type != EJson::Object)
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("fullBodyIK.goals[%d] must be an object"), Index));
			}
			const TSharedPtr<FJsonObject> GoalObject = (*GoalValues)[Index]->AsObject();
			FString Name;
			FString Bone;
			if (!ReadRequiredString(GoalObject, TEXT("name"), Name, ParseError) ||
				!ReadRequiredString(GoalObject, TEXT("bone"), Bone, ParseError))
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("fullBodyIK.goals[%d]: %s"), Index, *ParseError));
			}
			if (!GoalNameIsSafe(Name))
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("fullBodyIK.goals[%d].name is not a native-safe IK goal name"), Index));
			}
			FGoalRequest Request;
			Request.Name = FName(*Name);
			Request.Bone = FName(*Bone);
			if (RequestedGoalNames.Contains(Request.Name))
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("Duplicate fullBodyIK goal '%s'"), *Name));
			}
			RequestedGoalNames.Add(Request.Name);
			if (!ReadOptionalUnitFloat(GoalObject, TEXT("positionAlpha"), Request.PositionAlpha, ParseError) ||
				!ReadOptionalUnitFloat(GoalObject, TEXT("rotationAlpha"), Request.RotationAlpha, ParseError) ||
				!ReadOptionalNonNegativeInteger(GoalObject, TEXT("chainDepth"), Request.ChainDepth, ParseError) ||
				!ReadOptionalUnitFloat(GoalObject, TEXT("strengthAlpha"), Request.StrengthAlpha, ParseError) ||
				!ReadOptionalUnitFloat(GoalObject, TEXT("pullChainAlpha"), Request.PullChainAlpha, ParseError) ||
				!ReadOptionalUnitFloat(GoalObject, TEXT("pinRotation"), Request.PinRotation, ParseError))
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("fullBodyIK.goals[%d]: %s"), Index, *ParseError));
			}
			FullBody.Goals.Add(Request);
		}
	}

	TArray<FExclusionRequest> Exclusions;
	TSet<FName> RequestedExclusionBones;
	if (Params->HasField(TEXT("exclusions")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(TEXT("exclusions"), Values) || !Values || Values->Num() > MaxExclusions)
		{
			return Error(TEXT("invalid_params"), FString::Printf(TEXT("'exclusions' must be an array with at most %d entries"), MaxExclusions));
		}
		for (int32 Index = 0; Index < Values->Num(); ++Index)
		{
			if (!(*Values)[Index].IsValid() || (*Values)[Index]->Type != EJson::Object)
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("exclusions[%d] must be an object"), Index));
			}
			const TSharedPtr<FJsonObject> Object = (*Values)[Index]->AsObject();
			FString Bone;
			bool bExcluded = false;
			if (!ReadRequiredString(Object, TEXT("bone"), Bone, ParseError) ||
				!Object->TryGetBoolField(TEXT("excluded"), bExcluded))
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("exclusions[%d] requires string 'bone' and boolean 'excluded'"), Index));
			}
			const FName BoneName(*Bone);
			if (RequestedExclusionBones.Contains(BoneName))
			{
				return Error(TEXT("invalid_params"), FString::Printf(TEXT("Duplicate exclusion bone '%s'"), *Bone));
			}
			RequestedExclusionBones.Add(BoneName);
			Exclusions.Add({BoneName, bExcluded});
		}
	}

	if (!bAutoRetarget && !RetargetRoot.IsSet() && !RootMotionBone.IsSet() &&
		!Params->HasField(TEXT("chains")) && !FullBody.bPresent && !Params->HasField(TEXT("exclusions")))
	{
		return Error(TEXT("invalid_params"), TEXT("No IK Rig configuration was requested"));
	}

	UIKRigDefinition* Rig = LoadAssetByPath<UIKRigDefinition>(RigPath);
	if (!Rig)
	{
		return Error(TEXT("asset_not_found"), FString::Printf(TEXT("IK Rig not found: %s"), *RigPath));
	}
	UIKRigController* Controller = UIKRigController::GetController(Rig);
	if (!Controller)
	{
		return Error(TEXT("controller_unavailable"), FString::Printf(TEXT("Could not acquire the native IK Rig controller for %s"), *RigPath));
	}
	USkeletalMesh* Mesh = Controller->GetSkeletalMesh();
	const FIKRigSkeleton& Skeleton = Controller->GetIKRigSkeleton();
	if (!Mesh || Skeleton.BoneNames.IsEmpty() || !Controller->IsSkeletalMeshCompatible(Mesh))
	{
		return Error(TEXT("incompatible_rig"), TEXT("The IK Rig must have a non-empty compatible skeletal mesh before it can be configured"));
	}

	auto RequireBone = [&Skeleton](const FName Bone, const FString& Context, FString& OutError) -> bool
	{
		if (Bone.IsNone() || Skeleton.GetBoneIndexFromName(Bone) == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("%s references unknown bone '%s'"), *Context, *Bone.ToString());
			return false;
		}
		return true;
	};

	if ((RetargetRoot.IsSet() && !RequireBone(RetargetRoot.GetValue(), TEXT("retargetRoot"), ParseError)) ||
		(RootMotionBone.IsSet() && !RequireBone(RootMotionBone.GetValue(), TEXT("rootMotionBone"), ParseError)) ||
		(FullBody.bPresent && !RequireBone(FullBody.RootBone, TEXT("fullBodyIK.rootBone"), ParseError)))
	{
		return Error(TEXT("invalid_bone"), ParseError);
	}

	TMap<FName, FName> AvailableGoals;
	for (const UIKRigEffectorGoal* Goal : Controller->GetAllGoals())
	{
		if (Goal)
		{
			AvailableGoals.Add(Goal->GoalName, Goal->BoneName);
		}
	}

	FAutoCharacterizeResults AutoResults;
	if (bAutoRetarget)
	{
		Controller->AutoGenerateRetargetDefinition(AutoResults);
		if (!AutoResults.bUsedTemplate)
		{
			return Error(TEXT("auto_setup_unavailable"), TEXT("Unreal could not match this skeleton to a native IK Rig template"));
		}
		const FRetargetDefinition& Definition = AutoResults.AutoRetargetDefinition.RetargetDefinition;
		if (!RequireBone(Definition.PelvisBone, TEXT("autoSetup retarget root"), ParseError))
		{
			return Error(TEXT("invalid_bone"), ParseError);
		}
		for (const FBoneChain& Chain : Definition.BoneChains)
		{
			if (Chain.ChainName.IsNone() ||
				!RequireBone(Chain.StartBone.BoneName, FString::Printf(TEXT("auto chain '%s' start"), *Chain.ChainName.ToString()), ParseError) ||
				!RequireBone(Chain.EndBone.BoneName, FString::Printf(TEXT("auto chain '%s' end"), *Chain.ChainName.ToString()), ParseError) ||
				!Skeleton.IsBoneInDirectLineage(Chain.EndBone.BoneName, Chain.StartBone.BoneName))
			{
				return Error(TEXT("invalid_chain"), ParseError.IsEmpty()
					? FString::Printf(TEXT("Auto chain '%s' has invalid ancestry"), *Chain.ChainName.ToString())
					: ParseError);
			}
			if (bAutoFullBody && !Chain.IKGoalName.IsNone())
			{
				FString GoalName = Chain.IKGoalName.ToString();
				if (!GoalNameIsSafe(GoalName))
				{
					return Error(TEXT("invalid_goal"), FString::Printf(TEXT("Auto goal '%s' is not a native-safe goal name"), *GoalName));
				}
				AvailableGoals.Add(Chain.IKGoalName, Chain.EndBone.BoneName);
			}
		}
	}

	if (bAutoFullBody &&
		(Controller->GetNumSolvers() != 0 || !Controller->GetAllGoals().IsEmpty() ||
		 !Controller->GetRetargetChains().IsEmpty() || !Skeleton.ExcludedBones.IsEmpty() ||
		 !Controller->GetRetargetRoot().IsNone() || !Controller->GetRootMotionBone().IsNone()))
	{
		return Error(TEXT("non_empty_rig"), TEXT("autoSetup 'full_body' is only safe on an empty IK Rig definition"));
	}

	for (const FGoalRequest& Goal : FullBody.Goals)
	{
		if (!RequireBone(Goal.Bone, FString::Printf(TEXT("goal '%s'"), *Goal.Name.ToString()), ParseError))
		{
			return Error(TEXT("invalid_bone"), ParseError);
		}
		if (Goal.ChainDepth.IsSet() && Goal.ChainDepth.GetValue() > Skeleton.BoneNames.Num())
		{
			return Error(TEXT("invalid_range"), FString::Printf(TEXT("Goal '%s' chainDepth exceeds the skeleton bone count"), *Goal.Name.ToString()));
		}
		AvailableGoals.Add(Goal.Name, Goal.Bone);
	}

	for (const FChainRequest& Chain : Chains)
	{
		if (!RequireBone(Chain.StartBone, FString::Printf(TEXT("chain '%s' start"), *Chain.Name.ToString()), ParseError) ||
			!RequireBone(Chain.EndBone, FString::Printf(TEXT("chain '%s' end"), *Chain.Name.ToString()), ParseError))
		{
			return Error(TEXT("invalid_bone"), ParseError);
		}
		if (!Skeleton.IsBoneInDirectLineage(Chain.EndBone, Chain.StartBone))
		{
			return Error(TEXT("invalid_chain"), FString::Printf(TEXT("Chain '%s' end bone must descend from its start bone"), *Chain.Name.ToString()));
		}
		if (!Chain.Goal.IsNone() && !AvailableGoals.Contains(Chain.Goal))
		{
			return Error(TEXT("dangling_goal"), FString::Printf(TEXT("Chain '%s' references missing goal '%s'"), *Chain.Name.ToString(), *Chain.Goal.ToString()));
		}
	}

	TSet<FName> RequiredFBIKBones;
	for (int32 Index = 0; Index < Controller->GetNumSolvers(); ++Index)
	{
		FIKRigFullBodyIKSolver* ExistingSolver = GetFullBodySolver(Controller, Index);
		if (!ExistingSolver) continue;
		const FName ExistingRoot = Controller->GetStartBone(Index);
		if (!ExistingRoot.IsNone()) RequiredFBIKBones.Add(ExistingRoot);
		TSet<FName> ConnectedGoals;
		ExistingSolver->GetRequiredGoals(ConnectedGoals);
		for (const FName GoalName : ConnectedGoals)
		{
			if (const FName* GoalBone = AvailableGoals.Find(GoalName)) RequiredFBIKBones.Add(*GoalBone);
		}
	}
	if (FullBody.bPresent)
	{
		RequiredFBIKBones.Add(FullBody.RootBone);
		for (const FGoalRequest& Goal : FullBody.Goals) RequiredFBIKBones.Add(Goal.Bone);
	}
	if (bAutoFullBody)
	{
		RequiredFBIKBones.Add(AutoResults.AutoRetargetDefinition.RetargetDefinition.PelvisBone);
		for (const FBoneChain& Chain : AutoResults.AutoRetargetDefinition.RetargetDefinition.BoneChains)
		{
			if (const FName* GoalBone = AvailableGoals.Find(Chain.IKGoalName)) RequiredFBIKBones.Add(*GoalBone);
		}
	}

	for (const FExclusionRequest& Exclusion : Exclusions)
	{
		if (!RequireBone(Exclusion.Bone, TEXT("exclusions"), ParseError))
		{
			return Error(TEXT("invalid_bone"), ParseError);
		}
		if (Exclusion.bExcluded && RequiredFBIKBones.Contains(Exclusion.Bone))
		{
			return Error(TEXT("invalid_exclusion"), FString::Printf(TEXT("Bone '%s' cannot be excluded while an FBIK solver requires it as a root or goal bone"), *Exclusion.Bone.ToString()));
		}
	}

	int32 SolverIndex = INDEX_NONE;
	bool bCreateSolver = false;
	if (FullBody.bPresent || bAutoFullBody)
	{
		if (bAutoFullBody)
		{
			SolverIndex = 0;
			if (FullBody.SolverIndex.IsSet() && FullBody.SolverIndex.GetValue() != 0)
			{
				return Error(TEXT("invalid_solver"), TEXT("autoSetup 'full_body' creates its FBIK solver at index 0"));
			}
		}
		else if (FullBody.SolverIndex.IsSet())
		{
			SolverIndex = FullBody.SolverIndex.GetValue();
			if (SolverIndex >= Controller->GetNumSolvers() || !IsFullBodySolver(Controller, SolverIndex))
			{
				return Error(TEXT("invalid_solver"), FString::Printf(TEXT("Solver index %d is not an existing Full Body IK solver"), SolverIndex));
			}
		}
		else
		{
			for (int32 Index = 0; Index < Controller->GetNumSolvers(); ++Index)
			{
				if (IsFullBodySolver(Controller, Index))
				{
					SolverIndex = Index;
					break;
				}
			}
			if (SolverIndex == INDEX_NONE)
			{
				SolverIndex = Controller->GetNumSolvers();
				bCreateSolver = true;
			}
		}

		const FName SolverRoot = FullBody.bPresent
			? FullBody.RootBone
			: AutoResults.AutoRetargetDefinition.RetargetDefinition.PelvisBone;
		TSet<FName> ConnectedGoals;
		if (!bCreateSolver && !bAutoFullBody)
		{
			if (FIKRigFullBodyIKSolver* ExistingSolver = GetFullBodySolver(Controller, SolverIndex))
			{
				ExistingSolver->GetRequiredGoals(ConnectedGoals);
			}
		}
		if (bAutoFullBody)
		{
			for (const FBoneChain& Chain : AutoResults.AutoRetargetDefinition.RetargetDefinition.BoneChains)
			{
				if (!Chain.IKGoalName.IsNone()) ConnectedGoals.Add(Chain.IKGoalName);
			}
		}
		for (const FGoalRequest& Goal : FullBody.Goals) ConnectedGoals.Add(Goal.Name);
		for (const FName GoalName : ConnectedGoals)
		{
			const FName* GoalBone = AvailableGoals.Find(GoalName);
			if (!GoalBone)
			{
				return Error(TEXT("dangling_goal"), FString::Printf(TEXT("FBIK solver references missing goal '%s'"), *GoalName.ToString()));
			}
			if (!Skeleton.IsBoneInDirectLineage(*GoalBone, SolverRoot))
			{
				return Error(TEXT("invalid_ancestry"), FString::Printf(TEXT("Goal bone '%s' must descend from FBIK root '%s'"), *GoalBone->ToString(), *SolverRoot.ToString()));
			}
		}
	}

	const int32 BeforeChainCount = Controller->GetRetargetChains().Num();
	const int32 BeforeGoalCount = Controller->GetAllGoals().Num();
	const int32 BeforeSolverCount = Controller->GetNumSolvers();
	const TSet<FName> BeforeExcluded(Skeleton.ExcludedBones);
	UPackage* Package = Rig->GetOutermost();
	const bool bWasDirty = Package && Package->IsDirty();
	bool bFailed = false;
	FString ApplyError;
	TArray<FString> Warnings;

	{
		FScopedTransaction Transaction(NSLOCTEXT("UE_MCP", "ConfigureIKRig", "Configure IK Rig"));
		FScopedReinitializeIKRig Reinitialize(Controller, true);
		Rig->Modify();

		if (bAutoRetarget && !Controller->ApplyAutoGeneratedRetargetDefinition())
		{
			bFailed = true;
			ApplyError = TEXT("Native auto retarget setup failed");
		}
		if (!bFailed && bAutoFullBody && !Controller->ApplyAutoFBIK())
		{
			bFailed = true;
			ApplyError = TEXT("Native auto Full Body IK setup failed");
		}
		if (!bFailed && RetargetRoot.IsSet() && Controller->GetRetargetRoot() != RetargetRoot.GetValue() &&
			!Controller->SetRetargetRoot(RetargetRoot.GetValue()))
		{
			bFailed = true;
			ApplyError = TEXT("Failed to set retargetRoot");
		}
		if (!bFailed && RootMotionBone.IsSet() && Controller->GetRootMotionBone() != RootMotionBone.GetValue() &&
			!Controller->SetRootMotionBone(RootMotionBone.GetValue()))
		{
			bFailed = true;
			ApplyError = TEXT("Failed to set rootMotionBone");
		}

		for (const FGoalRequest& Request : FullBody.Goals)
		{
			if (bFailed) break;
			UIKRigEffectorGoal* Goal = Controller->GetGoal(Request.Name);
			if (!Goal)
			{
				if (Controller->AddNewGoal(Request.Name, Request.Bone) != Request.Name)
				{
					bFailed = true;
					ApplyError = FString::Printf(TEXT("Failed to create goal '%s'"), *Request.Name.ToString());
					break;
				}
				Goal = Controller->GetGoal(Request.Name);
			}
			else if (Goal->BoneName != Request.Bone && !Controller->SetGoalBone(Request.Name, Request.Bone))
			{
				bFailed = true;
				ApplyError = FString::Printf(TEXT("Failed to move goal '%s'"), *Request.Name.ToString());
				break;
			}
			Goal = Controller->GetGoal(Request.Name);
			const bool bCoreSettingsChanged = Goal &&
				((Request.PositionAlpha.IsSet() && !FMath::IsNearlyEqual(Goal->PositionAlpha, Request.PositionAlpha.GetValue())) ||
				 (Request.RotationAlpha.IsSet() && !FMath::IsNearlyEqual(Goal->RotationAlpha, Request.RotationAlpha.GetValue())));
			if (!Goal || (bCoreSettingsChanged && !Controller->ModifyGoal(Request.Name)))
			{
				bFailed = true;
				ApplyError = FString::Printf(TEXT("Failed to modify goal '%s'"), *Request.Name.ToString());
				break;
			}
			if (Request.PositionAlpha.IsSet()) Goal->PositionAlpha = Request.PositionAlpha.GetValue();
			if (Request.RotationAlpha.IsSet()) Goal->RotationAlpha = Request.RotationAlpha.GetValue();
		}

		for (const FChainRequest& Request : Chains)
		{
			if (bFailed) break;
			const FBoneChain* Existing = Controller->GetRetargetChainByName(Request.Name);
			if (!Existing)
			{
				if (Controller->AddRetargetChain(Request.Name, Request.StartBone, Request.EndBone, Request.Goal) != Request.Name)
				{
					bFailed = true;
					ApplyError = FString::Printf(TEXT("Failed to create chain '%s'"), *Request.Name.ToString());
				}
				continue;
			}
			if (Existing->StartBone.BoneName != Request.StartBone && !Controller->SetRetargetChainStartBone(Request.Name, Request.StartBone))
			{
				bFailed = true;
				ApplyError = FString::Printf(TEXT("Failed to set chain '%s' start bone"), *Request.Name.ToString());
				break;
			}
			Existing = Controller->GetRetargetChainByName(Request.Name);
			if (Existing->EndBone.BoneName != Request.EndBone && !Controller->SetRetargetChainEndBone(Request.Name, Request.EndBone))
			{
				bFailed = true;
				ApplyError = FString::Printf(TEXT("Failed to set chain '%s' end bone"), *Request.Name.ToString());
				break;
			}
			Existing = Controller->GetRetargetChainByName(Request.Name);
			if (Existing->IKGoalName != Request.Goal && !Controller->SetRetargetChainGoal(Request.Name, Request.Goal))
			{
				bFailed = true;
				ApplyError = FString::Printf(TEXT("Failed to set chain '%s' goal"), *Request.Name.ToString());
			}
		}

		if (!bFailed && (FullBody.bPresent || bAutoFullBody))
		{
			if (bCreateSolver)
			{
				const int32 AddedIndex = Controller->AddSolver(FIKRigFullBodyIKSolver::StaticStruct());
				if (AddedIndex != SolverIndex)
				{
					bFailed = true;
					ApplyError = TEXT("Failed to create the Full Body IK solver at the predicted index");
				}
			}
			FIKRigFullBodyIKSolver* Solver = bFailed ? nullptr : GetFullBodySolver(Controller, SolverIndex);
			if (!Solver)
			{
				bFailed = true;
				ApplyError = TEXT("Full Body IK solver readback failed");
			}
			if (!bFailed && FullBody.bPresent)
			{
				if (Controller->GetStartBone(SolverIndex) != FullBody.RootBone && !Controller->SetStartBone(FullBody.RootBone, SolverIndex))
				{
					bFailed = true;
					ApplyError = TEXT("Failed to set the Full Body IK root bone");
				}
				if (!bFailed && FullBody.bEnabled.IsSet() && Controller->GetSolverEnabled(SolverIndex) != FullBody.bEnabled.GetValue() &&
					!Controller->SetSolverEnabled(SolverIndex, FullBody.bEnabled.GetValue()))
				{
					bFailed = true;
					ApplyError = TEXT("Failed to set the Full Body IK enabled state");
				}
				for (const FGoalRequest& Request : FullBody.Goals)
				{
					if (bFailed) break;
					if (!Controller->IsGoalConnectedToSolver(Request.Name, SolverIndex) &&
						!Controller->ConnectGoalToSolver(Request.Name, SolverIndex))
					{
						bFailed = true;
						ApplyError = FString::Printf(TEXT("Failed to connect goal '%s' to the Full Body IK solver"), *Request.Name.ToString());
						break;
					}
					FIKRigFBIKGoalSettings* Settings = static_cast<FIKRigFBIKGoalSettings*>(Solver->GetGoalSettings(Request.Name));
					if (!Settings)
					{
						bFailed = true;
						ApplyError = FString::Printf(TEXT("Full Body IK settings are missing for goal '%s'"), *Request.Name.ToString());
						break;
					}
					if (Request.ChainDepth.IsSet()) Settings->ChainDepth = Request.ChainDepth.GetValue();
					if (Request.StrengthAlpha.IsSet()) Settings->StrengthAlpha = Request.StrengthAlpha.GetValue();
					if (Request.PullChainAlpha.IsSet()) Settings->PullChainAlpha = Request.PullChainAlpha.GetValue();
					if (Request.PinRotation.IsSet()) Settings->PinRotation = Request.PinRotation.GetValue();
				}
			}
		}

		for (const FExclusionRequest& Request : Exclusions)
		{
			if (bFailed) break;
			if (Controller->GetBoneExcluded(Request.Bone) != Request.bExcluded &&
				!Controller->SetBoneExcluded(Request.Bone, Request.bExcluded))
			{
				bFailed = true;
				ApplyError = FString::Printf(TEXT("Failed to set exclusion for bone '%s'"), *Request.Bone.ToString());
			}
		}

		if (!bFailed && RetargetRoot.IsSet() && Controller->GetRetargetRoot() != RetargetRoot.GetValue())
		{
			bFailed = true;
			ApplyError = TEXT("retargetRoot readback did not match");
		}
		if (!bFailed && RootMotionBone.IsSet() && Controller->GetRootMotionBone() != RootMotionBone.GetValue())
		{
			bFailed = true;
			ApplyError = TEXT("rootMotionBone readback did not match");
		}
		for (const FChainRequest& Request : Chains)
		{
			if (bFailed) break;
			const FBoneChain* Chain = Controller->GetRetargetChainByName(Request.Name);
			TSet<int32> ChainIndices;
			if (!Chain || Chain->StartBone.BoneName != Request.StartBone || Chain->EndBone.BoneName != Request.EndBone ||
				Chain->IKGoalName != Request.Goal || !Controller->ValidateChain(Request.Name, &Skeleton, ChainIndices))
			{
				bFailed = true;
				ApplyError = FString::Printf(TEXT("Chain '%s' failed native readback validation"), *Request.Name.ToString());
			}
		}
		for (const FGoalRequest& Request : FullBody.Goals)
		{
			if (bFailed) break;
			const UIKRigEffectorGoal* Goal = Controller->GetGoal(Request.Name);
			FIKRigFullBodyIKSolver* Solver = GetFullBodySolver(Controller, SolverIndex);
			const FIKRigFBIKGoalSettings* Settings = Solver
				? static_cast<const FIKRigFBIKGoalSettings*>(Solver->GetGoalSettings(Request.Name))
				: nullptr;
			if (!Goal || Goal->BoneName != Request.Bone || !Controller->IsGoalConnectedToSolver(Request.Name, SolverIndex) || !Settings ||
				(Request.PositionAlpha.IsSet() && !FMath::IsNearlyEqual(Goal->PositionAlpha, Request.PositionAlpha.GetValue())) ||
				(Request.RotationAlpha.IsSet() && !FMath::IsNearlyEqual(Goal->RotationAlpha, Request.RotationAlpha.GetValue())) ||
				(Request.ChainDepth.IsSet() && Settings->ChainDepth != Request.ChainDepth.GetValue()) ||
				(Request.StrengthAlpha.IsSet() && !FMath::IsNearlyEqual(Settings->StrengthAlpha, Request.StrengthAlpha.GetValue())) ||
				(Request.PullChainAlpha.IsSet() && !FMath::IsNearlyEqual(Settings->PullChainAlpha, Request.PullChainAlpha.GetValue())) ||
				(Request.PinRotation.IsSet() && !FMath::IsNearlyEqual(Settings->PinRotation, Request.PinRotation.GetValue())))
			{
				bFailed = true;
				ApplyError = FString::Printf(TEXT("Goal '%s' failed native readback validation"), *Request.Name.ToString());
			}
		}
		for (const FExclusionRequest& Request : Exclusions)
		{
			if (!bFailed && Controller->GetBoneExcluded(Request.Bone) != Request.bExcluded)
			{
				bFailed = true;
				ApplyError = FString::Printf(TEXT("Exclusion for bone '%s' failed readback"), *Request.Bone.ToString());
			}
		}
	}

	auto Undo = [&]() -> bool
	{
		const bool bUndone = GEditor && GEditor->UndoTransaction();
		if (bUndone && Package)
		{
			Package->SetDirtyFlag(bWasDirty);
			FScopedReinitializeIKRig Refresh(Controller, true);
		}
		return bUndone;
	};

	if (bFailed)
	{
		const bool bUndone = Undo();
		return Error(TEXT("mutation_failed"), ApplyError, bUndone);
	}

	Rig->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveLoadedAsset(Rig, false))
	{
		const bool bUndone = Undo();
		return Error(TEXT("save_failed"), TEXT("IK Rig changes could not be saved"), bUndone);
	}

	const FIKRigSkeleton& FinalSkeleton = Controller->GetIKRigSkeleton();
	const TSet<FName> FinalExcluded(FinalSkeleton.ExcludedBones);
	int32 ExclusionsChanged = 0;
	for (const FName Bone : BeforeExcluded) if (!FinalExcluded.Contains(Bone)) ++ExclusionsChanged;
	for (const FName Bone : FinalExcluded) if (!BeforeExcluded.Contains(Bone)) ++ExclusionsChanged;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("rigPath"), Rig->GetPathName());
	Result->SetBoolField(TEXT("validated"), true);
	Result->SetBoolField(TEXT("rollbackSafe"), true);
	Result->SetBoolField(TEXT("autoSetupApplied"), bAutoRetarget);
	if (!AutoSetup.IsEmpty()) Result->SetStringField(TEXT("autoSetup"), AutoSetup);
	Result->SetStringField(TEXT("retargetRoot"), Controller->GetRetargetRoot().ToString());
	Result->SetStringField(TEXT("rootMotionBone"), Controller->GetRootMotionBone().ToString());
	Result->SetNumberField(TEXT("chainsCreated"), FMath::Max(0, Controller->GetRetargetChains().Num() - BeforeChainCount));
	Result->SetNumberField(TEXT("chainsUpserted"), Chains.Num());
	Result->SetNumberField(TEXT("goalsCreated"), FMath::Max(0, Controller->GetAllGoals().Num() - BeforeGoalCount));
	Result->SetNumberField(TEXT("goalsUpserted"), FullBody.Goals.Num());
	Result->SetNumberField(TEXT("exclusionsChanged"), ExclusionsChanged);
	Result->SetNumberField(TEXT("chainCount"), Controller->GetRetargetChains().Num());
	Result->SetNumberField(TEXT("goalCount"), Controller->GetAllGoals().Num());
	Result->SetNumberField(TEXT("solverCount"), Controller->GetNumSolvers());
	if (SolverIndex != INDEX_NONE)
	{
		Result->SetNumberField(TEXT("fullBodyIKSolverIndex"), SolverIndex);
		Result->SetBoolField(TEXT("fullBodyIKSolverCreated"), Controller->GetNumSolvers() > BeforeSolverCount);
	}
	TArray<TSharedPtr<FJsonValue>> WarningValues;
	for (const FString& Warning : Warnings) WarningValues.Add(MakeShared<FJsonValueString>(Warning));
	Result->SetArrayField(TEXT("warnings"), WarningValues);
	return MCPResult(Result);
#endif
}
