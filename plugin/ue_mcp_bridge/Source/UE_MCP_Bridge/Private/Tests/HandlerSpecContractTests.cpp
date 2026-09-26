// #1057: a handler registered with a parameter spec reads exactly what it
// declares. Every declared parameter is sent under its declared name and the
// read set is compared both ways: a declared name the handler never reads is
// a parameter the surface advertises for nothing, and a name it reads that the
// spec does not declare is one the surface can never deliver.
//
// The values point at an asset that does not exist, and the same string is an
// actor path that names nothing, so every handler stops before it writes: at
// its first load, at its actor lookup, or at a validation these values fail
// (a zero duration, limit 0, an unknown mode). That is also why each spec'd
// handler reads all of its parameters before loading or validating anything.
//
// A spec with a choice is called once per branch, since a handler reads the
// side it was given: each call sends the parameters outside every choice, one
// branch of one choice and the first branch of each other one. Each call has
// to read what it sent.
//
// A handler whose values would reach a create, spawn, save or run before
// either failure is registered with MCPSpec::ContractExempt(reason). It is not
// called here; tests/unit/handler-spec-exempt.test.ts holds its source to its
// spec instead.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Animation/AnimationHandlers.h"
#include "Handlers/Audio/AudioHandlers.h"
#include "Handlers/Fab/FabHandlers.h"
#include "Handlers/Networking/NetworkingHandlers.h"
#include "Handlers/Project/ProjectHandlers.h"
#include "Handlers/StateTree/StateTreeHandlers.h"
#include "Handlers/Gameplay/GameplayHandlers.h"
#include "Handlers/Gas/GasHandlers.h"
#include "Handlers/Physics/PhysicsHandlers.h"
#include "Handlers/Mass/MassHandlers.h"
#include "Handlers/Dialog/DialogHandlers.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Handlers/Sequencer/SequencerHandlers.h"
#include "Handlers/PCG/PCGHandlers.h"
#include "Handlers/Niagara/NiagaraHandlers.h"
#include "Handlers/Material/MaterialHandlers.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Handlers/Asset/AssetHandlers_Geometry.h"
#include "Handlers/Asset/AssetHandlers_BulkRead.h"
#include "Handlers/Asset/AssetHandlers_MeshBoolean.h"
#include "Handlers/SkeletalMesh/SkeletalMeshHandlers.h"
#include "Handlers/Lock/LockHandlers.h"
#include "Handlers/Diff/DiffHandlers.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Handlers/Blueprint/BlueprintHandlers_Collision.h"
#include "Handlers/Chooser/ChooserHandlers.h"
#include "Handlers/Demo/DemoHandlers.h"
#include "Handlers/Epic/EpicHandlers.h"
#include "Handlers/Reflection/ReflectionHandlers.h"
#include "Handlers/Foliage/FoliageHandlers.h"
#include "Handlers/Landscape/LandscapeHandlers.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Handlers/Spline/SplineHandlers.h"
#include "Misc/AutomationTest.h"

namespace MCPHandlerSpecTests
{
	const TCHAR* const MissingAsset = TEXT("/Game/UEMCP/HandlerSpecContract/NoSuchAsset");

