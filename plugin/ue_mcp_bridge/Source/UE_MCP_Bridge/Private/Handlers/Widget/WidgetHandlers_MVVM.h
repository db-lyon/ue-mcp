#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FMCPHandlerRegistry;

/**
 * Viewmodel (UMG MVVM) authoring on Widget Blueprints: the viewmodels a widget
 * blueprint declares, the bindings between their fields and the widgets, and a
 * compile that reports each binding's errors. The MVVM work is done by the
 * UE_MCP_BridgeMVVM module through Public/HandlerMVVMBackend.h, because the
 * plugin it needs may be disabled; these handlers own the specs, the parameter
 * reads, the asset resolution and the response.
 *
 * Registered by FWidgetHandlers::RegisterHandlers, under the widget category.
 */
class FWidgetMVVMHandlers
{
public:
	static void RegisterHandlers(FMCPHandlerRegistry& Registry);

	static TSharedPtr<FJsonValue> ListViewModels(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddViewModel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveViewModel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListViewModelBindings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddViewModelBinding(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> UpdateViewModelBinding(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveViewModelBinding(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CompileViewModels(const TSharedPtr<FJsonObject>& Params);
};
