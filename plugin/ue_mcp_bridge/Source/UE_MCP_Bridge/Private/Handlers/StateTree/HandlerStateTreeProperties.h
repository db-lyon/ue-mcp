// StateTree node property writes that either land or fail. Shared by both
// StateTree translation units, which a unity build may merge.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "StateTreeEditorNode.h"
#include "HandlerJsonProperty.h"
#include "HandlerPropertyText.h"

namespace MCPStateTreeProperties
{
	/** Write instanceProperties onto a node's instance data. Every key must name
	 *  a property and every value must be written, or the call fails. */
	inline bool ApplyInstanceProperties(FStateTreeEditorNode& Node, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
	{
		if (!Properties.IsValid() || Properties->Values.Num() == 0) return true;

		UObject* Object = Node.InstanceObject.Get();
		const UStruct* Struct = Object ? static_cast<const UStruct*>(Object->GetClass())
			: (Node.Instance.IsValid() ? Node.Instance.GetScriptStruct() : nullptr);
		void* Container = Object ? static_cast<void*>(Object) : static_cast<void*>(Node.Instance.GetMutableMemory());
		if (!Struct || !Container)
		{
			OutError = TEXT("instanceProperties: this node has no instance data to write");
			return false;
		}

		for (const auto& Pair : Properties->Values)
		{
			const FString Key(Pair.Key);
			FProperty* Prop = Struct->FindPropertyByName(*Key);
			if (!Prop)
			{
				OutError = FString::Printf(TEXT("instanceProperties: '%s' is not a property of %s"), *Key, *Struct->GetName());
				return false;
			}
			FString Error;
			if (!MCPJsonProperty::SetJsonOnProperty(Prop, Prop->ContainerPtrToValuePtr<void>(Container), Pair.Value, Error))
			{
				OutError = FString::Printf(TEXT("instanceProperties: '%s' was not written: %s"), *Key, *Error);
				return false;
			}
		}
		return true;
	}

	/** Import Value into one property. On failure the prior text is restored. */
	inline bool ImportValue(FProperty* Prop, void* ValuePtr, const FString& Value, const FString& PriorValue, FString& OutError)
	{
		FString Error;
		if (MCPPropertyText::ImportTextRaw(Prop, ValuePtr, Value, nullptr, PPF_None, Error)) return true;
		FString Ignored;
		MCPPropertyText::ImportTextRaw(Prop, ValuePtr, PriorValue, nullptr, PPF_None, Ignored);
		OutError = FString::Printf(TEXT("Could not set '%s' (type %s): %s"), *Prop->GetName(), *Prop->GetCPPType(), *Error);
		return false;
	}
}
