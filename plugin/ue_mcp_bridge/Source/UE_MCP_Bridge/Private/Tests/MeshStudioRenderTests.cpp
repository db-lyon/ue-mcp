#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace MeshStudioRenderTests
{
	static TSet<FString> DirtySet()
	{
		TSet<FString> Names;
		for (TObjectIterator<UPackage> It; It; ++It)
			if (It->IsDirty() && !It->HasAnyPackageFlags(PKG_Transient) && *It != GetTransientPackage()) Names.Add(It->GetName());
		return Names;
	}

	static int32 StudioWorlds()
	{
		int32 Count = 0;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->GetName().StartsWith(TEXT("UEMCP_MeshStudio"))) ++Count;
		return Count;
	}

	static TSharedPtr<FJsonObject> MeshItem(const TCHAR* Id, const TCHAR* Mesh = TEXT("/Engine/BasicShapes/Cube.Cube"))
	{
		auto Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("id"), Id);
		Item->SetStringField(TEXT("meshPath"), Mesh);
		return Item;
	}

	class FRenderCommand : public IAutomationLatentCommand
	{
	public:
		FRenderCommand(FAutomationTestBase& InTest, bool bInCancel) : Test(InTest), bCancel(bInCancel)
		{
			FEditorHandlers::RegisterHandlers(Registry);
			Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/MeshStudio"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
			EditorWorld = GEditor->GetEditorWorldContext().World();
			for (TActorIterator<AActor> It(EditorWorld); It; ++It) ActorsBefore.Add(It->GetPathName());
			// An owned throwaway package proves existing dirty flags survive.
			// Never mark the editor map dirty just to set up a capture test.
			ExistingDirtyPackage = CreatePackage(*(TEXT("/Temp/UEMCPStudioDirty_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			ExistingDirtyPackage->AddToRoot();
			ExistingDirtyPackage->SetDirtyFlag(true);
			DirtyBefore = DirtySet();
		}

		virtual ~FRenderCommand() override
		{
			// Also runs when the automation framework abandons a latent command.
			if (!JobId.IsEmpty())
			{
				auto Params = MakeShared<FJsonObject>();
				Params->SetStringField(TEXT("jobId"), JobId);
				Registry.ExecuteHandler(TEXT("cancel_mesh_studio_render"), Params);
			}
			IFileManager::Get().DeleteDirectory(*Directory, false, true);
			ExistingDirtyPackage->SetDirtyFlag(false);
			ExistingDirtyPackage->RemoveFromRoot();
			ExistingDirtyPackage->MarkAsGarbage();
		}

		virtual bool Update() override
		{
			if (JobId.IsEmpty()) return Start();
			auto Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("jobId"), JobId);
			if (bCancel && StudioWorlds() > 0)
			{
				const auto Cancelled = Registry.ExecuteHandler(TEXT("cancel_mesh_studio_render"), Params)->AsObject();
				Test.TestEqual(TEXT("cancellation is final"), Cancelled->GetStringField(TEXT("state")), TEXT("cancelled"));
				VerifyCleanup(Cancelled);
				Test.TestFalse(TEXT("cancelled item was never exported"), IFileManager::Get().FileExists(*(Directory / TEXT("cube.png"))));
				return true;
			}
			const auto Report = Registry.ExecuteHandler(TEXT("get_mesh_studio_render"), Params)->AsObject();
			if (Report->GetStringField(TEXT("state")) == TEXT("running"))
			{
				if (FPlatformTime::Seconds() - StartedAt < 90.0) return false;
				Test.AddError(TEXT("Studio render did not complete on real editor frames within 90s"));
				return true;
			}
			if (bCancel) { Test.AddError(TEXT("Job finished before the preview-world cancellation could be exercised")); return true; }
			Test.TestEqual(TEXT("batch completed"), Report->GetStringField(TEXT("state")), TEXT("completed"));
			Test.TestFalse(TEXT("blocked part makes overall success false"), Report->GetBoolField(TEXT("success")));
			Test.TestEqual(TEXT("two rendered outcomes"), Report->GetIntegerField(TEXT("rendered")), 2);
			Test.TestEqual(TEXT("one blocked outcome"), Report->GetIntegerField(TEXT("blocked")), 1);
			const auto& Outcomes = Report->GetArrayField(TEXT("outcomes"));
			if (Test.TestEqual(TEXT("every item has an outcome"), Outcomes.Num(), 3))
			{
				VerifyPng(Outcomes[0]->AsObject(), 64, 128);
				VerifyPng(Outcomes[1]->AsObject(), 128, 128);
				if (Outcomes[1]->AsObject()->GetStringField(TEXT("status")) == TEXT("rendered"))
				{
					const auto Bounds = Outcomes[1]->AsObject()->GetObjectField(TEXT("bounds"));
					Test.TestTrue(TEXT("union bounds contain both translated cubes"), Bounds->GetObjectField(TEXT("extent"))->GetNumberField(TEXT("x")) >= 149.0);
				}
				Test.TestEqual(TEXT("wrong asset type is blocked"), Outcomes[2]->AsObject()->GetStringField(TEXT("status")), TEXT("blocked"));
			}
			VerifyCleanup(Report);
			FString Manifest;
			Test.TestTrue(TEXT("outcome manifest is on disk"), FFileHelper::LoadFileToString(Manifest, *Report->GetStringField(TEXT("manifest"))));
			Test.TestTrue(TEXT("manifest records the cleanup verification"), Manifest.Contains(TEXT("dirtyPackagesUnchanged")) && Manifest.Contains(TEXT("completed")));
			return true;
		}

	private:
		FAutomationTestBase& Test;
		FMCPHandlerRegistry Registry;
		bool bCancel;
		FString Directory;
		FString JobId;
		UWorld* EditorWorld = nullptr;
		UPackage* ExistingDirtyPackage = nullptr;
		TSet<FString> ActorsBefore;
		TSet<FString> DirtyBefore;
		double StartedAt = FPlatformTime::Seconds();

		bool Start()
		{
			auto Cube = MeshItem(TEXT("cube"));
			auto Aspect = MakeShared<FJsonObject>();
			Aspect->SetNumberField(TEXT("width"), 1);
			Aspect->SetNumberField(TEXT("height"), 2);
			Cube->SetObjectField(TEXT("aspect"), Aspect);
			auto Assembly = MakeShared<FJsonObject>();
			Assembly->SetStringField(TEXT("id"), TEXT("assembly"));
			TArray<TSharedPtr<FJsonValue>> Parts;
			for (int32 PartIndex = 0; PartIndex < 2; ++PartIndex)
			{
				auto Part = MeshItem(TEXT("unused"));
				Part->RemoveField(TEXT("id"));
				auto Transform = MakeShared<FJsonObject>();
				Transform->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(FVector(PartIndex * 200, 0, 0)));
				Part->SetObjectField(TEXT("relativeTransform"), Transform);
				Parts.Add(MakeShared<FJsonValueObject>(Part));
			}
			Assembly->SetArrayField(TEXT("parts"), Parts);
			auto Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("outputDir"), Directory);
			Params->SetNumberField(TEXT("resolution"), 128);
			Params->SetStringField(TEXT("captureMode"), TEXT("both"));
			Params->SetBoolField(TEXT("fullyLoadTextures"), true);
			TArray<TSharedPtr<FJsonValue>> Items = { MakeShared<FJsonValueObject>(Cube) };
			if (!bCancel)
			{
				Items.Add(MakeShared<FJsonValueObject>(Assembly));
				Items.Add(MakeShared<FJsonValueObject>(MeshItem(TEXT("wrong_type"), TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"))));
			}
			Params->SetArrayField(TEXT("items"), Items);
			const auto Result = Registry.ExecuteHandler(TEXT("render_mesh_studio"), Params)->AsObject();
			if (!Test.TestTrue(TEXT("render starts"), Result->GetBoolField(TEXT("success"))))
			{
				Test.AddError(Result->GetStringField(TEXT("error"))); return true;
			}
			JobId = Result->GetStringField(TEXT("jobId"));
			return false;
		}

		void VerifyPng(const TSharedPtr<FJsonObject>& Outcome, int32 Width, int32 Height)
		{
			if (!Test.TestEqual(TEXT("item rendered"), Outcome->GetStringField(TEXT("status")), TEXT("rendered"))) return;
			TArray<uint8> Bytes;
			if (!Test.TestTrue(TEXT("PNG exists"), FFileHelper::LoadFileToArray(Bytes, *Outcome->GetStringField(TEXT("file"))))) return;
			IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			const auto Png = Images.CreateImageWrapper(EImageFormat::PNG);
			if (!Test.TestTrue(TEXT("PNG decodes"), Png->SetCompressed(Bytes.GetData(), Bytes.Num()))) return;
			Test.TestEqual(TEXT("PNG width follows aspect"), Png->GetWidth(), static_cast<int64>(Width));
			Test.TestEqual(TEXT("PNG height follows aspect"), Png->GetHeight(), static_cast<int64>(Height));
			TArray64<uint8> Pixels;
			if (!Test.TestTrue(TEXT("RGBA pixels decode"), Png->GetRaw(Pixels))) return;
			if (!Test.TestEqual(TEXT("PNG stores four channels"), Pixels.Num(), static_cast<int64>(Width * Height * 4))) return;
			bool bTransparent = false, bOpaque = false, bLit = false;
			for (int64 Pixel = 0; Pixel < Pixels.Num(); Pixel += 4)
			{
				bTransparent |= Pixels[Pixel + 3] == 0;
				bOpaque |= Pixels[Pixel + 3] == 255;
				bLit |= Pixels[Pixel + 3] > 0 && (Pixels[Pixel] > 5 || Pixels[Pixel + 1] > 5 || Pixels[Pixel + 2] > 5);
			}
			Test.TestTrue(TEXT("background is transparent"), bTransparent);
			Test.TestTrue(TEXT("mesh has opaque pixels"), bOpaque);
			Test.TestTrue(TEXT("mesh has lit colour pixels"), bLit);
			Test.TestEqual(TEXT("clear corner alpha"), Pixels[3], static_cast<uint8>(0));
		}

		void VerifyCleanup(const TSharedPtr<FJsonObject>& Report)
		{
			Test.TestTrue(TEXT("handler confirms cleanup"), Report->GetBoolField(TEXT("cleanupComplete")));
			Test.TestTrue(TEXT("handler confirms dirty package set"), Report->GetBoolField(TEXT("dirtyPackagesUnchanged")));
			Test.TestTrue(TEXT("preexisting dirty package remains dirty"), ExistingDirtyPackage->IsDirty());
			const auto After = DirtySet();
			Test.TestTrue(TEXT("independent dirty set before and after matches"), DirtyBefore.Num() == After.Num() && DirtyBefore.Difference(After).IsEmpty());
			Test.TestEqual(TEXT("no studio world context survives"), StudioWorlds(), 0);
			TSet<FString> ActorsAfter;
			for (TActorIterator<AActor> It(EditorWorld); It; ++It) ActorsAfter.Add(It->GetPathName());
			Test.TestTrue(TEXT("editor actors unchanged"), ActorsBefore.Num() == ActorsAfter.Num() && ActorsBefore.Difference(ActorsAfter).IsEmpty());
			Test.TestTrue(TEXT("same editor world"), GEditor->GetEditorWorldContext().World() == EditorWorld);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshStudioRenderPngTest, "UE.MCP.Editor.MeshStudio.RenderAndClean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FMeshStudioRenderPngTest::RunTest(const FString& Parameters)
{
	if (!GEditor || !GEditor->GetEditorWorldContext().World()) { AddError(TEXT("Editor world required")); return false; }
	ADD_LATENT_AUTOMATION_COMMAND(MeshStudioRenderTests::FRenderCommand(*this, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshStudioRenderCancelTest, "UE.MCP.Editor.MeshStudio.CancelAndClean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FMeshStudioRenderCancelTest::RunTest(const FString& Parameters)
{
	if (!GEditor || !GEditor->GetEditorWorldContext().World()) { AddError(TEXT("Editor world required")); return false; }
	ADD_LATENT_AUTOMATION_COMMAND(MeshStudioRenderTests::FRenderCommand(*this, true));
	return true;
}

#endif
