// blueprint(compare_class_defaults): which default values differ between two
// classes' default objects.
//
// The question it answers is "does this Blueprint subclass silently override a
// native default", and get_cdo_properties could only answer it by dumping both
// classes and diffing the dumps by hand. Default subobjects (the components a
// native constructor creates) are compared field by field, because a Blueprint
// edit to an inherited native component lands on its own copy of that
// subobject, and comparing the two copies as object references would report
// every component as different.

#include "BlueprintHandlers.h"
#include "HandlerUtils.h"
#include "Engine/Blueprint.h"
#include "UObject/UnrealType.h"

namespace
{
	/** The class a spec names: whatever MCPResolveClass finds, else the
	 *  generated class of a Blueprint asset at that path. */
	UClass* MCPClassDefaultsResolve(const FString& Spec)
	{
		if (UClass* Class = MCPResolveClass(Spec)) return Class;
		if (Spec.StartsWith(TEXT("/")))
		{
			if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Spec, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Blueprint->GeneratedClass;
			}
		}
		return nullptr;
	}

	struct FMCPClassDefaultsQuery
	{
		TSet<FString> TopLevelNames; // lower-cased
		FString PathFilter;
		FString Category;
		bool bIncludeTransient = false;
		int32 SubobjectDepth = 1;
	};

	struct FMCPClassDefaultsOutput
	{
		TArray<TSharedPtr<FJsonValue>> Differences;
		int32 Compared = 0;
	};

	FString MCPClassDefaultsExport(const FProperty* Property, const void* Container, UObject* Owner)
	{
		FString Text;
		// ExportTextItem, not ExportText: the latter writes nothing for a zero value.
		Property->ExportTextItem_InContainer(Text, Container, nullptr, Owner, PPF_None);
		if (Property->ArrayDim > 1)
		{
			// A fixed array is one FProperty; export every element so a
			// difference past element 0 is visible.
			Text = TEXT("(") + Text;
			for (int32 Index = 1; Index < Property->ArrayDim; ++Index)
			{
				FString Element;
				Property->ExportTextItem_Direct(Element, Property->ContainerPtrToValuePtr<void>(Container, Index), nullptr, Owner, PPF_None);
				Text += TEXT(",") + Element;
			}
			Text += TEXT(")");
		}
		return Text;
	}

	/** True when both values are default subobjects that stand for the same
	 *  component: same class, same name. Those are compared field by field. */
	bool MCPClassDefaultsMatchingSubobjects(const FProperty* Property, const void* ContainerA, const void* ContainerB,
		UObject*& OutA, UObject*& OutB)
	{
		const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
		if (!ObjectProperty || Property->ArrayDim != 1) return false;
		OutA = ObjectProperty->GetObjectPropertyValue_InContainer(ContainerA);
		OutB = ObjectProperty->GetObjectPropertyValue_InContainer(ContainerB);
		return OutA && OutB && OutA != OutB
			&& OutA->IsDefaultSubobject() && OutB->IsDefaultSubobject()
			&& OutA->GetClass() == OutB->GetClass() && OutA->GetFName() == OutB->GetFName();
	}

	void MCPClassDefaultsCompare(
		const UStruct* SharedStruct, const UStruct* OtherStruct,
		const void* ContainerA, UObject* OwnerA, const void* ContainerB, UObject* OwnerB,
		const FString& Prefix, int32 Depth, TSet<const UObject*>& Visited,
		const FMCPClassDefaultsQuery& Query, FMCPClassDefaultsOutput& Out)
	{
		for (TFieldIterator<FProperty> It(SharedStruct); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || Property->HasAnyPropertyFlags(CPF_Deprecated)) continue;
			if (!Query.bIncludeTransient && Property->HasAnyPropertyFlags(CPF_Transient)) continue;
			// Only a property both sides declare can be compared; the rest is
			// reported as onlyIn* at the top level.
			if (const UStruct* Owner = Property->GetOwnerStruct(); !Owner || !OtherStruct->IsChildOf(Owner)) continue;

			const FString Name = Property->GetName();
			if (Prefix.IsEmpty() && Query.TopLevelNames.Num() > 0 && !Query.TopLevelNames.Contains(Name.ToLower())) continue;
			const FString Path = Prefix.IsEmpty() ? Name : Prefix + TEXT(".") + Name;

			UObject* SubA = nullptr;
			UObject* SubB = nullptr;
			if (Depth < Query.SubobjectDepth
				&& MCPClassDefaultsMatchingSubobjects(Property, ContainerA, ContainerB, SubA, SubB))
			{
				// Two properties can point at one component (RootComponent and
				// Mesh on a Character); compare it once.
				if (!Visited.Contains(SubA))
				{
					Visited.Add(SubA);
					MCPClassDefaultsCompare(SubA->GetClass(), SubB->GetClass(), SubA, SubA, SubB, SubB, Path, Depth + 1, Visited, Query, Out);
				}
				continue;
			}

			if (!Query.PathFilter.IsEmpty() && !Path.Contains(Query.PathFilter, ESearchCase::IgnoreCase)) continue;
#if WITH_EDITORONLY_DATA
			const FString Category = Property->GetMetaData(TEXT("Category"));
#else
			const FString Category;
#endif
			if (!Query.Category.IsEmpty() && !Category.Contains(Query.Category, ESearchCase::IgnoreCase)) continue;

			++Out.Compared;
			bool bIdentical = true;
			for (int32 Index = 0; Index < Property->ArrayDim && bIdentical; ++Index)
			{
				// DeepCompareDSOsOnly: a nested instanced subobject is compared by
				// its contents, any other reference by identity.
				bIdentical = Property->Identical_InContainer(ContainerA, ContainerB, Index,
					PPF_DeepComparison | PPF_DeepCompareDSOsOnly);
			}
			if (bIdentical) continue;

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Path);
			Row->SetStringField(TEXT("type"), Property->GetCPPType());
			if (!Category.IsEmpty()) Row->SetStringField(TEXT("category"), Category);
			if (const UStruct* Owner = Property->GetOwnerStruct()) Row->SetStringField(TEXT("declaredIn"), Owner->GetName());
			Row->SetStringField(TEXT("value"), MCPClassDefaultsExport(Property, ContainerA, OwnerA));
			Row->SetStringField(TEXT("otherValue"), MCPClassDefaultsExport(Property, ContainerB, OwnerB));
			Out.Differences.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	/** Names of the properties Class declares that Other does not, up to Cap. */
	TArray<TSharedPtr<FJsonValue>> MCPClassDefaultsOnlyIn(const UClass* Class, const UClass* Other, const FMCPClassDefaultsQuery& Query, int32& OutTotal)
	{
		constexpr int32 Cap = 200;
		TArray<TSharedPtr<FJsonValue>> Names;
		OutTotal = 0;
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || Property->HasAnyPropertyFlags(CPF_Deprecated)) continue;
			if (!Query.bIncludeTransient && Property->HasAnyPropertyFlags(CPF_Transient)) continue;
			if (const UStruct* Owner = Property->GetOwnerStruct(); Owner && Other->IsChildOf(Owner)) continue;
			const FString Name = Property->GetName();
			if (Query.TopLevelNames.Num() > 0 && !Query.TopLevelNames.Contains(Name.ToLower())) continue;
			if (!Query.PathFilter.IsEmpty() && !Name.Contains(Query.PathFilter, ESearchCase::IgnoreCase)) continue;
			++OutTotal;
			if (Names.Num() < Cap) Names.Add(MakeShared<FJsonValueString>(Name));
		}
		return Names;
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CompareClassDefaults(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("className"), TEXT("otherClassName"), TEXT("propertyNames"), TEXT("propertyFilter"),
		TEXT("category"), TEXT("includeTransient"), TEXT("subobjectDepth"),
	});

	FString ClassSpec;
	if (auto Err = RequireString(Params, TEXT("className"), ClassSpec)) return Err;
	FString OtherSpec;
	if (auto Err = RequireString(Params, TEXT("otherClassName"), OtherSpec)) return Err;

	FMCPClassDefaultsQuery Query;
	if (HasParam(Params, TEXT("propertyNames")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (!TryGetArrayParam(Params, TEXT("propertyNames"), Names) || !Names)
		{
			return MCPError(TEXT("'propertyNames' must be an array of property names"));
		}
		for (const TSharedPtr<FJsonValue>& Value : *Names)
		{
			FString Name;
			if (!Value.IsValid() || !Value->TryGetString(Name) || Name.TrimStartAndEnd().IsEmpty())
			{
				return MCPError(TEXT("'propertyNames' must be an array of non-empty property names"));
			}
			Query.TopLevelNames.Add(Name.TrimStartAndEnd().ToLower());
		}
	}
	Query.PathFilter = OptionalString(Params, TEXT("propertyFilter")).TrimStartAndEnd();
	Query.Category = OptionalString(Params, TEXT("category")).TrimStartAndEnd();
	Query.bIncludeTransient = OptionalBool(Params, TEXT("includeTransient"), false);
	Query.SubobjectDepth = FMath::Clamp(OptionalInt(Params, TEXT("subobjectDepth"), 1), 0, 3);

	UClass* Class = MCPClassDefaultsResolve(ClassSpec);
	if (!Class) return MCPClassNotFoundError(ClassSpec, TEXT("className"));
	UClass* Other = MCPClassDefaultsResolve(OtherSpec);
	if (!Other) return MCPClassNotFoundError(OtherSpec, TEXT("otherClassName"));

	UObject* DefaultObject = Class->GetDefaultObject();
	UObject* OtherDefaultObject = Other->GetDefaultObject();
	if (!DefaultObject || !OtherDefaultObject)
	{
		return MCPError(FString::Printf(TEXT("No class default object for %s"), *(!DefaultObject ? Class : Other)->GetPathName()));
	}

	// Iterating the first class visits every shared property whichever class is
	// deeper: a shared property is one whose owner both classes derive from.
	FMCPClassDefaultsOutput Out;
	TSet<const UObject*> Visited;
	MCPClassDefaultsCompare(Class, Other, DefaultObject, DefaultObject, OtherDefaultObject, OtherDefaultObject,
		FString(), 0, Visited, Query, Out);

	const UClass* Common = Class;
	while (Common && !Other->IsChildOf(Common)) Common = Common->GetSuperClass();

	int32 OnlyInClassTotal = 0;
	int32 OnlyInOtherTotal = 0;
	TArray<TSharedPtr<FJsonValue>> OnlyInClass = MCPClassDefaultsOnlyIn(Class, Other, Query, OnlyInClassTotal);
	TArray<TSharedPtr<FJsonValue>> OnlyInOther = MCPClassDefaultsOnlyIn(Other, Class, Query, OnlyInOtherTotal);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("className"), Class->GetPathName());
	Result->SetStringField(TEXT("otherClassName"), Other->GetPathName());
	Result->SetStringField(TEXT("relation"),
		Class == Other ? TEXT("same")
		: Other->IsChildOf(Class) ? TEXT("otherDerivesFromClass")
		: Class->IsChildOf(Other) ? TEXT("classDerivesFromOther")
		: TEXT("siblings"));
	if (Common) Result->SetStringField(TEXT("commonAncestor"), Common->GetPathName());
	Result->SetNumberField(TEXT("comparedCount"), Out.Compared);
	Result->SetNumberField(TEXT("differenceCount"), Out.Differences.Num());
	Result->SetArrayField(TEXT("differences"), Out.Differences);
	Result->SetArrayField(TEXT("onlyInClass"), OnlyInClass);
	Result->SetNumberField(TEXT("onlyInClassCount"), OnlyInClassTotal);
	Result->SetArrayField(TEXT("onlyInOtherClass"), OnlyInOther);
	Result->SetNumberField(TEXT("onlyInOtherClassCount"), OnlyInOtherTotal);
	return MCPResult(Result);
}
