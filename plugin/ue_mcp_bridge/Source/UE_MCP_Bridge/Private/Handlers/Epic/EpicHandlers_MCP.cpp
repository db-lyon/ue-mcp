// Epic's Unreal MCP plugin (ModelContextProtocol, 5.8+, off by default) keeps its
// own tool list and resource providers beside the ToolsetRegistry. These
// handlers list and call them. The plugin's headers are compiled in, its DLL is
// never linked: everything below is a virtual call on objects the plugin made,
// so the bridge loads whether or not a project enables it.

#include "EpicHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerDeferred.h"

#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"

#if WITH_UEMCP_EPIC_MCP
#include "IModelContextProtocolModule.h"
#include "IModelContextProtocolResourceProvider.h"
#include "IModelContextProtocolTool.h"
#include "ModelContextProtocolResources.h"
#include "ModelContextProtocolSession.h"
#include "ModelContextProtocolToolResults.h"
#endif

namespace UEMCPEpicMCP
{
	static const TCHAR* const PluginUnavailable =
		TEXT("Epic's Unreal MCP plugin (ModelContextProtocol) is not loaded. It ships with UE 5.8+ and is off by default: "
		     "enable it in the project's plugins, then restart the editor.");

	static FString ReadInputJson(const TSharedPtr<FJsonObject>& Params)
	{
		FString Input = OptionalString(Params, TEXT("inputJson"), TEXT(""));
		const TSharedPtr<FJsonObject>* InObj = nullptr;
		if (Input.IsEmpty() && TryGetObjectParam(Params, TEXT("input"), InObj) && InObj && (*InObj).IsValid())
		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Input);
			FJsonSerializer::Serialize((*InObj).ToSharedRef(), Writer);
		}
		return Input.IsEmpty() ? FString(TEXT("{}")) : Input;
	}

	static TSharedPtr<FJsonValue> Unavailable(bool bAsList, const TCHAR* ListField)
	{
		// Listing reads as empty rather than failing: no plugin means no tools and no resources.
		if (!bAsList) return MCPError(PluginUnavailable);
		TSharedPtr<FJsonObject> Res = MCPSuccess();
		Res->SetBoolField(TEXT("available"), false);
		Res->SetStringField(TEXT("reason"), PluginUnavailable);
		Res->SetArrayField(ListField, TArray<TSharedPtr<FJsonValue>>());
		return MCPResult(Res);
	}

