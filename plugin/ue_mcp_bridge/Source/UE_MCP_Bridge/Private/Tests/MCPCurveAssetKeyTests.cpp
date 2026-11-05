// Coverage for keying one channel of a curve asset (#1687).
//
// asset(set_property) on CurveVector FloatCurves[1] crashed the editor: the
// write snapshot copied ArrayDim elements starting at element 1 and ran off the
// end of the fixed array. asset(set_curve_keys) is the native per-channel writer.
// Fixtures live under a private mount, since the asset actions refuse
// protected mounts.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCPScopedTestMount.h"

#include "HandlerRegistry.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	const TCHAR* const MCPCurveKeyMountRoot = TEXT("/UEMCPCurveKeyTest/");

	template <typename CurveType>
	TStrongObjectPtr<CurveType> CurveKeyFixture(const TCHAR* Prefix)
	{
		UPackage* Package = CreatePackage(*FString::Printf(
			TEXT("%s%s_%s"), MCPCurveKeyMountRoot, Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		Package->SetDirtyFlag(false);
		return TStrongObjectPtr<CurveType>(NewObject<CurveType>(Package, FName(Prefix), RF_Public | RF_Standalone));
	}

	/** Channel i holds one key at time 0 with value (i + 1) * 10. */
	void CurveKeySeed(FRichCurve* Channels, int32 Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Channels[Index].Reset();
			Channels[Index].AddKey(0.f, (Index + 1) * 10.f);
		}
	}

	TSharedPtr<FJsonObject> CurveKeyCall(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Params);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	bool CurveKeySucceeded(const TSharedPtr<FJsonObject>& Response)
	{
		bool bSuccess = false;
		return Response.IsValid() && Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	FString CurveKeyError(const TSharedPtr<FJsonObject>& Response)
	{
		FString Error;
		if (Response.IsValid()) Response->TryGetStringField(TEXT("error"), Error);
		return Error;
	}

	TSharedPtr<FJsonValue> CurveKeyJson(double Time, double Value, const TCHAR* InterpMode, const TCHAR* TangentMode = nullptr, double Tangent = 0.0)
	{
		TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
		Key->SetNumberField(TEXT("time"), Time);
		Key->SetNumberField(TEXT("value"), Value);
		Key->SetStringField(TEXT("interpMode"), InterpMode);
		if (TangentMode)
		{
			Key->SetStringField(TEXT("tangentMode"), TangentMode);
			Key->SetNumberField(TEXT("arriveTangent"), Tangent);
			Key->SetNumberField(TEXT("leaveTangent"), Tangent);
		}
		return MakeShared<FJsonValueObject>(Key);
	}

	TSharedPtr<FJsonObject> CurveKeyParams(const UObject& Asset, const TSharedPtr<FJsonValue>& Channel, const TArray<TSharedPtr<FJsonValue>>& Keys)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), Asset.GetPathName());
		if (Channel.IsValid()) Params->SetField(Channel->Type == EJson::String ? TEXT("channelName") : TEXT("channel"), Channel);
		Params->SetArrayField(TEXT("keys"), Keys);
		Params->SetBoolField(TEXT("save"), false);
		return Params;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCurveAssetSetKeysTest,
	"UE.MCP.Asset.CurveKeys.SetChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCurveAssetSetKeysTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPCurveKeyMountRoot), TEXT("UEMCPCurveKeyTest") };
	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	// CurveVector channel 1 by index: replaced, its siblings untouched.
	TStrongObjectPtr<UCurveVector> Vector = CurveKeyFixture<UCurveVector>(TEXT("CV_Recoil"));
	CurveKeySeed(Vector->FloatCurves, 3);
	const TSharedPtr<FJsonObject> Response = CurveKeyCall(Registry, TEXT("set_curve_keys"), CurveKeyParams(*Vector,
		MakeShared<FJsonValueNumber>(1),
		{ CurveKeyJson(0.5, 2.0, TEXT("linear")), CurveKeyJson(0.0, 1.0, TEXT("constant")), CurveKeyJson(1.0, 3.0, TEXT("cubic"), TEXT("user"), 0.25) }));
	TestTrue(FString::Printf(TEXT("set_curve_keys keys channel 1 (%s)"), *CurveKeyError(Response)), CurveKeySucceeded(Response));
	const FRichCurve& Y = Vector->FloatCurves[1];
	if (TestEqual(TEXT("channel 1 holds the three keys"), Y.GetNumKeys(), 3))
	{
		const TArray<FRichCurveKey>& Keys = Y.GetConstRefOfKeys();
		TestEqual(TEXT("keys are sorted by time"), Keys[0].Time, 0.f);
		TestEqual(TEXT("first key value"), Keys[0].Value, 1.f);
		TestEqual(TEXT("first key is constant"), (int32)Keys[0].InterpMode, (int32)RCIM_Constant);
		TestEqual(TEXT("second key is linear"), (int32)Keys[1].InterpMode, (int32)RCIM_Linear);
		TestEqual(TEXT("last key keeps its user tangent mode"), (int32)Keys[2].TangentMode, (int32)RCTM_User);
		TestEqual(TEXT("last key keeps its user tangent"), Keys[2].ArriveTangent, 0.25f);
		TestEqual(TEXT("the curve evaluates the new keys"), Y.Eval(0.5f), 2.f);
	}
	TestEqual(TEXT("channel 0 is untouched"), Vector->FloatCurves[0].Eval(0.f), 10.f);
	TestEqual(TEXT("channel 2 is untouched"), Vector->FloatCurves[2].Eval(0.f), 30.f);
	double KeyCount = 0;
	TestTrue(TEXT("the response reports keyCount"), Response.IsValid() && Response->TryGetNumberField(TEXT("keyCount"), KeyCount));
	TestEqual(TEXT("keyCount is 3"), KeyCount, 3.0);
	FString ChannelName;
	if (Response.IsValid()) Response->TryGetStringField(TEXT("channelName"), ChannelName);
	TestEqual(TEXT("channel 1 is Y"), ChannelName, FString(TEXT("Y")));

	// The rollback puts the previous single key back.
	const TSharedPtr<FJsonObject>* Rollback = nullptr;
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	FString InverseMethod;
	if (TestTrue(TEXT("the response carries a rollback"), Response.IsValid() && Response->TryGetObjectField(TEXT("rollback"), Rollback) && Rollback
		&& (*Rollback)->TryGetObjectField(TEXT("payload"), Payload) && Payload && (*Rollback)->TryGetStringField(TEXT("method"), InverseMethod)))
	{
		TestEqual(TEXT("the inverse is set_curve_keys"), InverseMethod, FString(TEXT("set_curve_keys")));
		const TSharedPtr<FJsonObject> Undone = CurveKeyCall(Registry, *InverseMethod, *Payload);
		TestTrue(FString::Printf(TEXT("the rollback applies (%s)"), *CurveKeyError(Undone)), CurveKeySucceeded(Undone));
		TestEqual(TEXT("the rollback restores one key"), Vector->FloatCurves[1].GetNumKeys(), 1);
		TestEqual(TEXT("the rollback restores its value"), Vector->FloatCurves[1].Eval(0.f), 20.f);
	}

	// A channel by name, and [] clears it.
	const TSharedPtr<FJsonObject> Cleared = CurveKeyCall(Registry, TEXT("set_curve_keys"), CurveKeyParams(*Vector, MakeShared<FJsonValueString>(TEXT("z")), {}));
	TestTrue(FString::Printf(TEXT("channel z by name clears (%s)"), *CurveKeyError(Cleared)), CurveKeySucceeded(Cleared));
	TestEqual(TEXT("channel 2 is empty"), Vector->FloatCurves[2].GetNumKeys(), 0);
	TestEqual(TEXT("channel 0 still untouched"), Vector->FloatCurves[0].GetNumKeys(), 1);

	// A channel the class does not have is refused and changes nothing.
	const TSharedPtr<FJsonObject> OutOfRange = CurveKeyCall(Registry, TEXT("set_curve_keys"), CurveKeyParams(*Vector, MakeShared<FJsonValueNumber>(3), { CurveKeyJson(0.0, 9.0, TEXT("linear")) }));
	TestFalse(TEXT("channel 3 on a CurveVector is refused"), CurveKeySucceeded(OutOfRange));
	TestTrue(TEXT("the refusal lists the channels"), CurveKeyError(OutOfRange).Contains(TEXT("2 (Z)")));

	// Two keys at one time are refused.
	const TSharedPtr<FJsonObject> Duplicate = CurveKeyCall(Registry, TEXT("set_curve_keys"), CurveKeyParams(*Vector, MakeShared<FJsonValueNumber>(0),
		{ CurveKeyJson(0.0, 1.0, TEXT("linear")), CurveKeyJson(0.0, 2.0, TEXT("linear")) }));
	TestFalse(TEXT("duplicate key times are refused"), CurveKeySucceeded(Duplicate));
	TestEqual(TEXT("the refused write left channel 0 alone"), Vector->FloatCurves[0].Eval(0.f), 10.f);

	// CurveLinearColor alpha by name.
	TStrongObjectPtr<UCurveLinearColor> Color = CurveKeyFixture<UCurveLinearColor>(TEXT("CLC_Fade"));
	CurveKeySeed(Color->FloatCurves, 4);
	const TSharedPtr<FJsonObject> Alpha = CurveKeyCall(Registry, TEXT("set_curve_keys"), CurveKeyParams(*Color, MakeShared<FJsonValueString>(TEXT("A")),
		{ CurveKeyJson(0.0, 1.0, TEXT("linear")), CurveKeyJson(2.0, 0.0, TEXT("linear")) }));
	TestTrue(FString::Printf(TEXT("CurveLinearColor alpha keys (%s)"), *CurveKeyError(Alpha)), CurveKeySucceeded(Alpha));
	TestEqual(TEXT("alpha fades to half at t=1"), Color->FloatCurves[3].Eval(1.f), 0.5f);
	TestEqual(TEXT("red is untouched"), Color->FloatCurves[0].Eval(0.f), 10.f);

	Vector->GetOutermost()->SetDirtyFlag(false);
	Color->GetOutermost()->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCurveAssetFixedArrayPropertyTest,
	"UE.MCP.Asset.CurveKeys.FixedArrayPropertyWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCurveAssetFixedArrayPropertyTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPCurveKeyMountRoot), TEXT("UEMCPCurveKeyTest") };
	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	// The last element is the one whose complete copy ran furthest past the end.
	TStrongObjectPtr<UCurveVector> Vector = CurveKeyFixture<UCurveVector>(TEXT("CV_Property"));
	CurveKeySeed(Vector->FloatCurves, 3);
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), Vector->GetPathName());
	Params->SetStringField(TEXT("propertyName"), TEXT("FloatCurves[2]"));
	Params->SetStringField(TEXT("value"), TEXT("(Keys=((Time=0.000000,Value=5.000000),(Time=1.000000,Value=7.000000)))"));
	Params->SetBoolField(TEXT("save"), false);
	const TSharedPtr<FJsonObject> Response = CurveKeyCall(Registry, TEXT("set_asset_property"), Params);
	TestTrue(FString::Printf(TEXT("set_asset_property writes FloatCurves[2] (%s)"), *CurveKeyError(Response)), CurveKeySucceeded(Response));
	TestEqual(TEXT("element 2 holds the two keys"), Vector->FloatCurves[2].GetNumKeys(), 2);
	TestEqual(TEXT("element 2 evaluates the new keys"), Vector->FloatCurves[2].Eval(1.f), 7.f);
	TestEqual(TEXT("element 0 is untouched"), Vector->FloatCurves[0].Eval(0.f), 10.f);
	TestEqual(TEXT("element 1 is untouched"), Vector->FloatCurves[1].Eval(0.f), 20.f);

	// A refused write to element 1 restores that element only. Whichever field
	// is applied first, the unknown one fails the write.
	const float PriorDefault = Vector->FloatCurves[1].GetDefaultValue();
	TSharedPtr<FJsonObject> Partial = MakeShared<FJsonObject>();
	Partial->SetNumberField(TEXT("DefaultValue"), 99.0);
	Partial->SetNumberField(TEXT("NoSuchField"), 1.0);
	Params->SetStringField(TEXT("propertyName"), TEXT("FloatCurves[1]"));
	Params->SetObjectField(TEXT("value"), Partial);
	const TSharedPtr<FJsonObject> Refused = CurveKeyCall(Registry, TEXT("set_asset_property"), Params);
	TestFalse(TEXT("an unknown struct field is refused"), CurveKeySucceeded(Refused));
	TestEqual(TEXT("element 1 keys are unchanged after the refusal"), Vector->FloatCurves[1].Eval(0.f), 20.f);
	TestEqual(TEXT("element 1 DefaultValue is restored after the refusal"), Vector->FloatCurves[1].GetDefaultValue(), PriorDefault);
	TestEqual(TEXT("element 2 is unchanged after the refusal"), Vector->FloatCurves[2].Eval(1.f), 7.f);

	Vector->GetOutermost()->SetDirtyFlag(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
