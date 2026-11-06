#pragma once

// A user widget placed into another Widget Blueprint's tree keeps the class
// defaults its class had at placement, through the compile that follows (#1542).

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Animation/WidgetAnimation.h"
#include "Components/Widget.h"
#include "Engine/Blueprint.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace MCPWidgetClassDefaults
{
	struct FEntry
	{
		TWeakObjectPtr<UWidget> Widget;
		FName WidgetName;
		TWeakObjectPtr<UClass> Class;
		FName Property;
		FString Expected;
	};

	struct FSnapshot
	{
		TArray<FEntry> Entries;
	};

	/** Properties a user widget subclass declares, whose value a placed template
	 *  takes from the class default. Transient, instanced, delegate and bound
	 *  widget or animation references are left out: they are per instance. */
	inline bool IsTracked(const FProperty* Prop)
	{
		if (!Prop) return false;
		const UClass* Owner = Prop->GetOwnerClass();
		if (!Owner || Owner == UUserWidget::StaticClass() || !Owner->IsChildOf(UUserWidget::StaticClass())) return false;
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_InstancedReference
			| CPF_ContainsInstancedReference | CPF_Deprecated))
		{
			return false;
		}
		if (Prop->IsA<FDelegateProperty>() || Prop->IsA<FMulticastDelegateProperty>()) return false;
		if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			if (ObjProp->PropertyClass && (ObjProp->PropertyClass->IsChildOf(UWidget::StaticClass())
				|| ObjProp->PropertyClass->IsChildOf(UWidgetAnimation::StaticClass())))
			{
				return false;
			}
		}
		return true;
	}

	inline FString ExportValue(const FProperty* Prop, const UObject* Container)
	{
		FString Text;
		Prop->ExportTextItem_Direct(Text, Prop->ContainerPtrToValuePtr<void>(Container), nullptr, nullptr, PPF_None);
		return Text;
	}

	/** Records the class default of every tracked property of a newly placed
	 *  user widget. Skip names properties the caller sets on it explicitly. */
	inline void Capture(UWidget* Placed, const TSet<FName>& Skip, FSnapshot& Out)
	{
		UUserWidget* UserWidget = Cast<UUserWidget>(Placed);
		if (!UserWidget) return;
		UClass* Class = UserWidget->GetClass();
		const UObject* CDO = Class->GetDefaultObject(false);
		if (!CDO) return;
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			if (!IsTracked(*It) || Skip.Contains(It->GetFName())) continue;
			FEntry Entry;
			Entry.Widget = Placed;
			Entry.WidgetName = Placed->GetFName();
			Entry.Class = Class;
			Entry.Property = It->GetFName();
			Entry.Expected = ExportValue(*It, CDO);
			Out.Entries.Add(MoveTemp(Entry));
		}
	}

	/** The current class for a recorded one: a recompile replaces the class
	 *  object, and the Blueprint's GeneratedClass is the live one. */
	inline UClass* CurrentClass(const FEntry& Entry, const UWidget* Widget)
	{
		if (Widget) return Widget->GetClass();
		UClass* Recorded = Entry.Class.Get();
		if (!Recorded) return nullptr;
		if (UBlueprint* BP = UBlueprint::GetBlueprintFromClass(Recorded))
		{
			if (BP->GeneratedClass) return BP->GeneratedClass;
		}
		return Recorded;
	}

	/** Writes one recorded value back. Returns true when the value differed. */
	inline bool RestoreOn(UWidget* Widget, const FEntry& Entry, FString& OutFound)
	{
		if (!Widget) return false;
		FProperty* Prop = Widget->GetClass()->FindPropertyByName(Entry.Property);
		if (!Prop) return false;
		OutFound = ExportValue(Prop, Widget);
		if (OutFound == Entry.Expected) return false;
		Widget->Modify();
		if (!Prop->ImportText_Direct(*Entry.Expected, Prop->ContainerPtrToValuePtr<void>(Widget), Widget, PPF_None)) return false;
		return ExportValue(Prop, Widget) == Entry.Expected;
	}

	/**
	 * After the compile, puts back any recorded class default the placed
	 * template (in the designer tree and in the compiled class's tree) no longer
	 * holds, and reports each one. A row also says when the class default itself
	 * moved since placement, which means the placed class was recompiled.
	 */
	inline TArray<TSharedPtr<FJsonValue>> Restore(UWidgetBlueprint* WidgetBP, const FSnapshot& Snapshot)
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		if (!WidgetBP || Snapshot.Entries.Num() == 0) return Rows;
		UWidgetTree* Archetypes = nullptr;
		if (UWidgetBlueprintGeneratedClass* GenClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetBP->GeneratedClass))
		{
			Archetypes = GenClass->GetWidgetTreeArchetype();
		}

		for (const FEntry& Entry : Snapshot.Entries)
		{
			UWidget* Designer = Entry.Widget.Get();
			if (!Designer && WidgetBP->WidgetTree) Designer = WidgetBP->WidgetTree->FindWidget(Entry.WidgetName);
			if (!Designer) continue;
			UWidget* Compiled = Archetypes ? Archetypes->FindWidget(Designer->GetFName()) : nullptr;

			FString FoundDesigner, FoundCompiled;
			const bool bDesigner = RestoreOn(Designer, Entry, FoundDesigner);
			const bool bCompiled = RestoreOn(Compiled, Entry, FoundCompiled);
			if (!bDesigner && !bCompiled) continue;

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("widgetName"), Designer->GetName());
			Row->SetStringField(TEXT("widgetClass"), Designer->GetClass()->GetName());
			Row->SetStringField(TEXT("property"), Entry.Property.ToString());
			Row->SetStringField(TEXT("value"), Entry.Expected);
			Row->SetStringField(TEXT("found"), bDesigner ? FoundDesigner : FoundCompiled);
			if (UClass* Class = CurrentClass(Entry, Designer))
			{
				const UObject* CDO = Class->GetDefaultObject(false);
				const FProperty* Prop = Class->FindPropertyByName(Entry.Property);
				if (CDO && Prop)
				{
					const FString Now = ExportValue(Prop, CDO);
					if (Now != Entry.Expected) Row->SetStringField(TEXT("classDefaultNow"), Now);
				}
			}
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		if (Rows.Num() > 0) WidgetBP->MarkPackageDirty();
		return Rows;
	}

	/** Adds classDefaultsRestored to a placement result when anything was put back. */
	inline void Report(const TSharedPtr<FJsonObject>& Result, const TArray<TSharedPtr<FJsonValue>>& Rows)
	{
		if (!Result.IsValid() || Rows.Num() == 0) return;
		Result->SetArrayField(TEXT("classDefaultsRestored"), Rows);
		Result->SetStringField(TEXT("classDefaultsNote"), FString::Printf(
			TEXT("%d class default(s) of placed user widgets were lost by the compile and were written back onto the placed templates, so they now hold the values their class had at placement."),
			Rows.Num()));
	}
}
