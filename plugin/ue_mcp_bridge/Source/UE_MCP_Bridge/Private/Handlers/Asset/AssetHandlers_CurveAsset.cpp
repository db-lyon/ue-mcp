// Curve asset key authoring (#1687): set_curve_keys replaces the keys of one
// channel of a CurveFloat, CurveVector or CurveLinearColor. CurveVector and
// CurveLinearColor hold their channels in a fixed FRichCurve array that Python
// does not expose, so this is the only native way to key them per channel.

#include "AssetHandlers.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"
#include "ScopedTransaction.h"

namespace
{
	struct FMCPCurveAssetChannel
	{
		FString Name;
		FRichCurve* Curve = nullptr;
	};

	/** The channels of a curve asset in index order, or empty for any other class. */
	TArray<FMCPCurveAssetChannel> MCPCurveAssetChannels(UObject* Asset)
	{
		TArray<FMCPCurveAssetChannel> Channels;
		if (UCurveFloat* Float = Cast<UCurveFloat>(Asset))
		{
			Channels.Add({ TEXT("Value"), &Float->FloatCurve });
		}
		else if (UCurveVector* Vector = Cast<UCurveVector>(Asset))
		{
			const TCHAR* Names[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
			for (int32 Index = 0; Index < 3; ++Index) Channels.Add({ Names[Index], &Vector->FloatCurves[Index] });
		}
		else if (UCurveLinearColor* Color = Cast<UCurveLinearColor>(Asset))
		{
			const TCHAR* Names[] = { TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") };
			for (int32 Index = 0; Index < 4; ++Index) Channels.Add({ Names[Index], &Color->FloatCurves[Index] });
		}
		return Channels;
	}

	FString MCPCurveAssetChannelList(const TArray<FMCPCurveAssetChannel>& Channels)
	{
		TArray<FString> Parts;
		for (int32 Index = 0; Index < Channels.Num(); ++Index)
		{
			Parts.Add(FString::Printf(TEXT("%d (%s)"), Index, *Channels[Index].Name));
		}
		return FString::Join(Parts, TEXT(", "));
	}

	/** channelName, or channel as an index; absent means 0. */
	bool MCPCurveAssetResolveChannel(const TSharedPtr<FJsonValue>& Raw, const TArray<FMCPCurveAssetChannel>& Channels, int32& OutIndex, FString& OutError)
	{
		OutIndex = 0;
		if (!Raw.IsValid() || Raw->IsNull()) return true;
		double Number = 0.0;
		FString Text;
		if (Raw->Type == EJson::Number && Raw->TryGetNumber(Number))
		{
			OutIndex = FMath::FloorToInt32(Number);
			if (OutIndex == Number && Channels.IsValidIndex(OutIndex)) return true;
		}
		else if (Raw->TryGetString(Text))
		{
			if (Text.IsNumeric())
			{
				OutIndex = FCString::Atoi(*Text);
				if (Channels.IsValidIndex(OutIndex)) return true;
			}
			for (int32 Index = 0; Index < Channels.Num(); ++Index)
			{
				if (Channels[Index].Name.Equals(Text, ESearchCase::IgnoreCase)) { OutIndex = Index; return true; }
			}
		}
		OutError = FString::Printf(TEXT("channel %s is not a channel of this curve. Channels: %s."),
			*MCPJsonProperty::DescribeJsonValue(Raw), *MCPCurveAssetChannelList(Channels));
		return false;
	}

	FString MCPCurveAssetInterpName(ERichCurveInterpMode Mode)
	{
		switch (Mode)
		{
		case RCIM_Constant: return TEXT("constant");
		case RCIM_Cubic: return TEXT("cubic");
		case RCIM_None: return TEXT("none");
		default: return TEXT("linear");
		}
	}

	FString MCPCurveAssetTangentName(ERichCurveTangentMode Mode)
	{
		switch (Mode)
		{
		case RCTM_User: return TEXT("user");
		case RCTM_Break: return TEXT("break");
		case RCTM_None: return TEXT("none");
#if UE_MCP_HAS_5_5_API
		case RCTM_SmartAuto: return TEXT("smartAuto");
#endif
		default: return TEXT("auto");
		}
	}

	FString MCPCurveAssetWeightName(ERichCurveTangentWeightMode Mode)
	{
		switch (Mode)
		{
		case RCTWM_WeightedArrive: return TEXT("arrive");
		case RCTWM_WeightedLeave: return TEXT("leave");
		case RCTWM_WeightedBoth: return TEXT("both");
		default: return TEXT("none");
		}
	}

	template <typename EnumType, SIZE_T N>
	bool MCPCurveAssetParseName(const FString& Raw, const TPair<const TCHAR*, EnumType> (&Names)[N], TEnumAsByte<EnumType>& Out)
	{
		for (const TPair<const TCHAR*, EnumType>& Name : Names)
		{
			if (Raw.Equals(Name.Key, ESearchCase::IgnoreCase)) { Out = Name.Value; return true; }
		}
		return false;
	}

	TSharedPtr<FJsonValue> MCPCurveAssetKeyToJson(const FRichCurveKey& Key)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("time"), Key.Time);
		Obj->SetNumberField(TEXT("value"), Key.Value);
		Obj->SetStringField(TEXT("interpMode"), MCPCurveAssetInterpName(Key.InterpMode));
		Obj->SetStringField(TEXT("tangentMode"), MCPCurveAssetTangentName(Key.TangentMode));
		Obj->SetStringField(TEXT("tangentWeightMode"), MCPCurveAssetWeightName(Key.TangentWeightMode));
		Obj->SetNumberField(TEXT("arriveTangent"), Key.ArriveTangent);
		Obj->SetNumberField(TEXT("leaveTangent"), Key.LeaveTangent);
		Obj->SetNumberField(TEXT("arriveTangentWeight"), Key.ArriveTangentWeight);
		Obj->SetNumberField(TEXT("leaveTangentWeight"), Key.LeaveTangentWeight);
		return MakeShared<FJsonValueObject>(Obj);
	}

