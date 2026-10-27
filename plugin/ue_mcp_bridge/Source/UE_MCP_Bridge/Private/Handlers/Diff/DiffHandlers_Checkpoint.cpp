// Checkpoint asset diffs: the engine's own diff code run on two versions of a
// package loaded with DiffUtils::LoadPackageForDiff, serialized as JSON, plus a
// deterministic thumbnail of each version and the engine's diff window on request.
#include "DiffHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerLogCapture.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/ObjectVersion.h"

#include "Algo/Sort.h"
#include "AssetCompilingManager.h"
#include "AssetDefinition.h"
#include "AssetToolsModule.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "DiffResults.h"
#include "DiffUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GenericPlatform/GenericWindow.h"
#include "GameFramework/Actor.h"
#include "GraphDiffControl.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Misc/FileHelper.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Misc/ScopeExit.h"
#include "NiagaraSystem.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
#define UE_MCP_CKDIFF_HAS_DETAILS_DIFF 1
#include "SDetailsDiff.h"
#else
#define UE_MCP_CKDIFF_HAS_DETAILS_DIFF 0
#endif
#include "Settings/EditorLoadingSavingSettings.h"
#include "ShaderCompiler.h"
#include "Sound/SoundWave.h"
#include "StaticMeshResources.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Widgets/SWindow.h"

namespace
{
	/** Longest exported value kept per side; longer values are cut and say so. */
	constexpr int32 CkDiffMaxValueChars = 600;
	/** Thumbnail edge in pixels. */
	constexpr int32 CkDiffThumbnailSize = 256;

	/** The names one version's temp package and asset carry, mapped back to the project's. */
	struct FCkDiffNames
	{
		FString OriginalPackage;
		TArray<FString> TempPackages;

		FString Clean(FString Text) const
		{
			for (const FString& Temp : TempPackages)
			{
				if (!Temp.IsEmpty()) Text.ReplaceInline(*Temp, *OriginalPackage, ESearchCase::CaseSensitive);
			}
			return Text;
		}
	};

	/** Records a run collects, with a ceiling the caller sets, and how many it dropped. */
	struct FCkDiffSink
	{
		int32 Max = 2000;
		int32 Count = 0;
		int32 Dropped = 0;
		bool Take()
		{
			if (Count >= Max) { ++Dropped; return false; }
			++Count;
			return true;
		}
	};

	const TCHAR* CkDiffTypeName(EDiffType::Type Type)
	{
		switch (Type)
		{
		case EDiffType::NO_DIFFERENCE: return TEXT("NO_DIFFERENCE");
		case EDiffType::OBJECT_REMOVED: return TEXT("OBJECT_REMOVED");
		case EDiffType::OBJECT_ADDED: return TEXT("OBJECT_ADDED");
		case EDiffType::OBJECT_PROPERTY: return TEXT("OBJECT_PROPERTY");
		case EDiffType::OBJECT_REQUEST_DIFF: return TEXT("OBJECT_REQUEST_DIFF");
		case EDiffType::NODE_REMOVED: return TEXT("NODE_REMOVED");
		case EDiffType::NODE_ADDED: return TEXT("NODE_ADDED");
		case EDiffType::PIN_LINKEDTO_NUM_DEC: return TEXT("PIN_LINKEDTO_NUM_DEC");
		case EDiffType::PIN_LINKEDTO_NUM_INC: return TEXT("PIN_LINKEDTO_NUM_INC");
		case EDiffType::PIN_DEFAULT_VALUE: return TEXT("PIN_DEFAULT_VALUE");
		case EDiffType::PIN_TYPE_CATEGORY: return TEXT("PIN_TYPE_CATEGORY");
		case EDiffType::PIN_TYPE_SUBCATEGORY: return TEXT("PIN_TYPE_SUBCATEGORY");
		case EDiffType::PIN_TYPE_SUBCATEGORY_OBJECT: return TEXT("PIN_TYPE_SUBCATEGORY_OBJECT");
		case EDiffType::PIN_TYPE_IS_ARRAY: return TEXT("PIN_TYPE_IS_ARRAY");
		case EDiffType::PIN_TYPE_IS_REF: return TEXT("PIN_TYPE_IS_REF");
		case EDiffType::PIN_LINKEDTO_NODE: return TEXT("PIN_LINKEDTO_NODE");
		case EDiffType::PIN_LINKEDTO_PIN: return TEXT("PIN_LINKEDTO_PIN");
		case EDiffType::NODE_MOVED: return TEXT("NODE_MOVED");
		case EDiffType::TIMELINE_LENGTH: return TEXT("TIMELINE_LENGTH");
		case EDiffType::TIMELINE_AUTOPLAY: return TEXT("TIMELINE_AUTOPLAY");
		case EDiffType::TIMELINE_LOOP: return TEXT("TIMELINE_LOOP");
		case EDiffType::TIMELINE_IGNOREDILATION: return TEXT("TIMELINE_IGNOREDILATION");
		case EDiffType::TIMELINE_NUM_TRACKS: return TEXT("TIMELINE_NUM_TRACKS");
		case EDiffType::TIMELINE_TRACK_MODIFIED: return TEXT("TIMELINE_TRACK_MODIFIED");
		case EDiffType::NODE_PIN_COUNT: return TEXT("NODE_PIN_COUNT");
		case EDiffType::NODE_COMMENT: return TEXT("NODE_COMMENT");
		case EDiffType::NODE_PROPERTY: return TEXT("NODE_PROPERTY");
		case EDiffType::INFO_MESSAGE: return TEXT("INFO_MESSAGE");
		case EDiffType::CUSTOM_OBJECT: return TEXT("CUSTOM_OBJECT");
		default: return TEXT("UNKNOWN");
		}
	}

	const TCHAR* CkDiffCategoryName(EDiffType::Category Category)
	{
		switch (Category)
		{
		case EDiffType::ADDITION: return TEXT("ADDITION");
		case EDiffType::SUBTRACTION: return TEXT("SUBTRACTION");
		case EDiffType::MODIFICATION: return TEXT("MODIFICATION");
		case EDiffType::MINOR: return TEXT("MINOR");
		case EDiffType::CONTROL: return TEXT("CONTROL");
		default: return TEXT("UNKNOWN");
		}
	}

	const TCHAR* CkDiffPropertyTypeName(EPropertyDiffType::Type Type)
	{
		switch (Type)
		{
		case EPropertyDiffType::PropertyAddedToA: return TEXT("PropertyAddedToA");
		case EPropertyDiffType::PropertyAddedToB: return TEXT("PropertyAddedToB");
		case EPropertyDiffType::PropertyValueChanged: return TEXT("PropertyValueChanged");
		default: return TEXT("Invalid");
		}
	}

	const TCHAR* CkDiffTreeTypeName(ETreeDiffType::Type Type)
	{
		switch (Type)
		{
		case ETreeDiffType::NODE_ADDED: return TEXT("NODE_ADDED");
		case ETreeDiffType::NODE_REMOVED: return TEXT("NODE_REMOVED");
		case ETreeDiffType::NODE_TYPE_CHANGED: return TEXT("NODE_TYPE_CHANGED");
		case ETreeDiffType::NODE_PROPERTY_CHANGED: return TEXT("NODE_PROPERTY_CHANGED");
		case ETreeDiffType::NODE_MOVED: return TEXT("NODE_MOVED");
		case ETreeDiffType::NODE_CORRUPTED: return TEXT("NODE_CORRUPTED");
		case ETreeDiffType::NODE_FIXED: return TEXT("NODE_FIXED");
		default: return TEXT("UNKNOWN");
		}
	}

	/** A value exported as text, with temp package names mapped back and cut past CkDiffMaxValueChars. */
	FString CkDiffExport(const FProperty* Property, const void* Value, const FCkDiffNames& Names)
	{
		FString Text;
		Property->ExportTextItem_Direct(Text, Value, nullptr, nullptr, PPF_None);
		Text = Names.Clean(Text);
		if (Text.Len() > CkDiffMaxValueChars)
		{
			Text = Text.Left(CkDiffMaxValueChars) + FString::Printf(TEXT("... (%d characters)"), Text.Len());
		}
		return Text;
	}

	/** A resolved property's value as FProperty::ExportText writes it, or null when the path does not resolve on this side. */
	TSharedPtr<FJsonValue> CkDiffValue(const FResolvedProperty& Resolved, const FCkDiffNames& Names)
	{
		if (!Resolved.Property || !Resolved.Object) return MakeShared<FJsonValueNull>();
		const void* Value = Resolved.Property->ContainerPtrToValuePtr<void>(Resolved.Object);
		if (!Value) return MakeShared<FJsonValueNull>();
		return MakeShared<FJsonValueString>(CkDiffExport(Resolved.Property, Value, Names));
	}

	/** The property at the root of a soft path, on the struct that declares it. */
	const FProperty* CkDiffRootProperty(const FSingleObjectDiffEntry& Entry, const UStruct* StructA, const void* A, const UStruct* StructB, const void* B)
	{
		const FPropertySoftPath Root = Entry.Identifier.GetRootProperty(1);
		const FResolvedProperty RA = A ? Root.Resolve(StructA, A) : FResolvedProperty();
		if (RA.Property) return RA.Property;
		const FResolvedProperty RB = B ? Root.Resolve(StructB, B) : FResolvedProperty();
		return RB.Property;
	}

	/**
	 * DiffUtils::CompareUnrelatedStructs, serialized: every FSingleObjectDiffEntry
	 * with its path, type, the engine's message, and both values exported as text.
	 * A difference under a property the details panel does not show (no CPF_Edit) is
	 * listed too, marked hidden, and counted in OutHidden, so nothing that changed is
	 * left out of what the caller is asked to decide on.
	 */
	void CkDiffProperties(
		const UStruct* StructA, const void* A, const UObject* OuterA,
		const UStruct* StructB, const void* B, const UObject* OuterB,
		const FString& Group, const FText& ObjectName, const FCkDiffNames& Names,
		TArray<TSharedPtr<FJsonValue>>& Out, int32& OutHidden, FCkDiffSink& Sink, bool bOnlyEditable = true)
	{
		if (!StructA || !StructB || !A || !B) return;
		TArray<FSingleObjectDiffEntry> Entries;
		DiffUtils::CompareUnrelatedStructs(StructA, A, OuterA, StructB, B, OuterB, Entries);
		for (const FSingleObjectDiffEntry& Entry : Entries)
		{
			const FProperty* Root = CkDiffRootProperty(Entry, StructA, A, StructB, B);
			const bool bHidden = !Root || (bOnlyEditable && !Root->HasAnyPropertyFlags(CPF_Edit));
			if (bHidden) ++OutHidden;
			if (!Sink.Take()) continue;
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			if (!Group.IsEmpty()) Rec->SetStringField(TEXT("group"), Group);
			Rec->SetStringField(TEXT("path"), Entry.Identifier.ToDisplayName());
			if (Root) Rec->SetStringField(TEXT("property"), Root->GetName());
			if (bHidden) Rec->SetBoolField(TEXT("hidden"), true);
			Rec->SetStringField(TEXT("diffType"), CkDiffPropertyTypeName(Entry.DiffType));
			Rec->SetStringField(TEXT("displayString"), DiffViewUtils::PropertyDiffMessage(Entry, ObjectName).ToString());
			Rec->SetField(TEXT("before"), CkDiffValue(Entry.Identifier.Resolve(StructA, A), Names));
			Rec->SetField(TEXT("after"), CkDiffValue(Entry.Identifier.Resolve(StructB, B), Names));
			Out.Add(MakeShared<FJsonValueObject>(Rec));
		}
	}

