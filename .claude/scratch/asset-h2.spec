	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
-----
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

	// #686: create a UserDefinedEnum; reflection(create_enum) is this under another name.
	static TSharedPtr<FJsonValue> CreateUserDefinedEnum(const TSharedPtr<FJsonObject>& Params);

private:
=====
	// #686: UserDefinedEnum authoring (create, list values, add/rename/remove enumerator)
	static TSharedPtr<FJsonValue> CreateUserDefinedEnum(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListEnumValues(const TSharedPtr<FJsonObject>& Params);
-----
	// #686: UserDefinedEnum authoring (list values, add/rename/remove enumerator)
	static TSharedPtr<FJsonValue> ListEnumValues(const TSharedPtr<FJsonObject>& Params);
