	// Shared flow used by every GAS "create blueprint by parent class" handler.
	// Requires `name`, reads `packagePath` / `onConflict`, runs the existence
	// check + asset-tools create + compile + save + rollback record, and
	// invokes ExtraResultFields (if provided) to stamp handler-specific fields
	// onto the result before returning.
	static TSharedPtr<FJsonValue> CreateGasBlueprint(
		const TSharedPtr<FJsonObject>& Params,
		const FString& DefaultPackagePath,
		class UClass* ParentClass,
		const FString& FriendlyType,
		TFunction<void(TSharedPtr<FJsonObject>&)> ExtraResultFields = nullptr,
		TFunction<void(class UBlueprint*)> ConfigureBeforeSave = nullptr);

-----
