#include "EditorHandlers.h"
#include "HandlerUtils.h"
#include "HandlerSceneCapture.h"

#include "AssetCompilingManager.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/TextureCube.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "RHI.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/GCObject.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace MeshStudioRender
{
	constexpr double ItemBudgetSeconds = 60.0;
	constexpr double JobBudgetSeconds = 540.0;
	constexpr float CameraFov = 30.0f;

	struct FPart
	{
		FString Path;
		FTransform Transform = FTransform::Identity;
	};

	struct FItem
	{
		FString Id;
		TArray<FPart> Parts;
		FString InvalidReason;
		int32 Width = 512;
		int32 Height = 512;
	};

	static TSet<FString> DirtyPackages()
	{
		TSet<FString> Result;
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			if (It->IsDirty() && !It->HasAnyPackageFlags(PKG_Transient) && *It != GetTransientPackage())
				Result.Add(It->GetName());
		}
		return Result;
	}

	static TArray<TSharedPtr<FJsonValue>> PackageNames(const TSet<FString>& Packages)
	{
		TArray<FString> Sorted = Packages.Array();
		Sorted.Sort();
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FString& Name : Sorted) Result.Add(MakeShared<FJsonValueString>(Name));
		return Result;
	}

	static TSharedPtr<FJsonValue> Snapshot(const TSharedPtr<FJsonObject>& Report)
	{
		// Socket serialization happens after the game-thread handler returns.
		// Never hand it a report the ticker will mutate on the next frame.
		TSharedPtr<FJsonObject> Copy = MakeShared<FJsonObject>();
		FJsonObject::Duplicate(Report, Copy);
		return MCPResult(Copy);
	}

	static bool SamePackages(const TSet<FString>& A, const TSet<FString>& B)
	{
		return A.Num() == B.Num() && A.Difference(B).IsEmpty();
	}

	static bool SafeId(const FString& Id)
	{
		if (Id.IsEmpty() || Id.Len() > 100) return false;
		for (TCHAR C : Id)
		{
			if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-')) return false;
		}
		// Windows device names are invalid even with a PNG extension.
		const FString Upper = Id.ToUpper();
		if (Upper == TEXT("CON") || Upper == TEXT("PRN") || Upper == TEXT("AUX") || Upper == TEXT("NUL")) return false;
		if (Upper.Len() == 4 && (Upper.StartsWith(TEXT("COM")) || Upper.StartsWith(TEXT("LPT")))
			return Upper[3] < '1' || Upper[3] > '9';
		return true;
	}

	static bool FiniteNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double& Out)
	{
		return Object.IsValid() && Object->TryGetNumberField(Key, Out) && FMath::IsFinite(Out);
	}

	static bool VectorField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector& Out)
	{
		if (!Object->HasField(Key)) return true;
		const TSharedPtr<FJsonObject>* Value = nullptr;
		if (!Object->TryGetObjectField(Key, Value)) return false;
		return FiniteNumber(*Value, TEXT("x"), Out.X) && FiniteNumber(*Value, TEXT("y"), Out.Y) && FiniteNumber(*Value, TEXT("z"), Out.Z);
	}

	static bool ParsePart(const TSharedPtr<FJsonObject>& Object, FPart& Out)
	{
		if (!Object->TryGetStringField(TEXT("meshPath"), Out.Path) || Out.Path.IsEmpty()) return false;
		if (!Object->HasField(TEXT("relativeTransform"))) return true;
		const TSharedPtr<FJsonObject>* Transform = nullptr;
		if (!Object->TryGetObjectField(TEXT("relativeTransform"), Transform)) return false;
		FVector Location = FVector::ZeroVector;
		FVector Scale = FVector::OneVector;
		FRotator Rotation = FRotator::ZeroRotator;
		if (!VectorField(*Transform, TEXT("location"), Location) || !VectorField(*Transform, TEXT("scale"), Scale)) return false;
		if ((*Transform)->HasField(TEXT("rotation")))
		{
			const TSharedPtr<FJsonObject>* Rot = nullptr;
			if (!(*Transform)->TryGetObjectField(TEXT("rotation"), Rot)
				|| !FiniteNumber(*Rot, TEXT("pitch"), Rotation.Pitch) || !FiniteNumber(*Rot, TEXT("yaw"), Rotation.Yaw) || !FiniteNumber(*Rot, TEXT("roll"), Rotation.Roll)) return false;
		}
		if (Location.GetAbsMax() > 1.e7 || Scale.GetAbsMax() > 1.e4 || Scale.GetAbsMin() < SMALL_NUMBER) return false;
		Out.Transform = FTransform(Rotation, Location, Scale);
		return !Out.Transform.ContainsNaN();
	}

	static bool ParseItems(const TArray<TSharedPtr<FJsonValue>>& Values, int32 Resolution, TArray<FItem>& Out, FString& Error)
	{
		if (Values.Num() < 1 || Values.Num() > 128) { Error = TEXT("items must contain 1-128 entries"); return false; }
		TSet<FString> Ids;
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			FItem Item;
			if (!Object.IsValid() || !Object->TryGetStringField(TEXT("id"), Item.Id) || !SafeId(Item.Id) || Ids.Contains(Item.Id.ToLower()))
			{
				Error = TEXT("Every item needs a unique filename-safe id (letters, digits, underscore or hyphen; no device names)");
				return false;
			}
			Ids.Add(Item.Id.ToLower());
			Item.Width = Item.Height = Resolution;
			if (Object->HasField(TEXT("aspect")))
			{
				const TSharedPtr<FJsonObject>* Aspect = nullptr;
				double Width = 0, Height = 0;
				if (!Object->TryGetObjectField(TEXT("aspect"), Aspect) || !FiniteNumber(*Aspect, TEXT("width"), Width)
					|| !FiniteNumber(*Aspect, TEXT("height"), Height) || Width < 0.01 || Height < 0.01 || Width > 100 || Height > 100)
					Item.InvalidReason = TEXT("aspect needs width and height in [0.01,100]");
				else
				{
					Item.Width = FMath::Max(32, FMath::RoundToInt(Resolution * Width / FMath::Max(Width, Height)));
					Item.Height = FMath::Max(32, FMath::RoundToInt(Resolution * Height / FMath::Max(Width, Height)));
				}
			}
			if (Object->HasField(TEXT("meshPath")) == Object->HasField(TEXT("parts")))
				Item.InvalidReason = TEXT("Supply exactly one of meshPath or parts");
			else if (Object->HasField(TEXT("meshPath")))
			{
				FPart Part;
				if (!ParsePart(Object, Part)) Item.InvalidReason = TEXT("meshPath must be a nonempty string");
				else Item.Parts.Add(MoveTemp(Part));
			}
			else
			{
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (!Object->TryGetArrayField(TEXT("parts"), Parts) || Parts->Num() < 1 || Parts->Num() > 64)
					Item.InvalidReason = TEXT("parts must contain 1-64 mesh parts");
				else for (const TSharedPtr<FJsonValue>& PartValue : *Parts)
				{
					FPart Part;
					if (!PartValue.IsValid() || PartValue->Type != EJson::Object || !ParsePart(PartValue->AsObject(), Part))
						Item.InvalidReason = TEXT("Each part needs meshPath and a finite relativeTransform with nonzero scale");
					else Item.Parts.Add(MoveTemp(Part));
				}
			}
			Out.Add(MoveTemp(Item));
		}
		return true;
	}

	// The job references assets through GC and owns a completely separate render
	// scene. No actor, component or light is ever inserted into the editor map.
	class FJob : public FGCObject
	{
	public:
		FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		FString OutputDir;
		FString CubemapPath;
		FString CaptureMode;
		FVector ViewDirection;
		bool bFullyLoadTextures = true;
		TArray<FItem> Items;
		TSharedPtr<FJsonObject> Report = MakeShared<FJsonObject>();
		TSet<FString> DirtyBefore = DirtyPackages();
		TWeakObjectPtr<UWorld> EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		bool bFinished = false;

		FJob() { Report->SetStringField(TEXT("jobId"), Id); }
		virtual ~FJob() override { Cleanup(); }
		virtual FString GetReferencerName() const override { return TEXT("MeshStudioRender::FJob"); }
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			Collector.AddReferencedObject(World);
			Collector.AddReferencedObjects(Assets);
			Collector.AddReferencedObjects(Textures);
		}

		void Initialize()
		{
			Report->SetBoolField(TEXT("success"), true);
			Report->SetStringField(TEXT("state"), TEXT("running"));
			Report->SetStringField(TEXT("captureMode"), CaptureMode);
			Report->SetStringField(TEXT("manifest"), FPaths::Combine(OutputDir, TEXT("mesh-studio-") + Id + TEXT(".json")));
			Report->SetNumberField(TEXT("requested"), Items.Num());
			Report->SetArrayField(TEXT("dirtyPackagesBefore"), PackageNames(DirtyBefore));
			WriteManifest();
		}

		void Finish(const FString& State, const FString& Reason = FString())
		{
			if (bFinished) return;
			Cleanup();
			if (!Reason.IsEmpty())
			{
				while (Index < Items.Num())
				{
					auto Row = MakeShared<FJsonObject>();
					Row->SetStringField(TEXT("id"), Items[Index++].Id);
					Row->SetStringField(TEXT("status"), TEXT("blocked"));
					Row->SetStringField(TEXT("reason"), Reason);
					Outcomes.Add(MakeShared<FJsonValueObject>(Row));
				}
				Report->SetStringField(TEXT("error"), Reason);
			}
			const TSet<FString> After = DirtyPackages();
			const bool bUnchanged = SamePackages(DirtyBefore, After);
			Report->SetBoolField(TEXT("dirtyPackagesUnchanged"), bUnchanged);
			Report->SetArrayField(TEXT("dirtyPackagesAfter"), PackageNames(After));
			Report->SetBoolField(TEXT("cleanupComplete"), World == nullptr && Meshes.IsEmpty() && !Capture.IsValid());
			Report->SetStringField(TEXT("state"), bUnchanged ? State : TEXT("error"));
			Report->SetBoolField(TEXT("success"), bUnchanged && Reason.IsEmpty() && bAllRendered);
			if (!bUnchanged) Report->SetStringField(TEXT("error"), TEXT("Dirty-package set changed during capture; no flags were cleared and no packages were saved"));
			Report->SetNumberField(TEXT("durationSeconds"), FPlatformTime::Seconds() - StartedAt);
			int32 Rendered = 0, Blocked = 0, Errors = 0;
			for (const auto& Row : Outcomes)
			{
				const FString Status = Row->AsObject()->GetStringField(TEXT("status"));
				if (Status == TEXT("rendered")) ++Rendered;
				else if (Status == TEXT("blocked")) ++Blocked;
				else ++Errors;
			}
			Report->SetNumberField(TEXT("rendered"), Rendered);
			Report->SetNumberField(TEXT("blocked"), Blocked);
			Report->SetNumberField(TEXT("errors"), Errors);
			bFinished = true;
			WriteManifest();
		}

		void Advance()
		{
			// Core ticker callbacks and status requests can happen more than once
			// per engine frame. Only a real frame advances the rendering stages.
			if (LastFrame == GFrameCounter) return;
			LastFrame = GFrameCounter;
			if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor || !EditorWorld.IsValid()
				|| GEditor->GetEditorWorldContext().World() != EditorWorld.Get())
			{
				Finish(TEXT("cancelled"), TEXT("PIE started or the editor world changed")); return;
			}
			if (!SamePackages(DirtyBefore, DirtyPackages()))
			{
				Finish(TEXT("error"), TEXT("Dirty-package set changed while rendering")); return;
			}
			if (FPlatformTime::Seconds() - StartedAt > JobBudgetSeconds)
			{
				Finish(TEXT("timed_out"), TEXT("Studio render exceeded its 540s job budget")); return;
			}
			if (Index == Items.Num()) { Finish(TEXT("completed")); return; }
			if (Stage != EStage::Load && FPlatformTime::Seconds() - ItemStartedAt > ItemBudgetSeconds)
			{
				EndItem(TEXT("blocked"), TEXT("Mesh, shader or requested texture mips did not become ready within 60s")); return;
			}
			FString Error;
			switch (Stage)
			{
			case EStage::Load:
				BeginItem();
				break;
			case EStage::AwaitAssets:
				// Yield after loading before skeletal components register. A load
				// may still carry NeedPostLoad or asynchronous render-data work.
				FAssetCompilingManager::Get().ProcessAsyncTasks(true);
				for (UObject* Asset : Assets)
				{
					if (Asset->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad)) return;
					if (UStaticMesh* Static = Cast<UStaticMesh>(Asset)) { if (Static->IsCompiling()) return; }
					if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Asset)) { if (Skeletal->IsCompiling()) return; }
				}
				if (!SpawnScene(Error)) { EndItem(TEXT("error"), Error); return; }
				Stage = EStage::Warmup;
				WarmupFrames = 0;
				break;
			case EStage::Warmup:
				FAssetCompilingManager::Get().ProcessAsyncTasks(true);
				World->Tick(LEVELTICK_All, 1.0f / 60.0f);
				USkyLightComponent::UpdateSkyCaptureContents(World);
				World->SendAllEndOfFrameUpdates();
				if (WarmupFrames++ < 3) return;
				for (UMeshComponent* Mesh : Meshes)
					for (UMaterialInterface* Material : Mesh->GetMaterials())
						if (Material && !Material->IsComplete()) return;
				if (bFullyLoadTextures)
				{
					// Re-query after material compilation so the final texture set,
					// including instance parameters, is what gates the capture.
					CollectTextures();
					for (UTexture* Texture : Textures)
					{
						Texture->SetForceMipLevelsToBeResident(60.0f);
						if (Texture->IsCompiling() || !Texture->IsFullyStreamedIn()) return;
					}
				}
				if (!FitCamera(Error)) { EndItem(TEXT("error"), Error); return; }
				Capture->GetComponent()->CaptureSource = CaptureMode == TEXT("matte") ? SCS_SceneColorHDR : SCS_FinalColorLDR;
				Capture->Capture(false);
				Stage = EStage::ReadFirst;
				break;
			case EStage::ReadFirst:
				if (!ReadPixels(FirstPixels, Error)) { EndItem(TEXT("error"), Error); return; }
				if (CaptureMode == TEXT("both"))
				{
					Capture->GetComponent()->CaptureSource = SCS_SceneColorHDR;
					Capture->Capture(false);
					Stage = EStage::ReadMatte;
				}
				else Export(FirstPixels);
				break;
			case EStage::ReadMatte:
				{
					TArray<FColor> Matte;
					if (!ReadPixels(Matte, Error)) { EndItem(TEXT("error"), Error); return; }
					for (int32 Pixel = 0; Pixel < Matte.Num(); ++Pixel) FirstPixels[Pixel].A = 255 - Matte[Pixel].A;
					Export(FirstPixels);
					break;
				}
			}
		}

	private:
		enum class EStage { Load, AwaitAssets, Warmup, ReadFirst, ReadMatte };
		EStage Stage = EStage::Load;
		int32 Index = 0;
		int32 WarmupFrames = 0;
		uint64 LastFrame = MAX_uint64;
		double StartedAt = FPlatformTime::Seconds();
		double ItemStartedAt = 0;
		bool bAllRendered = true;
		UWorld* World = nullptr;
		TArray<UObject*> Assets;
		TArray<UTexture*> Textures;
		TArray<UMeshComponent*> Meshes;
		TArray<TSharedPtr<FJsonValue>> Outcomes;
		TArray<FColor> FirstPixels;
		TUniquePtr<UEMCP::SceneCapture::FTransientCapture> Capture;
		TSharedPtr<FJsonObject> Outcome;

		void WriteManifest()
		{
			Report->SetArrayField(TEXT("outcomes"), Outcomes);
			Report->SetNumberField(TEXT("completed"), Outcomes.Num());
			FString Json;
			const auto Writer = TJsonWriterFactory<>::Create(&Json);
			FJsonSerializer::Serialize(Report.ToSharedRef(), Writer);
			if (!FFileHelper::SaveStringToFile(Json, *Report->GetStringField(TEXT("manifest")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				Report->SetBoolField(TEXT("success"), false);
				Report->SetStringField(TEXT("manifestError"), TEXT("Could not write outcome manifest"));
			}
			else Report->RemoveField(TEXT("manifestError"));
		}

		void BeginItem()
		{
			ItemStartedAt = FPlatformTime::Seconds();
			const FItem& Item = Items[Index];
			Outcome = MakeShared<FJsonObject>();
			Outcome->SetStringField(TEXT("id"), Item.Id);
			if (!Item.InvalidReason.IsEmpty()) { EndItem(TEXT("blocked"), Item.InvalidReason); return; }
			for (const FPart& Part : Item.Parts)
			{
				UObject* Mesh = LoadObject<UObject>(nullptr, *Part.Path);
				if (!Cast<UStaticMesh>(Mesh) && !Cast<USkeletalMesh>(Mesh))
				{
					EndItem(TEXT("blocked"), FString::Printf(TEXT("Not a loadable StaticMesh or SkeletalMesh: %s"), *Part.Path)); return;
				}
				Assets.Add(Mesh);
			}
			UTextureCube* Cubemap = LoadObject<UTextureCube>(nullptr, *CubemapPath);
			if (!Cubemap) { EndItem(TEXT("blocked"), TEXT("Skylight cubemap could not be loaded")); return; }
			Assets.Add(Cubemap);
			Stage = EStage::AwaitAssets;
		}

		void Cleanup()
		{
			UTextureRenderTarget2D* Target = Capture.IsValid() ? Capture->GetTarget() : nullptr;
			Capture.Reset();
			if (Target)
			{
				// Large batches must not retain a full render target per item
				// until the editor next decides to collect garbage.
				Target->ReleaseResource();
				Target->MarkAsGarbage();
			}
			Meshes.Reset();
			if (World)
			{
				TArray<AActor*> Actors;
				for (TActorIterator<AActor> It(World); It; ++It) Actors.Add(*It);
				for (AActor* Actor : Actors) World->DestroyActor(Actor, false, false);
				if (GEngine) GEngine->DestroyWorldContext(World);
				// Release the render scene even on a partially-created item.
				World->DestroyWorld(false);
				World->MarkObjectsPendingKill();
				World = nullptr;
			}
			Textures.Reset();
			Assets.Reset();
			FirstPixels.Reset();
		}

		void EndItem(const FString& Status, const FString& Reason = FString())
		{
			Outcome->SetStringField(TEXT("status"), Status);
			if (!Reason.IsEmpty()) Outcome->SetStringField(TEXT("reason"), Reason);
			Outcome->SetNumberField(TEXT("durationSeconds"), FPlatformTime::Seconds() - ItemStartedAt);
			if (Status != TEXT("rendered")) bAllRendered = false;
			Cleanup();
			Outcomes.Add(MakeShared<FJsonValueObject>(Outcome));
			Outcome.Reset();
			++Index;
			Stage = EStage::Load;
			WriteManifest();
		}

		template <typename T>
		T* AddComponent(AActor* Actor)
		{
			T* Component = NewObject<T>(Actor, NAME_None, RF_Transient);
			Actor->SetRootComponent(Component);
			Actor->AddInstanceComponent(Component);
			Component->SetMobility(EComponentMobility::Movable);
			return Component;
		}

		AActor* SpawnActor()
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags = RF_Transient;
			Params.bTemporaryEditorActor = true;
			Params.bHideFromSceneOutliner = true;
			Params.bCreateActorPackage = false;
			return World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		}

		bool SpawnScene(FString& Error)
		{
			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
				.SetTransactional(false).CreateFXSystem(false).CreateWorldPartition(false);
			World = UWorld::CreateWorld(EWorldType::EditorPreview, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UEMCP_MeshStudio")),
				GetTransientPackage(), true, ERHIFeatureLevel::Num, &Init);
			if (!World) { Error = TEXT("Could not create preview render world"); return false; }
			GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
			for (int32 PartIndex = 0; PartIndex < Items[Index].Parts.Num(); ++PartIndex)
			{
				AActor* Actor = SpawnActor();
				if (!Actor) { Error = TEXT("Could not spawn preview mesh actor"); return false; }
				UMeshComponent* Mesh = nullptr;
				if (UStaticMesh* Static = Cast<UStaticMesh>(Assets[PartIndex]))
				{
					auto* Component = AddComponent<UStaticMeshComponent>(Actor);
					Component->SetStaticMesh(Static);
					Mesh = Component;
				}
				else
				{
					auto* Component = AddComponent<USkeletalMeshComponent>(Actor);
					Component->SetSkeletalMesh(CastChecked<USkeletalMesh>(Assets[PartIndex]), false);
					Component->bComponentUseFixedSkelBounds = true;
					Mesh = Component;
				}
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Mesh->SetRelativeTransform(Items[Index].Parts[PartIndex].Transform);
				Mesh->RegisterComponent();
				Meshes.Add(Mesh);
			}
			if (!FitBoundsAndLights(Error)) return false;
			return true;
		}

		FBox UnionBounds() const
		{
			FBox Bounds(ForceInit);
			for (UMeshComponent* Mesh : Meshes)
			{
				Mesh->UpdateBounds();
				Bounds += Mesh->Bounds.GetBox();
			}
			return Bounds;
		}

		bool FitBoundsAndLights(FString& Error)
		{
			const FBox Bounds = UnionBounds();
			if (!Bounds.IsValid || Bounds.GetExtent().IsNearlyZero() || Bounds.GetCenter().ContainsNaN()
				|| Bounds.GetExtent().ContainsNaN() || Bounds.GetExtent().GetAbsMax() > 1.e8 || Bounds.GetCenter().GetAbsMax() > 1.e8)
			{
				Error = TEXT("Meshes have empty, nonfinite or excessively large bounds"); return false;
			}
			const FVector Center = Bounds.GetCenter();
			const float Radius = FMath::Max(Bounds.GetExtent().GetMax(), 20.0);
			const FRotator Rotation = (-ViewDirection).Rotation();
			const FVector Right = Rotation.RotateVector(FVector::YAxisVector);
			const FVector Up = Rotation.RotateVector(FVector::ZAxisVector);
			const FVector Offsets[] = { ViewDirection * 3.0, ViewDirection * 1.5 - Right * 2.0 + Up, -ViewDirection * 2.0 + Right + Up * 1.5 };
			const float Lumens[] = {75.f, 50.f, 60.f};
			for (int32 LightIndex = 0; LightIndex < 3; ++LightIndex)
			{
				AActor* Actor = SpawnActor();
				if (!Actor) { Error = TEXT("Could not spawn studio light"); return false; }
				auto* Light = AddComponent<URectLightComponent>(Actor);
				const FVector Location = Center + Offsets[LightIndex] * Radius;
				const float Distance = FVector::Distance(Location, Center);
				Light->SetIntensityUnits(ELightUnits::Lumens);
				Light->SetIntensity(Lumens[LightIndex] * FMath::Square(Distance / 100.f));
				Light->SetAttenuationRadius(FMath::Max(1000.f, Distance * 2.f));
				Light->SetSourceWidth(Distance * 3.f);
				Light->SetSourceHeight(Distance * 3.f);
				Light->SetRelativeLocationAndRotation(Location, (Center - Location).Rotation());
				Light->RegisterComponent();
			}
			AActor* SkyActor = SpawnActor();
			if (!SkyActor) { Error = TEXT("Could not spawn studio skylight"); return false; }
			auto* Sky = AddComponent<USkyLightComponent>(SkyActor);
			Sky->SourceType = SLS_SpecifiedCubemap;
			Sky->SetCubemap(CastChecked<UTextureCube>(Assets.Last()));
			Sky->SetIntensity(1.0f);
			Sky->RegisterComponent();
			Capture = MakeUnique<UEMCP::SceneCapture::FTransientCapture>();
			if (!Capture->Spawn(World, Center, Rotation, Items[Index].Width, Items[Index].Height, CameraFov, Error)) return false;
			auto* Component = Capture->GetComponent();
			Component->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			for (UMeshComponent* Mesh : Meshes) Component->ShowOnlyComponent(Mesh);
			Component->ShowFlags.SetAtmosphere(false);
			Component->ShowFlags.SetFog(false);
			Component->ShowFlags.SetMotionBlur(false);
			Component->ShowFlags.SetTemporalAA(false);
			Component->ShowFlags.SetAntiAliasing(false);
			auto& Post = Component->PostProcessSettings;
			Post.bOverride_AutoExposureMethod = true;
			Post.AutoExposureMethod = AEM_Manual;
			Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			Post.AutoExposureApplyPhysicalCameraExposure = false;
			Post.bOverride_AutoExposureBias = true;
			Post.AutoExposureBias = 1.0f;
			Post.bOverride_BloomIntensity = true;
			Post.BloomIntensity = 0.f;
			Post.bOverride_VignetteIntensity = true;
			Post.VignetteIntensity = 0.f;
			Post.bOverride_LensFlareIntensity = true;
			Post.LensFlareIntensity = 0.f;
			Component->PostProcessBlendWeight = 1.f;
			Capture->GetTarget()->ClearColor = FLinearColor(0, 0, 0, 1);
			CollectTextures();
			return true;
		}

		void CollectTextures()
		{
			for (UMeshComponent* Mesh : Meshes)
			{
				for (UMaterialInterface* Material : Mesh->GetMaterials())
				{
					if (!Material) continue;
					TArray<UTexture*> Used;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
					Material->GetUsedTextures(Used);
#else
					Material->GetUsedTextures(Used, EMaterialQualityLevel::High, true, World->GetFeatureLevel(), true);
#endif
					for (UTexture* Texture : Used) if (Texture) Textures.AddUnique(Texture);
				}
			}
			if (!Assets.IsEmpty()) Textures.AddUnique(CastChecked<UTextureCube>(Assets.Last()));
			if (bFullyLoadTextures) for (UTexture* Texture : Textures) Texture->SetForceMipLevelsToBeResident(60.f);
		}

		bool FitCamera(FString& Error)
		{
			const FBox Bounds = UnionBounds();
			if (!Bounds.IsValid || Bounds.GetCenter().ContainsNaN() || Bounds.GetExtent().ContainsNaN())
			{
				Error = TEXT("Final mesh bounds are invalid"); return false;
			}
			const FItem& Item = Items[Index];
			const FVector Center = Bounds.GetCenter();
			const FRotator Rotation = (-ViewDirection).Rotation();
			const FVector Right = Rotation.RotateVector(FVector::YAxisVector);
			const FVector Up = Rotation.RotateVector(FVector::ZAxisVector);
			const double TanHorizontal = FMath::Tan(FMath::DegreesToRadians(CameraFov * 0.5));
			const double TanVertical = TanHorizontal * Item.Height / Item.Width;
			const double NearClip = FMath::Clamp(Bounds.GetExtent().GetMax() * 0.01, 0.001, 10.0);
			double Distance = NearClip * 2.0;
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Offset = FVector(Corner & 1 ? Bounds.Max.X : Bounds.Min.X,
					Corner & 2 ? Bounds.Max.Y : Bounds.Min.Y, Corner & 4 ? Bounds.Max.Z : Bounds.Min.Z) - Center;
				const double Depth = FVector::DotProduct(Offset, ViewDirection);
				Distance = FMath::Max(Distance, Depth + 1.12 * FMath::Max(FMath::Abs(FVector::DotProduct(Offset, Right)) / TanHorizontal,
					FMath::Abs(FVector::DotProduct(Offset, Up)) / TanVertical) + NearClip * 2.0);
			}
			const FVector Location = Center + ViewDirection * Distance;
			Capture->SetView(Location, Rotation, CameraFov);
			Capture->GetComponent()->bOverride_CustomNearClippingPlane = true;
			Capture->GetComponent()->CustomNearClippingPlane = NearClip;
			auto BoundsJson = MakeShared<FJsonObject>();
			BoundsJson->SetObjectField(TEXT("center"), MCPVec3ToJsonObject(Center));
			BoundsJson->SetObjectField(TEXT("extent"), MCPVec3ToJsonObject(Bounds.GetExtent()));
			BoundsJson->SetObjectField(TEXT("min"), MCPVec3ToJsonObject(Bounds.Min));
			BoundsJson->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(Bounds.Max));
			Outcome->SetObjectField(TEXT("bounds"), BoundsJson);
			auto Camera = MakeShared<FJsonObject>();
			Camera->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Location));
			Camera->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Rotation));
			Camera->SetObjectField(TEXT("viewDirection"), MCPVec3ToJsonObject(ViewDirection));
			Camera->SetNumberField(TEXT("fov"), CameraFov);
			Camera->SetNumberField(TEXT("nearClip"), NearClip);
			Camera->SetNumberField(TEXT("width"), Item.Width);
			Camera->SetNumberField(TEXT("height"), Item.Height);
			Outcome->SetObjectField(TEXT("camera"), Camera);
			Outcome->SetNumberField(TEXT("meshCount"), Item.Parts.Num());
			Outcome->SetBoolField(TEXT("texturesFullyResident"), bFullyLoadTextures);
			Outcome->SetNumberField(TEXT("textureCount"), Textures.Num());
			return true;
		}

		bool ReadPixels(TArray<FColor>& Pixels, FString& Error)
		{
			FTextureRenderTargetResource* Resource = Capture->GetTarget()->GameThread_GetRenderTargetResource();
			if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Items[Index].Width * Items[Index].Height)
			{
				Error = TEXT("Could not read render target pixels"); return false;
			}
			return true;
		}

		void Export(TArray<FColor>& Pixels)
		{
			if (CaptureMode == TEXT("beauty")) for (FColor& Pixel : Pixels) Pixel.A = 255;
			if (CaptureMode == TEXT("matte")) for (FColor& Pixel : Pixels)
			{
				const uint8 Coverage = 255 - Pixel.A;
				Pixel = FColor(Coverage, Coverage, Coverage, 255);
			}
			IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
			const FItem& Item = Items[Index];
			if (!Png.IsValid() || !Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Item.Width, Item.Height, ERGBFormat::BGRA, 8))
			{
				EndItem(TEXT("error"), TEXT("Could not encode PNG")); return;
			}
			const FString File = FPaths::Combine(OutputDir, Item.Id + TEXT(".png"));
			const TArray64<uint8> Bytes = Png->GetCompressed();
			if (Bytes.IsEmpty() || !FFileHelper::SaveArrayToFile(Bytes, *File))
			{
				EndItem(TEXT("error"), TEXT("Could not write PNG")); return;
			}
			Outcome->SetStringField(TEXT("file"), File);
			Outcome->SetNumberField(TEXT("sizeBytes"), Bytes.Num());
			EndItem(TEXT("rendered"));
		}
	};

	static TSharedPtr<FJob> Active;
	static FTSTicker::FDelegateHandle TickHandle;
	static FDelegateHandle PreExitHandle;
	static TMap<FString, TSharedPtr<FJsonObject>> Reports;
	static TArray<FString> ReportOrder;

	static void Stop(const FString& Reason)
	{
		if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
		if (Active.IsValid()) Active->Finish(TEXT("cancelled"), Reason);
		Active.Reset();
	}
}

