	// CreateUserDefinedEnum starts empty (only the trailing _MAX). Populate it.
	TArray<FString> DisplayNames;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (TryGetArrayParam(Params, TEXT("values"), Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& V : *Values)
		{
			FString S;
			if (V->TryGetString(S) && !S.IsEmpty()) DisplayNames.Add(S);
		}
	}
-----
	// CreateUserDefinedEnum starts empty (only the trailing _MAX). Populate it
	// from names, or {name, displayName} objects.
	TArray<FString> DisplayNames;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (TryGetArrayParam(Params, TEXT("values"), Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& V : *Values)
		{
			FString S;
			if (!V->TryGetString(S) && V->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> Obj = V->AsObject();
				if (!Obj->TryGetStringField(TEXT("displayName"), S)) Obj->TryGetStringField(TEXT("name"), S);
			}
			if (!S.IsEmpty()) DisplayNames.Add(S);
		}
	}