#if WITH_UEMCP_EPIC_MCP
	static IModelContextProtocolModule* Module()
	{
		return FModuleManager::GetModulePtr<IModelContextProtocolModule>(TEXT("ModelContextProtocol"));
	}

	// The names Epic's tool-search mode registers; each dispatches into the registry.
	static bool IsToolSearchMetaTool(const FString& Name)
	{
		return Name == TEXT("list_toolsets") || Name == TEXT("describe_toolset") || Name == TEXT("call_tool");
	}

	/** Qualified names of every registry tool, which Epic's eager mode registers as adapters under the same name. */
	static TSet<FString> RegistryToolNames()
	{
		TSet<FString> Names;
		FString Raw;
		TArray<TSharedPtr<FJsonValue>> Toolsets;
		if (!UEMCPEpic::GetAllSchemas(Raw)) return Names;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, Toolsets)) return Names;
		for (const TSharedPtr<FJsonValue>& TV : Toolsets)
		{
			const TSharedPtr<FJsonObject>* Toolset = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
			if (!TV.IsValid() || !TV->TryGetObject(Toolset) || !Toolset || !(*Toolset)->TryGetArrayField(TEXT("tools"), Tools) || !Tools) continue;
			FString ToolsetName;
			(*Toolset)->TryGetStringField(TEXT("name"), ToolsetName);
			for (const TSharedPtr<FJsonValue>& V : *Tools)
			{
				const TSharedPtr<FJsonObject>* Tool = nullptr;
				FString Name;
				if (!V.IsValid() || !V->TryGetObject(Tool) || !Tool || !(*Tool)->TryGetStringField(TEXT("name"), Name)) continue;
				Names.Add(Name.Contains(TEXT(".")) ? Name : ToolsetName + TEXT(".") + Name);
			}
		}
		return Names;
	}

	/** Tool name to the UModelContextProtocolToolLibrary class whose public function it is. */
	static TMap<FString, FString> LibraryToolOwners()
	{
		TMap<FString, FString> Owners;
		UClass* Base = FindObject<UClass>(nullptr, TEXT("/Script/ModelContextProtocolEngine.ModelContextProtocolToolLibrary"));
		if (!Base) return Owners;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class == Base || !Class->IsChildOf(Base) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists)
				|| Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
			{
				continue;
			}
			for (TFieldIterator<UFunction> Fn(Class, EFieldIteratorFlags::ExcludeSuper); Fn; ++Fn)
			{
				if (Fn->GetFName() != NAME_ExecuteUbergraph && Fn->HasAnyFunctionFlags(FUNC_Public))
				{
					Owners.Add(Fn->GetName(), Class->GetPathName());
				}
			}
		}
		return Owners;
	}

	/** An MCP tool result ({content, structuredContent, isError}) as a bridge answer body, images and audio moved to media. */
	static TSharedPtr<FJsonObject> AnswerFromToolResult(const FString& ToolName, const TSharedPtr<FJsonObject>& McpResult)
	{
		bool bIsError = false;
		if (McpResult.IsValid()) McpResult->TryGetBoolField(TEXT("isError"), bIsError);

		TArray<TSharedPtr<FJsonValue>> Content;
		TArray<TSharedPtr<FJsonValue>> Media;
		FString FirstText;
		const TArray<TSharedPtr<FJsonValue>>* Blocks = nullptr;
		if (McpResult.IsValid() && McpResult->TryGetArrayField(TEXT("content"), Blocks) && Blocks)
		{
			for (const TSharedPtr<FJsonValue>& Block : *Blocks)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				FString Type;
				if (!Block.IsValid() || !Block->TryGetObject(Obj) || !Obj) continue;
				(*Obj)->TryGetStringField(TEXT("type"), Type);
				if (Type == TEXT("image") || Type == TEXT("audio"))
				{
					Media.Add(Block);
					continue;
				}
				if (Type == TEXT("text") && FirstText.IsEmpty()) (*Obj)->TryGetStringField(TEXT("text"), FirstText);
				Content.Add(Block);
			}
		}

		TSharedPtr<FJsonObject> Res = bIsError
			? MCPErrorObject(FString::Printf(TEXT("Tool '%s' failed: %s"), *ToolName, FirstText.IsEmpty() ? TEXT("(no message)") : *FirstText))
			: MCPSuccess();
		Res->SetStringField(TEXT("tool"), ToolName);
		Res->SetArrayField(TEXT("content"), Content);
		if (Media.Num() > 0) Res->SetArrayField(TEXT("media"), Media);
		const TSharedPtr<FJsonValue> Structured = McpResult.IsValid() ? McpResult->TryGetField(TEXT("structuredContent")) : nullptr;
		if (Structured.IsValid()) Res->SetField(TEXT("structuredContent"), Structured);
		return Res;
	}

	/** Reads the protected JSON a provider wrote into a descriptor list or resource, without linking the plugin. */
	struct FDescriptorListAccess : FModelContextProtocolResourceDescriptorList
	{
		TArray<TSharedPtr<FJsonValue>> Descriptors() const { return JsonResourceDescriptorArray.AsJsonValue()->AsArray(); }
	};

	struct FResourceAccess : FModelContextProtocolResourceDescriptor
	{
		static TSharedRef<FJsonObject> Json(const FModelContextProtocolResourceDescriptor& Resource)
		{
			return (Resource.*(&FResourceAccess::JsonResourceObject)).AsJsonObject();
		}
	};
#endif
}

