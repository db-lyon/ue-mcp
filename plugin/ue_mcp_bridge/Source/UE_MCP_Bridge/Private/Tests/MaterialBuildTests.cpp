// Coverage for the material handlers that is safe to run anywhere.
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process when it is called without a filter, against whatever project the
// bridge is attached to. A test that created materials or wrote to textures
// would therefore mutate a user's live project as a side effect of asking for a
// test run. Everything below is confined to registration and to argument
// validation, all of which are specified to write nothing; the build, sampler
// selection and mesh assignment behaviour is exercised by the smoke suite
// against the dedicated test project instead.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/MaterialHandlers.h"
#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "Engine/EngineTypes.h"

namespace
{
TSharedPtr<FJsonObject> MaterialTestsRunHandler(
	FMCPHandlerRegistry& Registry,
	const FString& Method,
	const TSharedPtr<FJsonObject>& Request)
{
	const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
	if (Response.IsValid() && Response->Type == EJson::Object)
	{
		return Response->AsObject();
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaterialBuildRegistrationTest,
	"UE.MCP.Material.BuildMaterial.RegistrationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaterialBuildRegistrationTest::RunTest(const FString& Parameters)
{
	// The engine's own asset loader logs an Error every time it is asked for a
	// path that names nothing, and the automation framework fails a test on any
	// unexpected Error. Asserting what a deliberate miss reports therefore
	// failed the test doing the asserting. Declared the way SequencerHandlerTests
	// declares it; a count of 0 means any number of occurrences.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	FMCPHandlerRegistry Registry;
	FMaterialHandlers::RegisterHandlers(Registry);

	TestTrue(TEXT("build_material is registered"), Registry.HasHandler(TEXT("build_material")));

	// No texture set at all.
	if (TSharedPtr<FJsonObject> Missing = MaterialTestsRunHandler(Registry, TEXT("build_material"), MakeShared<FJsonObject>()))
	{
		TestFalse(TEXT("a missing texture set is rejected"), Missing->GetBoolField(TEXT("success")));
		TestTrue(TEXT("the rejection names the field"), Missing->GetStringField(TEXT("error")).Contains(TEXT("textures")));
	}
	else
	{
		AddError(TEXT("build_material did not return an object for a missing texture set"));
	}

	// An empty texture set is a different mistake and gets its own message.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetObjectField(TEXT("textures"), MakeShared<FJsonObject>());
		if (TSharedPtr<FJsonObject> Empty = MaterialTestsRunHandler(Registry, TEXT("build_material"), Request))
		{
			TestFalse(TEXT("an empty texture set is rejected"), Empty->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the rejection says the set is empty"), Empty->GetStringField(TEXT("error")).Contains(TEXT("empty")));
		}
		else
		{
			AddError(TEXT("build_material did not return an object for an empty texture set"));
		}
	}

	// A key that is not a material property is rejected before anything is
	// created, and the message lists what is accepted.
	{
		TSharedPtr<FJsonObject> Textures = MakeShared<FJsonObject>();
		Textures->SetStringField(TEXT("notAProperty"), TEXT("/Game/DoesNotExist"));
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetObjectField(TEXT("textures"), Textures);
		Request->SetStringField(TEXT("name"), TEXT("M_UEMCP_BuildValidationOnly"));
		if (TSharedPtr<FJsonObject> Bad = MaterialTestsRunHandler(Registry, TEXT("build_material"), Request))
		{
			TestFalse(TEXT("an unknown property key is rejected"), Bad->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the rejection names the offending key"), Bad->GetStringField(TEXT("error")).Contains(TEXT("notAProperty")));
			TestTrue(TEXT("the rejection lists the packed keys"), Bad->GetStringField(TEXT("error")).Contains(TEXT("orm")));
		}
		else
		{
			AddError(TEXT("build_material did not return an object for an unknown property key"));
		}
	}

	// A known key whose texture does not exist fails on the texture, not on the
	// key, and still creates nothing.
	{
		TSharedPtr<FJsonObject> Textures = MakeShared<FJsonObject>();
		Textures->SetStringField(TEXT("baseColor"), TEXT("/Game/UEMCPTests/DoesNotExist_UEMCP"));
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetObjectField(TEXT("textures"), Textures);
		Request->SetStringField(TEXT("name"), TEXT("M_UEMCP_BuildValidationOnly"));
		if (TSharedPtr<FJsonObject> Bad = MaterialTestsRunHandler(Registry, TEXT("build_material"), Request))
		{
			TestFalse(TEXT("a missing texture is rejected"), Bad->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the rejection names the texture path"), Bad->GetStringField(TEXT("error")).Contains(TEXT("DoesNotExist_UEMCP")));
		}
		else
		{
			AddError(TEXT("build_material did not return an object for a missing texture"));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaterialVirtualSamplerPairingTest,
	"UE.MCP.Material.BuildMaterial.VirtualSamplerTypesArePaired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaterialVirtualSamplerPairingTest::RunTest(const FString& Parameters)
{
	// The whole point of build_material is picking the virtual variant of a
	// sampler type for a virtual (UDIM) texture. That correction is only
	// possible while every plain type the builder can produce has a virtual
	// counterpart in the engine enum, so assert the pairing the builder relies
	// on rather than trusting it to stay true across engine versions.
	const EMaterialSamplerType PlainTypes[] =
	{
		SAMPLERTYPE_Color,
		SAMPLERTYPE_Grayscale,
		SAMPLERTYPE_Alpha,
		SAMPLERTYPE_Normal,
		SAMPLERTYPE_Masks,
		SAMPLERTYPE_LinearColor,
		SAMPLERTYPE_LinearGrayscale,
	};
	const EMaterialSamplerType VirtualTypes[] =
	{
		SAMPLERTYPE_VirtualColor,
		SAMPLERTYPE_VirtualGrayscale,
		SAMPLERTYPE_VirtualAlpha,
		SAMPLERTYPE_VirtualNormal,
		SAMPLERTYPE_VirtualMasks,
		SAMPLERTYPE_VirtualLinearColor,
		SAMPLERTYPE_VirtualLinearGrayscale,
	};
	static_assert(UE_ARRAY_COUNT(PlainTypes) == UE_ARRAY_COUNT(VirtualTypes), "sampler pairing table is lopsided");

	for (int32 Index = 0; Index < (int32)UE_ARRAY_COUNT(PlainTypes); ++Index)
	{
		TestFalse(
			FString::Printf(TEXT("plain sampler %d does not read as virtual"), Index),
			IsVirtualSamplerType(PlainTypes[Index]));
		TestTrue(
			FString::Printf(TEXT("virtual sampler %d reads as virtual"), Index),
			IsVirtualSamplerType(VirtualTypes[Index]));
	}

	UEnum* Enum = StaticEnum<EMaterialSamplerType>();
	TestNotNull(TEXT("EMaterialSamplerType is reflected, so sampler types can be named in results"), Enum);
	if (Enum)
	{
		TestEqual(
			TEXT("the virtual colour sampler still carries the name the builder reports"),
			Enum->GetNameStringByValue((int64)SAMPLERTYPE_VirtualColor),
			FString(TEXT("SAMPLERTYPE_VirtualColor")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaterialInstanceReadRegistrationTest,
	"UE.MCP.Material.Instance.ReadAndParameterValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaterialInstanceReadRegistrationTest::RunTest(const FString& Parameters)
{
	// The engine's own asset loader logs an Error every time it is asked for a
	// path that names nothing, and the automation framework fails a test on any
	// unexpected Error. Asserting what a deliberate miss reports therefore
	// failed the test doing the asserting. Declared the way SequencerHandlerTests
	// declares it; a count of 0 means any number of occurrences.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	FMCPHandlerRegistry Registry;
	FMaterialHandlers::RegisterHandlers(Registry);

	TestTrue(TEXT("read_material is registered"), Registry.HasHandler(TEXT("read_material")));
	TestTrue(TEXT("list_material_parameters is registered"), Registry.HasHandler(TEXT("list_material_parameters")));
	TestTrue(TEXT("set_material_parameter is registered"), Registry.HasHandler(TEXT("set_material_parameter")));

	// #952: a path that is neither a Material nor a MaterialInstance now says so
	// in one message, rather than claiming the asset is not a material when the
	// real cause was that only a base Material was ever accepted.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), TEXT("/Game/UEMCPTests/DoesNotExist_UEMCP"));
		const TCHAR* ReadMethods[] = { TEXT("read_material"), TEXT("list_material_parameters") };
		for (const TCHAR* Method : ReadMethods)
		{
			if (TSharedPtr<FJsonObject> Response = MaterialTestsRunHandler(Registry, Method, Request))
			{
				TestFalse(FString::Printf(TEXT("%s rejects a missing asset"), Method), Response->GetBoolField(TEXT("success")));
				TestTrue(
					FString::Printf(TEXT("%s says both kinds were tried"), Method),
					Response->GetStringField(TEXT("error")).Contains(TEXT("material instance")));
			}
			else
			{
				AddError(FString::Printf(TEXT("%s did not return an object for a missing asset"), Method));
			}
		}
	}

	// set_material_parameter still names its required arguments.
	{
		if (TSharedPtr<FJsonObject> Response = MaterialTestsRunHandler(Registry, TEXT("set_material_parameter"), MakeShared<FJsonObject>()))
		{
			TestFalse(TEXT("a parameter write with no asset path is rejected"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the rejection names assetPath"), Response->GetStringField(TEXT("error")).Contains(TEXT("assetPath")));
		}
		else
		{
			AddError(TEXT("set_material_parameter did not return an object for an empty request"));
		}

		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), TEXT("/Game/UEMCPTests/DoesNotExist_UEMCP"));
		if (TSharedPtr<FJsonObject> Response = MaterialTestsRunHandler(Registry, TEXT("set_material_parameter"), Request))
		{
			TestFalse(TEXT("a parameter write with no parameter name is rejected"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the rejection names parameterName"), Response->GetStringField(TEXT("error")).Contains(TEXT("parameterName")));
		}
		else
		{
			AddError(TEXT("set_material_parameter did not return an object for a request with no parameter name"));
		}
	}

	return true;
}

#endif