	TSharedPtr<FJsonValue> ValueFor(EMCPParamType Type)
	{
		switch (Type)
		{
		case EMCPParamType::Color:
		{
			TSharedPtr<FJsonObject> Color = MakeShared<FJsonObject>();
			Color->SetNumberField(TEXT("r"), 0.0);
			Color->SetNumberField(TEXT("g"), 0.0);
			Color->SetNumberField(TEXT("b"), 0.0);
			Color->SetNumberField(TEXT("a"), 1.0);
			return MakeShared<FJsonValueObject>(Color);
		}
		case EMCPParamType::String:
			return MakeShared<FJsonValueString>(MissingAsset);
		case EMCPParamType::Number:
		case EMCPParamType::Integer:
			return MakeShared<FJsonValueNumber>(0.0);
		case EMCPParamType::Boolean:
			return MakeShared<FJsonValueBoolean>(false);
		case EMCPParamType::Array:
			return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>());
		case EMCPParamType::Vec3:
		{
			TSharedPtr<FJsonObject> Vec = MakeShared<FJsonObject>();
			Vec->SetNumberField(TEXT("x"), 0.0);
			Vec->SetNumberField(TEXT("y"), 0.0);
			Vec->SetNumberField(TEXT("z"), 0.0);
			return MakeShared<FJsonValueObject>(Vec);
		}
		case EMCPParamType::Rotator:
		{
			TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
			Rot->SetNumberField(TEXT("pitch"), 0.0);
			Rot->SetNumberField(TEXT("yaw"), 0.0);
			Rot->SetNumberField(TEXT("roll"), 0.0);
			return MakeShared<FJsonValueObject>(Rot);
		}
		default:
			return MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
		}
	}

	/** A declared parameter's contract value: its literal when it has one, else a value of its own type. */
	TSharedPtr<FJsonValue> ValueForParam(const FMCPParamSpec& Param)
	{
		return Param.LiteralValue.IsValid() ? Param.LiteralValue : ValueFor(Param.Type);
	}

	/**
	 * The parameter sets one spec is called with. Without a choice that is every
	 * declared name. With choices, each branch of each choice gets one call,
	 * carrying the names outside every choice and the first branch of each
	 * other choice.
	 */
	TArray<TArray<FString>> ContractRuns(const FMCPHandlerSpec& Spec)
	{
		TSet<FString> InChoice;
		for (const FMCPParamChoice& Choice : Spec.Choices)
		{
			for (const TArray<FString>& Branch : Choice.Branches)
			{
				for (const FString& Name : Branch)
				{
					InChoice.Add(Name);
				}
			}
		}
		TArray<FString> Base;
		for (const FMCPParamSpec& Param : Spec.Params)
		{
			if (!InChoice.Contains(Param.Name)) Base.Add(Param.Name);
		}

		TArray<TArray<FString>> Runs;
		if (Spec.Choices.Num() == 0)
		{
			Runs.Add(Base);
			return Runs;
		}
		for (int32 ChoiceIndex = 0; ChoiceIndex < Spec.Choices.Num(); ++ChoiceIndex)
		{
			for (const TArray<FString>& Branch : Spec.Choices[ChoiceIndex].Branches)
			{
				TArray<FString> Run = Base;
				Run.Append(Branch);
				for (int32 Other = 0; Other < Spec.Choices.Num(); ++Other)
				{
					if (Other != ChoiceIndex && Spec.Choices[Other].Branches.Num() > 0)
					{
						Run.Append(Spec.Choices[Other].Branches[0]);
					}
				}
				Runs.Add(MoveTemp(Run));
			}
		}
		return Runs;
	}

	/** Reads `assetPath` and echoes it back, so alias resolution is visible in the result. */
	TSharedPtr<FJsonValue> AliasProbe(const TSharedPtr<FJsonObject>& Params)
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Result->SetStringField(TEXT("assetPath"), OptionalString(Params, TEXT("assetPath")));
		return MCPResult(Result);
	}

	TSharedPtr<FJsonObject> ObjectOf(const TSharedPtr<FJsonValue>& Value)
	{
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPHandlerSpecContractTest,
	"UE.MCP.Bridge.HandlerSpec.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPHandlerSpecContractTest::RunTest(const FString& Parameters)
{
	using namespace MCPHandlerSpecTests;
	// Loading a path with nothing behind it logs through the editor asset library,
	// and a direct LoadObject names the missing path in its warning.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("HandlerSpecContract/NoSuchAsset"), EAutomationExpectedErrorFlags::Contains, 0);
	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);
	FAudioHandlers::RegisterHandlers(Registry);
	FNetworkingHandlers::RegisterHandlers(Registry);
	FFabHandlers::RegisterHandlers(Registry);
	FProjectHandlers::RegisterHandlers(Registry);
	FStateTreeHandlers::RegisterHandlers(Registry);
	FGameplayHandlers::RegisterHandlers(Registry);
	FGasHandlers::RegisterHandlers(Registry);
	FPhysicsHandlers::RegisterHandlers(Registry);
	FMassHandlers::RegisterHandlers(Registry);
	FEditorHandlers::RegisterHandlers(Registry);
	FSequencerHandlers::RegisterHandlers(Registry);
	FDialogHandlers::RegisterHandlers(Registry);
	FPCGHandlers::RegisterHandlers(Registry);
	FNiagaraHandlers::RegisterHandlers(Registry);
	FMaterialHandlers::RegisterHandlers(Registry);
	FWidgetHandlers::RegisterHandlers(Registry);
	// asset's create actions are called only where a validation the contract
	// values fail (an unresolvable class or struct, a name holding '/', an
	// invalid package name) runs before anything is created; the rest are
	// contract-exempt.
	FAssetHandlers::RegisterHandlers(Registry);
	FAssetGeometryHandlers::RegisterHandlers(Registry);
	FAssetBulkReadHandlers::RegisterHandlers(Registry);
	FAssetMeshBooleanHandlers::RegisterHandlers(Registry);
	FSkeletalMeshHandlers::RegisterHandlers(Registry);
	FLockHandlers::RegisterHandlers(Registry);
	FDiffHandlers::RegisterHandlers(Registry);
	FBlueprintHandlers::RegisterHandlers(Registry);
	FCollisionQueryHandlers::RegisterHandlers(Registry);
	FChooserHandlers::RegisterHandlers(Registry);
	FDemoHandlers::RegisterHandlers(Registry);
	FEpicHandlers::RegisterHandlers(Registry);
	FReflectionHandlers::RegisterHandlers(Registry);
	FFoliageHandlers::RegisterHandlers(Registry);
	FLandscapeHandlers::RegisterHandlers(Registry);
	FLevelHandlers::RegisterHandlers(Registry);
	FSplineHandlers::RegisterHandlers(Registry);

	const TMap<FString, FMCPHandlerSpec>& Specs = Registry.GetHandlerSpecs();
	TestTrue(TEXT("handlers register with a parameter spec"), Specs.Num() > 0);

	TArray<FString> Methods;
	Specs.GetKeys(Methods);
	Methods.Sort();
	for (const FString& Method : Methods)
	{
		const FMCPHandlerSpec& Spec = Specs.FindChecked(Method);
		const FMCPHandlerRegistry::FHandlerFunction* Handler = Registry.FindCppHandler(Method);
		if (!TestNotNull(*FString::Printf(TEXT("%s is registered"), *Method), Handler))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("%s: the spec validates"), *Method), FMCPHandlerRegistry::ValidateHandlerSpec(Spec).IsEmpty());

		// Held to its spec by the source check in the unit tests instead: these
		// values would reach a write before anything failed.
		if (!Spec.ContractExemptReason.IsEmpty())
		{
			continue;
		}

		TMap<FString, const FMCPParamSpec*> Declared;
		for (const FMCPParamSpec& Param : Spec.Params)
		{
			Declared.Add(Param.Name, &Param);
		}

		for (const TArray<FString>& Run : ContractRuns(Spec))
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			for (const FString& Name : Run)
			{
				Params->SetField(Name, ValueForParam(*Declared.FindChecked(Name)));
			}

			TSet<FString> Read;
			{
				FMCPParamReadScope Scope(Params);
				(*Handler)(Params);
				Read = Scope.ReadKeys();
			}

			const FString Sent = FString::Join(Run, TEXT(", "));
			for (const FString& Name : Run)
			{
				TestTrue(*FString::Printf(TEXT("%s reads its declared parameter '%s' (sent: %s)"), *Method, *Name, *Sent), Read.Contains(Name));
			}
			for (const FString& Name : Read)
			{
				TestTrue(*FString::Printf(TEXT("%s reads only declared parameters, not '%s'"), *Method, *Name), Declared.Contains(Name));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPHandlerSpecRegistrationTest,
	"UE.MCP.Bridge.HandlerSpec.Registration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPHandlerSpecRegistrationTest::RunTest(const FString& Parameters)
{
	using namespace MCPHandlerSpecTests;

	// Every routing name is refused as a parameter and as an alias.
	for (const FString& Routing : MCPRoutingParamNames())
	{
		const FMCPParamSpec AsName = MCPParam::Optional(*Routing, EMCPParamType::String, TEXT("probe"));
		TestFalse(*FString::Printf(TEXT("'%s' is refused as a name"), *Routing),
			FMCPHandlerRegistry::ValidateParamSpecs({ AsName }).IsEmpty());
		const FMCPParamSpec AsAlias = MCPParam::Optional(TEXT("probeName"), EMCPParamType::String, TEXT("probe")).Alias(*Routing);
		TestFalse(*FString::Printf(TEXT("'%s' is refused as an alias"), *Routing),
			FMCPHandlerRegistry::ValidateParamSpecs({ AsAlias }).IsEmpty());
	}
	TestFalse(TEXT("a name declared twice is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("probeName"), EMCPParamType::String, TEXT("probe")),
		MCPParam::Optional(TEXT("other"), EMCPParamType::String, TEXT("probe")).Alias(TEXT("probeName")),
	}).IsEmpty());
	TestFalse(TEXT("an item type off an array is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("probeName"), EMCPParamType::String, TEXT("probe")).Items(EMCPParamType::Number),
	}).IsEmpty());
	TestTrue(TEXT("an ordinary spec validates"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Required(TEXT("assetPath"), EMCPParamType::String, TEXT("probe")).Alias(TEXT("path")),
		MCPParam::Optional(TEXT("frames"), EMCPParamType::Array, TEXT("probe")).Items(EMCPParamType::Integer),
	}).IsEmpty());

	// Value shapes.
	TestTrue(TEXT("a nullable colour-or-number union validates"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("value"), EMCPParamType::Number, TEXT("probe")).Or(EMCPParamType::Color).Nullable(),
		MCPParam::Optional(TEXT("tint"), EMCPParamType::Color, TEXT("probe")),
		MCPParam::Optional(TEXT("reduceKeys"), EMCPParamType::Boolean, TEXT("probe")).Literal(false),
		MCPParam::Optional(TEXT("entries"), EMCPParamType::Array, TEXT("probe")).Items(EMCPParamType::Object).WithFields({
			MCPParam::RequiredField(TEXT("mesh"), EMCPParamType::String, TEXT("probe")),
			MCPParam::OptionalField(TEXT("weight"), EMCPParamType::Number, TEXT("probe")),
		}),
	}).IsEmpty());
	TestFalse(TEXT("a type listed as its own alternative is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("value"), EMCPParamType::Number, TEXT("probe")).Or(EMCPParamType::Number),
	}).IsEmpty());
	TestFalse(TEXT("a union with any is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("value"), EMCPParamType::Number, TEXT("probe")).Or(EMCPParamType::Any),
	}).IsEmpty());
	TestFalse(TEXT("a literal of another type is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("flag"), EMCPParamType::Boolean, TEXT("probe")).Literal(TEXT("yes")),
	}).IsEmpty());
	TestFalse(TEXT("fields on a string are refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("name"), EMCPParamType::String, TEXT("probe")).WithFields({
			MCPParam::RequiredField(TEXT("x"), EMCPParamType::Number, TEXT("probe")),
		}),
	}).IsEmpty());
	TestFalse(TEXT("fields on an array of strings are refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("names"), EMCPParamType::Array, TEXT("probe")).Items(EMCPParamType::String).WithFields({
			MCPParam::RequiredField(TEXT("x"), EMCPParamType::Number, TEXT("probe")),
		}),
	}).IsEmpty());

	// Value forms.
	TestTrue(TEXT("an any parameter with forms, and a field with forms, validate"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("args"), EMCPParamType::Any, TEXT("probe")).OneOfForms({ EMCPValueForm::ArgMap, EMCPValueForm::String }),
		MCPParam::Optional(TEXT("calls"), EMCPParamType::Array, TEXT("probe")).Items(EMCPParamType::Object).WithFields({
			MCPParam::OptionalField(TEXT("args"), EMCPParamType::Any, TEXT("probe")).OneOfForms({ EMCPValueForm::ArgMap, EMCPValueForm::ArgEntryList }),
		}),
	}).IsEmpty());
	TestFalse(TEXT("forms on a typed parameter are refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("args"), EMCPParamType::Object, TEXT("probe")).OneOfForms({ EMCPValueForm::ArgMap }),
	}).IsEmpty());
	TestFalse(TEXT("a form listed twice is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("args"), EMCPParamType::Any, TEXT("probe")).OneOfForms({ EMCPValueForm::String, EMCPValueForm::String }),
	}).IsEmpty());
	TestFalse(TEXT("forms on a typed field are refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("calls"), EMCPParamType::Array, TEXT("probe")).Items(EMCPParamType::Object).WithFields({
			MCPParam::OptionalField(TEXT("args"), EMCPParamType::String, TEXT("probe")).OneOfForms({ EMCPValueForm::ArgMap }),
		}),
	}).IsEmpty());

	// Tagged unions.
	const TArray<FMCPParamVariant> ProbeVariants = {
		MCPParam::Variant(TEXT("set"), TEXT("probe"), { MCPParam::RequiredField(TEXT("frame"), EMCPParamType::Integer, TEXT("probe")) }),
		MCPParam::Variant(TEXT("clear"), TEXT("probe"), {}),
	};
	TestTrue(TEXT("a tagged array of objects validates"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("operations"), EMCPParamType::Array, TEXT("probe")).Items(EMCPParamType::Object).Tagged(TEXT("op"), ProbeVariants),
	}).IsEmpty());
	TestTrue(TEXT("a tagged object validates"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("operation"), EMCPParamType::Object, TEXT("probe")).Tagged(TEXT("op"), ProbeVariants),
	}).IsEmpty());
	TestFalse(TEXT("a tagged array of strings is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("operations"), EMCPParamType::Array, TEXT("probe")).Items(EMCPParamType::String).Tagged(TEXT("op"), ProbeVariants),
	}).IsEmpty());
	TestFalse(TEXT("a tagged union with one variant is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("operation"), EMCPParamType::Object, TEXT("probe")).Tagged(TEXT("op"), { ProbeVariants[0] }),
	}).IsEmpty());
	TestFalse(TEXT("a variant tag declared twice is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("operation"), EMCPParamType::Object, TEXT("probe")).Tagged(TEXT("op"), { ProbeVariants[0], ProbeVariants[0] }),
	}).IsEmpty());
	TestFalse(TEXT("a variant field named after the tag is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("operation"), EMCPParamType::Object, TEXT("probe")).Tagged(TEXT("op"), {
			ProbeVariants[1],
			MCPParam::Variant(TEXT("set"), TEXT("probe"), { MCPParam::RequiredField(TEXT("op"), EMCPParamType::String, TEXT("probe")) }),
		}),
	}).IsEmpty());
	TestFalse(TEXT("a tagged union with fields of its own is refused"), FMCPHandlerRegistry::ValidateParamSpecs({
		MCPParam::Optional(TEXT("operation"), EMCPParamType::Object, TEXT("probe"))
			.WithFields({ MCPParam::RequiredField(TEXT("x"), EMCPParamType::Number, TEXT("probe")) })
			.Tagged(TEXT("op"), ProbeVariants),
	}).IsEmpty());

	// Choices.
	auto SpecWith = [](const FMCPSpecRules& Rules)
	{
		FMCPHandlerSpec Spec;
		Spec.Params = {
			MCPParam::Required(TEXT("assetPath"), EMCPParamType::String, TEXT("probe")),
			MCPParam::Optional(TEXT("settings"), EMCPParamType::Object, TEXT("probe")),
			MCPParam::Optional(TEXT("propertyName"), EMCPParamType::String, TEXT("probe")),
			MCPParam::Optional(TEXT("propertyValue"), EMCPParamType::String, TEXT("probe")),
		};
		Spec.Choices = Rules.Choices;
		Spec.ContractExemptReason = Rules.ContractExemptReason;
		return Spec;
	};
	TestTrue(TEXT("a choice between one name and a pair validates"), FMCPHandlerRegistry::ValidateHandlerSpec(SpecWith(
		MCPSpec::ExactlyOne({ { TEXT("settings") }, { TEXT("propertyName"), TEXT("propertyValue") } }))).IsEmpty());
	TestFalse(TEXT("a choice with one branch is refused"), FMCPHandlerRegistry::ValidateHandlerSpec(SpecWith(
		MCPSpec::ExactlyOne({ { TEXT("settings") } }))).IsEmpty());
	TestFalse(TEXT("a choice naming an undeclared parameter is refused"), FMCPHandlerRegistry::ValidateHandlerSpec(SpecWith(
		MCPSpec::AtLeastOne({ { TEXT("settings") }, { TEXT("nothing") } }))).IsEmpty());
	TestFalse(TEXT("a choice naming a required parameter is refused"), FMCPHandlerRegistry::ValidateHandlerSpec(SpecWith(
		MCPSpec::ExactlyOne({ { TEXT("assetPath") }, { TEXT("settings") } }))).IsEmpty());
	TestFalse(TEXT("a name in two branches is refused"), FMCPHandlerRegistry::ValidateHandlerSpec(SpecWith(
		MCPSpec::ExactlyOne({ { TEXT("settings") }, { TEXT("settings"), TEXT("propertyName") } }))).IsEmpty());
	TestFalse(TEXT("a blank exemption reason is refused"), FMCPHandlerRegistry::ValidateHandlerSpec(SpecWith(
		MCPSpec::ContractExempt(TEXT("  ")))).IsEmpty());
	{
		const TArray<TArray<FString>> Runs = ContractRuns(SpecWith(
			MCPSpec::ExactlyOne({ { TEXT("settings") }, { TEXT("propertyName"), TEXT("propertyValue") } })));
		const bool bTwoRuns = Runs.Num() == 2
			&& Runs[0] == TArray<FString>({ TEXT("assetPath"), TEXT("settings") })
			&& Runs[1] == TArray<FString>({ TEXT("assetPath"), TEXT("propertyName"), TEXT("propertyValue") });
		TestTrue(TEXT("a choice is called once per branch, each with the names outside it"), bTwoRuns);
	}

	FMCPHandlerRegistry Registry;
	{
		FMCPHandlerRegistry::FCategoryScope Scope(Registry, TEXT("animation"));

		// A refused spec leaves the handler registered and unspecified.
		AddExpectedError(TEXT("Parameter spec for 'mcp_test_spec_refused' refused"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("a routing-name spec is refused at registration"), Registry.RegisterHandler(
			TEXT("mcp_test_spec_refused"), &AliasProbe,
			{ MCPParam::Required(TEXT("action"), EMCPParamType::String, TEXT("probe")) }));
		TestTrue(TEXT("the refused handler is still registered"), Registry.HasHandler(TEXT("mcp_test_spec_refused")));
		TestFalse(TEXT("but carries no spec"), Registry.GetHandlerSpecs().Contains(TEXT("mcp_test_spec_refused")));

		TestTrue(TEXT("a valid spec is accepted"), Registry.RegisterHandler(
			TEXT("mcp_test_spec_alias"), &AliasProbe,
			{ MCPParam::Required(TEXT("assetPath"), EMCPParamType::String, TEXT("probe")).Alias(TEXT("path")) }));

		// Named through a variable so bridge-timeout-parity.test.ts, which mirrors
		// every literal RegisterHandlerWithTimeout call, does not count a probe.
		const FString RulesMethod = TEXT("mcp_test_spec_rules");
		TestTrue(TEXT("a spec with a choice, value shapes and an exemption is accepted"), Registry.RegisterHandlerWithTimeout(
			RulesMethod, &AliasProbe, 60.0f, {
				MCPParam::Optional(TEXT("assetPath"), EMCPParamType::String, TEXT("probe")).Nullable(),
				MCPParam::Optional(TEXT("tint"), EMCPParamType::Number, TEXT("probe")).Or(EMCPParamType::Color),
				MCPParam::Optional(TEXT("confirm"), EMCPParamType::Boolean, TEXT("probe")).Literal(true),
				MCPParam::Optional(TEXT("entries"), EMCPParamType::Array, TEXT("probe")).Items(EMCPParamType::Object).WithFields({
					MCPParam::RequiredField(TEXT("mesh"), EMCPParamType::String, TEXT("probe")),
				}),
			},
			MCPSpec::AtLeastOne({ { TEXT("assetPath") }, { TEXT("tint") } }).ContractExempt(TEXT("probe writes"))));

		TestTrue(TEXT("a spec with forms and a tagged union is accepted"), Registry.RegisterHandler(
			TEXT("mcp_test_spec_shapes"), &AliasProbe, {
				MCPParam::Optional(TEXT("args"), EMCPParamType::Any, TEXT("probe")).OneOfForms({ EMCPValueForm::ArgMap, EMCPValueForm::StringList }),
				MCPParam::Optional(TEXT("operations"), EMCPParamType::Array, TEXT("probe")).Items(EMCPParamType::Object).Tagged(TEXT("op"), ProbeVariants),
			}));

		AddExpectedError(TEXT("Parameter spec for 'mcp_test_spec_bad_choice' refused"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("a choice over an undeclared name is refused at registration"), Registry.RegisterHandler(
			TEXT("mcp_test_spec_bad_choice"), &AliasProbe,
			{ MCPParam::Optional(TEXT("assetPath"), EMCPParamType::String, TEXT("probe")) },
			MCPSpec::ExactlyOne({ { TEXT("assetPath") }, { TEXT("nothing") } })));
		TestFalse(TEXT("and carries no spec"), Registry.GetHandlerSpecs().Contains(TEXT("mcp_test_spec_bad_choice")));
	}

	// The alias arrives as the declared name, and is not reported as unread.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("path"), TEXT("/Game/Probe"));
		const TSharedPtr<FJsonObject> Result = ObjectOf(Registry.ExecuteHandler(TEXT("mcp_test_spec_alias"), Params));
		if (TestNotNull(TEXT("the alias call answers an object"), Result.Get()))
		{
			TestEqual(TEXT("the handler read the alias under its declared name"), Result->GetStringField(TEXT("assetPath")), FString(TEXT("/Game/Probe")));
			TestFalse(TEXT("a resolved alias is not reported as unread"), Result->HasField(TEXT("paramsNotRead")));
		}
		TestTrue(TEXT("the caller's object is left as it was sent"), Params->HasField(TEXT("path")) && !Params->HasField(TEXT("assetPath")));
	}

	// Sent next to the name it stands for, the alias is not used and says so.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), TEXT("/Game/Declared"));
		Params->SetStringField(TEXT("path"), TEXT("/Game/Alias"));
		const TSharedPtr<FJsonObject> Result = ObjectOf(Registry.ExecuteHandler(TEXT("mcp_test_spec_alias"), Params));
		if (TestNotNull(TEXT("the conflicting call answers an object"), Result.Get()))
		{
			TestEqual(TEXT("the declared name wins"), Result->GetStringField(TEXT("assetPath")), FString(TEXT("/Game/Declared")));
			const TArray<TSharedPtr<FJsonValue>>* NotRead = nullptr;
			const bool bReported = Result->TryGetArrayField(TEXT("paramsNotRead"), NotRead) && NotRead
				&& NotRead->Num() == 1 && (*NotRead)[0]->AsString() == TEXT("path");
			TestTrue(TEXT("the unused alias is reported as not read"), bReported);
		}
	}

	// The published form carries name, type, required, description and aliases.
	{
		const TSharedPtr<FJsonObject> Json = Registry.BuildHandlerSpecsJson();
		TestFalse(TEXT("a refused spec is not published"), Json->HasField(TEXT("mcp_test_spec_refused")));
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (TestTrue(TEXT("the accepted spec is published"), Json->TryGetObjectField(TEXT("mcp_test_spec_alias"), Entry) && Entry))
		{
			TestEqual(TEXT("with its category"), (*Entry)->GetStringField(TEXT("category")), FString(TEXT("animation")));
			const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
			if (TestTrue(TEXT("and its params"), (*Entry)->TryGetArrayField(TEXT("params"), Params) && Params && Params->Num() == 1))
			{
				const TSharedPtr<FJsonObject> Param = (*Params)[0]->AsObject();
				TestEqual(TEXT("name"), Param->GetStringField(TEXT("name")), FString(TEXT("assetPath")));
				TestEqual(TEXT("type"), Param->GetStringField(TEXT("type")), FString(TEXT("string")));
				TestTrue(TEXT("required"), Param->GetBoolField(TEXT("required")));
				const TArray<TSharedPtr<FJsonValue>>* Aliases = nullptr;
				TestTrue(TEXT("aliases"), Param->TryGetArrayField(TEXT("aliases"), Aliases) && Aliases
					&& Aliases->Num() == 1 && (*Aliases)[0]->AsString() == TEXT("path"));
				TestFalse(TEXT("no shape fields when none are set"), Param->HasField(TEXT("nullable")) || Param->HasField(TEXT("orTypes"))
					|| Param->HasField(TEXT("literal")) || Param->HasField(TEXT("fields")));
			}
			TestFalse(TEXT("no choices or exemption when none are set"),
				(*Entry)->HasField(TEXT("choices")) || (*Entry)->HasField(TEXT("contractExempt")));
		}

		const TSharedPtr<FJsonObject>* Rules = nullptr;
		if (TestTrue(TEXT("the rules spec is published"), Json->TryGetObjectField(TEXT("mcp_test_spec_rules"), Rules) && Rules))
		{
			TestEqual(TEXT("with its exemption"), (*Rules)->GetStringField(TEXT("contractExempt")), FString(TEXT("probe writes")));
			const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
			if (TestTrue(TEXT("and its choice"), (*Rules)->TryGetArrayField(TEXT("choices"), Choices) && Choices && Choices->Num() == 1))
			{
				const TSharedPtr<FJsonObject> Choice = (*Choices)[0]->AsObject();
				TestEqual(TEXT("mode"), Choice->GetStringField(TEXT("mode")), FString(TEXT("atLeastOne")));
				const TArray<TSharedPtr<FJsonValue>>* Branches = nullptr;
				TestTrue(TEXT("branches"), Choice->TryGetArrayField(TEXT("branches"), Branches) && Branches && Branches->Num() == 2
					&& (*Branches)[1]->AsArray().Num() == 1 && (*Branches)[1]->AsArray()[0]->AsString() == TEXT("tint"));
			}
			const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
			if (TestTrue(TEXT("and its params"), (*Rules)->TryGetArrayField(TEXT("params"), Params) && Params && Params->Num() == 4))
			{
				TestTrue(TEXT("nullable"), (*Params)[0]->AsObject()->GetBoolField(TEXT("nullable")));
				const TArray<TSharedPtr<FJsonValue>>* OrTypes = nullptr;
				TestTrue(TEXT("orTypes"), (*Params)[1]->AsObject()->TryGetArrayField(TEXT("orTypes"), OrTypes) && OrTypes
					&& OrTypes->Num() == 1 && (*OrTypes)[0]->AsString() == TEXT("color"));
				TestTrue(TEXT("literal"), (*Params)[2]->AsObject()->GetBoolField(TEXT("literal")));
				const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
				TestTrue(TEXT("fields"), (*Params)[3]->AsObject()->TryGetArrayField(TEXT("fields"), Fields) && Fields
					&& Fields->Num() == 1 && (*Fields)[0]->AsObject()->GetStringField(TEXT("name")) == TEXT("mesh"));
			}
		}

		const TSharedPtr<FJsonObject>* Shapes = nullptr;
		if (TestTrue(TEXT("the shapes spec is published"), Json->TryGetObjectField(TEXT("mcp_test_spec_shapes"), Shapes) && Shapes))
		{
			const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
			if (TestTrue(TEXT("with its params"), (*Shapes)->TryGetArrayField(TEXT("params"), Params) && Params && Params->Num() == 2))
			{
				const TArray<TSharedPtr<FJsonValue>>* Forms = nullptr;
				TestTrue(TEXT("forms"), (*Params)[0]->AsObject()->TryGetArrayField(TEXT("forms"), Forms) && Forms && Forms->Num() == 2
					&& (*Forms)[0]->AsString() == TEXT("argMap") && (*Forms)[1]->AsString() == TEXT("stringList"));
				const TSharedPtr<FJsonObject>* OneOf = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Variants = nullptr;
				const bool bTagged = (*Params)[1]->AsObject()->TryGetObjectField(TEXT("oneOf"), OneOf) && OneOf
					&& (*OneOf)->GetStringField(TEXT("key")) == TEXT("op")
					&& (*OneOf)->TryGetArrayField(TEXT("variants"), Variants) && Variants && Variants->Num() == 2
					&& (*Variants)[0]->AsObject()->GetStringField(TEXT("tag")) == TEXT("set")
					&& (*Variants)[0]->AsObject()->GetArrayField(TEXT("fields")).Num() == 1;
				TestTrue(TEXT("oneOf"), bTagged);
			}
		}
	}
	return true;
}

#endif