TSharedPtr<FJsonValue> FEpicHandlers::ListMCPTools(const TSharedPtr<FJsonObject>& Params)
{
	const FString Filter = OptionalString(Params, TEXT("nameFilter"), TEXT(""));
	const bool bIncludeSchemas = OptionalBool(Params, TEXT("includeSchemas"), false);
	const bool bIncludeRegistry = OptionalBool(Params, TEXT("includeRegistryAdapters"), false);
#if WITH_UEMCP_EPIC_MCP
	IModelContextProtocolModule* Module = UEMCPEpicMCP::Module();
	if (!Module) return UEMCPEpicMCP::Unavailable(true, TEXT("tools"));

	const TSet<FString> RegistryNames = UEMCPEpicMCP::RegistryToolNames();
	const TMap<FString, FString> LibraryOwners = UEMCPEpicMCP::LibraryToolOwners();
	TArray<TSharedPtr<FJsonValue>> Out;
	int32 RegistryAdapters = 0;
	for (const TSharedRef<IModelContextProtocolTool>& Tool : Module->GetTools())
	{
		const FString Name = Tool->GetName();
		// Adapters onto the registry are already epic(list_toolsets) and the epic_* actions.
		FString Source = TEXT("plugin");
		if (UEMCPEpicMCP::IsToolSearchMetaTool(Name)) Source = TEXT("toolSearch");
		else if (RegistryNames.Contains(Name)) Source = TEXT("toolsetRegistry");
		else if (LibraryOwners.Contains(Name)) Source = TEXT("toolLibrary");
		const bool bAdapter = Source == TEXT("toolSearch") || Source == TEXT("toolsetRegistry");
		if (bAdapter) ++RegistryAdapters;
		if ((bAdapter && !bIncludeRegistry) || (!Filter.IsEmpty() && !Name.Contains(Filter))) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("description"), Tool->GetDescription());
		Entry->SetStringField(TEXT("source"), Source);
		if (const FString* Owner = LibraryOwners.Find(Name)) Entry->SetStringField(TEXT("library"), *Owner);
		if (bIncludeSchemas)
		{
			if (TSharedPtr<FJsonObject> In = Tool->GetInputJsonSchema()) Entry->SetObjectField(TEXT("inputSchema"), In);
			if (TSharedPtr<FJsonObject> Output = Tool->GetOutputJsonSchema()) Entry->SetObjectField(TEXT("outputSchema"), Output);
		}
		Out.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetBoolField(TEXT("available"), true);
	Res->SetNumberField(TEXT("toolCount"), Out.Num());
	Res->SetNumberField(TEXT("registryAdapterCount"), RegistryAdapters);
	Res->SetArrayField(TEXT("tools"), Out);
	return MCPResult(Res);
#else
	return UEMCPEpicMCP::Unavailable(true, TEXT("tools"));
#endif
}

TSharedPtr<FJsonValue> FEpicHandlers::CallMCPTool(const TSharedPtr<FJsonObject>& Params)
{
	FString ToolName;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("tool"), ToolName)) return Err;
	const FString Input = UEMCPEpicMCP::ReadInputJson(Params);
#if WITH_UEMCP_EPIC_MCP
	IModelContextProtocolModule* Module = UEMCPEpicMCP::Module();
	if (!Module) return UEMCPEpicMCP::Unavailable(false, nullptr);

	TSharedPtr<IModelContextProtocolTool> Tool = Module->FindTool(ToolName);
	if (!Tool.IsValid())
	{
		return MCPError(FString::Printf(TEXT("No MCP tool named '%s' is registered. Use epic(list_mcp_tools) for the names."), *ToolName));
	}

	TSharedPtr<FJsonObject> Arguments;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Input);
	if (!FJsonSerializer::Deserialize(Reader, Arguments) || !Arguments.IsValid())
	{
		return MCPError(TEXT("input must be a JSON object"));
	}

	// The tool may answer at once or later, from any thread. Over the bridge the
	// answer is deferred either way; called in-process, only an answer given
	// during RunAsync can be returned.
	struct FSyncAnswer { TSharedPtr<FJsonValue> Value; };
	const TSharedRef<FSyncAnswer, ESPMode::ThreadSafe> Sync = MakeShared<FSyncAnswer, ESPMode::ThreadSafe>();
	const FMCPDeferredCallPtr Deferred = MCPDeferRequest();
	const FModelContextProtocolToolRequestId RequestId(MakeShared<FJsonValueString>(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)));
	const FString Name = Tool->GetName();
	if (Deferred)
	{
		// A cancel that landed while this handler was queued: the tool never starts.
		if (Deferred->IsCancelled()) return MCPDeferredMarker();
		Deferred->SetCancelNote(TEXT("Forwarded to the tool's CancelAsync, the same call Epic's server makes on notifications/cancelled."));
		Deferred->OnCancel([Tool, RequestId](const FString&) { Tool->CancelAsync(RequestId); });
	}
	Tool->RunAsync(RequestId, Arguments, [Deferred, Sync, Name](const FModelContextProtocolToolResult& Result)
	{
		TSharedPtr<FJsonObject> Res = UEMCPEpicMCP::AnswerFromToolResult(Name, Result.JsonObject);
		// A dispatcher: the effect and whether it found work to do are the tool's, and its result says neither.
		MCPSetNoRollback(Res, FString::Printf(
			TEXT("Ran the MCP tool '%s' registered with Epic's Unreal MCP plugin. Its effect is its own and its result "
			     "does not describe what it wrote, so there is nothing here to reverse."), *Name));
		Res->SetBoolField(TEXT("idempotencyObservable"), false);
		Res->SetStringField(TEXT("idempotencyNote"), TEXT("The tool decides whether it changed anything and its MCP result does not say."));
		TSharedPtr<FJsonValue> Answer = MCPResult(Res);
		if (Deferred) Deferred->Complete(Answer);
		else Sync->Value = Answer;
	});
	if (Deferred) return MCPDeferredMarker();
	if (Sync->Value.IsValid()) return Sync->Value;
	Tool->CancelAsync(RequestId);
	return MCPError(FString::Printf(TEXT("Tool '%s' completes asynchronously, and only a bridge request can wait for it."), *Name));
