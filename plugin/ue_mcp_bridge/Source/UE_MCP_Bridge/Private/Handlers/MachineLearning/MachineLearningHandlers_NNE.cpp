// Neural Network Engine: the runtimes the editor has registered, model assets
// imported from a model file for chosen runtimes, and a timed CPU inference on
// one of them.
//
// NNE itself is an engine module (Engine/Source/Runtime/NNE). Build.cs links it
// when the engine has it and sets WITH_UEMCP_NNE; the runtimes are plugins
// (NNERuntimeORT, NNERuntimeLiteRT on 5.9, ...) that register themselves with
// NNE when enabled, so nothing here names one at compile time.

#include "MachineLearningHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_UEMCP_NNE && UE_MCP_HAS_5_5_API
#define UE_MCP_NNE_AVAILABLE 1
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntime.h"
#include "NNERuntimeCPU.h"
#include "NNETypes.h"
#if __has_include("NNERuntimeGPU.h")
#include "NNERuntimeGPU.h"
#define UE_MCP_NNE_HAS_GPU 1
#endif
#if __has_include("NNERuntimeRDG.h")
#include "NNERuntimeRDG.h"
#define UE_MCP_NNE_HAS_RDG 1
#endif
#if __has_include("NNERuntimeNPU.h")
#include "NNERuntimeNPU.h"
#define UE_MCP_NNE_HAS_NPU 1
#endif
#else
#define UE_MCP_NNE_AVAILABLE 0
#endif

#ifndef UE_MCP_NNE_HAS_GPU
#define UE_MCP_NNE_HAS_GPU 0
#endif
#ifndef UE_MCP_NNE_HAS_RDG
#define UE_MCP_NNE_HAS_RDG 0
#endif
#ifndef UE_MCP_NNE_HAS_NPU
#define UE_MCP_NNE_HAS_NPU 0
#endif

namespace
{
	/** Runtime plugins that ship with the engine, reported whether or not they are enabled. */
	const TCHAR* const NNEKnownRuntimePlugins[] = {
		TEXT("NNERuntimeORT"), TEXT("NNERuntimeLiteRT"), TEXT("NNERuntimeBasicCpu"),
		TEXT("NNERuntimeIREE"), TEXT("NNERuntimeRDG"), TEXT("NNERuntimeCoreML"),
	};

	TSharedPtr<FJsonObject> NNEPluginState(const TCHAR* PluginName)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("plugin"), PluginName);
		Row->SetBoolField(TEXT("installed"), Plugin.IsValid());
		Row->SetBoolField(TEXT("enabled"), Plugin.IsValid() && Plugin->IsEnabled());
		return Row;
	}

	TSharedPtr<FJsonValue> NNEUnavailableError()
	{
#if WITH_UEMCP_NNE
		return MCPUnsupportedEngineError(TEXT("The NNE actions"), TEXT("5.5"));
#else
		return MCPError(TEXT("This engine has no NNE module (Engine/Source/Runtime/NNE), so the NNE actions are not available."));
#endif
	}

#if UE_MCP_NNE_AVAILABLE
	FString NNEDataTypeName(ENNETensorDataType Type)
	{
		const UEnum* Enum = StaticEnum<ENNETensorDataType>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Type)) : FString::FromInt(static_cast<int32>(Type));
	}

	TArray<TSharedPtr<FJsonValue>> NNEShapeJson(TConstArrayView<int32> Dims)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 Dim : Dims) Out.Add(MakeShared<FJsonValueNumber>(Dim));
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> NNEShapeJson(TConstArrayView<uint32> Dims)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (uint32 Dim : Dims) Out.Add(MakeShared<FJsonValueNumber>(Dim));
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> NNEDescsJson(TConstArrayView<UE::NNE::FTensorDesc> Descs)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const UE::NNE::FTensorDesc& Desc : Descs)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Desc.GetName());
			Row->SetStringField(TEXT("dataType"), NNEDataTypeName(Desc.GetDataType()));
			// -1 is a dimension the model leaves to the caller.
			Row->SetArrayField(TEXT("shape"), NNEShapeJson(Desc.GetShape().GetData()));
			Out.Add(MakeShared<FJsonValueObject>(Row));
		}
		return Out;
	}

	bool NNERuntimeRegistered(const FString& Name)
	{
		return UE::NNE::GetAllRuntimeNames().Contains(Name);
	}
