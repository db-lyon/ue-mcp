#pragma once

#include "CoreMinimal.h"
#include "DiffUtils.h"
#include "HandlerEngineVersion.h"
#include "UObject/UnrealType.h"

/** The leaf a soft path names on one side of a diff: its property and the address of its value. */
struct FMCPCkDiffLeaf
{
	const FProperty* Property = nullptr;
	const void* Value = nullptr;
	bool IsValid() const { return Property && Value; }
};

namespace MCPCkDiffValuePrivate
{
	/** The property a path element names on Struct, by FName or by authored name (a user-defined struct's members differ). */
	inline const FProperty* FindMember(const UStruct* Struct, const FString& Name)
	{
		if (!Struct) return nullptr;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->GetName() == Name) return *It;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->GetAuthoredName() == Name) return *It;
		}
		return nullptr;
	}

	/** A soft path's elements as display strings: property names, and container indices as digits. */
	inline TArray<FString> Elements(const FPropertySoftPath& Path)
	{
		TArray<FString> Out;
		for (int32 Depth = 1; Depth < 256; ++Depth)
		{
			const FPropertySoftPath Prefix = Path.GetRootProperty(Depth);
			const FString Element = Prefix.ToDisplayName(1);
			if (Element.IsEmpty()) return {};
			Out.Add(Element);
			// GetRootProperty clamps to the path's length, so the next prefix equals this one at the end.
			if (Path.GetRootProperty(Depth + 1) == Prefix) break;
		}
		return Out;
	}
}

/**
 * The value a soft path names inside Data (an instance of Struct), or an invalid leaf
 * when any step of the path does not exist on this side: a member the struct or the
 * referenced object lacks, an index past a container's end, a null object reference.
 *
 * FPropertySoftPath::Resolve is not used for this. When it cannot walk the whole path it
 * stops early and returns the last property it reached with the block it was about to
 * look inside, which is not that property's container: an object property paired with
 * the object it points at, or a struct property paired with its own value. Reading the
 * value from that pair reads unrelated memory as an object pointer, and exporting it
 * crashed the editor (ue-mcp#2020). This walk computes the leaf's own value address and
 * gives up instead of guessing.
 */
inline FMCPCkDiffLeaf MCPCkDiffResolveLeaf(const FPropertySoftPath& Path, const UStruct* Struct, const void* Data)
{
	using namespace MCPCkDiffValuePrivate;
	const TArray<FString> Chain = Elements(Path);
	if (!Struct || !Data || Chain.Num() == 0) return {};

	const UStruct* ContainerStruct = Struct;
	const void* Container = Data;
	const FStructProperty* Enclosing = nullptr;
	const void* EnclosingValue = nullptr;

	for (int32 I = 0; I < Chain.Num(); ++I)
	{
		if (Chain[I].IsNumeric()) return {};
		const FProperty* Property = FindMember(ContainerStruct, Chain[I]);
#if UE_MCP_HAS_5_7_API
		if (!Property && Enclosing && EnclosingValue)
		{
			// An instanced struct holds its members in memory of its own, found by name.
			const FProperty* Inner = nullptr;
			const void* InnerContainer = nullptr;
			if (Enclosing->FindInnerPropertyInstance(FName(*Chain[I]), EnclosingValue, Inner, InnerContainer) && Inner && InnerContainer)
			{
				Property = Inner;
				Container = InnerContainer;
			}
		}
#endif
		if (!Property) return {};

		const void* Value = nullptr;
		if (I + 1 < Chain.Num() && Chain[I + 1].IsNumeric())
		{
			const int32 Index = FCString::Atoi(*Chain[++I]);
			if (Index < 0) return {};
			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Container));
				if (!Helper.IsValidIndex(Index)) return {};
				Value = Helper.GetRawPtr(Index);
				Property = ArrayProperty->Inner;
			}
			else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
			{
				FScriptSetHelper Helper(SetProperty, SetProperty->ContainerPtrToValuePtr<void>(Container));
				if (Index >= Helper.Num()) return {};
				Value = Helper.FindNthElementPtr(Index);
				Property = Helper.GetElementProperty();
			}
			else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
			{
				FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Container));
				if (Index >= Helper.Num()) return {};
				const uint8* Pair = Helper.FindNthPairPtr(Index);
				if (!Pair) return {};
				// The value property's offset is its place inside the key/value pair.
				Property = Helper.GetValueProperty();
				Value = Property->ContainerPtrToValuePtr<void>(Pair);
			}
			else if (Index < Property->ArrayDim)
			{
				Value = Property->ContainerPtrToValuePtr<void>(Container, Index);
			}
			else
			{
				return {};
			}
		}
		else
		{
			Value = Property->ContainerPtrToValuePtr<void>(Container);
		}
		if (!Value || !Property) return {};
		if (I == Chain.Num() - 1) return { Property, Value };

		// More of the path follows, so this step has to be something with members.
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			ContainerStruct = StructProperty->Struct;
			Container = Value;
			Enclosing = StructProperty;
			EnclosingValue = Value;
		}
		else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			const UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
			if (!IsValid(Object)) return {};
			ContainerStruct = Object->GetClass();
			Container = Object;
			Enclosing = nullptr;
			EnclosingValue = nullptr;
		}
		else
		{
			return {};
		}
	}
	return {};
}