TSharedPtr<FJsonValue> FEditorHandlers::RenderMeshStudio(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MeshStudioRender;
	FString Output;
	if (auto Error = RequireString(Params, TEXT("outputDir"), Output)) return Error;
	const double ResolutionValue = OptionalNumber(Params, TEXT("resolution"), 512);
	const FString Mode = OptionalString(Params, TEXT("captureMode"), TEXT("both"));
	const FString Preset = OptionalString(Params, TEXT("lightingPreset"), TEXT("studio"));
	const FString Cubemap = OptionalString(Params, TEXT("cubemapPath"), TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));
	const bool bFullyLoadTextures = OptionalBool(Params, TEXT("fullyLoadTextures"), true);
	FVector Direction = OptionalVec3(Params, TEXT("viewDirection"), FVector(1.0, -1.35, 0.9));
	const TArray<TSharedPtr<FJsonValue>>* ItemValues = nullptr;
	MCPNoteParamRead(Params, TEXT("items"));
	if (!Params->TryGetArrayField(TEXT("items"), ItemValues)) return MCPError(TEXT("items is required and must be an array"));
	if (!GEditor || !GEditor->GetEditorWorldContext().World()) return MCPError(TEXT("Editor world not available"));
	if (GEditor->PlayWorld || GEditor->bIsSimulatingInEditor) return MCPError(TEXT("render_mesh_studio is refused during PIE or simulation"));
	if (!FApp::CanEverRender() || GUsingNullRHI) return MCPError(TEXT("render_mesh_studio requires a rendering editor (no null RHI)"));
	if (Active.IsValid()) return MCPError(TEXT("A mesh studio render is already running; wait for it or cancel_mesh_studio_render"));
	if (!FMath::IsFinite(ResolutionValue) || ResolutionValue < 64 || ResolutionValue > 4096 || FMath::FloorToDouble(ResolutionValue) != ResolutionValue)
		return MCPError(TEXT("resolution must be an integer in [64,4096]"));
	if (Preset != TEXT("studio")) return MCPError(TEXT("lightingPreset must be studio"));
	if (Mode != TEXT("beauty") && Mode != TEXT("matte") && Mode != TEXT("both")) return MCPError(TEXT("captureMode must be beauty, matte or both"));
	if (!VectorField(Params, TEXT("viewDirection"), Direction) || Direction.ContainsNaN() || Direction.GetAbsMax() <= 0.0)
		return MCPError(TEXT("viewDirection must be finite and nonzero"));
	Direction /= Direction.GetAbsMax();
	if (!Direction.Normalize()) return MCPError(TEXT("viewDirection must be finite and nonzero"));
	TArray<FItem> Items;
	FString Error;
	if (!ParseItems(*ItemValues, static_cast<int32>(ResolutionValue), Items, Error)) return MCPError(Error);
	Output = FPaths::ConvertRelativePathToFull(UEMCP::SceneCapture::ResolveOutputPath(Output));
	if (!IFileManager::Get().MakeDirectory(*Output, true)) return MCPError(TEXT("Could not create outputDir"));
	Active = MakeShared<FJob>();
	Active->Items = MoveTemp(Items);
	Active->OutputDir = Output;
	Active->CubemapPath = Cubemap;
	Active->CaptureMode = Mode;
	Active->ViewDirection = Direction;
	Active->bFullyLoadTextures = bFullyLoadTextures;
	if (!PreExitHandle.IsValid())
		PreExitHandle = FCoreDelegates::OnPreExit.AddStatic(&FEditorHandlers::ShutdownMeshStudioRenders);
	Active->Initialize();
	if (Active->Report->HasField(TEXT("manifestError"))) Active->Finish(TEXT("error"), TEXT("Could not write initial outcome manifest"));
	while (ReportOrder.Num() >= 16) { Reports.Remove(ReportOrder[0]); ReportOrder.RemoveAt(0); }
	Reports.Add(Active->Id, Active->Report);
	ReportOrder.Add(Active->Id);
	if (Active->bFinished)
	{
		const auto Result = Snapshot(Active->Report);
		Active.Reset();
		return Result;
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
	{
		const TSharedPtr<FJob> Job = Active;
		if (!Job.IsValid()) return false;
		Job->Advance();
		if (Job->bFinished) { Active.Reset(); TickHandle.Reset(); return false; }
		return true;
	}));
	return Snapshot(Active->Report);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetMeshStudioRender(const TSharedPtr<FJsonObject>& Params)
{
	FString Id;
	if (auto Error = RequireString(Params, TEXT("jobId"), Id)) return Error;
	const auto* Report = MeshStudioRender::Reports.Find(Id);
	if (!Report) return MCPError(TEXT("Unknown or expired mesh studio jobId (last 16 jobs are retained)"));
	return MeshStudioRender::Snapshot(*Report);
}

TSharedPtr<FJsonValue> FEditorHandlers::CancelMeshStudioRender(const TSharedPtr<FJsonObject>& Params)
{
	FString Id;
	if (auto Error = RequireString(Params, TEXT("jobId"), Id)) return Error;
	if (MeshStudioRender::Active.IsValid() && MeshStudioRender::Active->Id == Id)
		MeshStudioRender::Stop(TEXT("Cancelled by caller"));
	const auto* Report = MeshStudioRender::Reports.Find(Id);
	if (!Report) return MCPError(TEXT("Unknown or expired mesh studio jobId"));
	return MeshStudioRender::Snapshot(*Report);
}

void FEditorHandlers::ShutdownMeshStudioRenders()
{
	if (MeshStudioRender::PreExitHandle.IsValid())
		FCoreDelegates::OnPreExit.Remove(MeshStudioRender::PreExitHandle);
	MeshStudioRender::PreExitHandle.Reset();
	MeshStudioRender::Stop(TEXT("Bridge module shutdown"));
	MeshStudioRender::Reports.Reset();
	MeshStudioRender::ReportOrder.Reset();
}
