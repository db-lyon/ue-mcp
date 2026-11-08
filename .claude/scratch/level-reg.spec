	Registry.RegisterHandler(TEXT("place_actor"), &PlaceActor, {
		MCPParam::Required(TEXT("actorClass"), EType::String, TEXT("Actor class: short name, /Script path or Blueprint class path")),
		SpecLabel, SpecOnConflict, SpecLocation, SpecRotation, SpecScale,
		MCPParam::Optional(TEXT("staticMesh"), EType::String, TEXT("Static mesh for a StaticMeshActor")),
		MCPParam::Optional(TEXT("material"), EType::String, TEXT("Material applied at slot 0")),
		SpecWorld, SpecPieInstance,
	});
-----
	Registry.RegisterHandler(TEXT("place_actor"), &PlaceActor, FMCPSpawn::ParamsWith({
		MCPParam::Required(TEXT("actorClass"), EType::String, TEXT("Actor class: short name, /Script path or Blueprint class path")),
		MCPParam::Optional(TEXT("staticMesh"), EType::String, TEXT("Static mesh for a StaticMeshActor")),
		MCPParam::Optional(TEXT("material"), EType::String, TEXT("Material applied at slot 0")),
	}));
=====
		Registry.RegisterHandler(TEXT("internal_spawn_actor"), &SpawnActorUnchecked, {
			MCPParam::Required(TEXT("actorClass"), EType::String, TEXT("Actor class: short name, /Script path or Blueprint class path")),
			MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label, applied even when another actor carries it")),
			SpecLocation, SpecRotation, SpecScale,
			MCPParam::Optional(TEXT("staticMesh"), EType::String, TEXT("Static mesh for a StaticMeshActor")),
			MCPParam::Optional(TEXT("material"), EType::String, TEXT("Material applied at slot 0")),
			MCPParam::Optional(TEXT("folderPath"), EType::String, TEXT("World Outliner folder")),
		});
-----
		Registry.RegisterHandler(TEXT("internal_spawn_actor"), &SpawnActorUnchecked, FMCPSpawn::ParamsWith({
			MCPParam::Required(TEXT("actorClass"), EType::String, TEXT("Actor class: short name, /Script path or Blueprint class path")),
			MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label, applied even when another actor carries it")),
			MCPParam::Optional(TEXT("staticMesh"), EType::String, TEXT("Static mesh for a StaticMeshActor")),
			MCPParam::Optional(TEXT("material"), EType::String, TEXT("Material applied at slot 0")),
		}));
=====
	Registry.RegisterHandler(TEXT("delete_actor"), &DeleteActor, {
		SpecActorLabel, SpecActorPath,
	});
-----
	Registry.RegisterHandler(TEXT("delete_actor"), &DeleteActor, {
		SpecActorLabel, SpecActorPath,
		SpecWorld, SpecPieInstance,
	});
=====
	Registry.RegisterHandler(TEXT("spawn_light"), &SpawnLight, {
		MCPParam::Required(TEXT("lightType"), EType::String, TEXT("point | spot | directional | rect | sky")),
		SpecOnConflict, SpecLabel, SpecLocation, SpecRotation, SpecIntensity, SpecColor, SpecMobility,
		MCPParam::Optional(TEXT("attenuationRadius"), EType::Number, TEXT("Point, spot and rect lights only")),
	});
-----
	Registry.RegisterHandler(TEXT("spawn_light"), &SpawnLight, FMCPSpawn::ParamsWith({
		MCPParam::Required(TEXT("lightType"), EType::String, TEXT("point | spot | directional | rect | sky")),
		SpecIntensity, SpecColor, SpecMobility,
		MCPParam::Optional(TEXT("attenuationRadius"), EType::Number, TEXT("Point, spot and rect lights only")),
	}, TEXT("light")));