	void CkDiffObjects(const UObject* A, const UObject* B, const FString& Group, const FCkDiffNames& Names,
		TArray<TSharedPtr<FJsonValue>>& Out, int32& OutHidden, FCkDiffSink& Sink)
	{
		if (!A || !B) return;
		CkDiffProperties(A->GetClass(), A, A->GetPackage(), B->GetClass(), B, B->GetPackage(),
			Group, FText::FromString(B->GetName()), Names, Out, OutHidden, Sink);
	}

	TSharedPtr<FJsonObject> CkDiffSection(const FString& Category, const FString& Kind, const FString& Source)
	{
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("category"), Category);
		S->SetStringField(TEXT("kind"), Kind);
		S->SetStringField(TEXT("engineSource"), Source);
		return S;
	}

	void CkDiffAddPropertySection(TArray<TSharedPtr<FJsonValue>>& Sections, const FString& Category, const FString& Source,
		TArray<TSharedPtr<FJsonValue>>& Records, int32 Hidden)
	{
		if (Records.Num() == 0 && Hidden == 0) return;
		TSharedPtr<FJsonObject> S = CkDiffSection(Category, TEXT("properties"), Source);
		S->SetArrayField(TEXT("entries"), Records);
		S->SetNumberField(TEXT("hiddenChanges"), Hidden);
		Sections.Add(MakeShared<FJsonValueObject>(S));
	}

	/** Most properties listed for one added or removed node. */
	constexpr int32 CkDiffMaxNodeProperties = 64;

	/**
	 * What an added or removed node holds: its full title, its visible input pins
	 * with their defaults, and the details panel's properties (CPF_Edit) of the
	 * object it edits, the UMaterialExpression for a material node and the node
	 * itself otherwise. Each property says whether it is still its class default.
	 */
	void CkDiffNodeContents(const UEdGraphNode* Node, const FCkDiffNames& Names, const TSharedPtr<FJsonObject>& O)
	{
		O->SetStringField(TEXT("fullTitle"), Names.Clean(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));

		TArray<TSharedPtr<FJsonValue>> Pins;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->Direction != EGPD_Input || Pin->PinType.PinCategory == TEXT("exec")) continue;
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Pin->PinName.ToString());
			P->SetStringField(TEXT("displayName"), Pin->GetDisplayName().ToString());
			P->SetStringField(TEXT("defaultValue"), Names.Clean(Pin->GetDefaultAsString()));
			P->SetBoolField(TEXT("linked"), Pin->LinkedTo.Num() > 0);
			Pins.Add(MakeShared<FJsonValueObject>(P));
		}
		O->SetArrayField(TEXT("pins"), Pins);

		const UObject* Edited = Node;
		if (const UMaterialGraphNode* MaterialNode = Cast<UMaterialGraphNode>(Node))
		{
			if (MaterialNode->MaterialExpression) Edited = MaterialNode->MaterialExpression;
		}
		O->SetStringField(TEXT("propertiesOf"), Edited->GetClass()->GetName());
		const UObject* Defaults = Edited->GetClass()->GetDefaultObject();
		TArray<TSharedPtr<FJsonValue>> Props;
		int32 Dropped = 0;
		for (TFieldIterator<FProperty> It(Edited->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Deprecated)) continue;
			if (Props.Num() >= CkDiffMaxNodeProperties) { ++Dropped; continue; }
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Property->GetName());
			P->SetStringField(TEXT("displayName"), Property->GetDisplayNameText().ToString());
			P->SetStringField(TEXT("value"), CkDiffExport(Property, Property->ContainerPtrToValuePtr<void>(Edited), Names));
			P->SetBoolField(TEXT("isDefault"), Defaults && Property->Identical_InContainer(Edited, Defaults));
			Props.Add(MakeShared<FJsonValueObject>(P));
		}
		O->SetArrayField(TEXT("properties"), Props);
		if (Dropped > 0) O->SetNumberField(TEXT("propertiesDropped"), Dropped);
	}

	TSharedPtr<FJsonObject> CkDiffNodeJson(const UEdGraphNode* Node, const FCkDiffNames& Names, bool bContents = false)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
		O->SetStringField(TEXT("name"), Node->GetName());
		O->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		O->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		if (bContents) CkDiffNodeContents(Node, Names, O);
		return O;
	}

	TSharedPtr<FJsonObject> CkDiffPinJson(const UEdGraphPin* Pin, const FCkDiffNames& Names)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Pin->PinName.ToString());
		O->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		if (const UEdGraphNode* Owner = Pin->GetOwningNodeUnchecked())
		{
			O->SetStringField(TEXT("nodeGuid"), Owner->NodeGuid.ToString());
		}
		O->SetStringField(TEXT("defaultValue"), Names.Clean(Pin->GetDefaultAsString()));
		TArray<TSharedPtr<FJsonValue>> Links;
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (!Linked || !Linked->GetOwningNodeUnchecked()) continue;
			Links.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s.%s"),
				*Linked->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Linked->PinName.ToString())));
		}
		O->SetArrayField(TEXT("linkedTo"), Links);
		return O;
	}

	/** What a node's properties live on: a material node's expression, otherwise the node. */
	const UObject* CkDiffNodeSubject(const UObject* Node)
	{
		const UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
		return MatNode && MatNode->MaterialExpression ? MatNode->MaterialExpression.Get() : Node;
	}

	/**
	 * A material node's settings are on its expression, which UEdGraphNode::FindDiffs does not compare.
	 * Every matched pair whose expressions differ in an editable property gets a NODE_PROPERTY result.
	 */
	void CkDiffMaterialExpressionResults(UEdGraph* OldGraph, UEdGraph* NewGraph, TArray<FDiffSingleResult>& Results)
	{
		if (!OldGraph || !NewGraph) return;
		for (UEdGraphNode* NewNode : NewGraph->Nodes)
		{
			UMaterialGraphNode* NewMat = Cast<UMaterialGraphNode>(NewNode);
			if (!NewMat || !NewMat->MaterialExpression) continue;
			UMaterialGraphNode* OldMat = nullptr;
			for (UEdGraphNode* OldNode : OldGraph->Nodes)
			{
				if (OldNode && OldNode->NodeGuid == NewNode->NodeGuid) { OldMat = Cast<UMaterialGraphNode>(OldNode); break; }
			}
			if (!OldMat || !OldMat->MaterialExpression || OldMat->MaterialExpression->GetClass() != NewMat->MaterialExpression->GetClass()) continue;
			const bool bReported = Results.ContainsByPredicate([&](const FDiffSingleResult& R)
			{
				return (R.Diff == EDiffType::NODE_PROPERTY || R.Diff == EDiffType::OBJECT_PROPERTY)
					&& ((R.Node1 == OldMat && R.Node2 == NewMat) || (R.Node1 == NewMat && R.Node2 == OldMat));
			});
			if (bReported) continue;
			const UMaterialExpression* A = OldMat->MaterialExpression;
			const UMaterialExpression* B = NewMat->MaterialExpression;
			TArray<FSingleObjectDiffEntry> Entries;
			DiffUtils::CompareUnrelatedObjects(A, B, Entries);
			const bool bEditable = Entries.ContainsByPredicate([&](const FSingleObjectDiffEntry& E)
			{
				const FProperty* Root = CkDiffRootProperty(E, A->GetClass(), A, B->GetClass(), B);
				return Root && Root->HasAnyPropertyFlags(CPF_Edit);
			});
			if (!bEditable) continue;
			FDiffSingleResult R;
			R.Diff = EDiffType::NODE_PROPERTY;
			R.Category = EDiffType::MODIFICATION;
			R.Node1 = OldMat;
			R.Node2 = NewMat;
			R.Object1 = A;
			R.Object2 = B;
			R.DisplayString = FText::Format(NSLOCTEXT("UEMCPCheckpointDiff", "MaterialNodeChanged", "Changed Node '{0}'"), NewMat->GetNodeTitle(ENodeTitleType::ListView));
			R.ToolTip = FText::Format(NSLOCTEXT("UEMCPCheckpointDiff", "MaterialNodeChangedTip", "Properties of '{0}' changed"), NewMat->GetNodeTitle(ENodeTitleType::ListView));
			R.OwningObjectPath = NewGraph->GetPathName();
			Results.Add(R);
		}
	}

	/**
	 * FGraphDiffControl::DiffGraphs, serialized: every FDiffSingleResult with its
	 * type, category, the engine's DisplayString and ToolTip, and the nodes and pins
	 * it names. A NODE_PROPERTY result carries the property records of the two
	 * objects it compares.
	 */
	void CkDiffGraph(UEdGraph* OldGraph, UEdGraph* NewGraph, const FString& GraphLabel, const FCkDiffNames& Names,
		TArray<TSharedPtr<FJsonValue>>& Out, FCkDiffSink& Sink)
	{
		TArray<FDiffSingleResult> Results;
		FGraphDiffControl::DiffGraphs(OldGraph, NewGraph, Results);
		CkDiffMaterialExpressionResults(OldGraph, NewGraph, Results);
		Algo::SortBy(Results, &FDiffSingleResult::Diff);
		for (const FDiffSingleResult& R : Results)
		{
			if (!R.IsRealDifference()) continue;
			if (!Sink.Take()) continue;
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("graph"), GraphLabel);
			Rec->SetStringField(TEXT("diffType"), CkDiffTypeName(R.Diff));
			Rec->SetStringField(TEXT("category"), CkDiffCategoryName(R.Category));
			Rec->SetStringField(TEXT("displayString"), Names.Clean(R.DisplayString.ToString()));
			const FString Tip = Names.Clean(R.ToolTip.ToString());
			if (!Tip.IsEmpty()) Rec->SetStringField(TEXT("toolTip"), Tip);
			if (!R.OwningObjectPath.IsEmpty()) Rec->SetStringField(TEXT("owningObjectPath"), Names.Clean(R.OwningObjectPath));
			// An added or removed node has no property records behind it, so it carries what it holds.
			const bool bContents = R.Diff == EDiffType::NODE_ADDED || R.Diff == EDiffType::NODE_REMOVED;
			if (R.Node1) Rec->SetObjectField(TEXT("node1"), CkDiffNodeJson(R.Node1, Names, bContents));
			if (R.Node2) Rec->SetObjectField(TEXT("node2"), CkDiffNodeJson(R.Node2, Names, bContents));
			if (R.Pin1) Rec->SetObjectField(TEXT("pin1"), CkDiffPinJson(R.Pin1, Names));
			if (R.Pin2) Rec->SetObjectField(TEXT("pin2"), CkDiffPinJson(R.Pin2, Names));
			if (R.Diff == EDiffType::NODE_PROPERTY || R.Diff == EDiffType::OBJECT_PROPERTY)
			{
				const UObject* A = R.Object1 ? R.Object1 : CkDiffNodeSubject(R.Node1);
				const UObject* B = R.Object2 ? R.Object2 : CkDiffNodeSubject(R.Node2);
				if (A && B)
				{
					TArray<TSharedPtr<FJsonValue>> Props;
					int32 Hidden = 0;
					FCkDiffSink Inner;
					// The node's own diff reports the change; these are the values behind it, in node order.
					CkDiffObjects(A, B, FString(), Names, Props, Hidden, Inner);
					Rec->SetArrayField(TEXT("properties"), Props);
				}
			}
			Out.Add(MakeShared<FJsonValueObject>(Rec));
		}
	}

	/** SBlueprintDiff::IsGraphDiffNeeded: graphs inside math expression nodes are rebuilt every compile. */
	bool CkDiffGraphNeeded(const UEdGraph* Graph)
	{
		const UObject* Outer = Graph ? Graph->GetOuter() : nullptr;
		return !(Outer && Outer->GetClass()->GetName() == TEXT("K2Node_MathExpression"));
	}

	FString CkDiffGraphLabel(UEdGraph* Graph)
	{
		if (const UEdGraphSchema* Schema = Graph->GetSchema())
		{
			FGraphDisplayInfo Info;
			Schema->GetGraphDisplayInformation(*Graph, Info);
			if (!Info.DisplayName.IsEmpty()) return Info.DisplayName.ToString();
		}
		return Graph->GetName();
	}

	/** The engine's SCS hierarchy for CompareUnrelatedSCS: each node's variable name and its index path from the roots. */
	void CkDiffSCSWalk(const USCS_Node* Node, TArray<int32> Location, TArray<FSCSResolvedIdentifier>& Out)
	{
		if (!Node) return;
		FSCSResolvedIdentifier Id;
		Id.Identifier.Name = Node->GetVariableName();
		Id.Identifier.TreeLocation = Location;
		Id.Object = Node->ComponentTemplate;
		Out.Add(Id);
		const TArray<USCS_Node*>& Children = Node->GetChildNodes();
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			TArray<int32> Child = Location;
			Child.Add(i);
			CkDiffSCSWalk(Children[i], Child, Out);
		}
	}

	TArray<FSCSResolvedIdentifier> CkDiffSCSHierarchy(const UBlueprint* BP)
	{
		TArray<FSCSResolvedIdentifier> Out;
		if (BP && BP->SimpleConstructionScript)
		{
			const TArray<USCS_Node*>& Roots = BP->SimpleConstructionScript->GetRootNodes();
			for (int32 i = 0; i < Roots.Num(); ++i) CkDiffSCSWalk(Roots[i], { i }, Out);
		}
		return Out;
	}

	const UObject* CkDiffSCSObject(const TArray<FSCSResolvedIdentifier>& Hierarchy, const FSCSIdentifier& Id)
	{
		for (const FSCSResolvedIdentifier& R : Hierarchy)
		{
			if (R.Identifier.Name == Id.Name && R.Identifier.TreeLocation == Id.TreeLocation) return R.Object;
		}
		for (const FSCSResolvedIdentifier& R : Hierarchy)
		{
			if (R.Identifier.Name == Id.Name) return R.Object;
		}
		return nullptr;
	}

	/** The graph a material or material function shows in its editor, built the way the engine's material diff builds it. */
	UMaterialGraph* CkDiffMaterialGraph(UObject* Asset)
	{
		UMaterialGraph* Graph = CastChecked<UMaterialGraph>(FBlueprintEditorUtils::CreateNewGraph(GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UMaterialGraph::StaticClass()), UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass()));
		if (UMaterial* Material = Cast<UMaterial>(Asset))
		{
			Graph->Material = Material;
			Graph->MaterialFunction = nullptr;
		}
		else if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
		{
			Graph->MaterialFunction = Function;
			Graph->Material = NewObject<UMaterial>();
			FArchiveUObject DummyArchive;
			Graph->Material->Serialize(DummyArchive);
			Graph->Material->SetShadingModel(MSM_Unlit);
			Graph->Material->AssignExpressionCollection(Function->GetExpressionCollection());
		}
		Graph->RebuildGraph();
		// RebuildGraph gives every node a fresh GUID, so DiffGraphs would match a node across versions by
		// title alone, and a parameter's title shows its value. The expression's own GUID is saved with it.
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
			if (MatNode && MatNode->MaterialExpression && MatNode->MaterialExpression->MaterialExpressionGuid.IsValid())
			{
				MatNode->NodeGuid = MatNode->MaterialExpression->MaterialExpressionGuid;
			}
		}
		return Graph;
	}

	// ── Supplementary facts per type ────────────────────────────────────────

	void CkDiffFact(TArray<TSharedPtr<FJsonValue>>& Facts, const FString& Name, const FString& Value)
	{
		TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
		F->SetStringField(TEXT("name"), Name);
		F->SetStringField(TEXT("value"), Value);
		Facts.Add(MakeShared<FJsonValueObject>(F));
	}

	FString CkDiffEnum(const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetNameStringByValue(Value) : FString::Printf(TEXT("%lld"), Value);
	}

	/** "Material slot 0" or "Material slot 0 (Body)": an unnamed slot has no name to show. */
	FString CkDiffSlotLabel(int32 Index, const FName& Name)
	{
		return Name.IsNone() ? FString::Printf(TEXT("Material slot %d"), Index) : FString::Printf(TEXT("Material slot %d (%s)"), Index, *Name.ToString());
	}

	FString CkDiffVector(const FVector& V)
	{
		return FString::Printf(TEXT("(%.2f, %.2f, %.2f)"), V.X, V.Y, V.Z);
	}

	FString CkDiffActorLabel(const AActor* Actor)
	{
#if WITH_EDITOR
		const FString Label = Actor->GetActorLabel(false);
		if (!Label.IsEmpty()) return Label;
#endif
		return Actor->GetName();
	}

	TArray<AActor*> CkDiffActors(UObject* Asset, UPackage* Package)
	{
		TArray<AActor*> Out;
		if (UWorld* World = Cast<UWorld>(Asset))
		{
			if (World->PersistentLevel)
			{
				for (AActor* Actor : World->PersistentLevel->Actors)
				{
					if (Actor) Out.Add(Actor);
				}
			}
		}
		else if (!Asset && Package)
		{
			// A World Partition actor package holds one actor and no asset.
			ForEachObjectWithPackage(Package, [&Out](UObject* Object)
			{
				if (AActor* Actor = Cast<AActor>(Object))
				{
					if (!Actor->IsTemplate()) Out.Add(Actor);
				}
				return true;
			}, false);
		}
		return Out;
	}

	void CkDiffActorFacts(const AActor* Actor, TArray<TSharedPtr<FJsonValue>>& Facts, const FString& Prefix)
	{
		CkDiffFact(Facts, Prefix + TEXT("Class"), Actor->GetClass()->GetName());
		CkDiffFact(Facts, Prefix + TEXT("Label"), CkDiffActorLabel(Actor));
		if (const USceneComponent* Root = Actor->GetRootComponent())
		{
			CkDiffFact(Facts, Prefix + TEXT("Location"), CkDiffVector(Root->GetRelativeLocation()));
			CkDiffFact(Facts, Prefix + TEXT("Rotation"), Root->GetRelativeRotation().ToString());
			CkDiffFact(Facts, Prefix + TEXT("Scale"), CkDiffVector(Root->GetRelativeScale3D()));
		}
	}

	TArray<TSharedPtr<FJsonValue>> CkDiffFacts(UObject* Asset, UPackage* Package)
	{
		TArray<TSharedPtr<FJsonValue>> Facts;
		if (UBlueprint* BP = Cast<UBlueprint>(Asset))
		{
			CkDiffFact(Facts, TEXT("Parent class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None"));
			CkDiffFact(Facts, TEXT("Blueprint type"), CkDiffEnum(StaticEnum<EBlueprintType>(), BP->BlueprintType));
			CkDiffFact(Facts, TEXT("Variables"), FString::FromInt(BP->NewVariables.Num()));
			TArray<UEdGraph*> Graphs;
			BP->GetAllGraphs(Graphs);
			CkDiffFact(Facts, TEXT("Graphs"), FString::FromInt(Graphs.Num()));
			CkDiffFact(Facts, TEXT("Components"), FString::FromInt(BP->SimpleConstructionScript ? BP->SimpleConstructionScript->GetAllNodes().Num() : 0));
			for (const FBPVariableDescription& V : BP->NewVariables)
			{
				CkDiffFact(Facts, FString::Printf(TEXT("Variable %s"), *V.VarName.ToString()), V.VarType.PinCategory.ToString() + (V.DefaultValue.IsEmpty() ? FString() : TEXT(" = ") + V.DefaultValue));
			}
		}
		else if (UMaterial* Material = Cast<UMaterial>(Asset))
		{
			CkDiffFact(Facts, TEXT("Domain"), CkDiffEnum(StaticEnum<EMaterialDomain>(), Material->MaterialDomain));
			CkDiffFact(Facts, TEXT("Blend mode"), CkDiffEnum(StaticEnum<EBlendMode>(), Material->BlendMode));
			CkDiffFact(Facts, TEXT("Two sided"), Material->IsTwoSided() ? TEXT("True") : TEXT("False"));
			CkDiffFact(Facts, TEXT("Expressions"), FString::FromInt(Material->GetExpressions().Num()));
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Material->GetAllScalarParameterInfo(Infos, Ids);
			for (const FMaterialParameterInfo& Info : Infos)
			{
				float Value = 0.f;
				Material->GetScalarParameterDefaultValue(Info, Value);
				CkDiffFact(Facts, FString::Printf(TEXT("Scalar parameter %s"), *Info.Name.ToString()), FString::SanitizeFloat(Value));
			}
			Infos.Reset(); Ids.Reset();
			Material->GetAllVectorParameterInfo(Infos, Ids);
			for (const FMaterialParameterInfo& Info : Infos)
			{
				FLinearColor Value;
				Material->GetVectorParameterDefaultValue(Info, Value);
				CkDiffFact(Facts, FString::Printf(TEXT("Vector parameter %s"), *Info.Name.ToString()), Value.ToString());
			}
			Infos.Reset(); Ids.Reset();
			Material->GetAllTextureParameterInfo(Infos, Ids);
			for (const FMaterialParameterInfo& Info : Infos)
			{
				UTexture* Value = nullptr;
				Material->GetTextureParameterDefaultValue(Info, Value);
				CkDiffFact(Facts, FString::Printf(TEXT("Texture parameter %s"), *Info.Name.ToString()), GetPathNameSafe(Value));
			}
		}
		else if (UMaterialInstance* MI = Cast<UMaterialInstance>(Asset))
		{
			CkDiffFact(Facts, TEXT("Parent"), GetPathNameSafe(MI->Parent));
			for (const FScalarParameterValue& P : MI->ScalarParameterValues)
			{
				CkDiffFact(Facts, FString::Printf(TEXT("Scalar parameter %s"), *P.ParameterInfo.Name.ToString()), FString::SanitizeFloat(P.ParameterValue));
			}
			for (const FVectorParameterValue& P : MI->VectorParameterValues)
			{
				CkDiffFact(Facts, FString::Printf(TEXT("Vector parameter %s"), *P.ParameterInfo.Name.ToString()), P.ParameterValue.ToString());
			}
			for (const FTextureParameterValue& P : MI->TextureParameterValues)
			{
				CkDiffFact(Facts, FString::Printf(TEXT("Texture parameter %s"), *P.ParameterInfo.Name.ToString()), GetPathNameSafe(P.ParameterValue));
			}
		}
		else if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
		{
			CkDiffFact(Facts, TEXT("LODs"), FString::FromInt(Mesh->GetNumLODs()));
			for (int32 Lod = 0; Lod < Mesh->GetNumLODs(); ++Lod)
			{
				CkDiffFact(Facts, FString::Printf(TEXT("LOD %d"), Lod), FString::Printf(TEXT("%d vertices, %d triangles"), Mesh->GetNumVertices(Lod), Mesh->GetNumTriangles(Lod)));
			}
			const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				CkDiffFact(Facts, CkDiffSlotLabel(i, Slots[i].MaterialSlotName), GetPathNameSafe(Slots[i].MaterialInterface));
			}
			CkDiffFact(Facts, TEXT("Bounds extent"), CkDiffVector(Mesh->GetBounds().BoxExtent));
			if (const UBodySetup* Body = Mesh->GetBodySetup())
			{
				CkDiffFact(Facts, TEXT("Collision complexity"), (Body->CollisionTraceFlag == CTF_UseSimpleAsComplex ? TEXT("Use Simple Collision As Complex") : Body->CollisionTraceFlag == CTF_UseComplexAsSimple ? TEXT("Use Complex Collision As Simple") : TEXT("Project Default")));
				CkDiffFact(Facts, TEXT("Simple collision"), FString::Printf(TEXT("%d boxes, %d spheres, %d capsules, %d convex"),
					Body->AggGeom.BoxElems.Num(), Body->AggGeom.SphereElems.Num(), Body->AggGeom.SphylElems.Num(), Body->AggGeom.ConvexElems.Num()));
			}
			else
			{
				CkDiffFact(Facts, TEXT("Collision"), TEXT("none"));
			}
		}
		else if (USkeletalMesh* Skel = Cast<USkeletalMesh>(Asset))
		{
			const FSkeletalMeshRenderData* Render = Skel->GetResourceForRendering();
			const int32 Lods = Render ? Render->LODRenderData.Num() : 0;
			CkDiffFact(Facts, TEXT("LODs"), FString::FromInt(Lods));
			for (int32 Lod = 0; Lod < Lods; ++Lod)
			{
				const FSkeletalMeshLODRenderData& Data = Render->LODRenderData[Lod];
				CkDiffFact(Facts, FString::Printf(TEXT("LOD %d"), Lod), FString::Printf(TEXT("%d vertices, %d triangles"), (int32)Data.GetNumVertices(), (int32)Data.GetTotalFaces()));
			}
			const TArray<FSkeletalMaterial>& Slots = Skel->GetMaterials();
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				CkDiffFact(Facts, CkDiffSlotLabel(i, Slots[i].MaterialSlotName), GetPathNameSafe(Slots[i].MaterialInterface));
			}
			CkDiffFact(Facts, TEXT("Skeleton"), GetPathNameSafe(Skel->GetSkeleton()));
			CkDiffFact(Facts, TEXT("Physics asset"), GetPathNameSafe(Skel->GetPhysicsAsset()));
			CkDiffFact(Facts, TEXT("Bounds extent"), CkDiffVector(Skel->GetBounds().BoxExtent));
		}
		else if (UTexture* Texture = Cast<UTexture>(Asset))
		{
			CkDiffFact(Facts, TEXT("Source size"), FString::Printf(TEXT("%d x %d"), (int32)Texture->Source.GetSizeX(), (int32)Texture->Source.GetSizeY()));
			if (UTexture2D* T2D = Cast<UTexture2D>(Texture))
			{
				CkDiffFact(Facts, TEXT("Platform size"), FString::Printf(TEXT("%d x %d"), T2D->GetSizeX(), T2D->GetSizeY()));
				CkDiffFact(Facts, TEXT("Pixel format"), CkDiffEnum(StaticEnum<EPixelFormat>(), T2D->GetPixelFormat()));
			}
			CkDiffFact(Facts, TEXT("Compression"), CkDiffEnum(StaticEnum<TextureCompressionSettings>(), Texture->CompressionSettings));
			CkDiffFact(Facts, TEXT("sRGB"), Texture->SRGB ? TEXT("True") : TEXT("False"));
			CkDiffFact(Facts, TEXT("LOD group"), CkDiffEnum(StaticEnum<TextureGroup>(), Texture->LODGroup));
		}
		else if (UDataTable* Table = Cast<UDataTable>(Asset))
		{
			CkDiffFact(Facts, TEXT("Row struct"), GetPathNameSafe(Table->RowStruct));
			CkDiffFact(Facts, TEXT("Rows"), FString::FromInt(Table->GetRowMap().Num()));
			TArray<FString> Names;
			for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
			{
				if (Names.Num() == 100) { Names.Add(FString::Printf(TEXT("and %d more"), Table->GetRowMap().Num() - 100)); break; }
				Names.Add(Row.Key.ToString());
			}
			CkDiffFact(Facts, TEXT("Row names"), FString::Join(Names, TEXT(", ")));
		}
		else if (USoundWave* Sound = Cast<USoundWave>(Asset))
		{
			CkDiffFact(Facts, TEXT("Duration"), FString::Printf(TEXT("%.3f s"), Sound->Duration));
			CkDiffFact(Facts, TEXT("Channels"), FString::FromInt(Sound->NumChannels));
			CkDiffFact(Facts, TEXT("Sample rate"), FString::FromInt((int32)Sound->GetSampleRateForCurrentPlatform()));
		}
		else if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
		{
			const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
			CkDiffFact(Facts, TEXT("Emitters"), FString::FromInt(Handles.Num()));
			for (const FNiagaraEmitterHandle& Handle : Handles)
			{
				CkDiffFact(Facts, FString::Printf(TEXT("Emitter %s"), *Handle.GetName().ToString()), Handle.GetIsEnabled() ? TEXT("enabled") : TEXT("disabled"));
			}
		}
		const TArray<AActor*> Actors = CkDiffActors(Asset, Package);
		if (Cast<UWorld>(Asset))
		{
			CkDiffFact(Facts, TEXT("Actors"), FString::FromInt(Actors.Num()));
			int32 Listed = 0;
			for (const AActor* Actor : Actors)
			{
				if (++Listed > 200) break;
				CkDiffFact(Facts, FString::Printf(TEXT("Actor %s"), *CkDiffActorLabel(Actor)), Actor->GetClass()->GetName());
			}
		}
		else if (!Asset && Actors.Num() > 0)
		{
			CkDiffActorFacts(Actors[0], Facts, FString());
		}
		return Facts;
	}

	// ── Loading and thumbnails ──────────────────────────────────────────────

	struct FCkDiffSide
	{
		UPackage* Package = nullptr;
		UObject* Asset = nullptr;
		FString Error;
	};

	/** The asset (or, for an actor package, nothing) and its package, loaded for diff. */
	FCkDiffSide CkDiffLoad(const FString& File, const FPackagePath& OriginalPath)
	{
		FCkDiffSide Side;
		if (!IFileManager::Get().FileExists(*File))
		{
			Side.Error = FString::Printf(TEXT("the package could not be loaded for diff: %s does not exist"), *File);
			return Side;
		}
		// A file that is not a package can come back from the loader as an empty package, which keeps the
		// file open; read the package tag first.
		{
			uint32 Tag = 0;
			TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*File));
			if (Reader && Reader->TotalSize() >= (int64)sizeof(Tag)) *Reader << Tag;
			if (Tag != PACKAGE_FILE_TAG && Tag != PACKAGE_FILE_TAG_SWAPPED)
			{
				Side.Error = FString::Printf(TEXT("the package could not be loaded for diff: %s is not a package file"), *File);
				return Side;
			}
		}
		FMCPScopedLogCapture Capture({});
		Side.Package = DiffUtils::LoadPackageForDiff(FPackagePath::FromLocalPath(File), OriginalPath);
		if (!Side.Package)
		{
			FString Why;
			for (const FMCPScopedLogCapture::FLine& Line : Capture.GetLines())
			{
				if (!Why.IsEmpty()) Why += TEXT(" | ");
				Why += Line.Message;
			}
			Side.Error = FString::Printf(TEXT("the package could not be loaded for diff: %s"),
				Why.IsEmpty() ? TEXT("LoadPackage returned nothing and logged no reason") : *Why.Left(1200));
			return Side;
		}
		Side.Asset = Side.Package->FindAssetInPackage();
		if (!Side.Asset && CkDiffActors(nullptr, Side.Package).Num() == 0)
		{
			Side.Error = TEXT("the package loaded for diff, but it holds no asset and no actor");
		}
		return Side;
	}

	bool CkDiffWritePng(const FObjectThumbnail& Thumb, const FString& OutFile, FString& OutError)
	{
		const int32 W = Thumb.GetImageWidth();
		const int32 H = Thumb.GetImageHeight();
		const TArray<uint8>& Bytes = const_cast<FObjectThumbnail&>(Thumb).GetUncompressedImageData();
		if (W <= 0 || H <= 0 || Bytes.Num() < W * H * 4)
		{
			OutError = TEXT("the thumbnail came back empty");
			return false;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetRaw(Bytes.GetData(), Bytes.Num(), W, H, ERGBFormat::BGRA, 8))
		{
			OutError = TEXT("PNG encoding failed");
			return false;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutFile), true);
		if (!FFileHelper::SaveArrayToFile(Png->GetCompressed(100), *OutFile))
		{
			OutError = FString::Printf(TEXT("could not write %s"), *OutFile);
			return false;
		}
		return true;
	}

	/**
	 * A picture of one version: rendered with ThumbnailTools::RenderThumbnail on
	 * the object loaded for diff, or, where the class has no renderer or is a level,
	 * the thumbnail saved inside that version's package.
	 */
	TSharedPtr<FJsonObject> CkDiffThumbnail(UObject* Asset, const FString& File, const FString& OriginalPackage, const FString& OutFile)
	{
		TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
		if (OutFile.IsEmpty()) return nullptr;
		FString Error;
		const bool bLevel = Asset && Asset->IsA<UWorld>();
		// A texture loaded for diff can still be building, with no resource for the renderer to draw.
		if (UTexture* Texture = Cast<UTexture>(Asset))
		{
			Texture->BlockOnAnyAsyncBuild();
			if (!Texture->GetResource()) Texture->UpdateResource();
			Texture->BlockOnAnyAsyncBuild();
		}
		FThumbnailRenderingInfo* Info = (Asset && !bLevel) ? UThumbnailManager::Get().GetRenderingInfo(Asset) : nullptr;
		if (Info && Info->Renderer)
		{
			FAssetCompilingManager::Get().FinishAllCompilation();
			if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
			FObjectThumbnail Thumb;
			ThumbnailTools::RenderThumbnail(Asset, CkDiffThumbnailSize, CkDiffThumbnailSize,
				ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush, nullptr, &Thumb);
			if (CkDiffWritePng(Thumb, OutFile, Error))
			{
				T->SetStringField(TEXT("file"), OutFile);
				T->SetStringField(TEXT("source"), TEXT("rendered"));
				return T;
			}
		}
		else
		{
			Error = bLevel ? TEXT("a level loaded for diff has no initialized scene to render")
				: Asset ? FString::Printf(TEXT("Unreal has no thumbnail renderer for %s"), *Asset->GetClass()->GetName())
				: TEXT("an actor package has no thumbnail renderer");
		}
		// The thumbnail saved with this version, under the name it had in the project.
		if (Asset)
		{
			const FName FullName(*FString::Printf(TEXT("%s %s.%s"), *Asset->GetClass()->GetName(), *OriginalPackage, *Asset->GetName()));
			FThumbnailMap Map;
			if (ThumbnailTools::LoadThumbnailsFromPackage(File, { FullName }, Map))
			{
				if (const FObjectThumbnail* Saved = Map.Find(FullName))
				{
					FString SaveError;
					if (!Saved->IsEmpty() && CkDiffWritePng(*Saved, OutFile, SaveError))
					{
						T->SetStringField(TEXT("file"), OutFile);
						T->SetStringField(TEXT("source"), TEXT("saved"));
						T->SetStringField(TEXT("note"), Error);
						return T;
					}
				}
			}
		}
		T->SetStringField(TEXT("error"), Error + TEXT(", and this version's package has no saved thumbnail"));
		return T;
	}

	TSharedPtr<FJsonObject> CkDiffSideJson(const FCkDiffSide& Side, const FString& File, const FString& OriginalPackage, const FString& Thumbnail)
	{
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("class"), Side.Asset ? Side.Asset->GetClass()->GetName()
			: (CkDiffActors(nullptr, Side.Package).Num() ? CkDiffActors(nullptr, Side.Package)[0]->GetClass()->GetName() : FString()));
		if (Side.Asset) S->SetStringField(TEXT("objectName"), Side.Asset->GetName());
		S->SetArrayField(TEXT("facts"), CkDiffFacts(Side.Asset, Side.Package));
		if (TSharedPtr<FJsonObject> Thumb = CkDiffThumbnail(Side.Asset, File, OriginalPackage, Thumbnail))
		{
			S->SetObjectField(TEXT("thumbnail"), Thumb);
		}
		return S;
	}

	/** Which of the engine's diff paths serves an asset of this class. */
	struct FCkDiffEngine
	{
		FString Tool;
		FString Note;
		/** "window": DiffAssets opens a Slate window; "details": SDetailsDiff; "text": an external text diff tool. */
		FString Native;
	};

	FCkDiffEngine CkDiffEngineFor(const UObject* Asset, bool bActorPackage)
	{
		FCkDiffEngine E;
		if (bActorPackage)
		{
			E.Tool = TEXT("DiffUtils::CompareUnrelatedObjects");
			E.Note = TEXT("An actor package of a World Partition level. Unreal's level diff writes the package as text for an external diff tool; these records are the details-panel property diff of the actor and its components.");
			E.Native = TEXT("details");
		}
		else if (Asset->IsA<UBlueprint>())
		{
			E.Tool = TEXT("SBlueprintDiff");
			E.Note = TEXT("The Blueprint diff: graphs through FGraphDiffControl::DiffGraphs, components through DiffUtils::CompareUnrelatedSCS, My Blueprint variables, Defaults on the class default object and Class Settings, the last three through DiffUtils::CompareUnrelatedStructs.");
			E.Native = TEXT("window");
		}
		else if (Asset->IsA<UMaterial>() || Asset->IsA<UMaterialFunction>())
		{
			E.Tool = TEXT("SMaterialDiff");
			E.Note = TEXT("The material diff: the material graph, rebuilt as the material editor shows it, through FGraphDiffControl::DiffGraphs, and the material's details through DiffUtils::CompareUnrelatedObjects.");
			E.Native = TEXT("window");
		}
		else if (Asset->IsA<UMaterialInstanceConstant>())
		{
			E.Tool = TEXT("SMaterialInstanceDiff");
			E.Note = TEXT("The material instance diff: the instance's details, overridden parameters included, through DiffUtils::CompareUnrelatedObjects.");
			E.Native = TEXT("window");
		}
		else if (Asset->IsA<UDataAsset>())
		{
			E.Tool = TEXT("SDetailsDiff");
			E.Note = TEXT("The data asset diff: the asset's details through DiffUtils::CompareUnrelatedObjects.");
			E.Native = TEXT("window");
		}
		else if (Asset->IsA<UDataTable>())
		{
			E.Tool = TEXT("UDataTable::GetTableAsCSV");
			E.Note = TEXT("Unreal diffs a DataTable as its CSV export in an external text diff tool. The CSV text of both versions is diffed here, and each row present in both is compared field by field with DiffUtils::CompareUnrelatedStructs.");
			E.Native = TEXT("text");
		}
		else if (Asset->IsA<UWorld>())
		{
			E.Tool = TEXT("DiffUtils::CompareUnrelatedObjects");
			E.Note = TEXT("Unreal's level diff writes the package as text for an external diff tool and has no in-editor view. These records match actors by name and run the details-panel property diff on each actor and its components.");
			E.Native = TEXT("text");
		}
		else
		{
			E.Tool = TEXT("DiffUtils::CompareUnrelatedObjects");
			E.Note = FString::Printf(TEXT("Unreal has no diff view of its own for %s: its asset diff exports both versions as text for an external diff tool. These records are the details-panel property diff (DiffUtils::CompareUnrelatedObjects), the comparison SDetailsDiff shows."), *Asset->GetClass()->GetName());
			E.Native = TEXT("details");
		}
		return E;
	}

	/** Actors matched by object name, each pair compared with the details diff, components included. */
	void CkDiffActorsSection(UObject* AssetA, UPackage* PackageA, UObject* AssetB, UPackage* PackageB, const FCkDiffNames& Names,
		TArray<TSharedPtr<FJsonValue>>& Sections, FCkDiffSink& Sink)
	{
		const TArray<AActor*> ActorsA = CkDiffActors(AssetA, PackageA);
		const TArray<AActor*> ActorsB = CkDiffActors(AssetB, PackageB);
		TMap<FName, AActor*> ByNameA, ByNameB;
		for (AActor* A : ActorsA) ByNameA.Add(A->GetFName(), A);
		for (AActor* B : ActorsB) ByNameB.Add(B->GetFName(), B);
		const bool bSingle = ActorsA.Num() == 1 && ActorsB.Num() == 1 && !AssetA && !AssetB;

		TArray<TSharedPtr<FJsonValue>> Records;
		int32 Hidden = 0;
		auto Presence = [&](AActor* Actor, bool bAdded)
		{
			if (!Sink.Take()) return;
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("group"), CkDiffActorLabel(Actor));
			Rec->SetStringField(TEXT("diffType"), bAdded ? TEXT("OBJECT_ADDED") : TEXT("OBJECT_REMOVED"));
			Rec->SetStringField(TEXT("displayString"), FString::Printf(TEXT("%s %s (%s)"), bAdded ? TEXT("Added actor") : TEXT("Removed actor"), *CkDiffActorLabel(Actor), *Actor->GetClass()->GetName()));
			TArray<TSharedPtr<FJsonValue>> Facts;
			CkDiffActorFacts(Actor, Facts, FString());
			Rec->SetArrayField(TEXT("facts"), Facts);
			Records.Add(MakeShared<FJsonValueObject>(Rec));
		};
		for (AActor* B : ActorsB)
		{
			AActor* const* A = bSingle ? &ActorsA[0] : ByNameA.Find(B->GetFName());
			if (!A) { Presence(B, true); continue; }
			const FString Group = CkDiffActorLabel(B);
			CkDiffObjects(*A, B, Group, Names, Records, Hidden, Sink);
			TMap<FName, UActorComponent*> CompsA;
			for (UActorComponent* C : (*A)->GetComponents()) if (C) CompsA.Add(C->GetFName(), C);
			for (UActorComponent* C : B->GetComponents())
			{
				if (!C) continue;
				if (UActorComponent** Old = CompsA.Find(C->GetFName()))
				{
					CkDiffObjects(*Old, C, Group + TEXT(" > ") + C->GetName(), Names, Records, Hidden, Sink);
				}
			}
		}
		if (!bSingle)
		{
			for (AActor* A : ActorsA)
			{
				if (!ByNameB.Contains(A->GetFName())) Presence(A, false);
			}
		}
		CkDiffAddPropertySection(Sections, TEXT("Actors"), TEXT("DiffUtils::CompareUnrelatedObjects"), Records, Hidden);
	}

	void CkDiffBlueprint(UBlueprint* A, UBlueprint* B, const FCkDiffNames& Names, TArray<TSharedPtr<FJsonValue>>& Sections, FCkDiffSink& Sink)
	{
		// Graphs, paired by path under the Blueprint as SBlueprintDiff pairs them.
		TArray<UEdGraph*> GraphsA, GraphsB;
		A->GetAllGraphs(GraphsA);
		B->GetAllGraphs(GraphsB);
		TMap<FString, TPair<UEdGraph*, UEdGraph*>> Pairs;
		for (UEdGraph* G : GraphsA) { FString P; G->GetPathName(A, P); Pairs.FindOrAdd(P).Key = G; }
		for (UEdGraph* G : GraphsB) { FString P; G->GetPathName(B, P); Pairs.FindOrAdd(P).Value = G; }
		Pairs.KeySort(TLess<FString>());
		for (const TPair<FString, TPair<UEdGraph*, UEdGraph*>>& Pair : Pairs)
		{
			UEdGraph* Old = Pair.Value.Key;
			UEdGraph* New = Pair.Value.Value;
			if (!CkDiffGraphNeeded(Old ? Old : New)) continue;
			const FString Label = CkDiffGraphLabel(New ? New : Old);
			TSharedPtr<FJsonObject> S = CkDiffSection(FString::Printf(TEXT("Graph: %s"), *Label), TEXT("graph"), TEXT("FGraphDiffControl::DiffGraphs"));
			if (!Old || !New)
			{
				TArray<TSharedPtr<FJsonValue>> One;
				TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
				Rec->SetStringField(TEXT("graph"), Label);
				Rec->SetStringField(TEXT("diffType"), New ? TEXT("OBJECT_ADDED") : TEXT("OBJECT_REMOVED"));
				Rec->SetStringField(TEXT("category"), New ? TEXT("ADDITION") : TEXT("SUBTRACTION"));
				Rec->SetStringField(TEXT("displayString"), FString::Printf(TEXT("%s graph %s (%d nodes)"), New ? TEXT("Added") : TEXT("Removed"), *Label, (New ? New : Old)->Nodes.Num()));
				One.Add(MakeShared<FJsonValueObject>(Rec));
				S->SetArrayField(TEXT("entries"), One);
				Sections.Add(MakeShared<FJsonValueObject>(S));
				continue;
			}
			TArray<TSharedPtr<FJsonValue>> Records;
			CkDiffGraph(Old, New, Label, Names, Records, Sink);
			if (Records.Num() == 0) continue;
			S->SetArrayField(TEXT("entries"), Records);
			Sections.Add(MakeShared<FJsonValueObject>(S));
		}

		// Components: DiffUtils::CompareUnrelatedSCS, as FSCSDiffControl runs it.
		{
			const TArray<FSCSResolvedIdentifier> HA = CkDiffSCSHierarchy(A);
			const TArray<FSCSResolvedIdentifier> HB = CkDiffSCSHierarchy(B);
			FSCSDiffRoot Root;
			DiffUtils::CompareUnrelatedSCS(A, HA, B, HB, Root);
			TArray<TSharedPtr<FJsonValue>> Records;
			const FText BPName = FText::FromString(B->GetName());
			for (const FSCSDiffEntry& Entry : Root.Entries)
			{
				if (!Sink.Take()) continue;
				TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
				Rec->SetStringField(TEXT("group"), Entry.TreeIdentifier.Name.ToString());
				Rec->SetStringField(TEXT("diffType"), CkDiffTreeTypeName(Entry.DiffType));
				Rec->SetStringField(TEXT("displayString"), DiffViewUtils::SCSDiffMessage(Entry, BPName).ToString());
				TArray<TSharedPtr<FJsonValue>> Loc;
				for (int32 I : Entry.TreeIdentifier.TreeLocation) Loc.Add(MakeShared<FJsonValueNumber>(I));
				Rec->SetArrayField(TEXT("treeLocation"), Loc);
				if (Entry.DiffType == ETreeDiffType::NODE_ADDED || Entry.DiffType == ETreeDiffType::NODE_REMOVED || Entry.DiffType == ETreeDiffType::NODE_TYPE_CHANGED)
				{
					// The component's class, from the side it exists on.
					const UObject* OB = CkDiffSCSObject(HB, Entry.TreeIdentifier);
					const UObject* OA = CkDiffSCSObject(HA, Entry.TreeIdentifier);
					TArray<TSharedPtr<FJsonValue>> Facts;
					if (OA && Entry.DiffType != ETreeDiffType::NODE_ADDED) CkDiffFact(Facts, Entry.DiffType == ETreeDiffType::NODE_TYPE_CHANGED ? TEXT("Class before") : TEXT("Class"), OA->GetClass()->GetName());
					if (OB && Entry.DiffType != ETreeDiffType::NODE_REMOVED) CkDiffFact(Facts, Entry.DiffType == ETreeDiffType::NODE_TYPE_CHANGED ? TEXT("Class after") : TEXT("Class"), OB->GetClass()->GetName());
					if (Facts.Num()) Rec->SetArrayField(TEXT("facts"), Facts);
				}
				if (Entry.DiffType == ETreeDiffType::NODE_PROPERTY_CHANGED)
				{
					const UObject* OA = CkDiffSCSObject(HA, Entry.TreeIdentifier);
					const UObject* OB = CkDiffSCSObject(HB, Entry.TreeIdentifier);
					Rec->SetStringField(TEXT("path"), Entry.PropertyDiff.Identifier.ToDisplayName());
					Rec->SetStringField(TEXT("propertyDiffType"), CkDiffPropertyTypeName(Entry.PropertyDiff.DiffType));
					Rec->SetField(TEXT("before"), OA ? CkDiffValue(Entry.PropertyDiff.Identifier.Resolve(OA), Names) : MakeShared<FJsonValueNull>());
					Rec->SetField(TEXT("after"), OB ? CkDiffValue(Entry.PropertyDiff.Identifier.Resolve(OB), Names) : MakeShared<FJsonValueNull>());
				}
				Records.Add(MakeShared<FJsonValueObject>(Rec));
			}
			if (Records.Num())
			{
				TSharedPtr<FJsonObject> S = CkDiffSection(TEXT("Components"), TEXT("components"), TEXT("DiffUtils::CompareUnrelatedSCS"));
				S->SetArrayField(TEXT("entries"), Records);
				Sections.Add(MakeShared<FJsonValueObject>(S));
			}
		}

		// My Blueprint: FMyBlueprintDiffControl's comparison of the declared variables.
		{
			TArray<TSharedPtr<FJsonValue>> Records;
			int32 Hidden = 0;
			TMap<FName, const FBPVariableDescription*> VarsA;
			for (const FBPVariableDescription& V : A->NewVariables) VarsA.Add(V.VarName, &V);
			TSet<FName> Seen;
			for (const FBPVariableDescription& V : B->NewVariables)
			{
				Seen.Add(V.VarName);
				if (const FBPVariableDescription* const* Old = VarsA.Find(V.VarName))
				{
					CkDiffProperties(FBPVariableDescription::StaticStruct(), *Old, A->GetPackage(), FBPVariableDescription::StaticStruct(), &V, B->GetPackage(),
						V.VarName.ToString(), FText::FromName(V.VarName), Names, Records, Hidden, Sink, /*bOnlyEditable*/ false);
				}
				else if (Sink.Take())
				{
					TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
					Rec->SetStringField(TEXT("group"), V.VarName.ToString());
					Rec->SetStringField(TEXT("diffType"), TEXT("PropertyAddedToB"));
					Rec->SetStringField(TEXT("displayString"), FString::Printf(TEXT("Added Variable %s"), *V.VarName.ToString()));
					Rec->SetField(TEXT("before"), MakeShared<FJsonValueNull>());
					Rec->SetStringField(TEXT("after"), V.VarType.PinCategory.ToString() + (V.DefaultValue.IsEmpty() ? FString() : TEXT(" = ") + V.DefaultValue));
					Records.Add(MakeShared<FJsonValueObject>(Rec));
				}
			}
			for (const FBPVariableDescription& V : A->NewVariables)
			{
				if (Seen.Contains(V.VarName) || !Sink.Take()) continue;
				TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
				Rec->SetStringField(TEXT("group"), V.VarName.ToString());
				Rec->SetStringField(TEXT("diffType"), TEXT("PropertyAddedToA"));
				Rec->SetStringField(TEXT("displayString"), FString::Printf(TEXT("Removed Variable %s"), *V.VarName.ToString()));
				Rec->SetStringField(TEXT("before"), V.VarType.PinCategory.ToString() + (V.DefaultValue.IsEmpty() ? FString() : TEXT(" = ") + V.DefaultValue));
				Rec->SetField(TEXT("after"), MakeShared<FJsonValueNull>());
				Records.Add(MakeShared<FJsonValueObject>(Rec));
			}
			CkDiffAddPropertySection(Sections, TEXT("My Blueprint"), TEXT("DiffUtils::CompareUnrelatedStructs"), Records, Hidden);
		}

		// Defaults: the class default objects, as SBlueprintDiff's Defaults panel compares them.
		{
			TArray<TSharedPtr<FJsonValue>> Records;
			int32 Hidden = 0;
			const UObject* CA = DiffUtils::GetCDO(A);
			const UObject* CB = DiffUtils::GetCDO(B);
			if (CA && CB) CkDiffObjects(CA, CB, FString(), Names, Records, Hidden, Sink);
			CkDiffAddPropertySection(Sections, TEXT("Defaults"), TEXT("DiffUtils::CompareUnrelatedObjects"), Records, Hidden);
		}
		// Class Settings: the Blueprint objects themselves.
		{
			TArray<TSharedPtr<FJsonValue>> Records;
			int32 Hidden = 0;
			CkDiffObjects(A, B, FString(), Names, Records, Hidden, Sink);
			CkDiffAddPropertySection(Sections, TEXT("Class Settings"), TEXT("DiffUtils::CompareUnrelatedObjects"), Records, Hidden);
		}
	}

	void CkDiffDataTable(UDataTable* A, UDataTable* B, const FCkDiffNames& Names, TSharedPtr<FJsonObject> Result,
		TArray<TSharedPtr<FJsonValue>>& Sections, FCkDiffSink& Sink)
	{
		// The engine's own representation of a DataTable diff: its CSV export, both sides.
		TSharedPtr<FJsonObject> Text = MakeShared<FJsonObject>();
		Text->SetStringField(TEXT("format"), TEXT("csv"));
		Text->SetStringField(TEXT("before"), Names.Clean(A->GetTableAsCSV()));
		Text->SetStringField(TEXT("after"), Names.Clean(B->GetTableAsCSV()));
		Result->SetObjectField(TEXT("engineText"), Text);

		TArray<TSharedPtr<FJsonValue>> Records;
		int32 Hidden = 0;
		const UScriptStruct* SA = A->GetRowStruct();
		const UScriptStruct* SB = B->GetRowStruct();
		for (const TPair<FName, uint8*>& Row : B->GetRowMap())
		{
			uint8* const* Old = A->GetRowMap().Find(Row.Key);
			if (Old && SA && SB)
			{
				CkDiffProperties(SA, *Old, A->GetPackage(), SB, Row.Value, B->GetPackage(), Row.Key.ToString(), FText::FromName(Row.Key), Names, Records, Hidden, Sink, /*bOnlyEditable*/ false);
			}
			else if (!Old && Sink.Take())
			{
				TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
				Rec->SetStringField(TEXT("group"), Row.Key.ToString());
				Rec->SetStringField(TEXT("diffType"), TEXT("OBJECT_ADDED"));
				Rec->SetStringField(TEXT("displayString"), FString::Printf(TEXT("Added row %s"), *Row.Key.ToString()));
				Records.Add(MakeShared<FJsonValueObject>(Rec));
			}
		}
		for (const TPair<FName, uint8*>& Row : A->GetRowMap())
		{
			if (B->GetRowMap().Contains(Row.Key) || !Sink.Take()) continue;
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("group"), Row.Key.ToString());
			Rec->SetStringField(TEXT("diffType"), TEXT("OBJECT_REMOVED"));
			Rec->SetStringField(TEXT("displayString"), FString::Printf(TEXT("Removed row %s"), *Row.Key.ToString()));
			Records.Add(MakeShared<FJsonValueObject>(Rec));
		}
		CkDiffAddPropertySection(Sections, TEXT("Rows"), TEXT("DiffUtils::CompareUnrelatedStructs"), Records, Hidden);
	}

	/** Non-default properties of one version: the details diff against its class defaults, which says what an added or deleted asset is. */
	void CkDiffAgainstDefaults(UObject* Asset, const FCkDiffNames& Names, TArray<TSharedPtr<FJsonValue>>& Sections, FCkDiffSink& Sink)
	{
		if (!Asset) return;
		const UObject* Defaults = Asset->GetClass()->GetDefaultObject();
		if (UBlueprint* BP = Cast<UBlueprint>(Asset))
		{
			// What the Blueprint sets: its class defaults against its parent's.
			const UObject* CDO = DiffUtils::GetCDO(BP);
			const UObject* ParentCDO = BP->ParentClass ? BP->ParentClass->GetDefaultObject() : nullptr;
			if (CDO && ParentCDO)
			{
				TArray<TSharedPtr<FJsonValue>> Records;
				int32 Hidden = 0;
				CkDiffProperties(ParentCDO->GetClass(), ParentCDO, ParentCDO->GetPackage(), CDO->GetClass(), CDO, CDO->GetPackage(),
					FString(), FText::FromString(BP->GetName()), Names, Records, Hidden, Sink);
				// Properties the parent does not have are the Blueprint's own; report them as set.
				CkDiffAddPropertySection(Sections, TEXT("Defaults (against parent class)"), TEXT("DiffUtils::CompareUnrelatedStructs"), Records, Hidden);
			}
			return;
		}
		TArray<TSharedPtr<FJsonValue>> Records;
		int32 Hidden = 0;
		CkDiffProperties(Defaults->GetClass(), Defaults, Defaults->GetPackage(), Asset->GetClass(), Asset, Asset->GetPackage(),
			FString(), FText::FromString(Asset->GetName()), Names, Records, Hidden, Sink);
		CkDiffAddPropertySection(Sections, TEXT("Properties (against class defaults)"), TEXT("DiffUtils::CompareUnrelatedObjects"), Records, Hidden);
	}

	void CkDiffUnload(TArray<UPackage*>& Packages)
	{
		if (Packages.Num() == 0) return;
		for (UPackage* Package : Packages) ResetLoaders(Package);
		FText Error;
		UPackageTools::UnloadPackages(Packages, Error, /*bUnloadDirtyPackages*/ true);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		Packages.Reset();
	}

	TSet<SWindow*> CkDiffWindows()
	{
		TSet<SWindow*> Out;
		if (!FSlateApplication::IsInitialized()) return Out;
		TArray<TSharedRef<SWindow>> All;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(All);
		for (const TSharedRef<SWindow>& W : All) Out.Add(&W.Get());
		return Out;
	}

	TSharedPtr<SWindow> CkDiffNewWindow(const TSet<SWindow*>& Before)
	{
		if (!FSlateApplication::IsInitialized()) return nullptr;
		TArray<TSharedRef<SWindow>> All;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(All);
		for (const TSharedRef<SWindow>& W : All)
		{
			if (!Before.Contains(&W.Get())) return W;
		}
		return nullptr;
	}
}

