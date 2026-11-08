	FMSSession* OpenBuilderSession(const TSharedPtr<FJsonObject>& Params, FString& OutAssetPath, TSharedPtr<FJsonValue>& OutEarly)
	{
		FString Name;
		if (auto Err = RequireString(Params, TEXT("name"), Name)) { OutEarly = Err; return nullptr; }

		const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/MetaSounds"));
		const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
		const FString Format
-----
	FMSSession* OpenBuilderSession(const TSharedPtr<FJsonObject>& Params, FMCPCreate& Create, FString& OutAssetPath, TSharedPtr<FJsonValue>& OutEarly)
	{
		if (auto Err = Create.Read()) { OutEarly = Err; return nullptr; }
		const FString Format
=====
		auto Created = MCPCreateAssetIdempotent<UMetaSoundSource>(Name, PackagePath, OnConflict, TEXT("MetaSoundSource"), Cls, Factory);
		if (Created.EarlyReturn) { OutEarly = Created.EarlyReturn; return nullptr; }
		OutAssetPath = Created.Asset->GetPathName();

		FMSSession* Session = MSAuthorOpenOnAsset(Created.Asset, OutAssetPath, OutEarly);
		if (!Session)
		{
			if (!OutEarly.IsValid()) { OutEarly = MSAuthorNoSessionError(OutAssetPath); }
			return nullptr;
		}
-----
		UMetaSoundSource* Source = nullptr;
		if (auto Early = Create.Make(Source, Cls, Factory)) { OutEarly = Early; return nullptr; }
		OutAssetPath = Source->GetPathName();

		// A source no builder can attach to is deleted again rather than left half-made.
		FMSSession* Session = MSAuthorOpenOnAsset(Source, OutAssetPath, OutEarly);
		if (!Session)
		{
			OutEarly = Create.Abandon(OutEarly.IsValid() ? OutEarly : MSAuthorNoSessionError(OutAssetPath));
			return nullptr;
		}
=====
	FString AssetPath;
	TSharedPtr<FJsonValue> Early;
	FMSSession* S = OpenBuilderSession(Params, AssetPath, Early);
	if (!S) return Early;

	// The builder writes into the asset's own document, so the asset is already
	// a loadable, interface-valid, silent MetaSound. Only the save is left.
	auto Res = MCPSuccess();
	MSAuthorSave(Res, AssetPath);
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetBoolField(TEXT("oneShot"), S->bOneShot);
	Res->SetNumberField(TEXT("audioOutputs"), S->AudioOuts.Num());
	Res->SetStringField(TEXT("note"), TEXT("Builder attached to the asset's own document. Author with metasound_* actions (or use metasound_author to stamp a whole graph); metasound_build saves. audio(metasound_read_document) reads back what has been authored so far."));
	MCPSetDeleteAssetRollback(Res, AssetPath);
	return MCPResult(Res);
-----
	FMCPCreate Create(Params, { TEXT("MetaSoundSource"), TEXT("/Game/Audio/MetaSounds") });
	FString AssetPath;
	TSharedPtr<FJsonValue> Early;
	FMSSession* S = OpenBuilderSession(Params, Create, AssetPath, Early);
	if (!S) return Early;

	// The builder writes into the asset's own document, so the asset is already
	// a loadable, interface-valid, silent MetaSound. Only the save is left, and
	// it goes through the builder's own save.
	TSharedPtr<FJsonObject> Res = Create.Finish(/*bSave=*/false);
	MSAuthorSave(Res, AssetPath);
	Res->SetBoolField(TEXT("oneShot"), S->bOneShot);
	Res->SetNumberField(TEXT("audioOutputs"), S->AudioOuts.Num());
	Res->SetStringField(TEXT("note"), TEXT("Builder attached to the asset's own document. Author with metasound_* actions (or use metasound_author to stamp a whole graph); metasound_build saves. audio(metasound_read_document) reads back what has been authored so far."));
	return MCPResult(Res);
=====
	FMSSession* S = OpenBuilderSession(Params, AssetPath, Early);
	if (!S) return Early;

	UMetaSoundSourceBuilder* B = S->Builder.Get();
-----
	FMCPCreate Create(Params, { TEXT("MetaSoundSource"), TEXT("/Game/Audio/MetaSounds") });
	FMSSession* S = OpenBuilderSession(Params, Create, AssetPath, Early);
	if (!S) return Early;

	UMetaSoundSourceBuilder* B = S->Builder.Get();