#endif
}

TSharedPtr<FJsonValue> FMachineLearningHandlers::NNEListRuntimes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));

	TArray<TSharedPtr<FJsonValue>> Plugins;
	for (const TCHAR* PluginName : NNEKnownRuntimePlugins) Plugins.Add(MakeShared<FJsonValueObject>(NNEPluginState(PluginName)));
	Result->SetArrayField(TEXT("runtimePlugins"), Plugins);

	// LiteRT is the 5.9 addition; say whether it is there, enabled and registered.
	TSharedPtr<FJsonObject> LiteRT = NNEPluginState(TEXT("NNERuntimeLiteRT"));
	LiteRT->SetStringField(TEXT("runtimeName"), TEXT("NNERuntimeLiteRT"));
	LiteRT->SetStringField(TEXT("modelFileType"), TEXT("tflite"));

#if UE_MCP_NNE_AVAILABLE
	TArray<TSharedPtr<FJsonValue>> Runtimes;
	for (const FString& Name : UE::NNE::GetAllRuntimeNames())
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Name);
		Row->SetBoolField(TEXT("cpu"), UE::NNE::GetRuntime<INNERuntimeCPU>(Name).IsValid());
#if UE_MCP_NNE_HAS_GPU
		Row->SetBoolField(TEXT("gpu"), UE::NNE::GetRuntime<INNERuntimeGPU>(Name).IsValid());
#endif
#if UE_MCP_NNE_HAS_RDG
		Row->SetBoolField(TEXT("rdg"), UE::NNE::GetRuntime<INNERuntimeRDG>(Name).IsValid());
#endif
#if UE_MCP_NNE_HAS_NPU
		Row->SetBoolField(TEXT("npu"), UE::NNE::GetRuntime<INNERuntimeNPU>(Name).IsValid());
