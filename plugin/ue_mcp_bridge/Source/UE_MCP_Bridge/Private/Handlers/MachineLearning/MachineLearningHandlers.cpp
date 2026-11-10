#include "MachineLearningHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

void FMachineLearningHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057). Exposed through the
	// gameplay tool, beside the AI and navigation actions.
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("gameplay"));
	using EType = EMCPParamType;

	Registry.RegisterHandler(TEXT("nne_list_runtimes"), &NNEListRuntimes, {});
	Registry.RegisterHandler(TEXT("nne_import_model"), &NNEImportModel, {
		MCPParam::Required(TEXT("sourceFile"), EType::String, TEXT("Absolute path of the model file (.onnx, .tflite, ...)")),
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("NNEModelData asset to create, e.g. /Game/ML/NNE_Model")),
		MCPParam::Optional(TEXT("fileType"), EType::String, TEXT("Model file type the runtimes read (default the file's extension)")),
		MCPParam::Optional(TEXT("targetRuntimes"), EType::Array, TEXT("Runtimes to cook the model for, e.g. NNERuntimeORTCpu or NNERuntimeLiteRT (default every runtime)")).Items(EType::String),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) reports an existing asset, error refuses it")),
	});
	Registry.RegisterHandler(TEXT("nne_run_inference"), &NNERunInference, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("NNEModelData asset")),
		MCPParam::Optional(TEXT("runtime"), EType::String, TEXT("CPU runtime to run on (default the first registered CPU runtime that accepts the model)")),
		MCPParam::Optional(TEXT("inputs"), EType::Array, TEXT("One entry per model input, in order: {values: number[], shape?: int[]}; shape is needed only for dynamic dimensions")).Items(EType::Object),
		MCPParam::Optional(TEXT("iterations"), EType::Integer, TEXT("Timed runs (default 1, max 1000)")),
		MCPParam::Optional(TEXT("maxOutputValues"), EType::Integer, TEXT("Values returned per output (default 256, max 65536)")),
	});

	Registry.RegisterHandler(TEXT("learning_agents_list_observation_types"), &LearningAgentsListObservationTypes, {
		MCPParam::Optional(TEXT("nameFilter"), EType::String, TEXT("Case-sensitive substring of the Specify function name")),
		MCPParam::Optional(TEXT("includeParameters"), EType::Boolean, TEXT("Include each function's parameters with types and defaults (default true)")),
	});
	Registry.RegisterHandler(TEXT("learning_agents_read_observations"), &LearningAgentsReadObservations, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Learning Agents interactor Blueprint")),
	});
	Registry.RegisterHandler(TEXT("learning_agents_add_observation"), &LearningAgentsAddObservation, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Learning Agents interactor Blueprint")),
		MCPParam::Required(TEXT("observationType"), EType::String, TEXT("Observation type: Float, Location, Set, ... or the Specify function name")),
		MCPParam::Optional(TEXT("settings"), EType::Object, TEXT("Input pin defaults by parameter name, e.g. {\"Tag\": \"Speed\", \"MaxNum\": 8}")),
		MCPParam::Optional(TEXT("connectTo"), EType::String, TEXT("result to return this observation from SpecifyAgentObservation, or the nodeId of an observation to feed")),
		MCPParam::Optional(TEXT("connectPin"), EType::String, TEXT("Input of the connectTo node to feed, e.g. Element or QueryElement")),
		MCPParam::Optional(TEXT("posX"), EType::Integer, TEXT("Node X position (default 0)")),
		MCPParam::Optional(TEXT("posY"), EType::Integer, TEXT("Node Y position (default below the existing nodes)")),
	});
}
