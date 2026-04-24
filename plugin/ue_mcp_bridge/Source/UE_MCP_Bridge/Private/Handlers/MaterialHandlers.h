#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FMaterialHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> ListExpressionTypes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetMaterialShadingModel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetMaterialBlendMode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetMaterialBaseColor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddMaterialExpression(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListMaterialParameters(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RecompileMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMaterialInstance(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetMaterialParameter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectExpression(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectMaterialProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteExpression(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetExpressionValue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMaterialFromTexture(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadMaterialInstance(const TSharedPtr<FJsonObject>& Params);

	// Name-based handlers matching TS tool expectations
	static TSharedPtr<FJsonValue> ConnectTextureToMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectToMaterialProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteMaterialExpression(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DisconnectMaterialProperty(const TSharedPtr<FJsonObject>& Params);

	// Helper to load a UMaterial from an asset path
	static UMaterial* LoadMaterialFromPath(const FString& AssetPath);

	// Helper to load a UMaterialInstanceConstant from an asset path
	static UMaterialInstanceConstant* LoadMaterialInstanceFromPath(const FString& AssetPath);

	// Helper to convert shading model string to enum
	static EMaterialShadingModel ParseShadingModel(const FString& ShadingModelStr);
	static FString ShadingModelToString(EMaterialShadingModel ShadingModel);

	// Helper to map material property name string to EMaterialProperty enum
	static bool ParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty);

	// Resolve an EMaterialProperty to its backing FExpressionInput* on the
	// material's editor-only data. Returns nullptr when the property is not
	// one of the supported direct-connection slots. Centralises the switch
	// that previously lived verbatim in three places.
	static FExpressionInput* GetMaterialPropertyInput(
		class UMaterialEditorOnlyData* EditorOnlyData,
		EMaterialProperty MatProperty);

	// Helper to find an expression by name (description or class) within a material
	static UMaterialExpression* FindExpressionByName(UMaterial* Material, const FString& ExpressionName);

	// v0.7.9 — material depth
	static TSharedPtr<FJsonValue> DuplicateMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ValidateMaterial(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetMaterialShaderStats(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExportMaterialGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportMaterialGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BuildMaterialGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RenderMaterialPreview(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BeginMaterialTransaction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> EndMaterialTransaction(const TSharedPtr<FJsonObject>& Params);
};
