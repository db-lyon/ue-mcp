// Environment Query System authoring and execution.
//
// The bridge could create an EQS asset and list the ones that exist. A query
// with no generator produces no items, and a query with no tests scores
// nothing, so what it could create was an empty asset: every Behavior Tree
// that depends on a query was blocked behind hand-editing in the editor.
//
// Three of these six actions are the authoring half and three are the half
// that makes the authoring checkable. That split is deliberate. An EQS query
// is not something you can eyeball for correctness, so a surface that could
// build one without running it would mostly produce queries nobody could
// trust.
//
// Configuration of a generator or a test is NOT here. Every tunable on them is
// a UPROPERTY, read_eqs_query returns the object path of each one, and
// editor(set_property) writes a property at an object path. Adding thirty
// typed setters would be a second way to do what already works.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"

namespace
{
	/** Load an EQS asset by content path, with the object-path retry the rest
	 *  of this bridge accepts. */
	UEnvQuery* LoadQuery(const FString& Path, TSharedPtr<FJsonValue>& OutError)
	{
		UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *Path);
		if (!Query && !Path.Contains(TEXT(".")))
		{
			FString AssetName;
			Path.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			Query = LoadObject<UEnvQuery>(nullptr, *(Path + TEXT(".") + AssetName));
		}
		if (!Query)
		{
			OutError = MCPError(FString::Printf(
				TEXT("EnvQuery not found: %s. Create one with gameplay(create_eqs_query), or list "
					 "the existing ones with gameplay(list_eqs_queries)."), *Path));
		}
		return Query;
	}

	/**
	 * Resolve a class by short name or path within a base class.
	 *
	 * EQS class names are long (EnvQueryGenerator_ActorsOfClass), so the short
	 * spelling without the prefix is accepted too: "ActorsOfClass" resolves,
	 * because that is what the editor's own dropdown shows.
	 */
	UClass* ResolveEqsClass(const FString& Spec, UClass* Base, TSharedPtr<FJsonValue>& OutError)
	{
		if (UClass* Direct = FindObject<UClass>(nullptr, *Spec))
		{
			if (Direct->IsChildOf(Base)) return Direct;
		}
		if (UClass* Loaded = LoadObject<UClass>(nullptr, *Spec))
		{
			if (Loaded->IsChildOf(Base)) return Loaded;
		}

		TArray<FString> Available;
		UClass* ShortMatch = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (Candidate == Base || !Candidate->IsChildOf(Base)) continue;
			if (Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) continue;
			const FString Name = Candidate->GetName();
			Available.Add(Name);
			FString Suffix = Name;
			int32 Underscore;
			if (Name.FindChar(TEXT('_'), Underscore)) Suffix = Name.RightChop(Underscore + 1);
			if (Name == Spec || Suffix == Spec) ShortMatch = Candidate;
		}
		if (ShortMatch) return ShortMatch;

		Available.Sort();
		OutError = MCPError(FString::Printf(
			TEXT("No %s named '%s'. Available: %s. gameplay(list_eqs_types) lists these with their full names."),
			*Base->GetName(), *Spec, *FString::Join(Available, TEXT(", "))));
		return nullptr;
	}

	/** One test, as JSON, including the object path that makes it editable. */
	TSharedPtr<FJsonObject> DescribeTest(const UEnvQueryTest* Test, int32 Index)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("index"), Index);
		Out->SetStringField(TEXT("class"), Test->GetClass()->GetName());
		// The whole reason no typed setters exist: this path plus
		// editor(set_property) configures every tunable on the test.
		Out->SetStringField(TEXT("objectPath"), Test->GetPathName());

		const UEnum* PurposeEnum = StaticEnum<EEnvTestPurpose::Type>();
		Out->SetStringField(TEXT("purpose"), PurposeEnum
			? PurposeEnum->GetNameStringByValue(Test->TestPurpose)
			: FString::FromInt(Test->TestPurpose));

		const UEnum* FilterEnum = StaticEnum<EEnvTestFilterType::Type>();
		Out->SetStringField(TEXT("filterType"), FilterEnum
			? FilterEnum->GetNameStringByValue(Test->FilterType)
			: FString::FromInt(Test->FilterType));

		const UEnum* EquationEnum = StaticEnum<EEnvTestScoreEquation::Type>();
		Out->SetStringField(TEXT("scoringEquation"), EquationEnum
			? EquationEnum->GetNameStringByValue(Test->ScoringEquation)
			: FString::FromInt(Test->ScoringEquation));

		if (!Test->TestComment.IsEmpty()) Out->SetStringField(TEXT("comment"), Test->TestComment);
		return Out;
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::ListEqsTypes(const TSharedPtr<FJsonObject>& Params)
{
	const FString Filter = OptionalString(Params, TEXT("filter"), TEXT(""));

	// T3: paged. Three class groups come back here, so ONE cursor pages ONE
	// collection: every row, each tagged with its `kind`, under `types`. The
	// three familiar arrays are still emitted and hold this page's rows of that
	// kind, while the counts stay counts of the WHOLE listing.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_eqs_types|filter=%s"), *Filter),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	const auto Collect = [&Filter](UClass* Base, const TCHAR* Kind)
	{
		TArray<MCPPagination::FPageRow> Out;
		TArray<UClass*> Found;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (Candidate == Base || !Candidate->IsChildOf(Base)) continue;
			if (Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) continue;
			if (!Filter.IsEmpty() && !Candidate->GetName().Contains(Filter)) continue;
			Found.Add(Candidate);
		}
		// TObjectIterator walks the object hash, whose order is not a contract,
		// so the group is sorted before paging. The sort is by class PATH, not
		// by short name: two modules can each declare an EnvQueryTest_Distance
		// and only the path separates them, which is also what the page anchors
		// on.
		Found.Sort([](const UClass& A, const UClass& B) { return A.GetPathName() < B.GetPathName(); });
		for (UClass* Candidate : Found)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const FString Name = Candidate->GetName();
			Entry->SetStringField(TEXT("name"), Name);
			// The short spelling is what the editor's dropdown shows and what
			// add_eqs_generator / add_eqs_test accept.
			int32 Underscore;
			Entry->SetStringField(TEXT("shortName"),
				Name.FindChar(TEXT('_'), Underscore) ? Name.RightChop(Underscore + 1) : Name);
			Entry->SetStringField(TEXT("path"), Candidate->GetPathName());
			Entry->SetStringField(TEXT("kind"), Kind);
			Out.Add({ Candidate->GetPathName(), MakeShared<FJsonValueObject>(Entry) });
		}
		return Out;
	};

	const TArray<MCPPagination::FPageRow> Generators = Collect(UEnvQueryGenerator::StaticClass(), TEXT("generator"));
	const TArray<MCPPagination::FPageRow> Tests = Collect(UEnvQueryTest::StaticClass(), TEXT("test"));
	const TArray<MCPPagination::FPageRow> Contexts = Collect(UEnvQueryContext::StaticClass(), TEXT("context"));

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("generatorCount"), Generators.Num());
	Result->SetNumberField(TEXT("testCount"), Tests.Num());
	Result->SetNumberField(TEXT("contextCount"), Contexts.Num());

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(Generators.Num() + Tests.Num() + Contexts.Num());
	Rows.Append(Generators);
	Rows.Append(Tests);
	Rows.Append(Contexts);
	MCPPagination::EmitPage(Page, Rows, TEXT("types"), Result);

	// This page's rows, regrouped the way this action always reported them.
	TArray<TSharedPtr<FJsonValue>> PageGenerators, PageTests, PageContexts;
	for (const TSharedPtr<FJsonValue>& Row : Result->GetArrayField(TEXT("types")))
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Row.IsValid() || !Row->TryGetObject(Obj) || !Obj || !Obj->IsValid()) continue;
		FString Kind;
		(*Obj)->TryGetStringField(TEXT("kind"), Kind);
		if (Kind == TEXT("generator")) PageGenerators.Add(Row);
		else if (Kind == TEXT("test")) PageTests.Add(Row);
		else if (Kind == TEXT("context")) PageContexts.Add(Row);
	}
	Result->SetArrayField(TEXT("generators"), PageGenerators);
	Result->SetArrayField(TEXT("tests"), PageTests);
	Result->SetArrayField(TEXT("contexts"), PageContexts);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ReadEqsQuery(const TSharedPtr<FJsonObject>& Params)
{
	FString QueryPath;
	if (auto Err = RequireString(Params, TEXT("queryPath"), QueryPath)) return Err;

	TSharedPtr<FJsonValue> Error;
	UEnvQuery* Query = LoadQuery(QueryPath, Error);
	if (!Query) return Error;

	TArray<TSharedPtr<FJsonValue>> Options;
	TArray<FString> Problems;
	int32 TotalTests = 0;
	int32 ScoringTests = 0;

	const TArray<UEnvQueryOption*>& QueryOptions = Query->GetOptions();
	for (int32 i = 0; i < QueryOptions.Num(); i++)
	{
		const UEnvQueryOption* Option = QueryOptions[i];
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), i);

		if (!Option)
		{
			Problems.Add(FString::Printf(TEXT("option %d is null"), i));
			Options.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}
		Entry->SetStringField(TEXT("objectPath"), Option->GetPathName());

		if (Option->Generator)
		{
			Entry->SetStringField(TEXT("generator"), Option->Generator->GetClass()->GetName());
			Entry->SetStringField(TEXT("generatorObjectPath"), Option->Generator->GetPathName());
		}
		else
		{
			// The single most common reason a query returns nothing.
			Problems.Add(FString::Printf(
				TEXT("option %d has no generator, so it can produce no items"), i));
		}

		TArray<TSharedPtr<FJsonValue>> Tests;
		for (int32 t = 0; t < Option->Tests.Num(); t++)
		{
			const UEnvQueryTest* Test = Option->Tests[t];
			if (!Test)
			{
				Problems.Add(FString::Printf(TEXT("option %d test %d is null"), i, t));
				continue;
			}
			TotalTests++;
			if (Test->TestPurpose != EEnvTestPurpose::Filter) ScoringTests++;
			Tests.Add(MakeShared<FJsonValueObject>(DescribeTest(Test, t)));
		}
		Entry->SetArrayField(TEXT("tests"), Tests);
		Options.Add(MakeShared<FJsonValueObject>(Entry));
	}

	if (QueryOptions.Num() == 0)
	{
		Problems.Add(TEXT("the query has no options at all: add one with gameplay(add_eqs_generator)"));
	}
	if (TotalTests > 0 && ScoringTests == 0)
	{
		// Every item scores the same, so "best" is arbitrary. Worth saying,
		// because the query still runs and still returns items.
		Problems.Add(TEXT("every test is filter-only, so all surviving items score equally and "
						  "the 'best' item is arbitrary"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("queryPath"), Query->GetPathName());
	Result->SetStringField(TEXT("queryName"), Query->GetQueryName().ToString());
	Result->SetNumberField(TEXT("optionCount"), QueryOptions.Num());
	Result->SetNumberField(TEXT("testCount"), TotalTests);
	Result->SetArrayField(TEXT("options"), Options);

	TArray<TSharedPtr<FJsonValue>> ProblemList;
	for (const FString& P : Problems) ProblemList.Add(MakeShared<FJsonValueString>(P));
	Result->SetArrayField(TEXT("problems"), ProblemList);
	Result->SetBoolField(TEXT("runnable"), Problems.Num() == 0);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddEqsGenerator(const TSharedPtr<FJsonObject>& Params)
{
	FString QueryPath, GeneratorSpec;
	if (auto Err = RequireString(Params, TEXT("queryPath"), QueryPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("generatorClass"), GeneratorSpec)) return Err;

	TSharedPtr<FJsonValue> Error;
	UEnvQuery* Query = LoadQuery(QueryPath, Error);
	if (!Query) return Error;

	UClass* GeneratorClass = ResolveEqsClass(GeneratorSpec, UEnvQueryGenerator::StaticClass(), Error);
	if (!GeneratorClass) return Error;

	// An option owns one generator, so a second option with the same generator
	// class is a real thing to want (two donuts at different radii) and is not
	// treated as a duplicate.
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
	UEnvQueryGenerator* Generator = NewObject<UEnvQueryGenerator>(Query, GeneratorClass);
	Option->Generator = Generator;
	Query->GetOptionsMutable().Add(Option);

	Query->MarkPackageDirty();
	SaveAssetPackage(Query);

	const int32 AddedIndex = Query->GetOptions().Num() - 1;

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("queryPath"), Query->GetPathName());
	Result->SetNumberField(TEXT("optionIndex"), AddedIndex);
	Result->SetStringField(TEXT("generator"), GeneratorClass->GetName());
	// Configure it from here with editor(set_property).
	Result->SetStringField(TEXT("generatorObjectPath"), Generator->GetPathName());
	Result->SetStringField(TEXT("optionObjectPath"), Option->GetPathName());
	Result->SetStringField(TEXT("note"), TEXT(
		"Tune the generator with editor(set_property) at generatorObjectPath. "
		"gameplay(read_eqs_query) lists what it now holds."));

	// Deliberately NOT idempotent: two options with the same generator class is
	// a real thing to want. But a caller who did not mean to duplicate one has
	// to be able to see that they did, so the siblings are named.
	TArray<TSharedPtr<FJsonValue>> Siblings;
	for (int32 i = 0; i < Query->GetOptions().Num(); i++)
	{
		if (i == AddedIndex) continue;
		const UEnvQueryOption* Other = Query->GetOptions()[i];
		if (Other && Other->Generator && Other->Generator->GetClass() == GeneratorClass)
		{
			Siblings.Add(MakeShared<FJsonValueNumber>(i));
		}
	}
	if (Siblings.Num() > 0)
	{
		Result->SetArrayField(TEXT("otherOptionsWithSameGenerator"), Siblings);
	}

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("queryPath"), Query->GetPathName());
	RollbackPayload->SetNumberField(TEXT("optionIndex"), AddedIndex);
	MCPSetRollback(Result, TEXT("remove_eqs_option"), RollbackPayload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddEqsTest(const TSharedPtr<FJsonObject>& Params)
{
	FString QueryPath, TestSpec;
	if (auto Err = RequireString(Params, TEXT("queryPath"), QueryPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("testClass"), TestSpec)) return Err;

	TSharedPtr<FJsonValue> Error;
	UEnvQuery* Query = LoadQuery(QueryPath, Error);
	if (!Query) return Error;

	UClass* TestClass = ResolveEqsClass(TestSpec, UEnvQueryTest::StaticClass(), Error);
	if (!TestClass) return Error;

	const int32 OptionIndex = static_cast<int32>(OptionalNumber(Params, TEXT("optionIndex"), 0.0));
	TArray<TObjectPtr<UEnvQueryOption>>& Options = Query->GetOptionsMutable();
	if (!Options.IsValidIndex(OptionIndex) || !Options[OptionIndex])
	{
		return MCPError(FString::Printf(
			TEXT("Option %d does not exist on '%s' (it has %d). Add a generator first with "
				 "gameplay(add_eqs_generator); every test belongs to an option."),
			OptionIndex, *QueryPath, Options.Num()));
	}

	UEnvQueryTest* Test = NewObject<UEnvQueryTest>(Query, TestClass);

	const FString Purpose = OptionalString(Params, TEXT("purpose"), TEXT(""));
	if (!Purpose.IsEmpty())
	{
		if (Purpose.Equals(TEXT("filter"), ESearchCase::IgnoreCase))
		{
			Test->TestPurpose = EEnvTestPurpose::Filter;
		}
		else if (Purpose.Equals(TEXT("score"), ESearchCase::IgnoreCase))
		{
			Test->TestPurpose = EEnvTestPurpose::Score;
		}
		else if (Purpose.Equals(TEXT("both"), ESearchCase::IgnoreCase)
			|| Purpose.Equals(TEXT("filterandscore"), ESearchCase::IgnoreCase))
		{
			Test->TestPurpose = EEnvTestPurpose::FilterAndScore;
		}
		else
		{
			return MCPError(FString::Printf(
				TEXT("purpose '%s' is not one of: filter, score, both."), *Purpose));
		}
	}

	Options[OptionIndex]->Tests.Add(Test);
	Query->MarkPackageDirty();
	SaveAssetPackage(Query);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("queryPath"), Query->GetPathName());
	Result->SetNumberField(TEXT("optionIndex"), OptionIndex);
	Result->SetNumberField(TEXT("testIndex"), Options[OptionIndex]->Tests.Num() - 1);
	Result->SetStringField(TEXT("test"), TestClass->GetName());
	Result->SetStringField(TEXT("testObjectPath"), Test->GetPathName());
	Result->SetStringField(TEXT("note"), TEXT(
		"Tune the test with editor(set_property) at testObjectPath: ScoringFactor, FilterType, "
		"FloatValueMin/Max, ScoringEquation and the test's own parameters are all UPROPERTYs."));

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("queryPath"), Query->GetPathName());
	RollbackPayload->SetNumberField(TEXT("optionIndex"), OptionIndex);
	RollbackPayload->SetNumberField(TEXT("testIndex"), Options[OptionIndex]->Tests.Num() - 1);
	MCPSetRollback(Result, TEXT("remove_eqs_test"), RollbackPayload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RemoveEqsTest(const TSharedPtr<FJsonObject>& Params)
{
	FString QueryPath;
	if (auto Err = RequireString(Params, TEXT("queryPath"), QueryPath)) return Err;

	TSharedPtr<FJsonValue> Error;
	UEnvQuery* Query = LoadQuery(QueryPath, Error);
	if (!Query) return Error;

	const int32 OptionIndex = static_cast<int32>(OptionalNumber(Params, TEXT("optionIndex"), 0.0));
	TArray<TObjectPtr<UEnvQueryOption>>& Options = Query->GetOptionsMutable();
	if (!Options.IsValidIndex(OptionIndex) || !Options[OptionIndex])
	{
		return MCPError(FString::Printf(TEXT("Option %d does not exist (the query has %d)."),
			OptionIndex, Options.Num()));
	}

	TArray<TObjectPtr<UEnvQueryTest>>& Tests = Options[OptionIndex]->Tests;
	const int32 TestIndex = static_cast<int32>(OptionalNumber(Params, TEXT("testIndex"), -1.0));
	if (TestIndex < 0)
	{
		return MCPError(FString::Printf(
			TEXT("Missing 'testIndex'. Option %d has %d tests; gameplay(read_eqs_query) lists "
				 "them with their indices."), OptionIndex, Tests.Num()));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("queryPath"), Query->GetPathName());
	Result->SetNumberField(TEXT("optionIndex"), OptionIndex);

	// Idempotent: an index past the end is work already done, not an error, so
	// replaying a rollback is safe.
	if (!Tests.IsValidIndex(TestIndex))
	{
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetNumberField(TEXT("remainingTests"), Tests.Num());
		return MCPResult(Result);
	}

	const UEnvQueryTest* Doomed = Tests[TestIndex];
	const FString Removed = Doomed ? Doomed->GetClass()->GetName() : TEXT("null");
	const UEnum* PurposeEnum = StaticEnum<EEnvTestPurpose::Type>();
	const FString RemovedPurpose = (Doomed && PurposeEnum)
		? PurposeEnum->GetNameStringByValue(Doomed->TestPurpose)
		: FString();

	Tests.RemoveAt(TestIndex);
	Query->MarkPackageDirty();
	SaveAssetPackage(Query);

	Result->SetStringField(TEXT("removed"), Removed);
	// Removing shifts every later index down, and a caller removing several
	// tests in one pass has to know that.
	Result->SetNumberField(TEXT("remainingTests"), Tests.Num());
	Result->SetStringField(TEXT("note"), TEXT(
		"Test indices after the removed one have shifted down by one."));

	if (Doomed)
	{
		// The inverse restores the test's CLASS and purpose, not the values
		// tuned on it, because those live on the object that just went away.
		// Saying so is the difference between a rollback a caller can trust and
		// one that quietly loses configuration.
		TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
		RollbackPayload->SetStringField(TEXT("queryPath"), Query->GetPathName());
		RollbackPayload->SetNumberField(TEXT("optionIndex"), OptionIndex);
		RollbackPayload->SetStringField(TEXT("testClass"), Removed);
		if (!RemovedPurpose.IsEmpty()) RollbackPayload->SetStringField(TEXT("purpose"), RemovedPurpose);
		MCPSetRollback(Result, TEXT("add_eqs_test"), RollbackPayload);

		Result->SetBoolField(TEXT("rollbackRestoresClassOnly"), true);
		Result->SetStringField(TEXT("rollbackNote"), TEXT(
			"The rollback re-adds this test's class and purpose at the END of the option. Values "
			"tuned on the removed test, and its position in the order, are not restored."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RemoveEqsOption(const TSharedPtr<FJsonObject>& Params)
{
	FString QueryPath;
	if (auto Err = RequireString(Params, TEXT("queryPath"), QueryPath)) return Err;

	TSharedPtr<FJsonValue> Error;
	UEnvQuery* Query = LoadQuery(QueryPath, Error);
	if (!Query) return Error;

	const int32 OptionIndex = static_cast<int32>(OptionalNumber(Params, TEXT("optionIndex"), -1.0));
	if (OptionIndex < 0)
	{
		return MCPError(TEXT("Missing 'optionIndex'. gameplay(read_eqs_query) lists the options with their indices."));
	}

	TArray<TObjectPtr<UEnvQueryOption>>& Options = Query->GetOptionsMutable();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("queryPath"), Query->GetPathName());
	Result->SetNumberField(TEXT("optionIndex"), OptionIndex);

	// Idempotent, like every other remove here, so a rollback replays safely.
	if (!Options.IsValidIndex(OptionIndex))
	{
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetNumberField(TEXT("remainingOptions"), Options.Num());
		return MCPResult(Result);
	}

	const UEnvQueryOption* Doomed = Options[OptionIndex];
	const FString GeneratorName = (Doomed && Doomed->Generator)
		? Doomed->Generator->GetClass()->GetName() : FString(TEXT("none"));
	const int32 LostTests = Doomed ? Doomed->Tests.Num() : 0;

	Options.RemoveAt(OptionIndex);
	Query->MarkPackageDirty();
	SaveAssetPackage(Query);

	Result->SetStringField(TEXT("removedGenerator"), GeneratorName);
	Result->SetNumberField(TEXT("removedTests"), LostTests);
	Result->SetNumberField(TEXT("remainingOptions"), Options.Num());
	Result->SetStringField(TEXT("note"), TEXT(
		"Option indices after the removed one have shifted down by one. Removing an option also "
		"removes every test on it."));

	if (Doomed && Doomed->Generator)
	{
		TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
		RollbackPayload->SetStringField(TEXT("queryPath"), Query->GetPathName());
		RollbackPayload->SetStringField(TEXT("generatorClass"), GeneratorName);
		MCPSetRollback(Result, TEXT("add_eqs_generator"), RollbackPayload);

		Result->SetBoolField(TEXT("rollbackRestoresGeneratorOnly"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
			"The rollback restores an option with this generator class at the END of the query. "
			"Generator tuning, and the %d test(s) that were on it, are not restored."), LostTests));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ReorderEqsTests(const TSharedPtr<FJsonObject>& Params)
{
	FString QueryPath;
	if (auto Err = RequireString(Params, TEXT("queryPath"), QueryPath)) return Err;

	TSharedPtr<FJsonValue> Error;
	UEnvQuery* Query = LoadQuery(QueryPath, Error);
	if (!Query) return Error;

	const int32 OptionIndex = static_cast<int32>(OptionalNumber(Params, TEXT("optionIndex"), 0.0));
	TArray<TObjectPtr<UEnvQueryOption>>& Options = Query->GetOptionsMutable();
	if (!Options.IsValidIndex(OptionIndex) || !Options[OptionIndex])
	{
		return MCPError(FString::Printf(TEXT("Option %d does not exist (the query has %d)."),
			OptionIndex, Options.Num()));
	}

	const TArray<TSharedPtr<FJsonValue>>* OrderJson = nullptr;
	if (!Params->TryGetArrayField(TEXT("order"), OrderJson) || !OrderJson)
	{
		return MCPError(TEXT("Missing 'order': an array of the current test indices, in the order you want them."));
	}

	TArray<TObjectPtr<UEnvQueryTest>>& Tests = Options[OptionIndex]->Tests;
	// A permutation, verified before anything moves: a partial reorder would
	// drop tests, and the caller would not see which.
	TArray<int32> Order;
	for (const TSharedPtr<FJsonValue>& Value : *OrderJson)
	{
		int32 Index = 0;
		if (!Value.IsValid() || !Value->TryGetNumber(Index)) continue;
		Order.Add(Index);
	}
	if (Order.Num() != Tests.Num())
	{
		return MCPError(FString::Printf(
			TEXT("'order' has %d entries but option %d has %d tests. It must be a permutation of "
				 "every current index, so no test is silently dropped."),
			Order.Num(), OptionIndex, Tests.Num()));
	}
	TArray<int32> Seen = Order;
	Seen.Sort();
	for (int32 i = 0; i < Seen.Num(); i++)
	{
		if (Seen[i] != i)
		{
			return MCPError(FString::Printf(
				TEXT("'order' is not a permutation of 0..%d: index %d is missing or repeated."),
				Tests.Num() - 1, i));
		}
	}

	TArray<TObjectPtr<UEnvQueryTest>> Reordered;
	Reordered.Reserve(Tests.Num());
	for (int32 Index : Order) Reordered.Add(Tests[Index]);
	Tests = Reordered;

	Query->MarkPackageDirty();
	SaveAssetPackage(Query);

	TArray<TSharedPtr<FJsonValue>> Names;
	for (const UEnvQueryTest* Test : Tests)
	{
		Names.Add(MakeShared<FJsonValueString>(Test ? Test->GetClass()->GetName() : TEXT("null")));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("queryPath"), Query->GetPathName());
	Result->SetNumberField(TEXT("optionIndex"), OptionIndex);
	Result->SetArrayField(TEXT("tests"), Names);

	// The inverse of a permutation is exact and cheap: Inverse[Order[i]] = i.
	TArray<int32> Inverse;
	Inverse.SetNum(Order.Num());
	for (int32 i = 0; i < Order.Num(); i++) Inverse[Order[i]] = i;
	TArray<TSharedPtr<FJsonValue>> InverseJson;
	for (int32 Index : Inverse) InverseJson.Add(MakeShared<FJsonValueNumber>(Index));

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("queryPath"), Query->GetPathName());
	RollbackPayload->SetNumberField(TEXT("optionIndex"), OptionIndex);
	RollbackPayload->SetArrayField(TEXT("order"), InverseJson);
	MCPSetRollback(Result, TEXT("reorder_eqs_tests"), RollbackPayload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RunEqsQuery(const TSharedPtr<FJsonObject>& Params)
{
	FString QueryPath;
	if (auto Err = RequireString(Params, TEXT("queryPath"), QueryPath)) return Err;

	TSharedPtr<FJsonValue> Error;
	UEnvQuery* Query = LoadQuery(QueryPath, Error);
	if (!Query) return Error;

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("auto"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World '%s' not available."), *WorldScope));
	}

	UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(World);
	if (!Manager)
	{
		return MCPError(TEXT(
			"This world has no EnvQueryManager. EQS runs in a world with an AI system, which the "
			"pure editor world does not have; run this in PIE (world=\"pie\")."));
	}

	// A query needs a querier. Without one, contexts relative to the querier
	// (Querier, and anything derived from it) have nothing to resolve against,
	// which is a silent empty result rather than an error.
	AActor* Querier = nullptr;
	if (Params->HasField(TEXT("querierLabel")) || Params->HasField(TEXT("querierPath")))
	{
		FMCPActorSelector Selector;
		Selector.LabelKey = TEXT("querierLabel");
		Selector.PathKey = TEXT("querierPath");
		Selector.Match = EMCPActorMatch::LabelNameOrPath;
		Querier = MCPResolveActor(World, Params, Error, Selector);
		if (!Querier) return Error;
	}

	const FString ModeName = OptionalString(Params, TEXT("runMode"), TEXT("all"));
	EEnvQueryRunMode::Type Mode = EEnvQueryRunMode::AllMatching;
	if (ModeName.Equals(TEXT("best"), ESearchCase::IgnoreCase)) Mode = EEnvQueryRunMode::SingleResult;
	else if (ModeName.Equals(TEXT("random"), ESearchCase::IgnoreCase)) Mode = EEnvQueryRunMode::RandomBest25Pct;
	else if (!ModeName.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		return MCPError(FString::Printf(TEXT("runMode '%s' is not one of: all, best, random."), *ModeName));
	}

	FEnvQueryRequest Request(Query, Querier ? static_cast<UObject*>(Querier) : static_cast<UObject*>(World));
	Request.SetWorldOverride(World);

	TSharedPtr<FEnvQueryResult> QueryResult = Manager->RunInstantQuery(Request, Mode);
	if (!QueryResult.IsValid())
	{
		return MCPError(TEXT("The query did not produce a result. Check gameplay(read_eqs_query) "
							 "for an option with no generator."));
	}

	const int32 Limit = static_cast<int32>(OptionalNumber(Params, TEXT("limit"), 50.0));
	TArray<TSharedPtr<FJsonValue>> Items;
	for (int32 i = 0; i < QueryResult->Items.Num() && Items.Num() < Limit; i++)
	{
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("score"), QueryResult->GetItemScore(i));

		const FVector Location = QueryResult->GetItemAsLocation(i);
		TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
		Loc->SetNumberField(TEXT("x"), Location.X);
		Loc->SetNumberField(TEXT("y"), Location.Y);
		Loc->SetNumberField(TEXT("z"), Location.Z);
		Item->SetObjectField(TEXT("location"), Loc);

		if (AActor* ItemActor = QueryResult->GetItemAsActor(i))
		{
			Item->SetStringField(TEXT("actorLabel"), ItemActor->GetActorLabel());
			Item->SetStringField(TEXT("actorPath"), ItemActor->GetPathName());
		}
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("queryPath"), Query->GetPathName());
	Result->SetStringField(TEXT("world"), World->IsPlayInEditor() ? TEXT("pie") : TEXT("editor"));
	Result->SetStringField(TEXT("runMode"), ModeName);
	if (Querier) Result->SetStringField(TEXT("querier"), Querier->GetActorLabel());
	Result->SetBoolField(TEXT("successful"), QueryResult->IsSuccessful());
	Result->SetNumberField(TEXT("itemCount"), QueryResult->Items.Num());
	Result->SetNumberField(TEXT("returned"), Items.Num());
	Result->SetArrayField(TEXT("items"), Items);

	// An empty result is the normal EQS failure and it is not an error, so it
	// has to explain itself or the caller has nothing to act on.
	if (QueryResult->Items.Num() == 0)
	{
		Result->SetStringField(TEXT("note"), TEXT(
			"No items. Either the generator produced none (check its extents and the querier's "
			"location), or every test with a Filter purpose rejected them all. runMode 'all' returns "
			"only items that pass the filters, so a filter whose range is left at its default rejects "
			"nearly everything; 'best' ranks instead and will still return an item. "
			"gameplay(read_eqs_query) lists every test with its purpose and its objectPath, and "
			"editor(set_property) tunes the range."));
	}
	return MCPResult(Result);
}