#else
	return UEMCPEpicMCP::Unavailable(false, nullptr);
#endif
}

TSharedPtr<FJsonValue> FEpicHandlers::ListMCPResources(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_UEMCP_EPIC_MCP
	IModelContextProtocolModule* Module = UEMCPEpicMCP::Module();
	if (!Module) return UEMCPEpicMCP::Unavailable(true, TEXT("resources"));

	TArray<TSharedPtr<FJsonValue>> Out;
	const TArray<TSharedRef<IModelContextProtocolResourceProvider>>& Providers = Module->GetResourceProviders();
	for (const TSharedRef<IModelContextProtocolResourceProvider>& Provider : Providers)
	{
		UEMCPEpicMCP::FDescriptorListAccess List;
		Provider->ListResources(List);
		Out.Append(List.Descriptors());
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetBoolField(TEXT("available"), true);
	Res->SetNumberField(TEXT("providerCount"), Providers.Num());
	Res->SetNumberField(TEXT("resourceCount"), Out.Num());
	Res->SetArrayField(TEXT("resources"), Out);
	return MCPResult(Res);
#else
	return UEMCPEpicMCP::Unavailable(true, TEXT("resources"));
#endif
}

TSharedPtr<FJsonValue> FEpicHandlers::ReadMCPResource(const TSharedPtr<FJsonObject>& Params)
{
	FString Uri;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("uri"), Uri)) return Err;
#if WITH_UEMCP_EPIC_MCP
	IModelContextProtocolModule* Module = UEMCPEpicMCP::Module();
	if (!Module) return UEMCPEpicMCP::Unavailable(false, nullptr);

	// The provider that listed the uri reads it, as Epic's server routes resources/read.
	for (const TSharedRef<IModelContextProtocolResourceProvider>& Provider : Module->GetResourceProviders())
	{
		UEMCPEpicMCP::FDescriptorListAccess List;
		Provider->ListResources(List);
		const bool bListed = List.Descriptors().ContainsByPredicate([&Uri](const TSharedPtr<FJsonValue>& D)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			FString Listed;
			return D.IsValid() && D->TryGetObject(Obj) && Obj && (*Obj)->TryGetStringField(TEXT("uri"), Listed) && Listed == Uri;
		});
		if (!bListed) continue;

		TValueOrError<FModelContextProtocolResource, FString> Read = Provider->ReadResource(Uri);
		if (Read.HasError())
		{
			return MCPError(FString::Printf(TEXT("Reading '%s' failed: %s"), *Uri, *Read.GetError()));
		}
		TSharedPtr<FJsonObject> Res = MCPSuccess();
		Res->SetStringField(TEXT("uri"), Uri);
		Res->SetArrayField(TEXT("contents"), { MakeShared<FJsonValueObject>(UEMCPEpicMCP::FResourceAccess::Json(Read.GetValue())) });
		return MCPResult(Res);
	}
	return MCPError(FString::Printf(TEXT("No registered resource provider lists '%s'. Use epic(list_mcp_resources) for the uris."), *Uri));
#else
	return UEMCPEpicMCP::Unavailable(false, nullptr);
#endif
}