	TArray<TSharedPtr<FJsonValue>> MCPCurveAssetKeysToJson(const FRichCurve& Curve)
	{
		TArray<TSharedPtr<FJsonValue>> Keys;
		for (const FRichCurveKey& Key : Curve.GetConstRefOfKeys()) Keys.Add(MCPCurveAssetKeyToJson(Key));
		return Keys;
	}

	/** One key from {time, value, interpMode?, tangentMode?, tangentWeightMode?,
	 *  arriveTangent?, leaveTangent?, arriveTangentWeight?, leaveTangentWeight?}.
	 *  Omitted fields take the FRichCurveKey defaults except interpMode, which
	 *  defaults to cubic as the curve editor's new keys do. */
	bool MCPCurveAssetParseKey(const TSharedPtr<FJsonValue>& Raw, int32 Index, FRichCurveKey& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Raw.IsValid() || !Raw->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
		{
			OutError = FString::Printf(TEXT("keys[%d] is not an object. Each key is {time, value, interpMode?, tangentMode?, arriveTangent?, leaveTangent?}."), Index);
			return false;
		}
		double Time = 0.0;
		double Value = 0.0;
		if (!(*Obj)->TryGetNumberField(TEXT("time"), Time) || !(*Obj)->TryGetNumberField(TEXT("value"), Value))
		{
			OutError = FString::Printf(TEXT("keys[%d] needs numeric time and value fields."), Index);
			return false;
		}
		Out = FRichCurveKey((float)Time, (float)Value);
		Out.InterpMode = RCIM_Cubic;

		static const TPair<const TCHAR*, ERichCurveInterpMode> InterpNames[] = {
			{ TEXT("linear"), RCIM_Linear }, { TEXT("constant"), RCIM_Constant },
			{ TEXT("cubic"), RCIM_Cubic }, { TEXT("none"), RCIM_None },
		};
		static const TPair<const TCHAR*, ERichCurveTangentMode> TangentNames[] = {
			{ TEXT("auto"), RCTM_Auto }, { TEXT("user"), RCTM_User },
			{ TEXT("break"), RCTM_Break }, { TEXT("none"), RCTM_None },
#if UE_MCP_HAS_5_5_API
			{ TEXT("smartAuto"), RCTM_SmartAuto },
#endif
		};
		static const TPair<const TCHAR*, ERichCurveTangentWeightMode> WeightNames[] = {
			{ TEXT("none"), RCTWM_WeightedNone }, { TEXT("arrive"), RCTWM_WeightedArrive },
			{ TEXT("leave"), RCTWM_WeightedLeave }, { TEXT("both"), RCTWM_WeightedBoth },
		};

		FString Text;
		if ((*Obj)->TryGetStringField(TEXT("interpMode"), Text) && !MCPCurveAssetParseName(Text, InterpNames, Out.InterpMode))
		{
			OutError = FString::Printf(TEXT("keys[%d].interpMode '%s' is not one of linear, constant, cubic, none."), Index, *Text);
			return false;
		}
		const bool bTangentModeGiven = (*Obj)->TryGetStringField(TEXT("tangentMode"), Text);
		if (bTangentModeGiven && !MCPCurveAssetParseName(Text, TangentNames, Out.TangentMode))
		{
			OutError = FString::Printf(TEXT("keys[%d].tangentMode '%s' is not one of auto, smartAuto, user, break, none."), Index, *Text);
			return false;
		}
		if ((*Obj)->TryGetStringField(TEXT("tangentWeightMode"), Text) && !MCPCurveAssetParseName(Text, WeightNames, Out.TangentWeightMode))
		{
			OutError = FString::Printf(TEXT("keys[%d].tangentWeightMode '%s' is not one of none, arrive, leave, both."), Index, *Text);
			return false;
		}

		double Number = 0.0;
		bool bTangentGiven = false;
		if ((*Obj)->TryGetNumberField(TEXT("arriveTangent"), Number)) { Out.ArriveTangent = (float)Number; bTangentGiven = true; }
		if ((*Obj)->TryGetNumberField(TEXT("leaveTangent"), Number)) { Out.LeaveTangent = (float)Number; bTangentGiven = true; }
		if ((*Obj)->TryGetNumberField(TEXT("arriveTangentWeight"), Number)) Out.ArriveTangentWeight = (float)Number;
		if ((*Obj)->TryGetNumberField(TEXT("leaveTangentWeight"), Number)) Out.LeaveTangentWeight = (float)Number;
		// An auto tangent is recomputed from the neighbours, so a tangent given
		// without a mode would be thrown away. Keep it by making it a user tangent.
		if (bTangentGiven && !bTangentModeGiven) Out.TangentMode = RCTM_User;
		return true;
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::SetCurveAssetKeys(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
	if (!TryGetArrayParam(Params, TEXT("keys"), KeyValues) || !KeyValues)
	{
		return MCPError(TEXT("Missing keys array. Each key is {time, value, interpMode?, tangentMode?, arriveTangent?, leaveTangent?}; [] clears the channel."));
	}
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;
	TArray<FMCPCurveAssetChannel> Channels = MCPCurveAssetChannels(Asset);
	if (Channels.Num() == 0) return MCPAssetWrongTypeError(AssetPath, Asset, TEXT("CurveFloat, CurveVector or CurveLinearColor"));

	int32 ChannelIndex = 0;
	FString ChannelError;
	TSharedPtr<FJsonValue> ChannelRaw = TryGetParam(Params, TEXT("channelName"));
	if (!ChannelRaw.IsValid() || ChannelRaw->IsNull()) ChannelRaw = TryGetParam(Params, TEXT("channel"));
	if (!MCPCurveAssetResolveChannel(ChannelRaw, Channels, ChannelIndex, ChannelError))
	{
		return MCPError(ChannelError);
	}

	TArray<FRichCurveKey> Keys;
	for (int32 Index = 0; Index < KeyValues->Num(); ++Index)
	{
		FRichCurveKey Key;
		FString KeyError;
		if (!MCPCurveAssetParseKey((*KeyValues)[Index], Index, Key, KeyError)) return MCPError(KeyError);
		Keys.Add(Key);
	}
	Keys.StableSort([](const FRichCurveKey& A, const FRichCurveKey& B) { return A.Time < B.Time; });
	for (int32 Index = 1; Index < Keys.Num(); ++Index)
	{
		if (FMath::IsNearlyEqual(Keys[Index - 1].Time, Keys[Index].Time))
		{
			return MCPError(FString::Printf(TEXT("Two keys share time %g. A channel holds one key per time."), Keys[Index].Time));
		}
	}

	if (bSave)
	{
		if (auto Blocked = MCPAssetWriteBlockedError(Asset, AssetPath, TEXT("set curve keys"))) return Blocked;
	}

	FRichCurve& Curve = *Channels[ChannelIndex].Curve;
	const TArray<TSharedPtr<FJsonValue>> PriorKeys = MCPCurveAssetKeysToJson(Curve);

	{
		FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "SetCurveAssetKeys", "Set curve keys"));
		Asset->Modify();
		Curve.SetKeys(Keys);
		Curve.AutoSetTangents();
		Asset->PostEditChange();
		Asset->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("curveClass"), Asset->GetClass()->GetName());
	Result->SetNumberField(TEXT("channel"), ChannelIndex);
	Result->SetStringField(TEXT("channelName"), Channels[ChannelIndex].Name);
	Result->SetNumberField(TEXT("channelCount"), Channels.Num());
	Result->SetNumberField(TEXT("keyCount"), Curve.GetNumKeys());
	Result->SetArrayField(TEXT("keys"), MCPCurveAssetKeysToJson(Curve));
	Result->SetNumberField(TEXT("previousKeyCount"), PriorKeys.Num());
	if (bSave)
	{
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(Asset, SaveReason);
		MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveReason);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
	}

	// Every FRichCurveKey field is in the payload, so the replay is exact.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetNumberField(TEXT("channel"), ChannelIndex);
	Payload->SetArrayField(TEXT("keys"), PriorKeys);
	Payload->SetBoolField(TEXT("save"), bSave);
	MCPSetRollback(Result, TEXT("set_curve_keys"), Payload);
	return MCPResult(Result);
}
