	static TSharedPtr<FJsonValue> CreateAssetByClass(const TSharedPtr<FJsonObject>& Params);
-----
	static TSharedPtr<FJsonValue> CreateAssetByClass(const TSharedPtr<FJsonObject>& Params);
	// The one implementation behind create_asset_by_class and create_data_asset (Base UDataAsset).
	static TSharedPtr<FJsonValue> CreateAssetOfClass(const TSharedPtr<FJsonObject>& Params, UClass* Base);
