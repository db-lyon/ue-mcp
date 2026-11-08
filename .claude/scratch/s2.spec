	// Helper to create a blueprint with a given parent class
	static TSharedPtr<FJsonValue> CreateBlueprintWithParent(const FString& Name, const FString& PackagePath, const FString& ParentClassPath, const FString& FriendlyTypeName);
-----
	// The five framework creators: a Blueprint subclass of the class at BasePath.
	static TSharedPtr<FJsonValue> CreateFrameworkBlueprint(const TSharedPtr<FJsonObject>& Params, const TCHAR* BasePath, const TCHAR* FriendlyType);
