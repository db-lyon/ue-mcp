#include "AnimationHandlers.h"
#include "HandlerUtils.h"

#if UE_MCP_HAS_5_8_API
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Engine/SkeletalMesh.h"
#include "Rig/IKRigDefinition.h"
#include "Rig/IKRigSkeleton.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "RetargetEditor/IKRetargeterPoseGenerator.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/RetargetOps/CurveRemapOp.h"
#include "Retargeter/RetargetOps/FKChainsOp.h"
#include "Retargeter/RetargetOps/PelvisMotionOp.h"
#include "Retargeter/RetargetOps/RootMotionGeneratorOp.h"
#include "Retargeter/RetargetOps/RunIKRigOp.h"

namespace
{
	constexpr int32 MaxRetargeterItems = 10000;

	struct FPreparedChainMapping
	{
		FName TargetChain;
		FName SourceChain;
	};

	struct FPreparedPoseRotation
	{
		FName Bone;
		FQuat Rotation;
	};

	struct FPreparedRetargetPose
	{
		ERetargetSourceOrTarget Side = ERetargetSourceOrTarget::Target;
		FName Name;
		bool bCreate = false;
		bool bReset = false;
		bool bHasAutoAlign = false;
		bool bAutoAlignAll = false;
		ERetargetAutoAlignMethod AutoAlignMethod = ERetargetAutoAlignMethod::ChainToChain;
		TArray<FName> AutoAlignBones;
		TArray<FPreparedPoseRotation> Rotations;
		TOptional<double> RootOffsetZ;
		TOptional<FName> SnapBone;
	};

	// Processor initialization temporarily points serialized ops/settings at its
	// live editor copies. Preserve any viewport-owned pointers so a validation-only
	// processor cannot leave the open retargeter editor with dangling references.
	class FScopedRetargetEditorInstanceRestore
	{
		struct FState
		{
			FIKRetargetOpBase* Op = nullptr;
			FIKRetargetOpBase* OpEditorInstance = nullptr;
			FIKRetargetOpSettingsBase* Settings = nullptr;
			FIKRetargetOpSettingsBase* SettingsEditorInstance = nullptr;
		};

	public:
		explicit FScopedRetargetEditorInstanceRestore(UIKRetargeter* Retargeter)
		{
			if (!Retargeter) return;
			States.Reserve(Retargeter->GetRetargetOps().Num());
			for (const FInstancedStruct& OpStruct : Retargeter->GetRetargetOps())
			{
				FIKRetargetOpBase* Op = const_cast<FIKRetargetOpBase*>(OpStruct.GetPtr<FIKRetargetOpBase>());
				if (!Op) continue;
				FIKRetargetOpSettingsBase* Settings = Op->GetSettings();
				States.Add({
					Op,
					Op->EditorInstance,
					Settings,
					Settings ? Settings->EditorInstance : nullptr});
			}
		}

		~FScopedRetargetEditorInstanceRestore()
		{
			for (const FState& State : States)
			{
				State.Op->EditorInstance = State.OpEditorInstance;
				if (State.Settings) State.Settings->EditorInstance = State.SettingsEditorInstance;
			}
		}

	private:
		TArray<FState> States;
	};

