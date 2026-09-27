#include "HandlerPinType.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

namespace
{
	/** The scalar half of a pin type, or of a map's value. */
	FString MCPPinScalarSpec(const FName Category, const FName SubCategory, const UObject* SubObject, bool& bOutRoundTrips)
	{
		bOutRoundTrips = true;
		const FString ObjectPath = SubObject ? SubObject->GetPathName() : FString();

		if (Category == UEdGraphSchema_K2::PC_Boolean) return TEXT("bool");
		if (Category == UEdGraphSchema_K2::PC_Int)     return TEXT("int");
		if (Category == UEdGraphSchema_K2::PC_Int64)   return TEXT("int64");
		if (Category == UEdGraphSchema_K2::PC_Float)   return TEXT("float");
		if (Category == UEdGraphSchema_K2::PC_Double)  return TEXT("double");
		if (Category == UEdGraphSchema_K2::PC_Real)
		{
			return SubCategory == UEdGraphSchema_K2::PC_Float ? TEXT("float") : TEXT("double");
		}
		if (Category == UEdGraphSchema_K2::PC_String) return TEXT("string");
		if (Category == UEdGraphSchema_K2::PC_Name)   return TEXT("name");
		if (Category == UEdGraphSchema_K2::PC_Text)   return TEXT("text");
		if (Category == UEdGraphSchema_K2::PC_Byte || Category == UEdGraphSchema_K2::PC_Enum)
		{
			if (Cast<UEnum>(SubObject)) return TEXT("enum:") + ObjectPath;
			return TEXT("byte");
		}
		if (Category == UEdGraphSchema_K2::PC_Struct)
		{
			if (SubObject) return TEXT("struct:") + ObjectPath;
			bOutRoundTrips = false;
			return TEXT("struct");
		}
		if (Category == UEdGraphSchema_K2::PC_Object)
		{
			if (SubObject) return TEXT("object:") + ObjectPath;
			return TEXT("object");
		}
		if (Category == UEdGraphSchema_K2::PC_Class)
		{
			return SubObject ? FString::Printf(TEXT("TSubclassOf<%s>"), *ObjectPath) : TEXT("class");
		}
		if (Category == UEdGraphSchema_K2::PC_SoftObject)
		{
			return SubObject ? FString::Printf(TEXT("TSoftObjectPtr<%s>"), *ObjectPath) : TEXT("softobject");
		}
		if (Category == UEdGraphSchema_K2::PC_SoftClass)
		{
			return SubObject ? FString::Printf(TEXT("TSoftClassPtr<%s>"), *ObjectPath) : TEXT("softclass");
		}

		// Real categories with no spelling in the write vocabulary. Say what
		// the pin is rather than emit a label a write would take for another.
		bOutRoundTrips = false;
		FString Text = Category.ToString();
		if (SubObject) Text += TEXT(":") + ObjectPath;
		else if (!SubCategory.IsNone()) Text += TEXT(".") + SubCategory.ToString();
		return Text;
	}
}

FString MCPPinTypeSpec(const FEdGraphPinType& PinType, bool& bOutRoundTrips)
{
	const FString Base = MCPPinScalarSpec(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get(), bOutRoundTrips);

	switch (PinType.ContainerType)
	{
	case EPinContainerType::Array:
		return Base + TEXT("[]");
	case EPinContainerType::Set:
		return FString::Printf(TEXT("set<%s>"), *Base);
	case EPinContainerType::Map:
	{
		bool bValueRoundTrips = true;
		const FString ValueSpec = MCPPinScalarSpec(
			PinType.PinValueType.TerminalCategory,
			PinType.PinValueType.TerminalSubCategory,
			PinType.PinValueType.TerminalSubCategoryObject.Get(),
			bValueRoundTrips);
		bOutRoundTrips = bOutRoundTrips && bValueRoundTrips;
		return FString::Printf(TEXT("map<%s,%s>"), *Base, *ValueSpec);
	}
	default:
		return Base;
	}
}
