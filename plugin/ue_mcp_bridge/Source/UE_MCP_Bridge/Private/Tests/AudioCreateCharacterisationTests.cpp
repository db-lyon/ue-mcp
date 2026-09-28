// The response of the Audio asset creators and of spawn_ambient_sound,
// recorded before the F4 and F6 port and held to the contract after it
// (plans/domain-design.md 0 and 8). The fixture is
// tests/golden/cpp/audio-create-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Handlers/Audio/AudioHandlers.h"
#include "Tests/MCPCharacterisationRun.h"
#include "Tests/MCPScopedTestMount.h"

class FMCPAudioCreateCharTestBase : public FAutomationTestBase
{
public:
	FMCPAudioCreateCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogAudio"));
		Categories.Add(TEXT("LogMetaSound"));
		Categories.Add(TEXT("LogMetasoundEngine"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogLinker"));
		Categories.Add(TEXT("LogUE_MCP"));
		return Categories;
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPAudioCreateCharacterisationTest, FMCPAudioCreateCharTestBase,
	"UE.MCP.Audio.Characterisation.Create",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAudioCreateCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPAudioCreateChar/"), TEXT("UEMCPAudioCreateChar"));
	const FString Folder = Mount.RootPath.LeftChop(1);

	FScriptedRun Run;
	FAudioHandlers::RegisterHandlers(Run.Registry);
	Run.Recording.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));
	Run.Recording.Normaliser.Numbered({ TEXT("actorName"), TEXT("actorPath") });

	auto At = [&](const TCHAR* Name) { return FArgs().S(TEXT("name"), Name).S(TEXT("packagePath"), Folder); };

	for (const TCHAR* Method : { TEXT("create_sound_cue"), TEXT("create_metasound_source"), TEXT("create_submix"),
		TEXT("create_sound_class"), TEXT("create_sound_mix"), TEXT("create_concurrency"), TEXT("create_attenuation") })
	{
		const FString Name = FString(TEXT("A_")) + Method;
		Run.Step(*(FString(Method) + TEXT("_01_created")), Method, At(*Name));
		Run.Step(*(FString(Method) + TEXT("_02_existed")), Method, At(*Name));
		Run.Step(*(FString(Method) + TEXT("_03_conflict_error")), Method, At(*Name).S(TEXT("onConflict"), TEXT("error")));
		Run.Step(*(FString(Method) + TEXT("_04_missing_name")), Method, FArgs().S(TEXT("packagePath"), Folder));
	}
	Run.Step(TEXT("create_submix_05_parent"), TEXT("create_submix"), At(TEXT("SM_Child")).S(TEXT("parentPath"), Mount.RootPath + TEXT("A_create_submix")).N(TEXT("outputVolume"), 0.5));
	Run.Step(TEXT("create_sound_class_05_parent"), TEXT("create_sound_class"), At(TEXT("SC_Child")).S(TEXT("parentPath"), Mount.RootPath + TEXT("A_create_sound_class")));
	Run.Step(TEXT("create_concurrency_05_settings"), TEXT("create_concurrency"), At(TEXT("CC_Set")).N(TEXT("maxCount"), 4).B(TEXT("limitToOwner"), true));
	Run.Step(TEXT("create_attenuation_05_settings"), TEXT("create_attenuation"), At(TEXT("AT_Set")).N(TEXT("falloffDistance"), 1200));

	// An ambient sound in the editor world, removed again afterwards.
	const FString Label = TEXT("UEMCPCharAmbient");
	auto RemoveSpawned = [&Label]()
	{
		UWorld* World = GetEditorWorld();
		if (!World) return;
		TArray<AActor*> Spawned;
		MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Spawned);
		for (AActor* Actor : Spawned) World->DestroyActor(Actor);
	};
	RemoveSpawned();
	const FString Cue = Mount.RootPath + TEXT("A_create_sound_cue");
	Run.Step(TEXT("spawn_ambient_sound_01_created"), TEXT("spawn_ambient_sound"), FArgs().S(TEXT("soundPath"), Cue).S(TEXT("label"), Label).V(TEXT("location"), 1, 2, 3));
	Run.Step(TEXT("spawn_ambient_sound_02_existed"), TEXT("spawn_ambient_sound"), FArgs().S(TEXT("soundPath"), Cue).S(TEXT("label"), Label));
	Run.Step(TEXT("spawn_ambient_sound_03_conflict_error"), TEXT("spawn_ambient_sound"), FArgs().S(TEXT("soundPath"), Cue).S(TEXT("label"), Label).S(TEXT("onConflict"), TEXT("error")));
	Run.Step(TEXT("spawn_ambient_sound_04_bad_sound"), TEXT("spawn_ambient_sound"), FArgs().S(TEXT("soundPath"), Mount.RootPath + TEXT("Nope")).S(TEXT("label"), TEXT("UEMCPCharAmbientBad")));
	RemoveSpawned();

	return Run.Finish(*this, TEXT("audio-create-responses"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
