#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "HandlerUtils.h"
#include "Misc/App.h"
#include "RHI.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "MaterialEditingLibrary.h"
#include "UObject/UObjectIterator.h"

// One way to compile a material and one shape to report it in, shared by the
// material actions and by the texture settings actions that recompile the
// materials reading a texture (#1947, #1441).
//
// The shape, written by Report:
//   compileChecked         false in an editor that compiles no shaders (-nullrhi); nothing else is then known
//   compiled               true or false, present only when compileChecked; false if anything below failed
//   compileErrors          the compiler's own text; empty when it compiled or nothing was checked
//   instanceCompileErrors  [{instancePath, compileErrors}] for the static-permutation instances under the
//                          material, present when the call compiled them too (FinishWithChildren)
//   shaderState            what the renderer will draw for the material and those instances
// What a compile error does to the call depends on what the call is for (EOnError):
// one that exists to compile the material or to finish it in one go fails, with an
// error quoting the first compiler error; one edit of a graph that may be built over
// several calls keeps its success and carries the same text as warning; a read
// (validate, stats) reports the fields alone.
namespace MCPMaterialCompile
{
	struct FOutcome
	{
		/** False when this editor compiled no shaders, so Errors says nothing. */
		bool bChecked = false;
		TArray<FString> Errors;
		/** The static-permutation instances under the material were compiled too (FinishWithChildren). */
		bool bChildrenChecked = false;
		/** Instance path and its errors, for each of those instances that does not compile. */
		TArray<TPair<FString, TArray<FString>>> ChildErrors;
	};

	enum class EOnError : uint8
	{
		/** The call exists to compile the material or to finish it in one go: a compile error fails it. */
		Fail,
		/** The call is one edit of a graph that may be built over several calls (a Custom node gains an
		 *  input in one call and its wire in the next): success stands, and warning quotes the error. */
		Warn,
		/** The call only reads: report the errors and leave success alone. */
		Report,
	};

	/** Compiles what the material's current shader map still lacks for the platform this editor draws
	 *  with, waits for it, and reads the errors. The editor compiles a shader map's shaders on demand,
	 *  when something first draws with them, so without the request a fresh edit has compiled nothing
	 *  and reports no errors. Regenerates nothing and dirties nothing. */
	inline FOutcome Finish(UMaterialInterface* Material)
	{
		FOutcome Out;
		if (!Material || !FApp::CanEverRender()) return Out;
		Material->EnsureIsComplete();
		FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		if (!Resource) return Out;
		Resource->FinishCompilation();
		Out.Errors = Resource->GetCompileErrors();
		Out.bChecked = true;
		return Out;
	}

	/** True when the resource has a shader map that compiled without errors, which is what it draws with.
	 *  FMaterial::HasValidGameThreadShaderMap is not this test: the editor compiles a map's shaders on
	 *  demand, and a map that is never complete is never finalized, so it reads false for a material that
	 *  draws correctly. */
	inline bool HasShaderMap(const FMaterialResource* Resource)
	{
		return Resource && Resource->GetGameThreadShaderMap() != nullptr && Resource->GetCompileErrors().Num() == 0;
	}

	/** The loaded instances under Material that compile shader maps of their own (a static parameter
	 *  override), which is what draws wherever they are used. */
	inline TArray<UMaterialInstance*> PermutationChildren(UMaterial* Material)
	{
		TArray<UMaterialInstance*> Children;
		for (TObjectIterator<UMaterialInstance> It; It && Material; ++It)
		{
			if (*It && It->bHasStaticPermutationResource && It->IsChildOf(Material)) Children.Add(*It);
		}
		return Children;
	}

	/** Finish for the material and for each instance under it with a shader map of its own. An instance
	 *  can fail where its parent compiles: its static switches reach nodes the parent's defaults do not. */
	inline FOutcome FinishWithChildren(UMaterial* Material)
	{
		FOutcome Out = Finish(Material);
		if (!Out.bChecked) return Out;
		Out.bChildrenChecked = true;
		for (UMaterialInstance* Child : PermutationChildren(Material))
		{
			const FOutcome ChildOutcome = Finish(Child);
			if (ChildOutcome.Errors.Num() > 0) Out.ChildErrors.Add({ Child->GetPathName(), ChildOutcome.Errors });
		}
		return Out;
	}

	/** The editor's own recompile, which also re-caches the static-permutation instances under the
	 *  material, then the errors of the material and those instances once their compile has finished.
	 *  Marks the package dirty, as the editor does. */
	inline FOutcome Recompile(UMaterial* Material)
	{
		if (!Material) return FOutcome();
		UMaterialEditingLibrary::RecompileMaterial(Material);
		return FinishWithChildren(Material);
	}