TSharedPtr<FJsonValue> FDiffHandlers::DiffCheckpointAsset(const TSharedPtr<FJsonObject>& Params)
{
	TArray<UPackage*> Loaded;
	ON_SCOPE_EXIT { CkDiffUnload(Loaded); };
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!TryGetArrayParam(Params, TEXT("items"), Items)) return DiffCheckpointAssetOne(Params, Loaded);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const TSharedPtr<FJsonValue>& Item : *Items)
	{
		const TSharedPtr<FJsonObject>* ItemParams = nullptr;
		TSharedPtr<FJsonValue> One = Item.IsValid() && Item->TryGetObject(ItemParams)
			? DiffCheckpointAssetOne(*ItemParams, Loaded)
			: MCPError(TEXT("each item is an object of checkpoint_diff_asset parameters"));
		Results.Add(One.IsValid() ? One : MakeShared<FJsonValueNull>());
	}
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), Results);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDiffHandlers::DiffCheckpointAssetOne(const TSharedPtr<FJsonObject>& Params, TArray<UPackage*>& Loaded)
{
	FString OriginalFile;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("originalFile"), OriginalFile)) return Err;
	const FString FromFile = OptionalString(Params, TEXT("fromFile"), FString());
	const FString ToFile = OptionalString(Params, TEXT("toFile"), FString());
	const FString FromThumbnail = OptionalString(Params, TEXT("fromThumbnail"), FString());
	const FString ToThumbnail = OptionalString(Params, TEXT("toThumbnail"), FString());
	const int32 MaxRecords = FMath::Clamp(OptionalInt(Params, TEXT("maxRecords"), 2000), 1, 20000);
	if (FromFile.IsEmpty() && ToFile.IsEmpty()) return MCPError(TEXT("fromFile or toFile is required: the versions to compare"));

	FString PackageName;
	if (!FPackageName::TryConvertFilenameToLongPackageName(OriginalFile, PackageName))
	{
		return MCPError(FString::Printf(TEXT("%s is not under a mounted content folder"), *OriginalFile));
	}
	const FPackagePath OriginalPath = FPackagePath::FromLocalPath(OriginalFile);

	FCkDiffSide A, B;
	if (!FromFile.IsEmpty())
	{
		A = CkDiffLoad(FromFile, OriginalPath);
		if (A.Package) Loaded.AddUnique(A.Package);
		if (!A.Error.IsEmpty()) return MCPError(FString::Printf(TEXT("The version before: %s"), *A.Error));
	}
	if (!ToFile.IsEmpty())
	{
		B = CkDiffLoad(ToFile, OriginalPath);
		if (B.Package) Loaded.AddUnique(B.Package);
		if (!B.Error.IsEmpty()) return MCPError(FString::Printf(TEXT("The version after: %s"), *B.Error));
	}

	FCkDiffNames Names;
	Names.OriginalPackage = PackageName;
	if (A.Package) Names.TempPackages.Add(A.Package->GetName());
	if (B.Package) Names.TempPackages.Add(B.Package->GetName());

	const FCkDiffSide& Any = B.Package ? B : A;
	const bool bActorPackage = !Any.Asset;
	const FCkDiffEngine Engine = CkDiffEngineFor(Any.Asset, bActorPackage);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("packageName"), PackageName);
	Result->SetStringField(TEXT("status"), !A.Package ? TEXT("added") : !B.Package ? TEXT("deleted") : TEXT("modified"));
	Result->SetStringField(TEXT("assetClass"), Any.Asset ? Any.Asset->GetClass()->GetName() : TEXT("Actor"));
	{
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("tool"), Engine.Tool);
		E->SetStringField(TEXT("note"), Engine.Note);
		E->SetStringField(TEXT("native"), Engine.Native);
		// Whether checkpoint_open_native_diff can show this, decided by the same rules it runs.
		FString NativeReason;
		if (!A.Package || !B.Package)
		{
			if (bActorPackage) NativeReason = TEXT("An actor package of a World Partition level has no asset editor of its own.");
		}
		else if (bActorPackage && Engine.Native == TEXT("details"))
		{
			NativeReason = TEXT("An actor package has no asset to show in a diff window.");
		}
		else if (Engine.Native == TEXT("details") && !UE_MCP_CKDIFF_HAS_DETAILS_DIFF)
		{
			NativeReason = TEXT("The details diff window needs UE 5.5 or later.");
		}
		else if (Engine.Native == TEXT("text"))
		{
			const FString Tool = GetDefault<UEditorLoadingSavingSettings>()->TextDiffToolPath.FilePath;
			if (Tool.IsEmpty() || !IFileManager::Get().FileExists(*Tool))
			{
				NativeReason = FString::Printf(TEXT("Unreal has no in-editor diff view for %s: it diffs this type as text in an external diff tool, and Editor Preferences > Loading & Saving > Text Diff Tool is %s."),
					*Result->GetStringField(TEXT("assetClass")), Tool.IsEmpty() ? TEXT("not set") : *FString::Printf(TEXT("'%s', which does not exist"), *Tool));
			}
		}
		E->SetBoolField(TEXT("nativeAvailable"), NativeReason.IsEmpty());
		if (!NativeReason.IsEmpty()) E->SetStringField(TEXT("nativeReason"), NativeReason);
		Result->SetObjectField(TEXT("engine"), E);
	}
	if (A.Package) Result->SetObjectField(TEXT("before"), CkDiffSideJson(A, FromFile, PackageName, FromThumbnail));
	else Result->SetField(TEXT("before"), MakeShared<FJsonValueNull>());
	if (B.Package) Result->SetObjectField(TEXT("after"), CkDiffSideJson(B, ToFile, PackageName, ToThumbnail));
	else Result->SetField(TEXT("after"), MakeShared<FJsonValueNull>());

	FCkDiffSink Sink;
	Sink.Max = MaxRecords;
	TArray<TSharedPtr<FJsonValue>> Sections;

	if (A.Package && B.Package)
	{
		if (A.Asset && B.Asset && A.Asset->GetClass() != B.Asset->GetClass())
		{
			Result->SetStringField(TEXT("classChanged"), FString::Printf(TEXT("%s to %s"), *A.Asset->GetClass()->GetName(), *B.Asset->GetClass()->GetName()));
		}
		UBlueprint* BPA = Cast<UBlueprint>(A.Asset);
		UBlueprint* BPB = Cast<UBlueprint>(B.Asset);
		if (BPA && BPB)
		{
			CkDiffBlueprint(BPA, BPB, Names, Sections, Sink);
			TSharedPtr<FJsonValue> Semantic = DiffBlueprintObjects(BPA, BPB, PackageName, PackageName);
			if (Semantic.IsValid() && Semantic->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> S = Semantic->AsObject();
				S->RemoveField(TEXT("success"));
				Result->SetObjectField(TEXT("blueprint"), S);
			}
		}
		else if ((A.Asset && (A.Asset->IsA<UMaterial>() || A.Asset->IsA<UMaterialFunction>())) && B.Asset && B.Asset->GetClass() == A.Asset->GetClass())
		{
			UMaterialGraph* GA = CkDiffMaterialGraph(A.Asset);
			UMaterialGraph* GB = CkDiffMaterialGraph(B.Asset);
			TArray<TSharedPtr<FJsonValue>> Records;
			CkDiffGraph(GA, GB, CkDiffGraphLabel(GB), Names, Records, Sink);
			if (Records.Num())
			{
				TSharedPtr<FJsonObject> S = CkDiffSection(TEXT("Material Graph"), TEXT("graph"), TEXT("FGraphDiffControl::DiffGraphs"));
				S->SetArrayField(TEXT("entries"), Records);
				Sections.Add(MakeShared<FJsonValueObject>(S));
			}
			TArray<TSharedPtr<FJsonValue>> Props;
			int32 Hidden = 0;
			CkDiffObjects(A.Asset, B.Asset, FString(), Names, Props, Hidden, Sink);
			CkDiffAddPropertySection(Sections, TEXT("Details"), TEXT("DiffUtils::CompareUnrelatedObjects"), Props, Hidden);
		}
		else if (Cast<UDataTable>(A.Asset) && Cast<UDataTable>(B.Asset))
		{
			CkDiffDataTable(Cast<UDataTable>(A.Asset), Cast<UDataTable>(B.Asset), Names, Result, Sections, Sink);
			TArray<TSharedPtr<FJsonValue>> Props;
			int32 Hidden = 0;
			CkDiffObjects(A.Asset, B.Asset, FString(), Names, Props, Hidden, Sink);
			CkDiffAddPropertySection(Sections, TEXT("Details"), TEXT("DiffUtils::CompareUnrelatedObjects"), Props, Hidden);
		}
		else if (Cast<UWorld>(A.Asset) || Cast<UWorld>(B.Asset) || bActorPackage)
		{
			CkDiffActorsSection(A.Asset, A.Package, B.Asset, B.Package, Names, Sections, Sink);
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Props;
			int32 Hidden = 0;
			CkDiffObjects(A.Asset, B.Asset, FString(), Names, Props, Hidden, Sink);
			CkDiffAddPropertySection(Sections, TEXT("Details"), TEXT("DiffUtils::CompareUnrelatedObjects"), Props, Hidden);
		}
	}
	else
	{
		// One version: what it is, through the details diff against its defaults.
		const FCkDiffSide& Only = A.Package ? A : B;
		if (Only.Asset && !Only.Asset->IsA<UWorld>()) CkDiffAgainstDefaults(Only.Asset, Names, Sections, Sink);
	}

	int32 Count = 0;
	for (const TSharedPtr<FJsonValue>& S : Sections)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (S->AsObject()->TryGetArrayField(TEXT("entries"), Entries)) Count += Entries->Num();
	}
	Result->SetArrayField(TEXT("sections"), Sections);
	Result->SetNumberField(TEXT("recordCount"), Count);
	if (Sink.Dropped > 0) Result->SetNumberField(TEXT("recordsDropped"), Sink.Dropped);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDiffHandlers::OpenCheckpointNativeDiff(const TSharedPtr<FJsonObject>& Params)
{
	FString OriginalFile;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("originalFile"), OriginalFile)) return Err;
	const FString FromFile = OptionalString(Params, TEXT("fromFile"), FString());
	const FString ToFile = OptionalString(Params, TEXT("toFile"), FString());
	const bool bCurrent = OptionalBool(Params, TEXT("current"), false);
	const FString FromLabel = OptionalString(Params, TEXT("fromLabel"), TEXT("Before"));
	const FString ToLabel = OptionalString(Params, TEXT("toLabel"), bCurrent ? TEXT("Current") : TEXT("After"));

	FString PackageName;
	if (!FPackageName::TryConvertFilenameToLongPackageName(OriginalFile, PackageName))
	{
		return MCPError(FString::Printf(TEXT("%s is not under a mounted content folder"), *OriginalFile));
	}
	const FPackagePath OriginalPath = FPackagePath::FromLocalPath(OriginalFile);

	// The packages stay loaded while the window shows them; their files are released for the caller to delete.
	UObject* Old = nullptr;
	UObject* New = nullptr;
	if (!FromFile.IsEmpty())
	{
		FCkDiffSide Side = CkDiffLoad(FromFile, OriginalPath);
		if (!Side.Error.IsEmpty()) return MCPError(FString::Printf(TEXT("The version before: %s"), *Side.Error));
		ResetLoaders(Side.Package);
		Old = Side.Asset;
	}
	if (bCurrent)
	{
		New = LoadObject<UObject>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName)));
		if (!New) return MCPError(FString::Printf(TEXT("The current asset %s could not be loaded"), *PackageName));
	}
	else if (!ToFile.IsEmpty())
	{
		FCkDiffSide Side = CkDiffLoad(ToFile, OriginalPath);
		if (!Side.Error.IsEmpty()) return MCPError(FString::Printf(TEXT("The version after: %s"), *Side.Error));
		ResetLoaders(Side.Package);
		New = Side.Asset;
	}
	if (!Old && !New)
	{
		return MCPError(TEXT("Neither version holds an asset; an actor package of a World Partition level has no diff view in Unreal"));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	const TSet<SWindow*> Before = CkDiffWindows();

	if (!Old || !New)
	{
		// Added or deleted: open the one version in its asset editor.
		UObject* Only = Old ? Old : New;
		UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Editors || !Editors->OpenEditorForAsset(Only))
		{
			return MCPError(FString::Printf(TEXT("Unreal could not open an editor for %s"), *Only->GetClass()->GetName()));
		}
		Result->SetStringField(TEXT("opened"), TEXT("editor"));
		Result->SetStringField(TEXT("version"), Old ? TEXT("before") : TEXT("after"));
	}
	else
	{
		const FCkDiffEngine Engine = CkDiffEngineFor(New, false);
		FRevisionInfo OldRev;
		OldRev.Revision = FromLabel;
		FRevisionInfo NewRev;
		NewRev.Revision = ToLabel;
		if (Engine.Native == TEXT("window"))
		{
			IAssetTools::Get().DiffAssets(Old, New, OldRev, NewRev);
			Result->SetStringField(TEXT("opened"), TEXT("window"));
		}
		else if (Engine.Native == TEXT("details"))
		{
#if !UE_MCP_CKDIFF_HAS_DETAILS_DIFF
			return MCPErrorWithCode(TEXT("no_native_diff"), TEXT("The details diff window needs UE 5.5 or later"));
#else
			SDetailsDiff::CreateDiffWindow(FText::FromString(FString::Printf(TEXT("Diff of %s: %s to %s"), *New->GetName(), *FromLabel, *ToLabel)), Old, New, OldRev, NewRev);
			Result->SetStringField(TEXT("opened"), TEXT("window"));
#endif
		}
		else
		{
			const FString Tool = GetDefault<UEditorLoadingSavingSettings>()->TextDiffToolPath.FilePath;
			if (Tool.IsEmpty() || !IFileManager::Get().FileExists(*Tool))
			{
				return MCPErrorWithCode(TEXT("no_native_diff"), FString::Printf(
					TEXT("Unreal has no in-editor diff view for %s: it diffs this type as text in an external diff tool, and Editor Preferences > Loading & Saving > Text Diff Tool is %s."),
					*New->GetClass()->GetName(), Tool.IsEmpty() ? TEXT("not set") : *FString::Printf(TEXT("'%s', which does not exist"), *Tool)));
			}
			IAssetTools::Get().DiffAssets(Old, New, OldRev, NewRev);
			Result->SetStringField(TEXT("opened"), TEXT("external"));
			Result->SetStringField(TEXT("tool"), Tool);
			return MCPResult(Result);
		}
	}

	TSharedPtr<SWindow> Window = CkDiffNewWindow(Before);
	if (!Window.IsValid() && Result->GetStringField(TEXT("opened")) == TEXT("window"))
	{
		return MCPError(FString::Printf(TEXT("Unreal opened no diff window for %s"), *(New ? New : Old)->GetClass()->GetName()));
	}
	if (Window.IsValid())
	{
		Window->BringToFront(true);
		if (TSharedPtr<FGenericWindow> Native = Window->GetNativeWindow()) Native->HACK_ForceToFront();
		Result->SetStringField(TEXT("windowTitle"), Window->GetTitle().ToString());
	}
	else if (TSharedPtr<SWindow> Root = FGlobalTabmanager::Get()->GetRootWindow())
	{
		// An asset editor docked as a tab in the main window.
		Root->BringToFront(true);
		if (TSharedPtr<FGenericWindow> Native = Root->GetNativeWindow()) Native->HACK_ForceToFront();
		Result->SetStringField(TEXT("windowTitle"), Root->GetTitle().ToString());
	}
	return MCPResult(Result);
}