	bool ReadOptionalString(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		bool& bOutPresent,
		FString& OutValue,
		FString& OutError)
	{
		bOutPresent = Object->HasField(Field);
		if (!bOutPresent)
		{
			OutValue.Reset();
			return true;
		}
		if (!Object->TryGetStringField(Field, OutValue) || OutValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("'%s' must be a non-empty string"), Field);
			return false;
		}
		return true;
	}

	bool ReadOptionalBool(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		const bool DefaultValue,
		bool& OutValue,
		FString& OutError)
	{
		OutValue = DefaultValue;
		if (!Object->HasField(Field)) return true;
		if (!Object->TryGetBoolField(Field, OutValue))
		{
			OutError = FString::Printf(TEXT("'%s' must be a boolean"), Field);
			return false;
		}
		return true;
	}

	bool ReadNormalizedQuaternion(
		const TSharedPtr<FJsonObject>& Object,
		FQuat& OutRotation,
		FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Quaternion = nullptr;
		if (!Object->TryGetObjectField(TEXT("rotationQuaternion"), Quaternion)
			|| !Quaternion || !Quaternion->IsValid())
		{
			OutError = TEXT("'rotationQuaternion' must be an object with finite x, y, z and w numbers");
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		double W = 0.0;
		if (!(*Quaternion)->TryGetNumberField(TEXT("x"), X)
			|| !(*Quaternion)->TryGetNumberField(TEXT("y"), Y)
			|| !(*Quaternion)->TryGetNumberField(TEXT("z"), Z)
			|| !(*Quaternion)->TryGetNumberField(TEXT("w"), W)
			|| !FMath::IsFinite(X) || !FMath::IsFinite(Y)
			|| !FMath::IsFinite(Z) || !FMath::IsFinite(W))
		{
			OutError = TEXT("'rotationQuaternion' must contain finite x, y, z and w numbers");
			return false;
		}

		OutRotation = FQuat(X, Y, Z, W);
		const double Length = OutRotation.Size();
		constexpr double NormalizedTolerance = 1e-3;
		if (Length <= UE_SMALL_NUMBER)
		{
			OutError = TEXT("'rotationQuaternion' must have non-zero length");
			return false;
		}
		if (FMath::Abs(Length - 1.0) > NormalizedTolerance)
		{
			OutError = FString::Printf(
				TEXT("'rotationQuaternion' must be normalized within %.4f (length was %.8f)"),
				NormalizedTolerance, Length);
			return false;
		}
		OutRotation.Normalize();
		return true;
	}

	bool ParseSide(const FString& Value, ERetargetSourceOrTarget& OutSide)
	{
		if (Value.Equals(TEXT("source"), ESearchCase::IgnoreCase))
		{
			OutSide = ERetargetSourceOrTarget::Source;
			return true;
		}
		if (Value.Equals(TEXT("target"), ESearchCase::IgnoreCase))
		{
			OutSide = ERetargetSourceOrTarget::Target;
			return true;
		}
		return false;
	}

	bool ParseAutoMapMode(const FString& Value, EAutoMapChainType& OutMode)
	{
		if (Value.Equals(TEXT("exact"), ESearchCase::IgnoreCase))
		{
			OutMode = EAutoMapChainType::Exact;
			return true;
		}
		if (Value.Equals(TEXT("fuzzy"), ESearchCase::IgnoreCase))
		{
			OutMode = EAutoMapChainType::Fuzzy;
			return true;
		}
		if (Value.Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			OutMode = EAutoMapChainType::Clear;
			return true;
		}
		return false;
	}

	bool ParseAutoAlignMethod(const FString& Value, ERetargetAutoAlignMethod& OutMethod)
	{
		if (Value.Equals(TEXT("chain_to_chain"), ESearchCase::IgnoreCase))
		{
			OutMethod = ERetargetAutoAlignMethod::ChainToChain;
			return true;
		}
		if (Value.Equals(TEXT("mesh_to_mesh"), ESearchCase::IgnoreCase))
		{
			OutMethod = ERetargetAutoAlignMethod::MeshToMesh;
			return true;
		}
		if (Value.Equals(TEXT("local_axes"), ESearchCase::IgnoreCase))
		{
			OutMethod = ERetargetAutoAlignMethod::LocalRotationAxes;
			return true;
		}
		if (Value.Equals(TEXT("global_axes"), ESearchCase::IgnoreCase))
		{
			OutMethod = ERetargetAutoAlignMethod::GlobalRotationAxes;
			return true;
		}
		return false;
	}

	bool HasRetargetOpType(const UIKRetargeter* Retargeter, const UScriptStruct* Type)
	{
		if (!Retargeter || !Type) return false;
		for (const FInstancedStruct& OpStruct : Retargeter->GetRetargetOps())
		{
			const UScriptStruct* ActualType = OpStruct.GetScriptStruct();
			if (ActualType && ActualType->IsChildOf(Type)) return true;
		}
		return false;
	}

	bool HasAllDefaultOps(const UIKRetargeter* Retargeter)
	{
		return HasRetargetOpType(Retargeter, FIKRetargetPelvisMotionOp::StaticStruct())
			&& HasRetargetOpType(Retargeter, FIKRetargetFKChainsOp::StaticStruct())
			&& HasRetargetOpType(Retargeter, FIKRetargetRunIKRigOp::StaticStruct())
			&& HasRetargetOpType(Retargeter, FIKRetargetRootMotionOp::StaticStruct())
			&& HasRetargetOpType(Retargeter, FIKRetargetCurveRemapOp::StaticStruct());
	}

	TSet<FName> GetChainNames(const UIKRigDefinition* Rig)
	{
		TSet<FName> Names;
		if (!Rig) return Names;
		for (const FBoneChain& Chain : Rig->GetRetargetChains()) Names.Add(Chain.ChainName);
		return Names;
	}

	bool HasBone(const UIKRigDefinition* Rig, const USkeletalMesh* Mesh, const FName Bone)
	{
		if (Mesh && Mesh->GetRefSkeleton().FindBoneIndex(Bone) != INDEX_NONE) return true;
		return Rig && Rig->GetSkeleton().GetBoneIndexFromName(Bone) != INDEX_NONE;
	}

	int32 CountMappedChains(const UIKRetargeter* Retargeter)
	{
		int32 Count = 0;
		if (!Retargeter) return Count;
		for (const FInstancedStruct& OpStruct : Retargeter->GetRetargetOps())
		{
			const FIKRetargetOpBase* Op = OpStruct.GetPtr<FIKRetargetOpBase>();
			const FRetargetChainMapping* Mapping = Op ? Op->GetChainMapping() : nullptr;
			if (!Mapping) continue;
			for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
			{
				if (!Pair.SourceChainName.IsNone()) ++Count;
			}
		}
		return Count;
	}

	TSharedPtr<FJsonObject> QuaternionJson(const FQuat& Rotation)
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("x"), Rotation.X);
		Result->SetNumberField(TEXT("y"), Rotation.Y);
		Result->SetNumberField(TEXT("z"), Rotation.Z);
		Result->SetNumberField(TEXT("w"), Rotation.W);
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> TextArrayJson(const TArray<FText>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Reserve(Values.Num());
		for (const FText& Value : Values)
		{
			Result.Add(MakeShared<FJsonValueString>(Value.ToString()));
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> BuildOpsJson(const UIKRetargeter* Retargeter)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		if (!Retargeter) return Result;
		const TArray<FInstancedStruct>& Ops = Retargeter->GetRetargetOps();
		Result.Reserve(Ops.Num());
		for (int32 Index = 0; Index < Ops.Num(); ++Index)
		{
			const FIKRetargetOpBase* Op = Ops[Index].GetPtr<FIKRetargetOpBase>();
			if (!Op) continue;
			auto Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), Index);
			Entry->SetStringField(TEXT("name"), Op->GetName().ToString());
			Entry->SetStringField(TEXT("parent"), Op->GetParentOpName().ToString());
			Entry->SetBoolField(TEXT("enabled"), Op->IsEnabled());
			Entry->SetStringField(TEXT("type"), Op->GetType() ? Op->GetType()->GetPathName() : TEXT(""));
			Entry->SetBoolField(TEXT("hasChainMapping"), Op->GetChainMapping() != nullptr);
			if (const UIKRigDefinition* TargetRig = Op->GetCustomTargetIKRig())
			{
				Entry->SetStringField(TEXT("targetRig"), TargetRig->GetPathName());
			}
			Result.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> BuildMappingsJson(const UIKRetargeter* Retargeter)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		if (!Retargeter) return Result;
		const TArray<FInstancedStruct>& Ops = Retargeter->GetRetargetOps();
		for (int32 Index = 0; Index < Ops.Num(); ++Index)
		{
			const FIKRetargetOpBase* Op = Ops[Index].GetPtr<FIKRetargetOpBase>();
			const FRetargetChainMapping* Mapping = Op ? Op->GetChainMapping() : nullptr;
			if (!Mapping) continue;
			auto MappingObject = MakeShared<FJsonObject>();
			MappingObject->SetNumberField(TEXT("opIndex"), Index);
			MappingObject->SetStringField(TEXT("opName"), Op->GetName().ToString());
			TArray<TSharedPtr<FJsonValue>> Chains;
			for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
			{
				auto Chain = MakeShared<FJsonObject>();
				Chain->SetStringField(TEXT("targetChain"), Pair.TargetChainName.ToString());
				Chain->SetStringField(TEXT("sourceChain"), Pair.SourceChainName.ToString());
				Chains.Add(MakeShared<FJsonValueObject>(Chain));
			}
			MappingObject->SetArrayField(TEXT("chains"), Chains);
			Result.Add(MakeShared<FJsonValueObject>(MappingObject));
		}
		return Result;
	}

	TSharedPtr<FJsonObject> BuildPoseJson(
		UIKRetargeterController* Controller,
		const ERetargetSourceOrTarget Side,
		const FName PoseName,
		const bool bAutoAlignResetPose)
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("side"), Side == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target"));
		Result->SetStringField(TEXT("name"), PoseName.ToString());
		Result->SetBoolField(TEXT("current"), Controller->GetCurrentRetargetPoseName(Side) == PoseName);
		Result->SetBoolField(TEXT("autoAlignResetPose"), bAutoAlignResetPose);
		FIKRetargetPose& Pose = Controller->GetRetargetPoses(Side).FindChecked(PoseName);
		Result->SetNumberField(TEXT("rootOffsetZ"), Pose.GetRootTranslationDelta().Z);

		TArray<FName> BoneNames;
		Pose.GetAllDeltaRotations().GetKeys(BoneNames);
		BoneNames.Sort([](const FName A, const FName B)
		{
			return A.ToString() < B.ToString();
		});
		TArray<TSharedPtr<FJsonValue>> RotationOffsets;
		RotationOffsets.Reserve(BoneNames.Num());
		for (const FName Bone : BoneNames)
		{
			auto Offset = MakeShared<FJsonObject>();
			Offset->SetStringField(TEXT("bone"), Bone.ToString());
			Offset->SetObjectField(TEXT("rotationQuaternion"), QuaternionJson(Pose.GetDeltaRotationForBone(Bone)));
			RotationOffsets.Add(MakeShared<FJsonValueObject>(Offset));
		}
		Result->SetArrayField(TEXT("rotationOffsets"), RotationOffsets);
		return Result;
	}
}
#endif

