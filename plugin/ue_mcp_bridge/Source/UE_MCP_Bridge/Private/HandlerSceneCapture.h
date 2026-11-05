#pragma once

// Transient SceneCapture2D rig shared by capture_scene_png and
// render_sequence_frames.

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "ContentStreaming.h"
#include "RenderingThread.h"
#include "SceneManagement.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace UEMCP::SceneCapture
{
	/** Removes capture actors earlier builds saved into the map (#966).
	 *  Their removal is a real edit, so it goes through the level. */
	inline int32 RemoveStrayCaptureActors(UWorld* World)
	{
		if (!World) return 0;
		static const FString StrayLabel = TEXT("__ClaudeSceneCapture");
		TArray<ASceneCapture2D*> Strays;
		for (TActorIterator<ASceneCapture2D> It(World); It; ++It)
		{
			if (It->GetActorLabel() == StrayLabel)
			{
				Strays.Add(*It);
			}
		}
		int32 Removed = 0;
		for (ASceneCapture2D* Stray : Strays)
		{
			if (IsValid(Stray) && World->DestroyActor(Stray))
			{
				++Removed;
			}
		}
		return Removed;
	}

	/** Project-relative paths resolve against the project directory. */
	inline FString ResolveOutputPath(const FString& Path)
	{
		FString Abs = Path;
		if (FPaths::IsRelative(Abs))
		{
			Abs = FPaths::Combine(FPaths::ProjectDir(), Abs);
		}
		return Abs;
	}

	/** A capture actor that is transient, outside the outliner and the map,
	 *  and destroyed with this object, so a capture leaves the level as it
	 *  found it (#966). Renders RGBA8 LDR, which exports as a real PNG. */
	class FTransientCapture : public FNoncopyable
	{
	public:
		~FTransientCapture() { Destroy(); }

		bool Spawn(UWorld* InWorld, const FVector& Location, const FRotator& Rotation,
			int32 Width, int32 Height, float FovDegrees, FString& OutError)
		{
			World = InWorld;
			if (!World) { OutError = TEXT("World not available"); return false; }

			FActorSpawnParameters SpawnParams;
			SpawnParams.ObjectFlags |= RF_Transient;
#if WITH_EDITOR
			SpawnParams.bTemporaryEditorActor = true;
			SpawnParams.bHideFromSceneOutliner = true;
			SpawnParams.bCreateActorPackage = false;
#endif
			Actor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), Location, Rotation, SpawnParams);
			if (!Actor) { OutError = TEXT("Failed to spawn SceneCapture2D actor"); return false; }
			Actor->SetActorHiddenInGame(true);

			Comp = Actor->GetCaptureComponent2D();
			if (!Comp) { OutError = TEXT("SceneCapture2D has no capture component"); return false; }
			Comp->FOVAngle = FovDegrees;
			Comp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			Comp->bCaptureEveryFrame = false;
			Comp->bCaptureOnMovement = false;

			Target = UKismetRenderingLibrary::CreateRenderTarget2D(
				World, Width, Height, ETextureRenderTargetFormat::RTF_RGBA8_SRGB, FLinearColor::Black, false);
			if (!Target) { OutError = TEXT("Failed to create RenderTarget2D"); return false; }
			Comp->TextureTarget = Target;
			return true;
		}

		void SetView(const FVector& Location, const FRotator& Rotation, float FovDegrees)
		{
			if (!IsValid(Actor) || !Comp) return;
			Actor->SetActorLocationAndRotation(Location, Rotation);
			Comp->FOVAngle = FovDegrees;
		}

		/** bFullyLoadTextures streams every texture in and captures twice, so
		 *  mips that arrive after the first pass are in the second (#662). */
		void Capture(bool bFullyLoadTextures)
		{
			if (!Comp) return;
			// Components created or moved this frame (BeginPlay attachments,
			// skinned poses) reach the renderer at end of frame; push them now
			// so the capture sees the same scene the game viewport does (#1203).
			if (World) World->SendAllEndOfFrameUpdates();
			if (bFullyLoadTextures)
			{
				IStreamingManager::Get().StreamAllResources(0.0f);
				FlushRenderingCommands();
				Comp->CaptureScene();
				FlushRenderingCommands();
			}
			Comp->CaptureScene();
			FlushRenderingCommands();
		}

		/** A one-shot capture has no view state, so it renders without Lumen
		 *  and with a fixed exposure from the middle of the auto exposure
		 *  range, which leaves an interior near black (#1549). This keeps one,
		 *  renders unexported passes so Lumen's surface cache fills, and cuts
		 *  the camera on the second-to-last pass so eye adaptation snaps to the
		 *  exposure the settled frame meters. The final pass is the image. */
		void CaptureSettled(bool bFullyLoadTextures, int32 SettlePasses)
		{
			USceneCaptureComponent2D* Capturer = GetComponent();
			if (!Capturer) return;
			Capturer->bAlwaysPersistRenderingState = true;
			if (UWorld* CaptureWorld = Capturer->GetWorld()) CaptureWorld->SendAllEndOfFrameUpdates();
			if (bFullyLoadTextures)
			{
				IStreamingManager::Get().StreamAllResources(0.0f);
				FlushRenderingCommands();
			}
			const int32 Passes = FMath::Max(SettlePasses, 1);
			for (int32 Pass = 0; Pass <= Passes; ++Pass)
			{
				if (Pass == Passes - 1) Capturer->bCameraCutThisFrame = true;
				Capturer->CaptureScene();
				FlushRenderingCommands();
			}
		}

		/** The exposure the capture's own eye adaptation last read back, or 0
		 *  when there is no view state or no readback has landed yet. */
		float GetLastExposure() const
		{
			USceneCaptureComponent2D* Capturer = GetComponent();
			FSceneViewStateInterface* State = Capturer ? Capturer->GetViewState(0) : nullptr;
			return State ? State->GetLastEyeAdaptationExposure() : 0.0f;
		}

		/** Writes the target to AbsPath, which must already end in .png. Encoded here so the file holds
		 *  exactly the PNG bytes; ExportRenderTarget wrote a padded buffer past IEND (#1399). */
		bool ExportPng(const FString& AbsPath, int64& OutSizeBytes, FString& OutError)
		{
			OutSizeBytes = -1;
			if (!Target) { OutError = TEXT("No render target to export"); return false; }
			FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
			TArray<FColor> Pixels;
			if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Target->SizeX * Target->SizeY)
			{
				OutError = TEXT("Could not read the captured pixels back from the render target");
				return false;
			}
			// A scene capture leaves alpha as inverse coverage; a screenshot is opaque.
			for (FColor& Pixel : Pixels) Pixel.A = 255;
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			if (!PngWrapper.IsValid()
				|| !PngWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Target->SizeX, Target->SizeY, ERGBFormat::BGRA, 8))
			{
				OutError = TEXT("The captured pixels could not be PNG-encoded");
				return false;
			}
			const TArray64<uint8> PngData = PngWrapper->GetCompressed();
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsPath), /*Tree*/ true);
			if (!FFileHelper::SaveArrayToFile(PngData, *AbsPath))
			{
				OutError = FString::Printf(TEXT("Could not write %s; the directory may be read-only or the path invalid"), *AbsPath);
				return false;
			}
			OutSizeBytes = IFileManager::Get().FileSize(*AbsPath);
			if (OutSizeBytes < 0)
			{
				OutError = FString::Printf(TEXT("Export did not produce a file at %s"), *AbsPath);
				return false;
			}
			return true;
		}

		USceneCaptureComponent2D* GetComponent() const { return Comp; }
		UTextureRenderTarget2D* GetTarget() const { return Target; }

		void Destroy()
		{
			if (IsValid(Actor) && World)
			{
				// The component holds the only reference keeping the target alive.
				if (USceneCaptureComponent2D* Dying = Actor->GetCaptureComponent2D())
				{
					Dying->TextureTarget = nullptr;
				}
				// Never part of the map, so removal must not dirty the package.
				World->DestroyActor(Actor, /*bNetForce*/ false, /*bShouldModifyLevel*/ false);
			}
			Actor = nullptr;
			Comp = nullptr;
			Target = nullptr;
		}

	private:
		UWorld* World = nullptr;
		ASceneCapture2D* Actor = nullptr;
		USceneCaptureComponent2D* Comp = nullptr;
		UTextureRenderTarget2D* Target = nullptr;
	};
}
