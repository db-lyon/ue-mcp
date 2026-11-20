#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "HandlerUtils.h"
#include "HandlerMaterialCompile.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#if UE_MCP_HAS_5_8_API
#include "Materials/MaterialExpressionUtils.h"
#endif
#include "ObjectCacheContext.h"
#include "Engine/Texture.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

// A texture's settings decide the sampler type every node reading it must use:
// turn a color map linear, or move it off an HDR compression, and each material
// sampling it stops compiling ("Sampler type is Color, should be Linear Color")
// and draws the default material. The editor does not fix the nodes, and the
// texture's own PostEditChange does not recompile the materials either, so they
// kept their old shaders until a restart. The texture settings actions call
// UpdateMaterialsForTextures once per batch (#1994).
namespace MCPTextureSamplers
{
	/** True when the node's sampler type is the one its texture now needs. */
	inline bool SamplerMatchesTexture(const UMaterialExpressionTextureBase* Node, FString& OutReason)
	{
		OutReason.Reset();
		if (!Node || !Node->Texture) return true;
#if UE_MCP_HAS_5_8_API
		return MaterialExpressionUtils::VerifySamplerType(GMaxRHIShaderPlatform, nullptr, Node->Texture, Node->SamplerType, OutReason);
#else
		return UMaterialExpressionTextureBase::VerifySamplerType(GMaxRHIShaderPlatform, nullptr, Node->Texture, Node->SamplerType, OutReason);
#endif
	}