#endif
		Runtimes.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("runtimes"), Runtimes);
	Result->SetNumberField(TEXT("count"), Runtimes.Num());
	Result->SetBoolField(TEXT("nneAvailable"), true);
	const bool bLiteRTRegistered = NNERuntimeRegistered(TEXT("NNERuntimeLiteRT"));
	LiteRT->SetBoolField(TEXT("runtimeRegistered"), bLiteRTRegistered);
	LiteRT->SetBoolField(TEXT("cpu"), bLiteRTRegistered && UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeLiteRT")).IsValid());
	bool bLiteRTEnabled = false;
	if (!bLiteRTRegistered && LiteRT->TryGetBoolField(TEXT("enabled"), bLiteRTEnabled) && bLiteRTEnabled)
	{
		// The 5.9 launcher build loads the plugin's module with the LiteRT library compiled out
		// (WITH_NNE_RUNTIME_LITERT=0 in its Build.cs), so it registers no runtime.
		LiteRT->SetStringField(TEXT("note"), TEXT("The NNERuntimeLiteRT plugin is enabled but registered no runtime. Engine builds that compile it without the LiteRT library (WITH_NNE_RUNTIME_LITERT=0) load the module and offer nothing; use NNERuntimeORT, or an engine built with LiteRT."));
	}
#else
	Result->SetArrayField(TEXT("runtimes"), TArray<TSharedPtr<FJsonValue>>());
	Result->SetNumberField(TEXT("count"), 0);
	Result->SetBoolField(TEXT("nneAvailable"), false);
	LiteRT->SetBoolField(TEXT("runtimeRegistered"), false);
#endif
	Result->SetObjectField(TEXT("liteRT"), LiteRT);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMachineLearningHandlers::NNEImportModel(const TSharedPtr<FJsonObject>& Params)
{
	FString SourceFile;
	if (auto Err = RequireString(Params, TEXT("sourceFile"), SourceFile)) return Err;
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString FileType = OptionalString(Params, TEXT("fileType"), TEXT(""));
	TArray<FString> TargetRuntimes;
	const TArray<TSharedPtr<FJsonValue>>* RuntimeValues = nullptr;
	if (TryGetArrayParam(Params, TEXT("targetRuntimes"), RuntimeValues) && RuntimeValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RuntimeValues)
		{
			FString Name;
			if (Value.IsValid() && Value->TryGetString(Name) && !Name.IsEmpty()) TargetRuntimes.AddUnique(Name);
		}
	}
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

#if UE_MCP_NNE_AVAILABLE
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("onConflict must be skip or error, got '%s'."), *OnConflict));
	}
	if (FileType.IsEmpty()) FileType = FPaths::GetExtension(SourceFile).ToLower();
	if (FileType.IsEmpty())
	{
		return MCPError(TEXT("The source file has no extension; pass fileType (onnx, tflite, ...)."));
	}
	if (!FPaths::FileExists(SourceFile))
	{
		return MCPError(FString::Printf(TEXT("sourceFile '%s' does not exist."), *SourceFile));
	}
	const int64 FileSize = IFileManager::Get().FileSize(*SourceFile);
	if (FileSize <= 0 || FileSize > (int64)1024 * 1024 * 1024)
	{
		return MCPError(FString::Printf(TEXT("sourceFile '%s' is empty or larger than 1 GB."), *SourceFile));
	}

	const TArray<FString> Registered = UE::NNE::GetAllRuntimeNames();
	for (const FString& Runtime : TargetRuntimes)
	{
		if (!Registered.Contains(Runtime))
		{
			return MCPError(FString::Printf(TEXT("Runtime '%s' is not registered. Registered runtimes: %s. A runtime registers when its plugin is enabled; nne_list_runtimes reports which are."),
				*Runtime, Registered.Num() > 0 ? *FString::Join(Registered, TEXT(", ")) : TEXT("(none)")));
		}
	}

	FString PackagePath;
	FString Name;
	{
		FString Package = AssetPath;
		int32 Dot = INDEX_NONE;
		if (Package.FindChar(TEXT('.'), Dot)) Package = Package.Left(Dot);
		if (!Package.StartsWith(TEXT("/")) || !FPackageName::IsValidLongPackageName(Package))
		{
			return MCPError(FString::Printf(TEXT("assetPath '%s' is not a valid long package name such as /Game/ML/NNE_Model."), *AssetPath));
		}
		PackagePath = FPackageName::GetLongPackagePath(Package);
		Name = FPackageName::GetLongPackageAssetName(Package);
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *SourceFile))
	{
		return MCPError(FString::Printf(TEXT("Could not read sourceFile '%s'."), *SourceFile));
	}

	FMCPAssetCreate<UNNEModelData> Create = MCPCreateAssetIdempotentNewObject<UNNEModelData>(Name, PackagePath, OnConflict, TEXT("NNEModelData"));
	if (Create.EarlyReturn) return Create.EarlyReturn;
	UNNEModelData* ModelData = Create.Asset;
	ModelData->Init(FileType, Bytes);
	if (TargetRuntimes.Num() > 0) ModelData->SetTargetRuntimes(TargetRuntimes);

	// Which runtimes can build a CPU model from it: this is what makes the model
	// data for each runtime, so a file a runtime cannot read shows up here.
	TArray<TSharedPtr<FJsonValue>> Compatibility;
	const TArray<FString> Candidates = TargetRuntimes.Num() > 0 ? TargetRuntimes : UE::NNE::GetAllRuntimeNames<INNERuntimeCPU>();
	for (const FString& Runtime : Candidates)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("runtime"), Runtime);
		TWeakInterfacePtr<INNERuntimeCPU> Cpu = UE::NNE::GetRuntime<INNERuntimeCPU>(Runtime);
		Row->SetBoolField(TEXT("cpuRuntime"), Cpu.IsValid());
		if (Cpu.IsValid())
		{
			Row->SetBoolField(TEXT("canCreateCpuModel"), Cpu->CanCreateModelCPU(ModelData) == INNERuntimeCPU::ECanCreateModelCPUStatus::Ok);
		}
		Compatibility.Add(MakeShared<FJsonValueObject>(Row));
	}

	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(ModelData, SaveError);
	const FString ObjectPath = ModelData->GetPathName();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, ObjectPath, bSaved, SaveError);
	Result->SetStringField(TEXT("assetPath"), ObjectPath);
	Result->SetStringField(TEXT("fileType"), FileType);
	Result->SetNumberField(TEXT("sizeBytes"), static_cast<double>(Bytes.Num()));
	TArray<TSharedPtr<FJsonValue>> Targets;
	for (const FString& Runtime : ModelData->GetTargetRuntimes()) Targets.Add(MakeShared<FJsonValueString>(Runtime));
	Result->SetArrayField(TEXT("targetRuntimes"), Targets);
	Result->SetStringField(TEXT("targetRuntimesNote"), Targets.Num() == 0 ? TEXT("No target runtimes set, so the model is cooked for every runtime.") : TEXT("Only these runtimes get cooked model data."));
	Result->SetArrayField(TEXT("compatibility"), Compatibility);
	MCPSetDeleteAssetRollback(Result, ModelData->GetOutermost()->GetName());
	return MCPResult(Result);
