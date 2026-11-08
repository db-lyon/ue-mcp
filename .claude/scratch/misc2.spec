TSharedPtr<FJsonValue> FPCGHandlers::CreatePCGGraph(
TSharedPtr<FJsonValue> FPCGHandlers::CreatePCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("PCGGraph"), TEXT("/Game/PCG") });
	if (auto Err = Create.Read()) return Err;
	UPCGGraph* Graph = nullptr;
	if (auto Early = Create.Make(Graph)) return Early;
	return MCPResult(Create.Finish());
}