	/** What the renderer will draw for a material and its loaded instances that compile their own shader
	 *  maps. A finished resource without a valid shader map renders as the default material (#1441). An
	 *  instance's shaders compile when something first draws it, so instances are judged only once
	 *  FinishWithChildren has compiled them (bChildrenCompiled). */
	inline TSharedPtr<FJsonObject> DescribeShaderState(UMaterial* Material, bool bChildrenCompiled)
	{
		TSharedPtr<FJsonObject> State = MakeShared<FJsonObject>();
		const FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
		const bool bFinished = Resource && Resource->IsCompilationFinished();
		State->SetBoolField(TEXT("compiling"), Resource && !bFinished);
		if (bFinished)
		{
			State->SetBoolField(TEXT("validShaderMap"), HasShaderMap(Resource));
		}

		const TArray<UMaterialInstance*> Children = PermutationChildren(Material);
		State->SetNumberField(TEXT("staticPermutationChildren"), Children.Num());
		if (!bChildrenCompiled)
		{
			if (Children.Num() > 0)
			{
				State->SetStringField(TEXT("childrenNote"), TEXT("The instances compile their own shaders when something first draws them, which this call did not wait for. recompile_material compiles and reports them."));
			}
			return State;
		}
		TArray<TSharedPtr<FJsonValue>> Fallbacks;
		for (UMaterialInstance* Instance : Children)
		{
			if (!HasShaderMap(Instance->GetMaterialResource(GMaxRHIShaderPlatform)))
			{
				Fallbacks.Add(MakeShared<FJsonValueString>(Instance->GetPathName()));
			}
		}
		State->SetArrayField(TEXT("childrenWithoutShaderMap"), Fallbacks);
		return State;
	}

	/** Writes the shape described at the top of this file onto Result. Returns false when the material
	 *  does not compile. */
	inline bool Report(const TSharedPtr<FJsonObject>& Result, UMaterialInterface* Material, const FOutcome& Outcome, EOnError OnError)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorValues;
		for (const FString& Error : Outcome.Errors) ErrorValues.Add(MakeShared<FJsonValueString>(Error));
		Result->SetArrayField(TEXT("compileErrors"), ErrorValues);
		Result->SetBoolField(TEXT("compileChecked"), Outcome.bChecked);
		if (UMaterial* Base = Cast<UMaterial>(Material))
		{
			Result->SetObjectField(TEXT("shaderState"), DescribeShaderState(Base, Outcome.bChildrenChecked));
		}
		else if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Material); Instance && Outcome.bChecked)
		{
			// An instance draws with its own shader map only when it overrides a static parameter.
			TSharedPtr<FJsonObject> State = MakeShared<FJsonObject>();
			State->SetBoolField(TEXT("staticPermutation"), Instance->bHasStaticPermutationResource);
			State->SetBoolField(TEXT("validShaderMap"), HasShaderMap(Instance->GetMaterialResource(GMaxRHIShaderPlatform)));
			Result->SetObjectField(TEXT("shaderState"), State);
		}
		if (!Outcome.bChecked)
		{
			Result->RemoveField(TEXT("compiled"));
			Result->SetStringField(TEXT("compileErrorsNote"),
				TEXT("This editor compiles no shaders (a -nullrhi session), so whether the material compiles was not checked. The same call in an editor that renders reports it."));
			return true;
		}
		Result->RemoveField(TEXT("compileErrorsNote"));
		if (Outcome.bChildrenChecked)
		{
			TArray<TSharedPtr<FJsonValue>> ChildRows;
			for (const TPair<FString, TArray<FString>>& Child : Outcome.ChildErrors)
			{
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("instancePath"), Child.Key);
				TArray<TSharedPtr<FJsonValue>> ChildErrorValues;
				for (const FString& Error : Child.Value) ChildErrorValues.Add(MakeShared<FJsonValueString>(Error));
				Row->SetArrayField(TEXT("compileErrors"), ChildErrorValues);
				ChildRows.Add(MakeShared<FJsonValueObject>(Row));
			}
			Result->SetArrayField(TEXT("instanceCompileErrors"), ChildRows);
		}
		const bool bCompiled = Outcome.Errors.Num() == 0 && Outcome.ChildErrors.Num() == 0;
		Result->SetBoolField(TEXT("compiled"), bCompiled);
		if (bCompiled || OnError == EOnError::Report) return bCompiled;

		const bool bOwnErrors = Outcome.Errors.Num() > 0;
		const FString Failing = bOwnErrors ? (Material ? Material->GetPathName() : FString(TEXT("(null)"))) : Outcome.ChildErrors[0].Key;
		const TArray<FString>& FirstErrors = bOwnErrors ? Outcome.Errors : Outcome.ChildErrors[0].Value;
		const FString Message = FString::Printf(
			TEXT("'%s' does not compile, so it draws as the default material: %s (%d error(s), all in %s)."),
			*Failing, *FirstErrors[0], FirstErrors.Num(), bOwnErrors ? TEXT("compileErrors") : TEXT("instanceCompileErrors"));
		if (OnError == EOnError::Warn)
		{
			Result->SetStringField(TEXT("warning"), Message + TEXT(" The edit was applied; a later edit that completes the graph clears it."));
			return false;
		}
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), Message + TEXT(" Any change this call made is still applied; fix the graph or undo it."));
		return false;
	}
}