#else
	return NNEUnavailableError();
#endif
}

TSharedPtr<FJsonValue> FMachineLearningHandlers::NNERunInference(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString RuntimeName = OptionalString(Params, TEXT("runtime"), TEXT(""));
	const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
	const bool bHasInputs = TryGetArrayParam(Params, TEXT("inputs"), Inputs) && Inputs;
	const int32 Iterations = OptionalInt(Params, TEXT("iterations"), 1);
	const int32 MaxOutputValues = OptionalInt(Params, TEXT("maxOutputValues"), 256);

#if UE_MCP_NNE_AVAILABLE
	if (Iterations < 1 || Iterations > 1000)
	{
		return MCPError(FString::Printf(TEXT("iterations must be between 1 and 1000, got %d."), Iterations));
	}
	if (MaxOutputValues < 0 || MaxOutputValues > 65536)
	{
		return MCPError(FString::Printf(TEXT("maxOutputValues must be between 0 and 65536, got %d."), MaxOutputValues));
	}
	if (!bHasInputs)
	{
		return MCPError(TEXT("inputs is required: one entry per model input, each {values: number[], shape?: int[]}."));
	}

	UNNEModelData* ModelData = LoadAssetByPath<UNNEModelData>(AssetPath);
	if (!ModelData) return MCPAssetLoadError(AssetPath, TEXT("NNEModelData"));

	TWeakInterfacePtr<INNERuntimeCPU> Runtime;
	if (!RuntimeName.IsEmpty())
	{
		Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(RuntimeName);
		if (!Runtime.IsValid())
		{
			return MCPError(FString::Printf(TEXT("'%s' is not a registered CPU runtime. CPU runtimes: %s."),
				*RuntimeName, *FString::Join(UE::NNE::GetAllRuntimeNames<INNERuntimeCPU>(), TEXT(", "))));
		}
		if (Runtime->CanCreateModelCPU(ModelData) != INNERuntimeCPU::ECanCreateModelCPUStatus::Ok)
		{
			return MCPError(FString::Printf(TEXT("Runtime '%s' cannot build a CPU model from '%s' (file type %s). Check the file type it reads and the asset's target runtimes."),
				*RuntimeName, *AssetPath, *ModelData->GetFileType()));
		}
	}
	else
	{
		for (const FString& Candidate : UE::NNE::GetAllRuntimeNames<INNERuntimeCPU>())
		{
			TWeakInterfacePtr<INNERuntimeCPU> Cpu = UE::NNE::GetRuntime<INNERuntimeCPU>(Candidate);
			if (Cpu.IsValid() && Cpu->CanCreateModelCPU(ModelData) == INNERuntimeCPU::ECanCreateModelCPUStatus::Ok)
			{
				Runtime = Cpu;
				RuntimeName = Candidate;
				break;
			}
		}
		if (!Runtime.IsValid())
		{
			return MCPError(FString::Printf(TEXT("No registered CPU runtime can build a model from '%s' (file type %s). Enable a runtime plugin that reads it, such as NNERuntimeORT for onnx or NNERuntimeLiteRT for tflite."),
				*AssetPath, *ModelData->GetFileType()));
		}
	}

	const double T0 = FPlatformTime::Seconds();
	TSharedPtr<UE::NNE::IModelCPU> Model = Runtime->CreateModelCPU(ModelData);
	const double T1 = FPlatformTime::Seconds();
	if (!Model.IsValid()) return MCPError(FString::Printf(TEXT("Runtime '%s' failed to create the model."), *RuntimeName));
	TSharedPtr<UE::NNE::IModelInstanceCPU> Instance = Model->CreateModelInstanceCPU();
	const double T2 = FPlatformTime::Seconds();
	if (!Instance.IsValid()) return MCPError(FString::Printf(TEXT("Runtime '%s' failed to create a model instance."), *RuntimeName));

	TConstArrayView<UE::NNE::FTensorDesc> InputDescs = Instance->GetInputTensorDescs();
	if (Inputs->Num() != InputDescs.Num())
	{
		TSharedPtr<FJsonObject> Err = MCPErrorObject(FString::Printf(TEXT("The model has %d input(s) and %d were given."), InputDescs.Num(), Inputs->Num()));
		Err->SetArrayField(TEXT("inputDescs"), NNEDescsJson(InputDescs));
		return MCPResult(Err);
	}

	TArray<TArray<float>> InputData;
	TArray<UE::NNE::FTensorShape> InputShapes;
	for (int32 Index = 0; Index < InputDescs.Num(); ++Index)
	{
		const UE::NNE::FTensorDesc& Desc = InputDescs[Index];
		if (Desc.GetDataType() != ENNETensorDataType::Float)
		{
			return MCPError(FString::Printf(TEXT("Input '%s' is %s; only Float inputs are supported."), *Desc.GetName(), *NNEDataTypeName(Desc.GetDataType())));
		}
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!(*Inputs)[Index].IsValid() || !(*Inputs)[Index]->TryGetObject(Entry) || !Entry)
		{
			return MCPError(FString::Printf(TEXT("inputs[%d] must be an object {values: number[], shape?: int[]}."), Index));
		}
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!(*Entry)->TryGetArrayField(TEXT("values"), Values) || !Values)
		{
			return MCPError(FString::Printf(TEXT("inputs[%d].values is missing."), Index));
		}
		TArray<float>& Data = InputData.AddDefaulted_GetRef();
		for (const TSharedPtr<FJsonValue>& V : *Values)
		{
			double Number = 0.0;
			if (!V.IsValid() || !V->TryGetNumber(Number))
			{
				return MCPError(FString::Printf(TEXT("inputs[%d].values holds a value that is not a number."), Index));
			}
			Data.Add(static_cast<float>(Number));
		}

		UE::NNE::FTensorShape Shape;
		const TArray<TSharedPtr<FJsonValue>>* ShapeValues = nullptr;
		if ((*Entry)->TryGetArrayField(TEXT("shape"), ShapeValues) && ShapeValues)
		{
			TArray<uint32> Dims;
			for (const TSharedPtr<FJsonValue>& V : *ShapeValues)
			{
				double Dim = 0.0;
				if (!V.IsValid() || !V->TryGetNumber(Dim) || Dim < 1) return MCPError(FString::Printf(TEXT("inputs[%d].shape must hold positive integers."), Index));
				Dims.Add(static_cast<uint32>(Dim));
			}
			Shape = UE::NNE::FTensorShape::Make(Dims);
			if (!Shape.IsCompatibleWith(Desc.GetShape()))
			{
				return MCPError(FString::Printf(TEXT("inputs[%d].shape does not fit input '%s'."), Index, *Desc.GetName()));
			}
		}
		else if (Desc.GetShape().IsConcrete())
		{
			Shape = UE::NNE::FTensorShape::MakeFromSymbolic(Desc.GetShape());
		}
		else if (Desc.GetShape().Rank() == 1)
		{
			Shape = UE::NNE::FTensorShape::Make({ static_cast<uint32>(Data.Num()) });
		}
		else
		{
			return MCPError(FString::Printf(TEXT("Input '%s' has dynamic dimensions; pass inputs[%d].shape."), *Desc.GetName(), Index));
		}
		if (Shape.Volume() != static_cast<uint64>(Data.Num()))
		{
			return MCPError(FString::Printf(TEXT("inputs[%d] holds %d values, and its shape needs %llu."), Index, Data.Num(), Shape.Volume()));
		}
		InputShapes.Add(Shape);
	}

	if (Instance->SetInputTensorShapes(InputShapes) != UE::NNE::IModelInstanceCPU::ESetInputTensorShapesStatus::Ok)
	{
		return MCPError(TEXT("The runtime refused the input shapes."));
	}
	const double T3 = FPlatformTime::Seconds();

	TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = Instance->GetOutputTensorDescs();
	TConstArrayView<UE::NNE::FTensorShape> OutputShapes = Instance->GetOutputTensorShapes();
	if (OutputShapes.Num() != OutputDescs.Num())
	{
		return MCPError(TEXT("The runtime did not resolve the output shapes from the input shapes, so the output buffers cannot be sized."));
	}

	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	for (TArray<float>& Data : InputData)
	{
		InputBindings.Add({ Data.GetData(), static_cast<uint64>(Data.Num()) * sizeof(float) });
	}
	TArray<TArray<uint8>> OutputData;
	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
	{
		const uint64 Bytes = OutputShapes[Index].Volume() * OutputDescs[Index].GetElementByteSize();
		TArray<uint8>& Buffer = OutputData.AddDefaulted_GetRef();
		Buffer.SetNumZeroed(static_cast<int32>(Bytes));
	}
	for (TArray<uint8>& Buffer : OutputData)
	{
		OutputBindings.Add({ Buffer.GetData(), static_cast<uint64>(Buffer.Num()) });
	}

	TArray<double> RunMs;
	for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
	{
		const double Start = FPlatformTime::Seconds();
		if (Instance->RunSync(InputBindings, OutputBindings) != UE::NNE::IModelInstanceCPU::ERunSyncStatus::Ok)
		{
			return MCPError(FString::Printf(TEXT("RunSync failed on iteration %d."), Iteration + 1));
		}
		RunMs.Add((FPlatformTime::Seconds() - Start) * 1000.0);
	}

	TArray<TSharedPtr<FJsonValue>> Outputs;
	for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
	{
		const UE::NNE::FTensorDesc& Desc = OutputDescs[Index];
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Desc.GetName());
		Row->SetStringField(TEXT("dataType"), NNEDataTypeName(Desc.GetDataType()));
		Row->SetArrayField(TEXT("shape"), NNEShapeJson(OutputShapes[Index].GetData()));
		const int64 Volume = static_cast<int64>(OutputShapes[Index].Volume());
		Row->SetNumberField(TEXT("valueCount"), static_cast<double>(Volume));
		if (Desc.GetDataType() == ENNETensorDataType::Float)
		{
			const float* Values = reinterpret_cast<const float*>(OutputData[Index].GetData());
			TArray<TSharedPtr<FJsonValue>> Out;
			for (int64 V = 0; V < Volume && V < MaxOutputValues; ++V) Out.Add(MakeShared<FJsonValueNumber>(Values[V]));
			Row->SetArrayField(TEXT("values"), Out);
			if (Volume > MaxOutputValues) Row->SetBoolField(TEXT("valuesTruncated"), true);
		}
		else
		{
			Row->SetStringField(TEXT("note"), TEXT("Only Float outputs are returned as values."));
		}
		Outputs.Add(MakeShared<FJsonValueObject>(Row));
	}

	double Total = 0.0, Min = TNumericLimits<double>::Max(), Max = 0.0;
	for (double Ms : RunMs) { Total += Ms; Min = FMath::Min(Min, Ms); Max = FMath::Max(Max, Ms); }
	TSharedPtr<FJsonObject> Timings = MakeShared<FJsonObject>();
	Timings->SetNumberField(TEXT("createModelMs"), (T1 - T0) * 1000.0);
	Timings->SetNumberField(TEXT("createInstanceMs"), (T2 - T1) * 1000.0);
	Timings->SetNumberField(TEXT("setInputShapesMs"), (T3 - T2) * 1000.0);
	Timings->SetNumberField(TEXT("runCount"), RunMs.Num());
	Timings->SetNumberField(TEXT("runTotalMs"), Total);
	Timings->SetNumberField(TEXT("runAverageMs"), Total / RunMs.Num());
	Timings->SetNumberField(TEXT("runMinMs"), Min);
	Timings->SetNumberField(TEXT("runMaxMs"), Max);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), ModelData->GetPathName());
	Result->SetStringField(TEXT("runtime"), RuntimeName);
	Result->SetArrayField(TEXT("inputDescs"), NNEDescsJson(InputDescs));
	Result->SetArrayField(TEXT("outputDescs"), NNEDescsJson(OutputDescs));
	Result->SetArrayField(TEXT("outputs"), Outputs);
	Result->SetObjectField(TEXT("timings"), Timings);
	return MCPResult(Result);
#else
	return NNEUnavailableError();
#endif
}

#undef UE_MCP_NNE_AVAILABLE
#undef UE_MCP_NNE_HAS_GPU
#undef UE_MCP_NNE_HAS_RDG
#undef UE_MCP_NNE_HAS_NPU
