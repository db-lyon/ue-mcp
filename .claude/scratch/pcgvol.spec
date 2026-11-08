TSharedPtr<FJsonValue> FPCGHandlers::SpawnPCGVolume(
// add_pcg_volume is level(spawn_volume) with volumeType pcg and a 500 cm default half-size.
TSharedPtr<FJsonValue> FPCGHandlers::SpawnPCGVolume(const TSharedPtr<FJsonObject>& Params)
{
	return UEMCP::SpawnVolume(Params, APCGVolume::StaticClass(), FVector(500.0), TEXT("PCGVolume"),
		[](AActor*) -> TSharedPtr<FJsonValue> { return nullptr; });
}
