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
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

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

		/** Writes the target to AbsPath, which must already end in .png. */
		bool ExportPng(const FString& AbsPath, int64& OutSizeBytes, FString& OutError)
		{
			OutSizeBytes = -1;
			if (!Target) { OutError = TEXT("No render target to export"); return false; }
			const FString OutDir = FPaths::GetPath(AbsPath);
			const FString OutName = FPaths::GetCleanFilename(AbsPath);
			IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
			UKismetRenderingLibrary::ExportRenderTarget(World, Target, OutDir, OutName);
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
