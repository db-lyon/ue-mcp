#include "DeletedActorPackages.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/GCObject.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	class FMCPDeletedActorPackageHolder : public FGCObject
	{
	public:
		struct FEntry
		{
			TObjectPtr<UPackage> Package;
			TWeakObjectPtr<UWorld> World;
		};

		void Add(UPackage* Package, UWorld* World)
		{
			for (const FEntry& Entry : Entries)
			{
				if (Entry.Package == Package) return;
			}
			Entries.Add({ Package, World });
		}

		/** Drop packages a save has committed, and any whose world is gone. Run outside reference collection. */
		void Prune()
		{
			Entries.RemoveAll([](const FEntry& Entry)
			{
				return !IsValid(Entry.Package) || !Entry.Package->IsDirty() || !Entry.World.IsValid();
			});
		}

		void ReleaseWorld(const UWorld* World)
		{
			Entries.RemoveAll([World](const FEntry& Entry)
			{
				return !Entry.World.IsValid() || Entry.World.Get() == World;
			});
		}

		void Reset() { Entries.Reset(); }
		int32 Num() const { return Entries.Num(); }

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			for (FEntry& Entry : Entries)
			{
				Collector.AddReferencedObject(Entry.Package);
			}
		}

		virtual FString GetReferencerName() const override { return TEXT("FMCPDeletedActorPackageHolder"); }

	private:
		TArray<FEntry> Entries;
	};

	TUniquePtr<FMCPDeletedActorPackageHolder> GMCPDeletedActorPackageHolder;
	FDelegateHandle GMCPActorDeletedHandle;
	FDelegateHandle GMCPPreGCHandle;
	FDelegateHandle GMCPWorldCleanupHandle;

	void MCPOnLevelActorDeleted(AActor* Actor)
	{
		if (!GMCPDeletedActorPackageHolder || !Actor || !Actor->IsPackageExternal()) return;
		UWorld* World = Actor->GetWorld();
		// Only the editor world is saved to disk; a PIE or preview world's actors never are.
		if (!World || World->WorldType != EWorldType::Editor) return;
		if (UPackage* Package = Actor->GetExternalPackage())
		{
			GMCPDeletedActorPackageHolder->Add(Package, World);
		}
	}
}

void FMCPDeletedActorPackages::Install()
{
	if (GMCPDeletedActorPackageHolder || !GEngine) return;
	GMCPDeletedActorPackageHolder = MakeUnique<FMCPDeletedActorPackageHolder>();
	GMCPActorDeletedHandle = GEngine->OnLevelActorDeleted().AddStatic(&MCPOnLevelActorDeleted);
	GMCPPreGCHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddLambda([]()
	{
		if (GMCPDeletedActorPackageHolder) GMCPDeletedActorPackageHolder->Prune();
	});
	// A map change or discarded edit tears the world down; an empty package left in memory under the actor's
	// name would then stop the actor loading from its file when the map is opened again.
	GMCPWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World, bool, bool)
	{
		if (GMCPDeletedActorPackageHolder) GMCPDeletedActorPackageHolder->ReleaseWorld(World);
	});
}

void FMCPDeletedActorPackages::Remove()
{
	if (GEngine && GMCPActorDeletedHandle.IsValid()) GEngine->OnLevelActorDeleted().Remove(GMCPActorDeletedHandle);
	if (GMCPPreGCHandle.IsValid()) FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(GMCPPreGCHandle);
	if (GMCPWorldCleanupHandle.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(GMCPWorldCleanupHandle);
	GMCPActorDeletedHandle.Reset();
	GMCPPreGCHandle.Reset();
	GMCPWorldCleanupHandle.Reset();
	GMCPDeletedActorPackageHolder.Reset();
}

int32 FMCPDeletedActorPackages::NumHeld()
{
	return GMCPDeletedActorPackageHolder ? GMCPDeletedActorPackageHolder->Num() : 0;
}