	inline FString SamplerTypeName(EMaterialSamplerType Type)
	{
		const UEnum* Enum = StaticEnum<EMaterialSamplerType>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Type)) : FString::FromInt(static_cast<int32>(Type));
	}

	/** The sampler type the node would take for Texture, read without leaving the node changed. */
	inline EMaterialSamplerType SamplerTypeFor(UMaterialExpressionTextureBase* Node, UTexture* Texture)
	{
		const TObjectPtr<UTexture> SavedTexture = Node->Texture;
		const EMaterialSamplerType SavedType = Node->SamplerType;
		Node->Texture = Texture;
		Node->AutoSetSampleType();
		const EMaterialSamplerType Needed = Node->SamplerType;
		Node->Texture = SavedTexture;
		Node->SamplerType = SavedType;
		return Needed;
	}

	/** After the textures' settings changed: sets every node sampling one of them to the sampler type the
	 *  texture now needs (when bFixSamplers), recompiles each affected material once for the whole batch
	 *  through the engine's material update, which re-caches the instances under it, and reports instances
	 *  whose texture overrides now disagree with their parent's sampler. With bDryRun only the plan is
	 *  reported. Returns false when an affected material does not compile; Result then carries the error. */
	inline bool UpdateMaterialsForTextures(const TSharedPtr<FJsonObject>& Result, const TArray<UTexture*>& Textures,
		bool bFixSamplers, bool bDryRun, bool bSave)
	{
		const TSet<UTexture*> Changed(Textures);
		TSet<UMaterial*> Materials;
		TSet<UMaterialFunction*> Functions;
		TSet<UMaterialInstance*> Instances;

		// Assets on disk that reference a texture, loaded so their nodes can be read.
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		for (UTexture* Texture : Textures)
		{
			TArray<FName> Referencers;
			Registry.GetReferencers(Texture->GetOutermost()->GetFName(), Referencers);
			for (const FName& PackageName : Referencers)
			{
				TArray<FAssetData> Assets;
				Registry.GetAssetsByPackageName(PackageName, Assets);
				for (const FAssetData& Data : Assets)
				{
					UClass* Class = Data.GetClass();
					if (!Class || !(Class->IsChildOf(UMaterialInterface::StaticClass()) || Class->IsChildOf(UMaterialFunction::StaticClass()))) continue;
					UObject* Asset = Data.GetAsset();
					if (UMaterial* M = Cast<UMaterial>(Asset)) Materials.Add(M);
					else if (UMaterialFunction* F = Cast<UMaterialFunction>(Asset)) Functions.Add(F);
					else if (UMaterialInstance* I = Cast<UMaterialInstance>(Asset)) Instances.Add(I);
				}
			}
		}
		// Loaded assets whose edits the registry has not seen yet, and the materials the engine tracks
		// through their compiled texture references.
		{
			FObjectCacheContextScope ObjectCache;
			for (UTexture* Texture : Textures)
			{
				for (UMaterialInterface* Interface : ObjectCache.GetContext().GetMaterialsAffectedByTexture(Texture))
				{
					if (UMaterial* M = Cast<UMaterial>(Interface)) Materials.Add(M);
					else if (UMaterialInstance* I = Cast<UMaterialInstance>(Interface)) Instances.Add(I);
				}
			}
		}
		for (TObjectIterator<UMaterial> It(RF_ClassDefaultObject | RF_Transient); It; ++It) Materials.Add(*It);
		for (TObjectIterator<UMaterialFunction> It(RF_ClassDefaultObject | RF_Transient); It; ++It) Functions.Add(*It);
		for (TObjectIterator<UMaterialInstance> It(RF_ClassDefaultObject | RF_Transient); It; ++It)
		{
			for (const FTextureParameterValue& Value : It->TextureParameterValues)
			{
				if (Value.ParameterValue && Changed.Contains(Value.ParameterValue)) { Instances.Add(*It); break; }
			}
		}

		TArray<TSharedPtr<FJsonValue>> Fixes;
		TSet<UMaterial*> EditedMaterials;
		TSet<UMaterialFunction*> EditedFunctions;
		TSet<UMaterial*> Affected;

		// Visits the nodes of one graph. Returns true when the graph samples a changed texture; sets
		// bOutEdited when it changed a node's sampler type.
		auto VisitGraph = [&](UObject* Owner, TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, bool& bOutEdited) -> bool
		{
			bool bSamples = false;
			bOutEdited = false;
			for (UMaterialExpression* Expression : Expressions)
			{
				UMaterialExpressionTextureBase* Node = Cast<UMaterialExpressionTextureBase>(Expression);
				if (!Node || !Node->Texture || !Changed.Contains(Node->Texture)) continue;
				bSamples = true;
				FString Reason;
				if (SamplerMatchesTexture(Node, Reason)) continue;
				const EMaterialSamplerType From = Node->SamplerType;
				const EMaterialSamplerType To = SamplerTypeFor(Node, Node->Texture);

				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("assetPath"), Owner->GetPathName());
				Row->SetStringField(TEXT("expression"), Node->GetName());
				Row->SetStringField(TEXT("texturePath"), Node->Texture->GetPathName());
				Row->SetStringField(TEXT("from"), SamplerTypeName(From));
				Row->SetStringField(TEXT("to"), SamplerTypeName(To));
				Row->SetStringField(TEXT("reason"), Reason);
				const bool bApply = bFixSamplers && !bDryRun;
				Row->SetBoolField(TEXT("applied"), bApply);
				Fixes.Add(MakeShared<FJsonValueObject>(Row));
				if (!bApply) continue;

				// The node's own edit notifications, as a details-panel edit sends them.
				FProperty* SamplerProp = Node->GetClass()->FindPropertyByName(TEXT("SamplerType"));
				Node->Modify();
				Node->PreEditChange(SamplerProp);
				Node->SamplerType = To;
				FPropertyChangedEvent SamplerChanged(SamplerProp, EPropertyChangeType::ValueSet);
				Node->PostEditChangeProperty(SamplerChanged);
				bOutEdited = true;
			}
			if (bOutEdited) Owner->MarkPackageDirty();
			return bSamples;
		};

		for (UMaterial* Material : Materials)
		{
			bool bEdited = false;
			if (Material && VisitGraph(Material, Material->GetExpressions(), bEdited)) Affected.Add(Material);
			if (bEdited) EditedMaterials.Add(Material);
		}
		for (UMaterialFunction* Function : Functions)
		{
			bool bEdited = false;
			if (Function) VisitGraph(Function, Function->GetExpressions(), bEdited);
			if (bEdited) EditedFunctions.Add(Function);
		}

		// An instance that overrides a texture parameter with a changed texture samples it through its
		// parent's node, which every other instance shares, so it is reported rather than edited.
		TArray<TSharedPtr<FJsonValue>> InstanceConflicts;
		for (UMaterialInstance* Instance : Instances)
		{
			UMaterial* Base = Instance ? Instance->GetMaterial() : nullptr;
			if (!Base) continue;
			Affected.Add(Base);
			for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
			{
				if (!Value.ParameterValue || !Changed.Contains(Value.ParameterValue)) continue;
				for (UMaterialExpression* Expression : Base->GetExpressions())
				{
					UMaterialExpressionTextureSampleParameter* Parameter = Cast<UMaterialExpressionTextureSampleParameter>(Expression);
					if (!Parameter || Parameter->ParameterName != Value.ParameterInfo.Name) continue;
					const EMaterialSamplerType Needed = SamplerTypeFor(Parameter, Value.ParameterValue);
					if (Needed == Parameter->SamplerType) continue;
					TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
					Row->SetStringField(TEXT("instancePath"), Instance->GetPathName());
					Row->SetStringField(TEXT("parameterName"), Value.ParameterInfo.Name.ToString());
					Row->SetStringField(TEXT("texturePath"), Value.ParameterValue->GetPathName());
					Row->SetStringField(TEXT("parentPath"), Base->GetPathName());
					Row->SetStringField(TEXT("samplerType"), SamplerTypeName(Parameter->SamplerType));
					Row->SetStringField(TEXT("needs"), SamplerTypeName(Needed));
					InstanceConflicts.Add(MakeShared<FJsonValueObject>(Row));
				}
			}
		}

		Result->SetBoolField(TEXT("fixSamplers"), bFixSamplers);
		Result->SetArrayField(TEXT("samplerFixes"), Fixes);
		Result->SetArrayField(TEXT("instanceSamplerConflicts"), InstanceConflicts);
		if (bDryRun) return true;

		// A function edit reaches the materials calling it through the editor's function update, which
		// recompiles them.
		for (UMaterialFunction* Function : EditedFunctions)
		{
			UMaterialEditingLibrary::UpdateMaterialFunction(Function, nullptr);
		}

		// One update for the whole batch, as the engine's UTexture::NotifyMaterials does it: each base
		// material compiles once, and the context re-caches every instance under it when it closes.
		// PostEditChange alone leaves a material that only reads the texture clean on disk.
		{
			FMaterialUpdateContext UpdateContext;
			for (UMaterial* Material : Affected)
			{
				UpdateContext.AddMaterial(Material);
				Material->PostEditChange();
			}
		}

		TArray<TSharedPtr<FJsonValue>> Recompiled;
		TArray<FString> Failing;
		for (UMaterial* Material : Affected)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("materialPath"), Material->GetPathName());
			Row->SetBoolField(TEXT("samplerFixed"), EditedMaterials.Contains(Material));
			if (!MCPMaterialCompile::Report(Row, Material, MCPMaterialCompile::FinishWithChildren(Material), MCPMaterialCompile::EOnError::Report))
			{
				Failing.Add(Material->GetPathName());
			}
			Recompiled.Add(MakeShared<FJsonValueObject>(Row));
		}
		Result->SetArrayField(TEXT("recompiledMaterials"), Recompiled);

		TArray<TSharedPtr<FJsonValue>> SaveErrors;
		if (bSave)
		{
			TArray<UObject*> ToSave;
			for (UMaterial* M : EditedMaterials) ToSave.Add(M);
			for (UMaterialFunction* F : EditedFunctions) ToSave.Add(F);
			for (UObject* Asset : ToSave)
			{
				FString SaveError;
				if (SaveAssetPackageChecked(Asset, SaveError)) continue;
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("assetPath"), Asset->GetPathName());
				Row->SetStringField(TEXT("error"), SaveError);
				SaveErrors.Add(MakeShared<FJsonValueObject>(Row));
			}
			if (SaveErrors.Num() > 0) Result->SetArrayField(TEXT("materialSaveErrors"), SaveErrors);
		}

		if (Failing.Num() > 0)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(
				TEXT("The texture settings were applied, but %d material(s) reading these textures, or instances of them, do not compile: %s. recompiledMaterials holds the errors.%s"),
				Failing.Num(), *FString::Join(Failing, TEXT(", ")),
				bFixSamplers ? TEXT("") : TEXT(" fixSamplers=true sets each node's sampler type to what its texture now needs.")));
			return false;
		}
		if (SaveErrors.Num() > 0)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("A material whose sampler type was fixed could not be saved; materialSaveErrors names it and why."));
			return false;
		}
		return true;
	}
}