=====
	Registry.RegisterHandler(TEXT("spawn_volume"), &SpawnVolume, {
		MCPParam::Required(TEXT("volumeType"), EType::String, TEXT("Volume class, by short name or alias such as trigger, blocking, postprocess, navmesh")),
		SpecOnConflict, SpecLabel, SpecLocation,
		MCPParam::Optional(TEXT("extent"), EType::Vec3, TEXT("Half extent of the cube brush (default 100, 100, 100)")),
		MCPParam::Optional(TEXT("graphPath"), EType::String, TEXT("PCG graph for a PCGVolume")),
	});
-----
	Registry.RegisterHandler(TEXT("spawn_volume"), &SpawnVolume, FMCPSpawn::ParamsWith({
		MCPParam::Required(TEXT("volumeType"), EType::String, TEXT("Volume class, by short name or alias such as trigger, blocking, postprocess, navmesh, pcg")),
		MCPParam::Optional(TEXT("extent"), EType::Vec3, TEXT("Half extent of the cube brush, positive on every axis (default 100, 100, 100)")),
		MCPParam::Optional(TEXT("graphPath"), EType::String, TEXT("PCG graph for a volume with a PCG component; refused when it does not load")),
	}, TEXT("volume")));
=====
	const TArray<FMCPParamSpec> SpawnSkeletalSpec = {
		MCPParam::Optional(TEXT("skeletalMesh"), EType::String, TEXT("SkeletalMesh asset path to spawn; null is refused here")).Nullable(),
		MCPParam::Optional(TEXT("meshPath"), EType::String, TEXT("The older spelling of skeletalMesh")),
		SpecLabel, SpecOnConflict,
		MCPParam::Optional(TEXT("transform"), EType::Object, TEXT("Spawn transform; location, rotation and scale given on their own win over its parts")).WithFields({
			MCPParam::OptionalField(TEXT("location"), EType::Vec3, TEXT("World location")),
			MCPParam::OptionalField(TEXT("rotation"), EType::Rotator, TEXT("World rotation")),
			MCPParam::OptionalField(TEXT("scale"), EType::Vec3, TEXT("Actor scale")),
		}),
		SpecLocation, SpecRotation, SpecScale,
		MCPParam::Optional(TEXT("materials"), EType::Array, TEXT("Per-slot component material override paths; index is the slot, and a null or empty entry leaves that slot alone")),
		MCPParam::Optional(TEXT("animSequence"), EType::String, TEXT("Single-node preview animation")),
		MCPParam::Optional(TEXT("loop"), EType::Boolean, TEXT("Loop the preview animation (default true)")),
	};
-----
	const TArray<FMCPParamSpec> SpawnSkeletalSpec = FMCPSpawn::ParamsWith({
		MCPParam::Optional(TEXT("skeletalMesh"), EType::String, TEXT("SkeletalMesh asset path to spawn; null is refused here")).Nullable(),
		MCPParam::Optional(TEXT("meshPath"), EType::String, TEXT("The older spelling of skeletalMesh")),
		MCPParam::Optional(TEXT("transform"), EType::Object, TEXT("Spawn transform; location, rotation and scale given on their own win over its parts")).WithFields({
			MCPParam::OptionalField(TEXT("location"), EType::Vec3, TEXT("World location")),
			MCPParam::OptionalField(TEXT("rotation"), EType::Rotator, TEXT("World rotation")),
			MCPParam::OptionalField(TEXT("scale"), EType::Vec3, TEXT("Actor scale")),
		}),
		MCPParam::Optional(TEXT("materials"), EType::Array, TEXT("Per-slot component material override paths; index is the slot, and a null or empty entry leaves that slot alone")),
		MCPParam::Optional(TEXT("animSequence"), EType::String, TEXT("Single-node preview animation; refused when it does not load")),
		MCPParam::Optional(TEXT("loop"), EType::Boolean, TEXT("Loop the preview animation (default true)")),
	}, TEXT("skeletal mesh actor"));