TSharedPtr<FJsonValue> FAnimationHandlers::ConfigureIKRetargeter(const TSharedPtr<FJsonObject>& Params)
{
#if !UE_MCP_HAS_5_8_API
	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("errorCode"), TEXT("unsupported_engine_version"));
	Result->SetStringField(TEXT("error"), TEXT("configure_ik_retargeter requires Unreal Engine 5.8 or newer"));
	return MCPResult(Result);
#else
	FString RetargeterPath;
	if (auto Error = RequireString(Params, TEXT("retargeterPath"), RetargeterPath)) return Error;
	if (MCPIsProtectedAssetPath(RetargeterPath))
	{
		return MCPError(FString::Printf(TEXT("Protected asset cannot be modified: %s"), *RetargeterPath));
	}
	UIKRetargeter* Retargeter = LoadAssetByPath<UIKRetargeter>(RetargeterPath);
	if (!Retargeter) return MCPError(FString::Printf(TEXT("IKRetargeter not found: %s"), *RetargeterPath));
	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller) return MCPError(TEXT("IKRetargeterController unavailable"));

	FString Error;
	bool bHasSourceRig = false;
	bool bHasTargetRig = false;
	bool bHasSourcePreview = false;
	bool bHasTargetPreview = false;
	FString SourceRigPath;
	FString TargetRigPath;
	FString SourcePreviewPath;
	FString TargetPreviewPath;
	if (!ReadOptionalString(Params, TEXT("sourceRig"), bHasSourceRig, SourceRigPath, Error)
		|| !ReadOptionalString(Params, TEXT("targetRig"), bHasTargetRig, TargetRigPath, Error)
		|| !ReadOptionalString(Params, TEXT("sourcePreviewMesh"), bHasSourcePreview, SourcePreviewPath, Error)
		|| !ReadOptionalString(Params, TEXT("targetPreviewMesh"), bHasTargetPreview, TargetPreviewPath, Error))
	{
		return MCPError(Error);
	}

	bool bEnsureDefaultOps = true;
	bool bForceRemap = false;
	if (!ReadOptionalBool(Params, TEXT("ensureDefaultOps"), true, bEnsureDefaultOps, Error)
		|| !ReadOptionalBool(Params, TEXT("forceRemap"), false, bForceRemap, Error))
	{
		return MCPError(Error);
	}
	if (bEnsureDefaultOps && Controller->GetNumRetargetOps() > 0 && !HasAllDefaultOps(Retargeter))
	{
		return MCPError(TEXT("ensureDefaultOps cannot safely augment a partial retarget op stack; complete the stack in the editor or pass ensureDefaultOps=false"));
	}

	UIKRigDefinition* SourceRig = bHasSourceRig
		? LoadAssetByPath<UIKRigDefinition>(SourceRigPath)
		: Retargeter->GetIKRigWriteable(ERetargetSourceOrTarget::Source);
	UIKRigDefinition* TargetRig = bHasTargetRig
		? LoadAssetByPath<UIKRigDefinition>(TargetRigPath)
		: Retargeter->GetIKRigWriteable(ERetargetSourceOrTarget::Target);
	if (bHasSourceRig && !SourceRig) return MCPError(FString::Printf(TEXT("Source IKRig not found: %s"), *SourceRigPath));
	if (bHasTargetRig && !TargetRig) return MCPError(FString::Printf(TEXT("Target IKRig not found: %s"), *TargetRigPath));

	USkeletalMesh* SourcePreview = bHasSourcePreview
		? LoadAssetByPath<USkeletalMesh>(SourcePreviewPath)
		: (bHasSourceRig ? SourceRig->GetPreviewMesh() : Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source));
	USkeletalMesh* TargetPreview = bHasTargetPreview
		? LoadAssetByPath<USkeletalMesh>(TargetPreviewPath)
		: (bHasTargetRig ? TargetRig->GetPreviewMesh() : Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target));
	if (bHasSourcePreview && !SourcePreview) return MCPError(FString::Printf(TEXT("Source SkeletalMesh not found: %s"), *SourcePreviewPath));
	if (bHasTargetPreview && !TargetPreview) return MCPError(FString::Printf(TEXT("Target SkeletalMesh not found: %s"), *TargetPreviewPath));

	bool bHasAutoMap = false;
	FString AutoMapModeString;
	EAutoMapChainType AutoMapMode = EAutoMapChainType::Exact;
	if (!ReadOptionalString(Params, TEXT("autoMapMode"), bHasAutoMap, AutoMapModeString, Error)) return MCPError(Error);
	if (bHasAutoMap && !ParseAutoMapMode(AutoMapModeString, AutoMapMode))
	{
		return MCPError(TEXT("'autoMapMode' must be 'exact', 'fuzzy' or 'clear'"));
	}

	TArray<FPreparedChainMapping> PreparedMappings;
	TSet<FName> RequestedTargetChains;
	const TArray<TSharedPtr<FJsonValue>>* MappingValues = nullptr;
	if (Params->HasField(TEXT("chainMappings")))
	{
		if (!Params->TryGetArrayField(TEXT("chainMappings"), MappingValues) || !MappingValues)
			return MCPError(TEXT("'chainMappings' must be an array"));
		if (MappingValues->Num() > MaxRetargeterItems)
			return MCPError(FString::Printf(TEXT("'chainMappings' exceeds the %d item limit"), MaxRetargeterItems));
		if (!SourceRig || !TargetRig)
			return MCPError(TEXT("'chainMappings' requires both source and target IK Rigs"));
		const TSet<FName> SourceChains = GetChainNames(SourceRig);
		const TSet<FName> TargetChains = GetChainNames(TargetRig);
		for (int32 Index = 0; Index < MappingValues->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& MappingValue = (*MappingValues)[Index];
			const TSharedPtr<FJsonObject> Mapping = MappingValue.IsValid()
				&& MappingValue->Type == EJson::Object
				? (*MappingValues)[Index]->AsObject() : nullptr;
			FString TargetName;
			if (!Mapping || !Mapping->TryGetStringField(TEXT("targetChain"), TargetName) || TargetName.IsEmpty())
				return MCPError(FString::Printf(TEXT("chainMappings[%d].targetChain must be a non-empty string"), Index));
			const FName TargetChain(*TargetName);
			if (TargetChain.IsNone())
				return MCPError(FString::Printf(TEXT("chainMappings[%d].targetChain cannot be 'None'"), Index));
			if (!TargetChains.Contains(TargetChain))
				return MCPError(FString::Printf(TEXT("Target chain not found: %s"), *TargetName));
			if (RequestedTargetChains.Contains(TargetChain))
				return MCPError(FString::Printf(TEXT("Duplicate target chain mapping: %s"), *TargetName));
			RequestedTargetChains.Add(TargetChain);

			FName SourceChain = NAME_None;
			const TSharedPtr<FJsonValue> SourceValue = Mapping->TryGetField(TEXT("sourceChain"));
			if (SourceValue.IsValid() && SourceValue->Type != EJson::Null)
			{
				FString SourceName;
				if (!SourceValue->TryGetString(SourceName) || SourceName.IsEmpty())
					return MCPError(FString::Printf(TEXT("chainMappings[%d].sourceChain must be a non-empty string or null"), Index));
				SourceChain = FName(*SourceName);
				if (SourceChain.IsNone())
					return MCPError(FString::Printf(TEXT("chainMappings[%d].sourceChain cannot be 'None'; omit it or use null to clear the mapping"), Index));
				if (!SourceChains.Contains(SourceChain))
					return MCPError(FString::Printf(TEXT("Source chain not found: %s"), *SourceName));
			}
			PreparedMappings.Add({TargetChain, SourceChain});
		}
	}
	if ((bHasAutoMap || !PreparedMappings.IsEmpty()) && (!SourceRig || !TargetRig))
		return MCPError(TEXT("Chain mapping requires both source and target IK Rigs"));

	TOptional<FPreparedRetargetPose> PreparedPose;
	const TSharedPtr<FJsonObject>* PoseObjectPointer = nullptr;
	if (Params->HasField(TEXT("pose")))
	{
		if (!Params->TryGetObjectField(TEXT("pose"), PoseObjectPointer)
			|| !PoseObjectPointer || !PoseObjectPointer->IsValid())
		{
			return MCPError(TEXT("'pose' must be an object"));
		}
		const TSharedPtr<FJsonObject>& PoseObject = *PoseObjectPointer;
		FPreparedRetargetPose Pose;
		FString SideString;
		FString PoseName;
		if (!PoseObject->TryGetStringField(TEXT("side"), SideString) || !ParseSide(SideString, Pose.Side))
			return MCPError(TEXT("pose.side must be 'source' or 'target'"));
		if (!PoseObject->TryGetStringField(TEXT("name"), PoseName) || PoseName.IsEmpty())
			return MCPError(TEXT("pose.name must be a non-empty string"));
		Pose.Name = FName(*PoseName);
		if (Pose.Name.IsNone()) return MCPError(TEXT("pose.name cannot be 'None'"));
		if (!ReadOptionalBool(PoseObject, TEXT("create"), false, Pose.bCreate, Error)
			|| !ReadOptionalBool(PoseObject, TEXT("reset"), false, Pose.bReset, Error))
		{
			return MCPError(Error);
		}

		UIKRigDefinition* PoseRig = Pose.Side == ERetargetSourceOrTarget::Source ? SourceRig : TargetRig;
		USkeletalMesh* PoseMesh = Pose.Side == ERetargetSourceOrTarget::Source ? SourcePreview : TargetPreview;
		if (!PoseRig) return MCPError(TEXT("Pose authoring requires an IK Rig on the selected side"));
		const bool bPoseExists = Controller->GetRetargetPoses(Pose.Side).Contains(Pose.Name);
		if (Pose.bCreate && bPoseExists)
			return MCPError(FString::Printf(TEXT("Retarget pose already exists: %s"), *PoseName));
		if (!Pose.bCreate && !bPoseExists)
			return MCPError(FString::Printf(TEXT("Retarget pose not found: %s"), *PoseName));

		FString AutoAlignString;
		if (PoseObject->HasField(TEXT("autoAlign")))
		{
			if (!PoseObject->TryGetStringField(TEXT("autoAlign"), AutoAlignString)
				|| !ParseAutoAlignMethod(AutoAlignString, Pose.AutoAlignMethod))
			{
				return MCPError(TEXT("pose.autoAlign must be 'chain_to_chain', 'mesh_to_mesh', 'local_axes' or 'global_axes'"));
			}
			Pose.bHasAutoAlign = true;
			if (!SourcePreview || !TargetPreview)
				return MCPError(TEXT("pose.autoAlign requires both source and target preview meshes"));
		}

		const TArray<TSharedPtr<FJsonValue>>* BoneValues = nullptr;
		if (PoseObject->HasField(TEXT("bones")))
		{
			if (!Pose.bHasAutoAlign) return MCPError(TEXT("pose.bones requires pose.autoAlign"));
			if (!PoseObject->TryGetArrayField(TEXT("bones"), BoneValues) || !BoneValues)
				return MCPError(TEXT("pose.bones must be an array of bone names"));
			if (BoneValues->Num() > MaxRetargeterItems)
				return MCPError(FString::Printf(TEXT("pose.bones exceeds the %d item limit"), MaxRetargeterItems));
			TSet<FName> SeenBones;
			for (int32 Index = 0; Index < BoneValues->Num(); ++Index)
			{
				FString BoneString;
				if (!(*BoneValues)[Index].IsValid()
					|| !(*BoneValues)[Index]->TryGetString(BoneString) || BoneString.IsEmpty())
					return MCPError(FString::Printf(TEXT("pose.bones[%d] must be a non-empty string"), Index));
				const FName Bone(*BoneString);
				if (Bone.IsNone()) return MCPError(FString::Printf(TEXT("pose.bones[%d] cannot be 'None'"), Index));
				if (SeenBones.Contains(Bone)) return MCPError(FString::Printf(TEXT("Duplicate pose auto-align bone: %s"), *BoneString));
				if (!HasBone(PoseRig, PoseMesh, Bone)) return MCPError(FString::Printf(TEXT("Pose bone not found: %s"), *BoneString));
				SeenBones.Add(Bone);
				Pose.AutoAlignBones.Add(Bone);
			}
		}
		Pose.bAutoAlignAll = Pose.bHasAutoAlign && Pose.AutoAlignBones.IsEmpty();
		if (Pose.bAutoAlignAll && !Pose.bCreate && !Pose.bReset)
		{
			return MCPError(TEXT("Auto-aligning all bones resets the entire existing pose; pass pose.reset=true to acknowledge that replacement"));
		}

		const TArray<TSharedPtr<FJsonValue>>* RotationValues = nullptr;
		if (PoseObject->HasField(TEXT("rotationOffsets")))
		{
			if (!PoseObject->TryGetArrayField(TEXT("rotationOffsets"), RotationValues) || !RotationValues)
				return MCPError(TEXT("pose.rotationOffsets must be an array"));
			if (RotationValues->Num() > MaxRetargeterItems)
				return MCPError(FString::Printf(TEXT("pose.rotationOffsets exceeds the %d item limit"), MaxRetargeterItems));
			TSet<FName> SeenBones;
			for (int32 Index = 0; Index < RotationValues->Num(); ++Index)
			{
				const TSharedPtr<FJsonValue>& RotationValue = (*RotationValues)[Index];
				const TSharedPtr<FJsonObject> Offset = RotationValue.IsValid()
					&& RotationValue->Type == EJson::Object
					? (*RotationValues)[Index]->AsObject() : nullptr;
				FString BoneString;
				if (!Offset || !Offset->TryGetStringField(TEXT("bone"), BoneString) || BoneString.IsEmpty())
					return MCPError(FString::Printf(TEXT("pose.rotationOffsets[%d].bone must be a non-empty string"), Index));
				const FName Bone(*BoneString);
				if (Bone.IsNone()) return MCPError(FString::Printf(TEXT("pose.rotationOffsets[%d].bone cannot be 'None'"), Index));
				if (SeenBones.Contains(Bone)) return MCPError(FString::Printf(TEXT("Duplicate pose rotation bone: %s"), *BoneString));
				if (!HasBone(PoseRig, PoseMesh, Bone)) return MCPError(FString::Printf(TEXT("Pose bone not found: %s"), *BoneString));
				FQuat Rotation;
				if (!ReadNormalizedQuaternion(Offset, Rotation, Error))
					return MCPError(FString::Printf(TEXT("pose.rotationOffsets[%d]: %s"), Index, *Error));
				SeenBones.Add(Bone);
				Pose.Rotations.Add({Bone, Rotation});
			}
		}

		if (PoseObject->HasField(TEXT("rootOffsetZ")))
		{
			double RootOffsetZ = 0.0;
			if (!PoseObject->TryGetNumberField(TEXT("rootOffsetZ"), RootOffsetZ) || !FMath::IsFinite(RootOffsetZ))
				return MCPError(TEXT("pose.rootOffsetZ must be a finite number"));
			Pose.RootOffsetZ = RootOffsetZ;
		}
		if (PoseObject->HasField(TEXT("snapBoneToGround")))
		{
			FString SnapBoneString;
			if (!PoseObject->TryGetStringField(TEXT("snapBoneToGround"), SnapBoneString) || SnapBoneString.IsEmpty())
				return MCPError(TEXT("pose.snapBoneToGround must be a non-empty bone name"));
			const FName SnapBone(*SnapBoneString);
			if (SnapBone.IsNone()) return MCPError(TEXT("pose.snapBoneToGround cannot be 'None'"));
			if (!SourcePreview || !TargetPreview)
				return MCPError(TEXT("pose.snapBoneToGround requires both source and target preview meshes"));
			if (!PoseMesh || PoseMesh->GetRefSkeleton().FindBoneIndex(SnapBone) == INDEX_NONE)
				return MCPError(FString::Printf(TEXT("Snap bone is not present in the selected preview mesh: %s"), *SnapBoneString));
			Pose.SnapBone = SnapBone;
		}
		if (Pose.RootOffsetZ.IsSet() && Pose.SnapBone.IsSet())
			return MCPError(TEXT("pose.rootOffsetZ and pose.snapBoneToGround are mutually exclusive because snapping changes the root offset"));
		PreparedPose = MoveTemp(Pose);
	}

	bool bMutationFailed = false;
	FString MutationError;
	bool bAddedDefaultOps = false;
	bool bValidationRan = false;
	bool bValidationInitialized = false;
	TArray<FText> ValidationErrors;
	TArray<FText> ValidationWarnings;
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UE_MCP", "ConfigureIKRetargeter", "Configure IK Retargeter"));
		Retargeter->Modify();

		if (bEnsureDefaultOps && !HasAllDefaultOps(Retargeter))
		{
			Controller->AddDefaultOps();
			bAddedDefaultOps = true;
			if (!HasAllDefaultOps(Retargeter))
			{
				bMutationFailed = true;
				MutationError = TEXT("Unreal failed to install the complete default retarget op stack");
			}
		}

		if (!bMutationFailed && bHasSourceRig) Controller->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
		if (!bMutationFailed && bHasTargetRig) Controller->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);
		if (!bMutationFailed && (bHasSourceRig || bAddedDefaultOps) && SourceRig)
			Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SourceRig);
		if (!bMutationFailed && (bHasTargetRig || bAddedDefaultOps) && TargetRig)
			Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TargetRig);
		if (!bMutationFailed && bHasSourcePreview) Controller->SetPreviewMesh(ERetargetSourceOrTarget::Source, SourcePreview);
		if (!bMutationFailed && bHasTargetPreview) Controller->SetPreviewMesh(ERetargetSourceOrTarget::Target, TargetPreview);

		if (!bMutationFailed && bHasAutoMap) Controller->AutoMapChains(AutoMapMode, bForceRemap);
		if (!bMutationFailed)
		{
			for (const FPreparedChainMapping& Mapping : PreparedMappings)
			{
				if (!Controller->SetSourceChain(Mapping.SourceChain, Mapping.TargetChain))
				{
					bMutationFailed = true;
					MutationError = FString::Printf(TEXT("No retarget op accepted target chain '%s'"), *Mapping.TargetChain.ToString());
					break;
				}
			}
		}
		if (!bMutationFailed && PreparedPose.IsSet()
			&& PreparedPose.GetValue().bHasAutoAlign && !PreparedPose.GetValue().bAutoAlignAll)
		{
			const FPreparedRetargetPose& Pose = PreparedPose.GetValue();
			FIKRetargetProcessor MappingProcessor;
			FScopedRetargetEditorInstanceRestore RestoreEditorInstances(Retargeter);
			FRetargetInitParameters InitParameters;
			InitParameters.SourceSkeletalMesh = SourcePreview;
			InitParameters.TargetSkeletalMesh = TargetPreview;
			InitParameters.RetargeterAsset = Retargeter;
			InitParameters.bSuppressWarnings = true;
			MappingProcessor.Initialize(InitParameters);
			const TArray<FText>& MappingErrors = MappingProcessor.Log.GetErrors();
			if (!MappingProcessor.IsInitialized() || !MappingErrors.IsEmpty())
			{
				bMutationFailed = true;
				MutationError = !MappingErrors.IsEmpty()
					? MappingErrors[0].ToString()
					: TEXT("Retarget pose auto-alignment could not initialize the retarget processor");
			}
			else
			{
				for (const FName Bone : Pose.AutoAlignBones)
				{
					if (!MappingProcessor.IsBoneMapped(Bone, Pose.Side))
					{
						bMutationFailed = true;
						MutationError = FString::Printf(
							TEXT("Retarget pose auto-align bone is not mapped: %s"), *Bone.ToString());
						break;
					}
				}
			}
		}

		if (!bMutationFailed && PreparedPose.IsSet())
		{
			const FPreparedRetargetPose& Pose = PreparedPose.GetValue();
			if (Pose.bCreate)
			{
				const FName CreatedPose = Controller->CreateRetargetPose(Pose.Name, Pose.Side);
				if (CreatedPose != Pose.Name)
				{
					bMutationFailed = true;
					MutationError = FString::Printf(TEXT("Unreal created retarget pose '%s' instead of '%s'"), *CreatedPose.ToString(), *Pose.Name.ToString());
				}
			}
			else if (!Controller->SetCurrentRetargetPose(Pose.Name, Pose.Side))
			{
				bMutationFailed = true;
				MutationError = FString::Printf(TEXT("Unreal failed to select retarget pose '%s'"), *Pose.Name.ToString());
			}

			if (!bMutationFailed && Pose.bReset)
				Controller->ResetRetargetPose(Pose.Name, TArray<FName>(), Pose.Side);
			if (!bMutationFailed && Pose.bHasAutoAlign)
			{
				if (CountMappedChains(Retargeter) == 0)
				{
					bMutationFailed = true;
					MutationError = TEXT("Retarget pose auto-alignment requires at least one mapped chain");
				}
				else if (Pose.bAutoAlignAll)
				{
					Controller->AutoAlignAllBones(Pose.Side, Pose.AutoAlignMethod);
				}
				else
				{
					Controller->AutoAlignBones(Pose.AutoAlignBones, Pose.AutoAlignMethod, Pose.Side);
				}
			}
			if (!bMutationFailed)
			{
				for (const FPreparedPoseRotation& Rotation : Pose.Rotations)
					Controller->SetRotationOffsetForRetargetPoseBone(Rotation.Bone, Rotation.Rotation, Pose.Side);
				if (Pose.RootOffsetZ.IsSet())
				{
					const double CurrentZ = Controller->GetRootOffsetInRetargetPose(Pose.Side).Z;
					Controller->SetRootOffsetInRetargetPose(FVector(0.0, 0.0, Pose.RootOffsetZ.GetValue() - CurrentZ), Pose.Side);
				}
				if (Pose.SnapBone.IsSet()) Controller->SnapBoneToGround(Pose.SnapBone.GetValue(), Pose.Side);
			}
		}

		if (!bMutationFailed) Controller->CleanAsset();

		if (!bMutationFailed && bHasSourceRig
			&& Controller->GetIKRig(ERetargetSourceOrTarget::Source) != SourceRig)
		{
			bMutationFailed = true;
			MutationError = TEXT("Source IK Rig assignment did not survive native readback");
		}
		if (!bMutationFailed && bHasTargetRig
			&& Controller->GetIKRig(ERetargetSourceOrTarget::Target) != TargetRig)
		{
			bMutationFailed = true;
			MutationError = TEXT("Target IK Rig assignment did not survive native readback");
		}
		if (!bMutationFailed && bHasSourcePreview
			&& Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source) != SourcePreview)
		{
			bMutationFailed = true;
			MutationError = TEXT("Source preview mesh assignment did not survive native readback");
		}
		if (!bMutationFailed && bHasTargetPreview
			&& Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target) != TargetPreview)
		{
			bMutationFailed = true;
			MutationError = TEXT("Target preview mesh assignment did not survive native readback");
		}

		if (!bMutationFailed)
		{
			for (const FPreparedChainMapping& Expected : PreparedMappings)
			{
				bool bFoundTarget = false;
				for (const FInstancedStruct& OpStruct : Retargeter->GetRetargetOps())
				{
					const FIKRetargetOpBase* Op = OpStruct.GetPtr<FIKRetargetOpBase>();
					const FRetargetChainMapping* Mapping = Op ? Op->GetChainMapping() : nullptr;
					if (!Mapping || !Mapping->HasChain(Expected.TargetChain, ERetargetSourceOrTarget::Target)) continue;
					bFoundTarget = true;
					if (Mapping->GetChainMappedTo(Expected.TargetChain, ERetargetSourceOrTarget::Target) != Expected.SourceChain)
					{
						bMutationFailed = true;
						MutationError = FString::Printf(TEXT("Chain mapping readback failed for target '%s' on op '%s'"),
							*Expected.TargetChain.ToString(), *Op->GetName().ToString());
						break;
					}
				}
				if (bMutationFailed) break;
				if (!bFoundTarget)
				{
					bMutationFailed = true;
					MutationError = FString::Printf(TEXT("No chain mapping contained target '%s' during readback"), *Expected.TargetChain.ToString());
					break;
				}
			}
		}

		if (!bMutationFailed && bHasAutoMap && AutoMapMode == EAutoMapChainType::Clear)
		{
			for (const FInstancedStruct& OpStruct : Retargeter->GetRetargetOps())
			{
				const FIKRetargetOpBase* Op = OpStruct.GetPtr<FIKRetargetOpBase>();
				const FRetargetChainMapping* Mapping = Op ? Op->GetChainMapping() : nullptr;
				if (!Mapping) continue;
				for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
				{
					if (!RequestedTargetChains.Contains(Pair.TargetChainName) && !Pair.SourceChainName.IsNone())
					{
						bMutationFailed = true;
						MutationError = FString::Printf(TEXT("Clear auto-map readback left target chain '%s' mapped"), *Pair.TargetChainName.ToString());
						break;
					}
				}
				if (bMutationFailed) break;
			}
		}

		if (!bMutationFailed && PreparedPose.IsSet())
		{
			const FPreparedRetargetPose& Pose = PreparedPose.GetValue();
			if (Controller->GetCurrentRetargetPoseName(Pose.Side) != Pose.Name)
			{
				bMutationFailed = true;
				MutationError = TEXT("Retarget pose selection did not survive native readback");
			}
			for (const FPreparedPoseRotation& Expected : Pose.Rotations)
			{
				const FQuat Actual = Controller->GetRotationOffsetForRetargetPoseBone(Expected.Bone, Pose.Side).GetNormalized();
				if (FMath::Abs(Actual | Expected.Rotation) < 1.0 - 1e-6)
				{
					bMutationFailed = true;
					MutationError = FString::Printf(TEXT("Retarget pose rotation readback failed for bone '%s'"), *Expected.Bone.ToString());
					break;
				}
			}
			if (!bMutationFailed && Pose.RootOffsetZ.IsSet()
				&& !FMath::IsNearlyEqual(Controller->GetRootOffsetInRetargetPose(Pose.Side).Z, Pose.RootOffsetZ.GetValue(), 1e-4))
			{
				bMutationFailed = true;
				MutationError = TEXT("Retarget pose root offset did not survive native readback");
			}
		}

		USkeletalMesh* EffectiveSourceMesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source);
		USkeletalMesh* EffectiveTargetMesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target);
		if (!bMutationFailed && EffectiveSourceMesh && EffectiveTargetMesh)
		{
			bValidationRan = true;
			FIKRetargetProcessor Processor;
			FScopedRetargetEditorInstanceRestore RestoreEditorInstances(Retargeter);
			FRetargetInitParameters InitParameters;
			InitParameters.SourceSkeletalMesh = EffectiveSourceMesh;
			InitParameters.TargetSkeletalMesh = EffectiveTargetMesh;
			InitParameters.RetargeterAsset = Retargeter;
			InitParameters.bSuppressWarnings = false;
			Processor.Initialize(InitParameters);
			bValidationInitialized = Processor.IsInitialized();
			ValidationErrors = Processor.Log.GetErrors();
			ValidationWarnings = Processor.Log.GetWarnings();
			if (!bValidationInitialized || !ValidationErrors.IsEmpty())
			{
				bMutationFailed = true;
				MutationError = !ValidationErrors.IsEmpty()
					? ValidationErrors[0].ToString()
					: TEXT("IK Retargeter processor validation failed to initialize");
			}
		}
	}

	if (bMutationFailed)
	{
		const bool bRolledBack = GEditor && GEditor->UndoTransaction();
		return MCPError(bRolledBack
			? MutationError
			: MutationError + TEXT("; the editor transaction could not be rolled back"));
	}
	if (!SaveAssetPackage(Retargeter))
	{
		const bool bRolledBack = GEditor && GEditor->UndoTransaction();
		return MCPError(bRolledBack
			? TEXT("IK Retargeter configuration could not be saved and was rolled back")
			: TEXT("IK Retargeter configuration could not be saved and the editor transaction could not be rolled back"));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("retargeterPath"), Retargeter->GetPathName());
	if (const UIKRigDefinition* Rig = Controller->GetIKRig(ERetargetSourceOrTarget::Source))
		Result->SetStringField(TEXT("sourceRig"), Rig->GetPathName());
	if (const UIKRigDefinition* Rig = Controller->GetIKRig(ERetargetSourceOrTarget::Target))
		Result->SetStringField(TEXT("targetRig"), Rig->GetPathName());
	if (USkeletalMesh* Mesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source))
		Result->SetStringField(TEXT("sourcePreviewMesh"), Mesh->GetPathName());
	if (USkeletalMesh* Mesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target))
		Result->SetStringField(TEXT("targetPreviewMesh"), Mesh->GetPathName());
	Result->SetBoolField(TEXT("defaultOpsComplete"), HasAllDefaultOps(Retargeter));
	Result->SetBoolField(TEXT("defaultOpsAdded"), bAddedDefaultOps);
	Result->SetArrayField(TEXT("ops"), BuildOpsJson(Retargeter));
	Result->SetArrayField(TEXT("mappings"), BuildMappingsJson(Retargeter));
	Result->SetNumberField(TEXT("mappedChainCountAcrossOps"), CountMappedChains(Retargeter));
	if (PreparedPose.IsSet())
	{
		const FPreparedRetargetPose& Pose = PreparedPose.GetValue();
		Result->SetObjectField(TEXT("pose"), BuildPoseJson(Controller, Pose.Side, Pose.Name, Pose.bAutoAlignAll));
	}

	auto Validation = MakeShared<FJsonObject>();
	Validation->SetBoolField(TEXT("ran"), bValidationRan);
	Validation->SetBoolField(TEXT("initialized"), bValidationInitialized);
	Validation->SetArrayField(TEXT("errors"), TextArrayJson(ValidationErrors));
	Validation->SetArrayField(TEXT("warnings"), TextArrayJson(ValidationWarnings));
	if (!bValidationRan)
		Validation->SetStringField(TEXT("reason"), TEXT("Both source and target preview meshes are required for processor validation"));
	Result->SetObjectField(TEXT("validation"), Validation);
	return MCPResult(Result);
#endif
}
