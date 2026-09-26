// Shared notify reporting for the animation handlers. read_anim_montage and the
// notify-state actions live in different translation units of a unity build, so
// this is a header rather than a file-local copy in each.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "HandlerUtils.h"

namespace MCPAnimNotify
{
	/** Every editable reflected property on a notify or notify-state object, as
	 *  export text keyed by property name. The same names editor(set_property)
	 *  takes against the object's objectPath. */
	inline TSharedPtr<FJsonObject> EditableProperties(const UObject* Object)
	{
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		if (!Object) return Props;
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Prop = *It;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Deprecated)) continue;
			Props->SetField(Prop->GetName(), MCPExportPropertyValue(Prop, Object));
		}
		return Props;
	}

	/** Every notify STATE on the asset, reported the way add_notify_state takes them. */
	inline TArray<TSharedPtr<FJsonValue>> ListNotifyStates(const UAnimSequenceBase* Asset)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (!Asset) return Out;
		for (const FAnimNotifyEvent& Event : Asset->Notifies)
		{
			if (!Event.NotifyStateClass) continue;
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("notifyName"), Event.NotifyName.ToString());
			O->SetStringField(TEXT("notifyStateClass"), Event.NotifyStateClass->GetClass()->GetName());
			O->SetStringField(TEXT("objectPath"), Event.NotifyStateClass->GetPathName());
			O->SetNumberField(TEXT("triggerTime"), Event.GetTriggerTime());
			O->SetNumberField(TEXT("duration"), Event.GetDuration());
			O->SetNumberField(TEXT("endTime"), Event.GetEndTriggerTime());
			Out.Add(MakeShared<FJsonValueObject>(O));
		}
		return Out;
	}
}
