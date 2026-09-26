#include "HandlerCatalog.h"
#include "HandlerRegistry.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Handlers/Asset/AssetHandlers_Geometry.h"
#include "Handlers/Asset/AssetHandlers_MeshBoolean.h"
#include "Handlers/Asset/AssetHandlers_BulkRead.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Handlers/Blueprint/BlueprintHandlers_Collision.h"
#include "Handlers/Project/ProjectHandlers.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Handlers/Reflection/ReflectionHandlers.h"
#include "Handlers/Gas/GasHandlers.h"
#include "Handlers/Gameplay/GameplayHandlers.h"
#include "Handlers/Dialog/DialogHandlers.h"
#include "Handlers/Material/MaterialHandlers.h"
#include "Handlers/Animation/AnimationHandlers.h"
#include "Handlers/Audio/AudioHandlers.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "Handlers/Foliage/FoliageHandlers.h"
#include "Handlers/Landscape/LandscapeHandlers.h"
#include "Handlers/Networking/NetworkingHandlers.h"
#include "Handlers/Niagara/NiagaraHandlers.h"
#include "Handlers/PCG/PCGHandlers.h"
#include "Handlers/Sequencer/SequencerHandlers.h"
#include "Handlers/Spline/SplineHandlers.h"
#include "Handlers/Physics/PhysicsHandlers.h"
#include "Handlers/Demo/DemoHandlers.h"
#include "Handlers/StateTree/StateTreeHandlers.h"
#include "Handlers/Chooser/ChooserHandlers.h"
#include "Handlers/Epic/EpicHandlers.h"
#include "Handlers/Mass/MassHandlers.h"
#include "Handlers/SkeletalMesh/SkeletalMeshHandlers.h"
#include "Handlers/Fab/FabHandlers.h"
#include "Handlers/Lock/LockHandlers.h"
#include "Handlers/Diff/DiffHandlers.h"

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
