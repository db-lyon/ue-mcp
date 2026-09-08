#include "AnimationHandlers.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "JsonObjectConverter.h"

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

	bool ReadRetargeterOptionalBool(
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
			// #1000: the op stack was reported without the settings objects, so the
			// thing that decides what a retarget actually does - the root motion
			// source, the pelvis alphas, the per-chain FK modes - was only visible
			// through Python. The settings struct is reflected, so it serialises
			// whole rather than one hand-listed field at a time.
			if (const UScriptStruct* const SettingsType = Op->GetSettingsType())
			{
				Entry->SetStringField(TEXT("settingsType"), SettingsType->GetPathName());
				if (const FIKRetargetOpSettingsBase* const Settings = Op->GetSettingsConst())
				{
					auto SettingsJson = MakeShared<FJsonObject>();
					if (FJsonObjectConverter::UStructToJsonObject(SettingsType, Settings, SettingsJson, 0, 0))
					{
						Entry->SetObjectField(TEXT("settings"), SettingsJson);
					}
				}
			}
			if (const UIKRigDefinition* TargetRig = Op->GetCustomTargetIKRig())
			{
				Entry->SetStringField(TEXT("targetRig"), TargetRig->GetPathName());
			}
			Result.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Result;
	}


	/** One op settings write, validated before the transaction opens. */
	struct FPreparedOpSettings
	{
		int32 OpIndex = INDEX_NONE;
		FString OpName;
		/** True when the caller named the op rather than indexing it. */
		bool bResolvedByName = false;
		bool bHasEnabled = false;
		bool bEnabled = true;
		/** Property name to the value to write onto the op settings struct. */
		TArray<TPair<FString, TSharedPtr<FJsonValue>>> Settings;
		/** Chain name to the properties to merge into its ChainsToRetarget entry. */
		TArray<TPair<FString, TArray<TPair<FString, TSharedPtr<FJsonValue>>>>> ChainSettings;
	};

	/**
	 * Merge named properties into the ChainsToRetarget element for one chain.
	 *
	 * #1034 asks to set one chain's rotation mode. Writing the whole
	 * ChainsToRetarget array would do it and would also silently reset every
	 * chain the caller did not name, which is not what "set the Hips chain to
	 * OneToOne" means. So the element is found by its own TargetChainName and
	 * only the named properties on it are written.
	 */
	static bool MergeChainSettings(
		FIKRetargetOpSettingsBase* SettingsPtr,
		const UScriptStruct* SettingsType,
		const FString& ChainName,
		const TArray<TPair<FString, TSharedPtr<FJsonValue>>>& Properties,
		TArray<FString>& OutApplied,
		FString& OutError)
	{
		FArrayProperty* const ChainsProp = CastField<FArrayProperty>(
			SettingsType->FindPropertyByName(FName(TEXT("ChainsToRetarget"))));
		if (!ChainsProp)
		{
			OutError = FString::Printf(
				TEXT("chainSettings is only meaningful on an op whose settings carry ChainsToRetarget; %s does not"),
				*SettingsType->GetName());
			return false;
		}
		FStructProperty* const ElementProp = CastField<FStructProperty>(ChainsProp->Inner);
		if (!ElementProp || !ElementProp->Struct)
		{
			OutError = TEXT("ChainsToRetarget is not an array of structs on this engine build");
			return false;
		}
		FNameProperty* const NameProp = CastField<FNameProperty>(
			ElementProp->Struct->FindPropertyByName(FName(TEXT("TargetChainName"))));
		if (!NameProp)
		{
			OutError = TEXT("a chain settings entry has no TargetChainName on this engine build, so a chain cannot be addressed by name");
			return false;
		}

		FScriptArrayHelper Helper(ChainsProp, ChainsProp->ContainerPtrToValuePtr<void>(SettingsPtr));
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			void* const Element = Helper.GetRawPtr(Index);
			const FName* const Existing = NameProp->ContainerPtrToValuePtr<FName>(Element);
			if (!Existing || !Existing->ToString().Equals(ChainName, ESearchCase::IgnoreCase)) continue;

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties)
			{
				FProperty* const Prop = ElementProp->Struct->FindPropertyByName(FName(*Pair.Key));
				if (!Prop)
				{
					OutError = FString::Printf(TEXT("chain '%s' has no property '%s' on %s"),
						*ChainName, *Pair.Key, *ElementProp->Struct->GetName());
					return false;
				}
				FString PropError;
				if (!MCPJsonProperty::SetJsonOnProperty(
						Prop, Prop->ContainerPtrToValuePtr<void>(Element), Pair.Value, PropError))
				{
					OutError = FString::Printf(TEXT("chain '%s' property '%s': %s"),
						*ChainName, *Pair.Key, *PropError);
					return false;
				}
				OutApplied.Add(FString::Printf(TEXT("%s.%s"), *ChainName, *Pair.Key));
			}
			return true;
		}
		OutError = FString::Printf(
			TEXT("no retarget chain named '%s' on this op; read_ik_retargeter lists the chains it has"), *ChainName);
		return false;
	}

	/** Read the requested op writes, refusing anything that cannot be applied. */
	static bool ReadPreparedOps(
		const TSharedPtr<FJsonObject>& Params,
		const UIKRetargeter* Retargeter,
		TArray<FPreparedOpSettings>& OutPrepared,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
		if (!Params->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray) return true;

		const TArray<FInstancedStruct>& Ops = Retargeter->GetRetargetOps();
		auto NameOfOp = [&Ops](int32 Index) -> FString
		{
			const FIKRetargetOpBase* const Op = Ops.IsValidIndex(Index) ? Ops[Index].GetPtr<FIKRetargetOpBase>() : nullptr;
			return Op ? Op->GetName().ToString() : FString();
		};
		auto OpNamesForError = [&Ops, &NameOfOp]() -> FString
		{
			TArray<FString> Names;
			for (int32 Index = 0; Index < Ops.Num(); ++Index)
			{
				const FString Name = NameOfOp(Index);
				if (!Name.IsEmpty()) Names.Add(Name);
			}
			return FString::Join(Names, TEXT(", "));
		};

		for (const TSharedPtr<FJsonValue>& Entry : *OpsArray)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(Object) || !Object || !Object->IsValid())
			{
				OutError = TEXT("each entry in 'ops' must be an object with a name or index");
				return false;
			}

			FPreparedOpSettings Prepared;
			FString RequestedName;
			double RequestedIndex = 0.0;
			const bool bHasName = (*Object)->TryGetStringField(TEXT("name"), RequestedName) && !RequestedName.IsEmpty();
			const bool bHasIndex = (*Object)->TryGetNumberField(TEXT("index"), RequestedIndex);
			if (bHasName)
			{
				for (int32 Index = 0; Index < Ops.Num(); ++Index)
				{
					if (NameOfOp(Index).Equals(RequestedName, ESearchCase::IgnoreCase))
					{
						Prepared.OpIndex = Index;
						break;
					}
				}
				Prepared.bResolvedByName = true;
				if (Prepared.OpIndex == INDEX_NONE)
				{
					// Naming the stack beats "not found": op names are authored, and a
					// caller guessing one has no other way to learn the real set.
					OutError = FString::Printf(TEXT("no retarget op named '%s'. This retargeter has: %s"),
						*RequestedName, *OpNamesForError());
					return false;
				}
			}
			else if (bHasIndex)
			{
				Prepared.OpIndex = static_cast<int32>(RequestedIndex);
				if (!Ops.IsValidIndex(Prepared.OpIndex))
				{
					OutError = FString::Printf(TEXT("op index %d is out of range; this retargeter has %d ops"),
						Prepared.OpIndex, Ops.Num());
					return false;
				}
			}
			else
			{
				OutError = FString::Printf(TEXT("each entry in 'ops' needs a 'name' or an 'index'. This retargeter has: %s"),
					*OpNamesForError());
				return false;
			}
			Prepared.OpName = NameOfOp(Prepared.OpIndex);

			bool bEnabled = false;
			if ((*Object)->TryGetBoolField(TEXT("enabled"), bEnabled))
			{
				Prepared.bHasEnabled = true;
				Prepared.bEnabled = bEnabled;
			}

			const TSharedPtr<FJsonObject>* SettingsObject = nullptr;
			if ((*Object)->TryGetObjectField(TEXT("settings"), SettingsObject) && SettingsObject && SettingsObject->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*SettingsObject)->Values)
				{
					Prepared.Settings.Add({ Pair.Key, Pair.Value });
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* ChainsArray = nullptr;
			if ((*Object)->TryGetArrayField(TEXT("chainSettings"), ChainsArray) && ChainsArray)
			{
				for (const TSharedPtr<FJsonValue>& ChainEntry : *ChainsArray)
				{
					const TSharedPtr<FJsonObject>* ChainObject = nullptr;
					if (!ChainEntry.IsValid() || !ChainEntry->TryGetObject(ChainObject) || !ChainObject || !ChainObject->IsValid())
					{
						OutError = TEXT("each entry in 'chainSettings' must be an object with a 'chain' name");
						return false;
					}
					FString ChainName;
					if (!(*ChainObject)->TryGetStringField(TEXT("chain"), ChainName) || ChainName.IsEmpty())
					{
						OutError = TEXT("each entry in 'chainSettings' needs a 'chain' naming the target chain to change");
						return false;
					}
					TArray<TPair<FString, TSharedPtr<FJsonValue>>> Properties;
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ChainObject)->Values)
					{
						if (Pair.Key.Equals(TEXT("chain"), ESearchCase::IgnoreCase)) continue;
						Properties.Add({ Pair.Key, Pair.Value });
					}
					if (Properties.Num() == 0)
					{
						OutError = FString::Printf(
							TEXT("chainSettings entry for '%s' names no property to change"), *ChainName);
						return false;
					}
					Prepared.ChainSettings.Add({ ChainName, MoveTemp(Properties) });
				}
			}

			if (!Prepared.bHasEnabled && Prepared.Settings.Num() == 0 && Prepared.ChainSettings.Num() == 0)
			{
				OutError = FString::Printf(
					TEXT("the 'ops' entry for '%s' changes nothing: give it enabled, settings or chainSettings"),
					*Prepared.OpName);
				return false;
			}

			// Every property name is resolved here, against the settings struct this
			// op actually carries, so a typo is refused before a transaction opens.
			// Leaving it to the apply loop would be worse than slow: a transaction
			// that records no change is not pushed onto the undo stack, so the
			// UndoTransaction that unwinds a failed call would roll back whatever
			// the PREVIOUS call did instead. The apply loop still checks, because
			// ensureDefaultOps can rebuild the stack in between.
			const FIKRetargetOpBase* const ParsedOp = Ops[Prepared.OpIndex].GetPtr<FIKRetargetOpBase>();
			const UScriptStruct* const ParsedSettingsType = ParsedOp ? ParsedOp->GetSettingsType() : nullptr;
			if ((Prepared.Settings.Num() > 0 || Prepared.ChainSettings.Num() > 0) && !ParsedSettingsType)
			{
				OutError = FString::Printf(
					TEXT("retarget op '%s' exposes no settings struct on this engine build"), *Prepared.OpName);
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Prepared.Settings)
			{
				if (ParsedSettingsType->FindPropertyByName(FName(*Pair.Key))) continue;
				OutError = FString::Printf(
					TEXT("retarget op '%s' has no setting '%s'; read_ik_retargeter reports the settings each op carries"),
					*Prepared.OpName, *Pair.Key);
				return false;
			}
			if (Prepared.ChainSettings.Num() > 0
				&& !CastField<FArrayProperty>(ParsedSettingsType->FindPropertyByName(FName(TEXT("ChainsToRetarget")))))
			{
				OutError = FString::Printf(
					TEXT("chainSettings is only meaningful on an op whose settings carry ChainsToRetarget; '%s' does not"),
					*Prepared.OpName);
				return false;
			}
			OutPrepared.Add(MoveTemp(Prepared));
		}
		return true;
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
	if (!ReadRetargeterOptionalBool(Params, TEXT("ensureDefaultOps"), true, bEnsureDefaultOps, Error)
		|| !ReadRetargeterOptionalBool(Params, TEXT("forceRemap"), false, bForceRemap, Error))
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

	// #1000 + #1034: op settings are read and validated here, before anything
	// is written, so a bad property name or an op that does not exist is an
	// error about the request rather than a half-applied stack.
	TArray<FPreparedOpSettings> PreparedOps;
	if (!ReadPreparedOps(Params, Retargeter, PreparedOps, Error)) return MCPError(Error);

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
		if (!ReadRetargeterOptionalBool(PoseObject, TEXT("create"), false, Pose.bCreate, Error)
			|| !ReadRetargeterOptionalBool(PoseObject, TEXT("reset"), false, Pose.bReset, Error))
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

	// Read before the transaction reassigns them. A rollback payload built after
	// the write would carry the values this call just installed.
	const UIKRigDefinition* PreviousSourceRig = Controller->GetIKRig(ERetargetSourceOrTarget::Source);
	const UIKRigDefinition* PreviousTargetRig = Controller->GetIKRig(ERetargetSourceOrTarget::Target);
	USkeletalMesh* PreviousSourcePreview = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source);
	USkeletalMesh* PreviousTargetPreview = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target);

	bool bMutationFailed = false;
	FString MutationError;
	bool bAddedDefaultOps = false;
	TArray<TSharedPtr<FJsonValue>> AppliedOps;
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

		// The op settings go in inside the same transaction as everything else, so
		// a later failure unwinds them with the rest rather than leaving an op
		// half-configured. GetSettings() hands back the live struct, which is what
		// makes an in-place reflected write stick: the Python route in #1000 got a
		// copy back and reported success while changing nothing.
		for (const FPreparedOpSettings& Prepared : PreparedOps)
		{
			if (bMutationFailed) break;
			// The controller hands back a writeable op, so nothing here has to
			// const_cast its way out of GetRetargetOps.
			//
			// A named op is re-resolved by name rather than by the index the parse
			// found it at, because ensureDefaultOps can install the default stack
			// earlier in this same transaction and move everything. An index the
			// caller gave is still an index, but it is checked against the name that
			// was there when it was read, so a reshuffle is an error rather than a
			// write landing on whichever op ended up in that slot.
			FIKRetargetOpBase* const Op = Prepared.bResolvedByName
				? Controller->GetRetargetOpByName(FName(*Prepared.OpName))
				: Controller->GetRetargetOpByIndex(Prepared.OpIndex);
			if (!Op)
			{
				bMutationFailed = true;
				MutationError = FString::Printf(
					TEXT("retarget op '%s' is gone from the stack; it changed during this call"),
					*Prepared.OpName);
				break;
			}
			if (!Prepared.bResolvedByName && !Op->GetName().ToString().Equals(Prepared.OpName, ESearchCase::IgnoreCase))
			{
				bMutationFailed = true;
				MutationError = FString::Printf(
					TEXT("op index %d was '%s' when this call was read and is '%s' now; the op stack changed during this call. Name the op instead of indexing it."),
					Prepared.OpIndex, *Prepared.OpName, *Op->GetName().ToString());
				break;
			}

			auto Applied = MakeShared<FJsonObject>();
			Applied->SetNumberField(TEXT("index"), Prepared.OpIndex);
			Applied->SetStringField(TEXT("name"), Prepared.OpName);
			TArray<TSharedPtr<FJsonValue>> AppliedProperties;

			if (Prepared.bHasEnabled)
			{
				Op->SetEnabled(Prepared.bEnabled);
				Applied->SetBoolField(TEXT("enabled"), Prepared.bEnabled);
			}

			const UScriptStruct* const SettingsType = Op->GetSettingsType();
			FIKRetargetOpSettingsBase* const SettingsPtr = Op->GetSettings();
			if ((Prepared.Settings.Num() > 0 || Prepared.ChainSettings.Num() > 0) && (!SettingsType || !SettingsPtr))
			{
				bMutationFailed = true;
				MutationError = FString::Printf(
					TEXT("retarget op '%s' exposes no settings struct on this engine build"), *Prepared.OpName);
				break;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Prepared.Settings)
			{
				FProperty* const Prop = SettingsType->FindPropertyByName(FName(*Pair.Key));
				if (!Prop)
				{
					bMutationFailed = true;
					MutationError = FString::Printf(
						TEXT("retarget op '%s' has no setting '%s'; read_ik_retargeter reports the settings each op carries"),
						*Prepared.OpName, *Pair.Key);
					break;
				}
				FString PropError;
				if (!MCPJsonProperty::SetJsonOnProperty(
						Prop, Prop->ContainerPtrToValuePtr<void>(SettingsPtr), Pair.Value, PropError))
				{
					bMutationFailed = true;
					MutationError = FString::Printf(TEXT("retarget op '%s' setting '%s': %s"),
						*Prepared.OpName, *Pair.Key, *PropError);
					break;
				}
				AppliedProperties.Add(MakeShared<FJsonValueString>(Pair.Key));
			}
			if (bMutationFailed) break;

			for (const TPair<FString, TArray<TPair<FString, TSharedPtr<FJsonValue>>>>& Chain : Prepared.ChainSettings)
			{
				TArray<FString> ChainApplied;
				FString ChainError;
				if (!MergeChainSettings(SettingsPtr, SettingsType, Chain.Key, Chain.Value, ChainApplied, ChainError))
				{
					bMutationFailed = true;
					MutationError = FString::Printf(TEXT("retarget op '%s': %s"), *Prepared.OpName, *ChainError);
					break;
				}
				for (const FString& Name : ChainApplied)
				{
					AppliedProperties.Add(MakeShared<FJsonValueString>(Name));
				}
			}
			if (bMutationFailed) break;

			Applied->SetArrayField(TEXT("settingsApplied"), AppliedProperties);
			AppliedOps.Add(MakeShared<FJsonValueObject>(Applied));
		}
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
	// What this call wrote onto the op stack, named property by property, so a
	// caller can see that a setting landed rather than inferring it from the
	// full readback above.
	if (AppliedOps.Num() > 0) Result->SetArrayField(TEXT("opsConfigured"), AppliedOps);
	Result->SetArrayField(TEXT("mappings"), BuildMappingsJson(Retargeter));
	Result->SetNumberField(TEXT("mappedChainCountAcrossOps"), CountMappedChains(Retargeter));
	if (PreparedPose.IsSet())
	{
		const FPreparedRetargetPose& Pose = PreparedPose.GetValue();
		Result->SetObjectField(TEXT("pose"), BuildPoseJson(Controller, Pose.Side, Pose.Name, Pose.bAutoAlignAll));
	}

	// Only the assignment half of this action is reversible: the rigs and preview
	// meshes each had a previous asset that a replay of this same action can
	// point back at. ensureDefaultOps is forced off in that replay so a pure
	// assignment restore cannot install an op stack of its own.
	const bool bAssignmentsOnly = !bAddedDefaultOps && !bHasAutoMap
		&& PreparedMappings.IsEmpty() && !PreparedPose.IsSet();
	const bool bAssignedAnything = bHasSourceRig || bHasTargetRig || bHasSourcePreview || bHasTargetPreview;
	const bool bAssignmentsRestorable =
		(!bHasSourceRig || PreviousSourceRig != nullptr)
		&& (!bHasTargetRig || PreviousTargetRig != nullptr)
		&& (!bHasSourcePreview || PreviousSourcePreview != nullptr)
		&& (!bHasTargetPreview || PreviousTargetPreview != nullptr);
	if (bAssignmentsOnly && bAssignedAnything && bAssignmentsRestorable)
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("retargeterPath"), Retargeter->GetPathName());
		Payload->SetBoolField(TEXT("ensureDefaultOps"), false);
		if (bHasSourceRig) Payload->SetStringField(TEXT("sourceRig"), PreviousSourceRig->GetPathName());
		if (bHasTargetRig) Payload->SetStringField(TEXT("targetRig"), PreviousTargetRig->GetPathName());
		if (bHasSourcePreview) Payload->SetStringField(TEXT("sourcePreviewMesh"), PreviousSourcePreview->GetPathName());
		if (bHasTargetPreview) Payload->SetStringField(TEXT("targetPreviewMesh"), PreviousTargetPreview->GetPathName());
		MCPSetRollback(Result, TEXT("configure_ik_retargeter"), Payload);
	}
	else
	{
		TArray<FString> Causes;
		if (bAddedDefaultOps) Causes.Add(TEXT("a default retarget op stack was installed and no action removes a retarget op"));
		if (bHasAutoMap || !PreparedMappings.IsEmpty()) Causes.Add(TEXT("chain mappings were rewritten over mappings this call did not capture"));
		if (PreparedPose.IsSet()) Causes.Add(TEXT("a retarget pose was created, reset or realigned and no action deletes a pose"));
		if (!bAssignmentsRestorable) Causes.Add(TEXT("a rig or preview mesh this call assigned had none before, and configure_ik_retargeter takes asset paths so it cannot clear one back to unset"));
		if (Causes.IsEmpty()) Causes.Add(TEXT("this call assigned no rig or preview mesh, so there is no previous asset for a replay to point back at"));
		MCPSetNoRollback(Result, FString::Join(Causes, TEXT("; ")) + TEXT("."));
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
