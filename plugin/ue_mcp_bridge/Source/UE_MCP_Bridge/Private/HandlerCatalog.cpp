#include "HandlerCatalog.h"
#include "HandlerRegistry.h"
#include "Handlers/EditorHandlers.h"
#include "Handlers/AssetHandlers.h"
#include "Handlers/AssetHandlers_Geometry.h"
#include "Handlers/AssetHandlers_MeshBoolean.h"
#include "Handlers/AssetHandlers_BulkRead.h"
#include "Handlers/BlueprintHandlers.h"
#include "Handlers/BlueprintHandlers_Collision.h"
#include "Handlers/ProjectHandlers.h"
#include "Handlers/LevelHandlers.h"
#include "Handlers/ReflectionHandlers.h"
#include "Handlers/GasHandlers.h"
#include "Handlers/GameplayHandlers.h"
#include "Handlers/DialogHandlers.h"
#include "Handlers/MaterialHandlers.h"
#include "Handlers/AnimationHandlers.h"
#include "Handlers/AudioHandlers.h"
#include "Handlers/WidgetHandlers.h"
#include "Handlers/FoliageHandlers.h"
#include "Handlers/LandscapeHandlers.h"
#include "Handlers/NetworkingHandlers.h"
#include "Handlers/NiagaraHandlers.h"
#include "Handlers/PCGHandlers.h"
#include "Handlers/SequencerHandlers.h"
#include "Handlers/SplineHandlers.h"
#include "Handlers/PhysicsHandlers.h"
#include "Handlers/DemoHandlers.h"
#include "Handlers/StateTreeHandlers.h"
#include "Handlers/ChooserHandlers.h"
#include "Handlers/EpicHandlers.h"
#include "Handlers/MassHandlers.h"
#include "Handlers/SkeletalMeshHandlers.h"
#include "Handlers/FabHandlers.h"
#include "Handlers/LockHandlers.h"
#include "Handlers/DiffHandlers.h"

void MCPHandlerCatalog::RegisterAllHandlers(FMCPHandlerRegistry& Registry)
{
	FEditorHandlers::RegisterHandlers(Registry);
	FAssetHandlers::RegisterHandlers(Registry);
	FAssetGeometryHandlers::RegisterHandlers(Registry);
	FAssetMeshBooleanHandlers::RegisterHandlers(Registry);
	// #909: bulk_read_asset_properties, in its own translation unit so a
	// library-wide read lands without reopening AssetHandlers.cpp.
	FAssetBulkReadHandlers::RegisterHandlers(Registry);
	FBlueprintHandlers::RegisterHandlers(Registry);
	FCollisionQueryHandlers::RegisterHandlers(Registry);
	FLevelHandlers::RegisterHandlers(Registry);
	FReflectionHandlers::RegisterHandlers(Registry);
	FGasHandlers::RegisterHandlers(Registry);
	FGameplayHandlers::RegisterHandlers(Registry);
	FDialogHandlers::RegisterHandlers(Registry);
	FMaterialHandlers::RegisterHandlers(Registry);
	FAnimationHandlers::RegisterHandlers(Registry);
	FAudioHandlers::RegisterHandlers(Registry);
	FWidgetHandlers::RegisterHandlers(Registry);
	FFoliageHandlers::RegisterHandlers(Registry);
	FLandscapeHandlers::RegisterHandlers(Registry);
	FNetworkingHandlers::RegisterHandlers(Registry);
	FNiagaraHandlers::RegisterHandlers(Registry);
	FPCGHandlers::RegisterHandlers(Registry);
	FSequencerHandlers::RegisterHandlers(Registry);
	FSplineHandlers::RegisterHandlers(Registry);
	FPhysicsHandlers::RegisterHandlers(Registry);
	FDemoHandlers::RegisterHandlers(Registry);
	FProjectHandlers::RegisterHandlers(Registry);
	FStateTreeHandlers::RegisterHandlers(Registry);
	FChooserHandlers::RegisterHandlers(Registry);
	FEpicHandlers::RegisterHandlers(Registry);
	FMassHandlers::RegisterHandlers(Registry);
	FSkeletalMeshHandlers::RegisterHandlers(Registry);
	FFabHandlers::RegisterHandlers(Registry);
	FLockHandlers::RegisterHandlers(Registry);
	FDiffHandlers::RegisterHandlers(Registry);
}
