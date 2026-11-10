// Movie Render Queue: jobs in the editor's render queue, rendered in-editor by
// the PIE executor, with progress, cancel and the files each render wrote.
//
// The Movie Render Pipeline plugin is optional and off by default, so nothing
// here links against it. Its classes, the queue subsystem and every call made
// on them are found by name through reflection; the functions used are all
// UFUNCTIONs the plugin exposes to Blueprint and Python, which is the surface
// it keeps stable. With the plugin off each action says so and stops.

#include "MovieRenderQueueHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerDeferred.h"
#include "HandlerLogCapture.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorSubsystem.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "LevelSequence.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* const MRQPluginOffError =
		TEXT("Movie Render Queue is not available: enable the Movie Render Pipeline plugin (MovieRenderPipeline) for this project and restart the editor.");

	const TCHAR* const MRQSubsystemClassPath = TEXT("/Script/MovieRenderPipelineEditor.MoviePipelineQueueSubsystem");
	const TCHAR* const MRQJobClassPath = TEXT("/Script/MovieRenderPipelineCore.MoviePipelineExecutorJob");
	const TCHAR* const MRQPIEExecutorClassPath = TEXT("/Script/MovieRenderPipelineEditor.MoviePipelinePIEExecutor");
	const TCHAR* const MRQPrimaryConfigClassPath = TEXT("/Script/MovieRenderPipelineCore.MoviePipelinePrimaryConfig");
	const TCHAR* const MRQGraphConfigClassPath = TEXT("/Script/MovieRenderPipelineCore.MovieGraphConfig");
	const TCHAR* const MRQOutputSettingClassPath = TEXT("/Script/MovieRenderPipelineCore.MoviePipelineOutputSetting");
	const TCHAR* const MRQOutputBaseClassPath = TEXT("/Script/MovieRenderPipelineCore.MoviePipelineOutputBase");
	const TCHAR* const MRQRenderPassClassPath = TEXT("/Script/MovieRenderPipelineCore.MoviePipelineRenderPass");
	const TCHAR* const MRQDeferredPassClassPath = TEXT("/Script/MovieRenderPipelineRenderPasses.MoviePipelineDeferredPassBase");
	const TCHAR* const MRQPipelineClassPath = TEXT("/Script/MovieRenderPipelineCore.MoviePipeline");
	const TCHAR* const MRQBlueprintLibraryClassPath = TEXT("/Script/MovieRenderPipelineCore.MoviePipelineBlueprintLibrary");
	const TCHAR* const MRQDefaultOutputDirectory = TEXT("{project_dir}/Saved/MovieRenders/");

	UClass* MRQFindClass(const TCHAR* Path)
	{
		return FindObject<UClass>(nullptr, Path);
	}

	/** True once the plugin's classes exist, loading its modules when the plugin
	 *  is enabled but they have not been loaded yet. */
	bool MRQEnsureLoaded()
	{
		if (MRQFindClass(MRQSubsystemClassPath)) return true;
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MovieRenderPipeline"));
		if (!Plugin.IsValid() || !Plugin->IsEnabled()) return false;
		for (const TCHAR* Module : { TEXT("MovieRenderPipelineCore"), TEXT("MovieRenderPipelineSettings"),
			TEXT("MovieRenderPipelineRenderPasses"), TEXT("MovieRenderPipelineEditor") })
		{
			FModuleManager::Get().LoadModule(Module);
		}
		return MRQFindClass(MRQSubsystemClassPath) != nullptr;
	}

	/** One reflected UFUNCTION call: parameters set by name, then read back. */
	class FMRQCall
	{
	public:
		FMRQCall(UObject* InTarget, const TCHAR* FunctionName)
			: Target(InTarget)
		{
			Func = Target ? Target->FindFunction(FName(FunctionName)) : nullptr;
			if (Func) Parms = MakeUnique<FStructOnScope>(Func);
		}
		FMRQCall(const FMRQCall&) = delete;
		FMRQCall& operator=(const FMRQCall&) = delete;

		bool IsValid() const { return Func != nullptr; }
		bool HasParam(const TCHAR* Name) const { return Func && Func->FindPropertyByName(Name); }

		FMRQCall& SetObject(const TCHAR* Name, UObject* Value)
		{
			if (FObjectPropertyBase* Prop = Func ? CastField<FObjectPropertyBase>(Func->FindPropertyByName(Name)) : nullptr)
			{
				Prop->SetObjectPropertyValue_InContainer(Memory(), Value);
			}
			return *this;
		}
		FMRQCall& SetBool(const TCHAR* Name, bool Value)
		{
			if (FBoolProperty* Prop = Func ? CastField<FBoolProperty>(Func->FindPropertyByName(Name)) : nullptr)
			{
				Prop->SetPropertyValue_InContainer(Memory(), Value);
			}
			return *this;
		}
		FMRQCall& SetSoftPath(const TCHAR* Name, const FSoftObjectPath& Value)
		{
			FStructProperty* Prop = Func ? CastField<FStructProperty>(Func->FindPropertyByName(Name)) : nullptr;
			if (Prop && Prop->Struct == TBaseStructure<FSoftObjectPath>::Get())
			{
				*Prop->ContainerPtrToValuePtr<FSoftObjectPath>(Memory()) = Value;
			}
			return *this;
		}

		bool Invoke()
		{
			if (!Func || !Target) return false;
			Target->ProcessEvent(Func, Memory());
			return true;
		}

		UObject* ReturnObject() const
		{
			const FObjectPropertyBase* Prop = Func ? CastField<FObjectPropertyBase>(Func->GetReturnProperty()) : nullptr;
			return Prop ? Prop->GetObjectPropertyValue_InContainer(Memory()) : nullptr;
		}
		bool ReturnBool() const
		{
			const FBoolProperty* Prop = Func ? CastField<FBoolProperty>(Func->GetReturnProperty()) : nullptr;
			return Prop ? Prop->GetPropertyValue_InContainer(Memory()) : false;
		}
		double ReturnNumber() const
		{
			const FNumericProperty* Prop = Func ? CastField<FNumericProperty>(Func->GetReturnProperty()) : nullptr;
			if (!Prop) return 0.0;
			const void* Value = Prop->ContainerPtrToValuePtr<void>(Memory());
			return Prop->IsFloatingPoint() ? Prop->GetFloatingPointPropertyValue(Value) : static_cast<double>(Prop->GetSignedIntPropertyValue(Value));
		}
		FString ReturnString() const
		{
			const FProperty* Prop = Func ? Func->GetReturnProperty() : nullptr;
			if (const FStrProperty* Str = CastField<FStrProperty>(Prop)) return Str->GetPropertyValue_InContainer(Memory());
			if (const FTextProperty* Text = CastField<FTextProperty>(Prop)) return Text->GetPropertyValue_InContainer(Memory()).ToString();
			return FString();
		}
		TArray<UObject*> ReturnObjects() const
		{
			TArray<UObject*> Out;
			const FArrayProperty* Prop = Func ? CastField<FArrayProperty>(Func->GetReturnProperty()) : nullptr;
			const FObjectPropertyBase* Inner = Prop ? CastField<FObjectPropertyBase>(Prop->Inner) : nullptr;
			if (!Inner) return Out;
			FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Memory()));
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				Out.Add(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
			}
			return Out;
		}

	private:
		uint8* Memory() const { return Parms.IsValid() ? Parms->GetStructMemory() : nullptr; }

		UObject* Target = nullptr;
		UFunction* Func = nullptr;
		TUniquePtr<FStructOnScope> Parms;
	};

	UObject* MRQCallObject(UObject* Target, const TCHAR* Function)
	{
		FMRQCall Call(Target, Function);
		return Call.Invoke() ? Call.ReturnObject() : nullptr;
	}
	bool MRQCallBool(UObject* Target, const TCHAR* Function, bool bDefault = false)
	{
		FMRQCall Call(Target, Function);
		return Call.Invoke() ? Call.ReturnBool() : bDefault;
	}
	double MRQCallNumber(UObject* Target, const TCHAR* Function)
	{
		FMRQCall Call(Target, Function);
		return Call.Invoke() ? Call.ReturnNumber() : 0.0;
	}
	FString MRQCallString(UObject* Target, const TCHAR* Function)
	{
		FMRQCall Call(Target, Function);
		return Call.Invoke() ? Call.ReturnString() : FString();
	}

	// ── Reflected properties ────────────────────────────────────────────────

	FProperty* MRQProp(const UObject* Object, const TCHAR* Name)
	{
		return Object ? Object->GetClass()->FindPropertyByName(Name) : nullptr;
	}

	FString MRQGetString(const UObject* Object, const TCHAR* Name)
	{
		const FStrProperty* Prop = CastField<FStrProperty>(MRQProp(Object, Name));
		return Prop ? Prop->GetPropertyValue_InContainer(Object) : FString();
	}
	void MRQSetString(UObject* Object, const TCHAR* Name, const FString& Value)
	{
		if (FStrProperty* Prop = CastField<FStrProperty>(MRQProp(Object, Name))) Prop->SetPropertyValue_InContainer(Object, Value);
	}
	bool MRQGetBool(const UObject* Object, const TCHAR* Name, bool bDefault = false)
	{
		const FBoolProperty* Prop = CastField<FBoolProperty>(MRQProp(Object, Name));
		return Prop ? Prop->GetPropertyValue_InContainer(Object) : bDefault;
	}
	void MRQSetBool(UObject* Object, const TCHAR* Name, bool Value)
	{
		if (FBoolProperty* Prop = CastField<FBoolProperty>(MRQProp(Object, Name))) Prop->SetPropertyValue_InContainer(Object, Value);
	}
	int32 MRQGetInt(const UObject* Object, const TCHAR* Name)
	{
		const FIntProperty* Prop = CastField<FIntProperty>(MRQProp(Object, Name));
		return Prop ? Prop->GetPropertyValue_InContainer(Object) : 0;
	}
	void MRQSetInt(UObject* Object, const TCHAR* Name, int32 Value)
	{
		if (FIntProperty* Prop = CastField<FIntProperty>(MRQProp(Object, Name))) Prop->SetPropertyValue_InContainer(Object, Value);
	}
	template <typename TStruct>
	TStruct* MRQStructPtr(const UObject* Object, const TCHAR* Name)
	{
		const FStructProperty* Prop = CastField<FStructProperty>(MRQProp(Object, Name));
		if (!Prop || Prop->Struct != TBaseStructure<TStruct>::Get()) return nullptr;
		return const_cast<TStruct*>(Prop->ContainerPtrToValuePtr<TStruct>(Object));
	}
	/** The Path member of an FDirectoryPath property, read through reflection so
	 *  this file needs no Engine type for it. */
	FString* MRQDirectoryPathPtr(const UObject* Object, const TCHAR* Name)
	{
		const FStructProperty* Prop = CastField<FStructProperty>(MRQProp(Object, Name));
		if (!Prop) return nullptr;
		const FStrProperty* Inner = CastField<FStrProperty>(Prop->Struct->FindPropertyByName(TEXT("Path")));
		if (!Inner) return nullptr;
		void* StructValue = const_cast<void*>(Prop->ContainerPtrToValuePtr<void>(Object));
		return Inner->ContainerPtrToValuePtr<FString>(StructValue);
	}

	// ── Queue access ────────────────────────────────────────────────────────

	UObject* MRQSubsystem()
	{
		UClass* Class = MRQFindClass(MRQSubsystemClassPath);
		if (!Class || !GEditor || !Class->IsChildOf(UEditorSubsystem::StaticClass())) return nullptr;
		return GEditor->GetEditorSubsystemBase(TSubclassOf<UEditorSubsystem>(Class));
	}

	TArray<UObject*> MRQJobs(UObject* Queue)
	{
		FMRQCall Call(Queue, TEXT("GetJobs"));
		return Call.Invoke() ? Call.ReturnObjects() : TArray<UObject*>();
	}

	/** Subsystem and queue, or the error that stops the action. */
	TSharedPtr<FJsonValue> MRQResolveQueue(UObject*& OutSubsystem, UObject*& OutQueue)
	{
		if (!MRQEnsureLoaded()) return MCPErrorWithCode(TEXT("plugin_not_enabled"), MRQPluginOffError);
		OutSubsystem = MRQSubsystem();
		OutQueue = OutSubsystem ? MRQCallObject(OutSubsystem, TEXT("GetQueue")) : nullptr;
		if (!OutQueue) return MCPError(TEXT("The Movie Render Queue subsystem did not hand back its queue."));
		return nullptr;
	}

	UObject* MRQFindJob(UObject* Queue, const FString& JobName, int32 JobIndex, int32& OutIndex)
	{
		const TArray<UObject*> Jobs = MRQJobs(Queue);
		OutIndex = INDEX_NONE;
		if (!JobName.IsEmpty())
		{
			for (int32 Index = 0; Index < Jobs.Num(); ++Index)
			{
				if (Jobs[Index] && MRQGetString(Jobs[Index], TEXT("JobName")) == JobName)
				{
					OutIndex = Index;
					return Jobs[Index];
				}
			}
			return nullptr;
		}
		if (Jobs.IsValidIndex(JobIndex))
		{
			OutIndex = JobIndex;
			return Jobs[JobIndex];
		}
		return nullptr;
	}

	TArray<UObject*> MRQSettings(UObject* Config)
	{
		FMRQCall Call(Config, TEXT("GetUserSettings"));
		return Call.Invoke() ? Call.ReturnObjects() : TArray<UObject*>();
	}

	UObject* MRQFindSetting(UObject* Config, UClass* SettingClass)
	{
		if (!Config || !SettingClass) return nullptr;
		for (UObject* Setting : MRQSettings(Config))
		{
			if (Setting && Setting->IsA(SettingClass)) return Setting;
		}
		return nullptr;
	}

	UObject* MRQFindOrAddSetting(UObject* Config, UClass* SettingClass)
	{
		FMRQCall Call(Config, TEXT("FindOrAddSettingByClass"));
		Call.SetObject(TEXT("InClass"), SettingClass);
		Call.SetBool(TEXT("bIncludeDisabledSettings"), true);
		return Call.Invoke() ? Call.ReturnObject() : nullptr;
	}

	void MRQRemoveSetting(UObject* Config, UObject* Setting)
	{
		FMRQCall Call(Config, TEXT("RemoveSetting"));
		Call.SetObject(TEXT("InSetting"), Setting);
		Call.Invoke();
	}

	bool MRQUsesGraph(UObject* Job)
	{
		FMRQCall Call(Job, TEXT("IsUsingGraphConfiguration"));
		if (Call.Invoke()) return Call.ReturnBool();
		return MRQCallObject(Job, TEXT("GetGraphPreset")) != nullptr;
	}

	/** {project_dir} and {engine_dir} are the two tokens a directory can be
	 *  resolved from without a running pipeline; the rest name a shot, a camera
	 *  or a frame, so scanning stops at the deepest folder before the first one. */
	FString MRQScanRoot(const FString& Directory)
	{
		FString Resolved = Directory.IsEmpty() ? FString(MRQDefaultOutputDirectory) : Directory;
		Resolved.ReplaceInline(TEXT("\\"), TEXT("/"));
		Resolved.ReplaceInline(TEXT("{project_dir}"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
		Resolved.ReplaceInline(TEXT("{engine_dir}"), *FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
		int32 Token = INDEX_NONE;
		if (Resolved.FindChar(TEXT('{'), Token))
		{
			Resolved = Resolved.Left(Token);
			int32 Slash = INDEX_NONE;
			if (Resolved.FindLastChar(TEXT('/'), Slash)) Resolved = Resolved.Left(Slash);
		}
		FPaths::NormalizeDirectoryName(Resolved);
		return FPaths::ConvertRelativePathToFull(Resolved);
	}

	FString MRQOutputDirectory(UObject* Job)
	{
		if (!Job || MRQUsesGraph(Job)) return FString(MRQDefaultOutputDirectory);
		UObject* Config = MRQCallObject(Job, TEXT("GetConfiguration"));
		UObject* Output = MRQFindSetting(Config, MRQFindClass(MRQOutputSettingClassPath));
		const FString* Dir = Output ? MRQDirectoryPathPtr(Output, TEXT("OutputDirectory")) : nullptr;
		return Dir && !Dir->IsEmpty() ? *Dir : FString(MRQDefaultOutputDirectory);
	}

	TSharedPtr<FJsonObject> MRQJobJson(UObject* Job, int32 Index)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), Index);
		Row->SetStringField(TEXT("jobName"), MRQGetString(Job, TEXT("JobName")));
		if (const FSoftObjectPath* Sequence = MRQStructPtr<FSoftObjectPath>(Job, TEXT("Sequence"))) Row->SetStringField(TEXT("sequence"), Sequence->ToString());
		if (const FSoftObjectPath* Map = MRQStructPtr<FSoftObjectPath>(Job, TEXT("Map"))) Row->SetStringField(TEXT("map"), Map->ToString());
		Row->SetBoolField(TEXT("enabled"), MRQCallBool(Job, TEXT("IsEnabled"), true));
		Row->SetBoolField(TEXT("consumed"), MRQCallBool(Job, TEXT("IsConsumed")));
		Row->SetStringField(TEXT("statusMessage"), MRQCallString(Job, TEXT("GetStatusMessage")));
		Row->SetNumberField(TEXT("statusProgress"), MRQCallNumber(Job, TEXT("GetStatusProgress")));

		const bool bGraph = MRQUsesGraph(Job);
		Row->SetStringField(TEXT("configMode"), bGraph ? TEXT("graph") : TEXT("preset"));
		if (UObject* Graph = MRQCallObject(Job, TEXT("GetGraphPreset"))) Row->SetStringField(TEXT("graph"), Graph->GetPathName());
		if (UObject* Origin = MRQCallObject(Job, TEXT("GetPresetOrigin"))) Row->SetStringField(TEXT("presetOrigin"), Origin->GetPathName());

		if (!bGraph)
		{
			UObject* Config = MRQCallObject(Job, TEXT("GetConfiguration"));
			UClass* OutputSettingClass = MRQFindClass(MRQOutputSettingClassPath);
			UClass* OutputBaseClass = MRQFindClass(MRQOutputBaseClassPath);
			UClass* RenderPassClass = MRQFindClass(MRQRenderPassClassPath);
			TArray<TSharedPtr<FJsonValue>> Settings;
			TArray<TSharedPtr<FJsonValue>> Formats;
			TArray<TSharedPtr<FJsonValue>> Passes;
			for (UObject* Setting : MRQSettings(Config))
			{
				if (!Setting) continue;
				TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
				S->SetStringField(TEXT("class"), Setting->GetClass()->GetName());
				S->SetBoolField(TEXT("enabled"), MRQGetBool(Setting, TEXT("bEnabled"), true));
				Settings.Add(MakeShared<FJsonValueObject>(S));
				if (OutputBaseClass && Setting->IsA(OutputBaseClass)) Formats.Add(MakeShared<FJsonValueString>(Setting->GetClass()->GetName()));
				if (RenderPassClass && Setting->IsA(RenderPassClass)) Passes.Add(MakeShared<FJsonValueString>(Setting->GetClass()->GetName()));
				if (OutputSettingClass && Setting->IsA(OutputSettingClass))
				{
					if (const FString* Dir = MRQDirectoryPathPtr(Setting, TEXT("OutputDirectory"))) Row->SetStringField(TEXT("outputDirectory"), *Dir);
					Row->SetStringField(TEXT("fileNameFormat"), MRQGetString(Setting, TEXT("FileNameFormat")));
					if (const FIntPoint* Resolution = MRQStructPtr<FIntPoint>(Setting, TEXT("OutputResolution")))
					{
						Row->SetNumberField(TEXT("width"), Resolution->X);
						Row->SetNumberField(TEXT("height"), Resolution->Y);
					}
					if (MRQGetBool(Setting, TEXT("bUseCustomPlaybackRange")))
					{
						Row->SetNumberField(TEXT("startFrame"), MRQGetInt(Setting, TEXT("CustomStartFrame")));
						Row->SetNumberField(TEXT("endFrame"), MRQGetInt(Setting, TEXT("CustomEndFrame")));
					}
				}
			}
			Row->SetArrayField(TEXT("settings"), Settings);
			Row->SetArrayField(TEXT("outputFormats"), Formats);
			Row->SetArrayField(TEXT("renderPasses"), Passes);
		}
		if (!Row->HasField(TEXT("outputDirectory"))) Row->SetStringField(TEXT("outputDirectory"), MRQOutputDirectory(Job));
		return Row;
	}

	const TCHAR* MRQFormatClassPath(const FString& Format)
	{
		if (Format == TEXT("png")) return TEXT("/Script/MovieRenderPipelineRenderPasses.MoviePipelineImageSequenceOutput_PNG");
		if (Format == TEXT("jpg") || Format == TEXT("jpeg")) return TEXT("/Script/MovieRenderPipelineRenderPasses.MoviePipelineImageSequenceOutput_JPG");
		if (Format == TEXT("bmp")) return TEXT("/Script/MovieRenderPipelineRenderPasses.MoviePipelineImageSequenceOutput_BMP");
		if (Format == TEXT("exr")) return TEXT("/Script/MovieRenderPipelineRenderPasses.MoviePipelineImageSequenceOutput_EXR");
		return nullptr;
	}

	bool MRQIsOutputFile(const FString& File)
	{
		static const TCHAR* const Extensions[] = { TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("bmp"), TEXT("exr"),
			TEXT("mp4"), TEXT("mov"), TEXT("avi"), TEXT("wav"), TEXT("mxf"), TEXT("psd"), TEXT("xml"), TEXT("otio") };
		const FString Extension = FPaths::GetExtension(File).ToLower();
		for (const TCHAR* Candidate : Extensions)
		{
			if (Extension == Candidate) return true;
		}
		return false;
	}

	/** Output files under the roots, newest first, those written before Since skipped. */
	TArray<TSharedPtr<FJsonValue>> MRQScanOutputs(const TArray<FString>& Roots, const FDateTime& Since, int32 Limit, int32& OutTotal)
	{
		struct FFound { FString Path; FDateTime Modified; int64 Size; };
		TArray<FFound> Found;
		TSet<FString> Seen;
		for (const FString& Root : Roots)
		{
			if (Root.IsEmpty() || !IFileManager::Get().DirectoryExists(*Root)) continue;
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.*"), true, false);
			for (const FString& File : Files)
			{
				if (!MRQIsOutputFile(File) || Seen.Contains(File)) continue;
				const FDateTime Modified = IFileManager::Get().GetTimeStamp(*File);
				if (Modified < Since) continue;
				Seen.Add(File);
				Found.Add({ File, Modified, IFileManager::Get().FileSize(*File) });
			}
		}
		Found.Sort([](const FFound& A, const FFound& B) { return A.Modified == B.Modified ? A.Path < B.Path : A.Modified > B.Modified; });
		OutTotal = Found.Num();
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 Index = 0; Index < Found.Num() && Index < Limit; ++Index)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("path"), Found[Index].Path);
			Row->SetNumberField(TEXT("sizeBytes"), static_cast<double>(Found[Index].Size));
			Row->SetStringField(TEXT("modifiedUtc"), Found[Index].Modified.ToIso8601());
			Out.Add(MakeShared<FJsonValueObject>(Row));
		}
		return Out;
	}

	// ── The render session ──────────────────────────────────────────────────

	/** The render in flight, or the last one, kept so status and outputs can be
	 *  read after the call that started it has returned. */
	struct FMRQRenderSession
	{
		TWeakObjectPtr<UObject> Executor;
		FString ExecutorClass;
		FDateTime StartUtc;
		double StartSeconds = 0.0;
		double EndSeconds = 0.0;
		TArray<TWeakObjectPtr<UObject>> Jobs;
		TArray<FString> JobNames;
		TArray<FString> ScanRoots;
		TArray<TPair<TWeakObjectPtr<UObject>, bool>> RestoreEnabled;
		TSharedPtr<FMCPScopedLogCapture> Log;
		TArray<FMCPScopedLogCapture::FLine> LogLines;
		bool bFinished = false;
		bool bCancelRequested = false;
		FString CancelReason;
		TArray<TSharedPtr<FJsonValue>> Outputs;
		int32 OutputCount = 0;
		FMCPDeferredCallPtr Deferred;
	};

	TSharedPtr<FMRQRenderSession> GMCPMovieRenderSession;

	double MRQSessionProgress(const FMRQRenderSession& Session, FString& OutMessage)
	{
		double Sum = 0.0;
		int32 Count = 0;
		for (const TWeakObjectPtr<UObject>& Job : Session.Jobs)
		{
			if (!Job.IsValid()) continue;
			Sum += FMath::Clamp(MRQCallNumber(Job.Get(), TEXT("GetStatusProgress")), 0.0, 1.0);
			++Count;
		}
		double Progress = Count > 0 ? Sum / Count : 0.0;
		UObject* Executor = Session.Executor.Get();
		if (Executor)
		{
			OutMessage = MRQCallString(Executor, TEXT("GetStatusMessage"));
			// A classic pipeline reports its own completion, which moves sooner than the job's.
			const FObjectPropertyBase* ActiveProp = CastField<FObjectPropertyBase>(MRQProp(Executor, TEXT("ActiveMoviePipeline")));
			UObject* Pipeline = ActiveProp ? ActiveProp->GetObjectPropertyValue_InContainer(Executor) : nullptr;
			UClass* PipelineClass = MRQFindClass(MRQPipelineClassPath);
			UClass* LibraryClass = MRQFindClass(MRQBlueprintLibraryClassPath);
			if (Pipeline && PipelineClass && Pipeline->IsA(PipelineClass) && LibraryClass && Count == 1)
			{
				FMRQCall Call(LibraryClass->GetDefaultObject(), TEXT("GetCompletionPercentage"));
				Call.SetObject(TEXT("InPipeline"), Pipeline);
				if (Call.Invoke()) Progress = FMath::Max(Progress, FMath::Clamp(Call.ReturnNumber(), 0.0, 1.0));
			}
		}
		if (OutMessage.IsEmpty()) OutMessage = Session.bFinished ? TEXT("Finished") : TEXT("Rendering");
		return Progress;
	}

	TSharedPtr<FJsonObject> MRQSessionJson(const FMRQRenderSession& Session)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Names;
		for (const FString& Name : Session.JobNames) Names.Add(MakeShared<FJsonValueString>(Name));
		Out->SetArrayField(TEXT("jobNames"), Names);
		Out->SetStringField(TEXT("executor"), Session.ExecutorClass);
		Out->SetStringField(TEXT("startedUtc"), Session.StartUtc.ToIso8601());
		Out->SetBoolField(TEXT("finished"), Session.bFinished);
		Out->SetBoolField(TEXT("cancelled"), Session.bCancelRequested);
		if (!Session.CancelReason.IsEmpty()) Out->SetStringField(TEXT("cancelReason"), Session.CancelReason);
		const double End = Session.bFinished ? Session.EndSeconds : FPlatformTime::Seconds();
		Out->SetNumberField(TEXT("elapsedSeconds"), End - Session.StartSeconds);

		FString Message;
		Out->SetNumberField(TEXT("progress"), Session.bFinished && !Session.bCancelRequested ? 1.0 : MRQSessionProgress(Session, Message));
		if (!Message.IsEmpty()) Out->SetStringField(TEXT("statusMessage"), Message);

		TArray<TSharedPtr<FJsonValue>> Jobs;
		for (int32 Index = 0; Index < Session.Jobs.Num(); ++Index)
		{
			UObject* Job = Session.Jobs[Index].Get();
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("jobName"), Session.JobNames.IsValidIndex(Index) ? Session.JobNames[Index] : FString());
			if (Job)
			{
				Row->SetNumberField(TEXT("progress"), MRQCallNumber(Job, TEXT("GetStatusProgress")));
				Row->SetStringField(TEXT("statusMessage"), MRQCallString(Job, TEXT("GetStatusMessage")));
			}
			Jobs.Add(MakeShared<FJsonValueObject>(Row));
		}
		Out->SetArrayField(TEXT("jobs"), Jobs);

		const TArray<FMCPScopedLogCapture::FLine> Lines = Session.bFinished || !Session.Log.IsValid() ? Session.LogLines : Session.Log->GetLines();
		TArray<TSharedPtr<FJsonValue>> Problems;
		int32 ErrorCount = 0;
		for (const FMCPScopedLogCapture::FLine& Line : Lines)
		{
			if (Line.Verbosity == TEXT("Log")) continue;
			if (Line.Verbosity == TEXT("Error")) ++ErrorCount;
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("category"), Line.Category);
			Row->SetStringField(TEXT("verbosity"), Line.Verbosity);
			Row->SetStringField(TEXT("message"), Line.Message);
			Problems.Add(MakeShared<FJsonValueObject>(Row));
		}
		Out->SetArrayField(TEXT("log"), Problems);
		Out->SetNumberField(TEXT("errorCount"), ErrorCount);

		if (Session.bFinished)
		{
			Out->SetArrayField(TEXT("outputs"), Session.Outputs);
			Out->SetNumberField(TEXT("outputCount"), Session.OutputCount);
			Out->SetBoolField(TEXT("rendered"), !Session.bCancelRequested && ErrorCount == 0 && Session.OutputCount > 0);
		}
		return Out;
	}

	/** Restore the queue, collect the files written and stop listening to the log. */
	void MRQFinishSession(FMRQRenderSession& Session)
	{
		if (Session.bFinished) return;
		Session.bFinished = true;
		Session.EndSeconds = FPlatformTime::Seconds();
		for (const TPair<TWeakObjectPtr<UObject>, bool>& Restore : Session.RestoreEnabled)
		{
			if (UObject* Job = Restore.Key.Get())
			{
				FMRQCall Call(Job, TEXT("SetIsEnabled"));
				Call.SetBool(TEXT("bInEnabled"), Restore.Value);
				Call.Invoke();
			}
		}
		if (Session.Log.IsValid())
		{
			Session.LogLines = Session.Log->GetLines();
			Session.Log.Reset();
		}
		// File times are second-resolution on some file systems.
		Session.Outputs = MRQScanOutputs(Session.ScanRoots, Session.StartUtc - FTimespan::FromSeconds(2.0), 500, Session.OutputCount);
	}

	bool MRQSessionRunning()
	{
		return GMCPMovieRenderSession.IsValid() && !GMCPMovieRenderSession->bFinished;
	}

	/** Ticks until the executor stops, then closes the session and answers a waiting caller. */
	void MRQWatchSession(const TSharedRef<FMRQRenderSession>& Session)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Session](float) -> bool
		{
			if (Session->bFinished) return false;
			UObject* Subsystem = MRQSubsystem();
			const bool bRendering = Subsystem && MRQCallBool(Subsystem, TEXT("IsRendering"));
			UObject* Executor = Session->Executor.Get();
			if (bRendering && Executor)
			{
				if (Session->Deferred.IsValid() && !Session->Deferred->IsComplete())
				{
					FString Message;
					const double Progress = MRQSessionProgress(*Session, Message);
					Session->Deferred->ReportProgress(Progress, 1.0, Message);
				}
				return true;
			}
			MRQFinishSession(*Session);
			if (Session->Deferred.IsValid() && !Session->Deferred->IsCancelled())
			{
				TSharedPtr<FJsonObject> Result = MCPSuccess();
				Result->SetObjectField(TEXT("render"), MRQSessionJson(*Session));
				const bool bRendered = Session->OutputCount > 0 && !Session->bCancelRequested;
				Result->SetBoolField(TEXT("rendered"), bRendered);
				MCPSetNoRollback(Result, TEXT("A render writes image files to disk and marks its jobs consumed; there is no inverse call. Delete the files listed under render.outputs to undo it."));
				Result->SetBoolField(TEXT("changed"), bRendered);
				Session->Deferred->Complete(MCPResult(Result));
			}
			return false;
		}), 0.25f);
	}
}

void FMovieRenderQueueHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057). Tagged with the editor
	// category, where the sequencer actions these sit beside are exposed.
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("editor"));
	using EType = EMCPParamType;

	Registry.RegisterHandler(TEXT("mrq_create_job"), &CreateJob, {
		MCPParam::Required(TEXT("sequence"), EType::String, TEXT("Level Sequence asset path to render")),
		MCPParam::Optional(TEXT("map"), EType::String, TEXT("World asset path to render in (default the level open in the editor)")),
		MCPParam::Optional(TEXT("jobName"), EType::String, TEXT("Job name, unique in the queue (default the sequence name)")),
		MCPParam::Optional(TEXT("preset"), EType::String, TEXT("MoviePipelinePrimaryConfig asset to copy into the job's configuration")),
		MCPParam::Optional(TEXT("graph"), EType::String, TEXT("MovieGraphConfig asset: render through Movie Render Graph instead of a preset")),
		MCPParam::Optional(TEXT("outputDirectory"), EType::String, TEXT("Output folder; {project_dir} and the other Movie Render Queue tokens are kept as written (default {project_dir}/Saved/MovieRenders/)")),
		MCPParam::Optional(TEXT("fileNameFormat"), EType::String, TEXT("File name format, e.g. {sequence_name}.{frame_number}")),
		MCPParam::Optional(TEXT("width"), EType::Number, TEXT("Output width in pixels (with height)")),
		MCPParam::Optional(TEXT("height"), EType::Number, TEXT("Output height in pixels (with width)")),
		MCPParam::Optional(TEXT("format"), EType::String, TEXT("png | jpg | exr | bmp; replaces the job's other image outputs (default png when the job has none)")),
		MCPParam::Optional(TEXT("startFrame"), EType::Number, TEXT("First display frame of a custom range (with endFrame)")),
		MCPParam::Optional(TEXT("endFrame"), EType::Number, TEXT("End display frame of a custom range, exclusive (with startFrame)")),
	});
	Registry.RegisterHandler(TEXT("mrq_list_jobs"), &ListJobs, {});
	Registry.RegisterHandler(TEXT("mrq_delete_job"), &DeleteJob, {
		MCPParam::Optional(TEXT("jobName"), EType::String, TEXT("Name of the job to delete")),
		MCPParam::Optional(TEXT("jobIndex"), EType::Integer, TEXT("Queue index of the job to delete, when jobName is not given")),
	});
	// A wait covers the whole render, so the registered timeout is long.
	Registry.RegisterHandlerWithTimeout(TEXT("mrq_render"), &Render, 1800.0f, {
		MCPParam::Optional(TEXT("jobNames"), EType::Array, TEXT("Jobs to render (default every enabled job in the queue)")).Items(EType::String),
		MCPParam::Optional(TEXT("wait"), EType::Boolean, TEXT("Wait for the render and answer with its outputs, reporting progress (default true); false returns once it starts")),
	}, MCPSpec::ContractExempt(TEXT("starts a render of every enabled job in the editor's queue")));
	Registry.RegisterHandler(TEXT("mrq_get_render_status"), &GetRenderStatus, {});
	Registry.RegisterHandler(TEXT("mrq_cancel_render"), &CancelRender, {});
	Registry.RegisterHandler(TEXT("mrq_list_outputs"), &ListOutputs, {
		MCPParam::Optional(TEXT("jobName"), EType::String, TEXT("List the files under this job's output folder")),
		MCPParam::Optional(TEXT("outputDirectory"), EType::String, TEXT("List the files under this folder instead")),
		MCPParam::Optional(TEXT("limit"), EType::Number, TEXT("Files to return, newest first (default 200, max 5000)")),
	});
}

TSharedPtr<FJsonValue> FMovieRenderQueueHandlers::CreateJob(const TSharedPtr<FJsonObject>& Params)
{
	FString SequencePath;
	if (auto Err = RequireString(Params, TEXT("sequence"), SequencePath)) return Err;
	const FString MapPath = OptionalString(Params, TEXT("map"), TEXT(""));
	FString JobName = OptionalString(Params, TEXT("jobName"), TEXT(""));
	const FString PresetPath = OptionalString(Params, TEXT("preset"), TEXT(""));
	const FString GraphPath = OptionalString(Params, TEXT("graph"), TEXT(""));
	const FString OutputDirectory = OptionalString(Params, TEXT("outputDirectory"), TEXT(""));
	const FString FileNameFormat = OptionalString(Params, TEXT("fileNameFormat"), TEXT(""));
	const bool bHasWidth = HasParam(Params, TEXT("width"));
	const bool bHasHeight = HasParam(Params, TEXT("height"));
	const int32 Width = OptionalInt(Params, TEXT("width"), 0);
	const int32 Height = OptionalInt(Params, TEXT("height"), 0);
	const FString Format = OptionalString(Params, TEXT("format"), TEXT("")).ToLower();
	const bool bHasStart = HasParam(Params, TEXT("startFrame"));
	const bool bHasEnd = HasParam(Params, TEXT("endFrame"));
	const int32 StartFrame = OptionalInt(Params, TEXT("startFrame"), 0);
	const int32 EndFrame = OptionalInt(Params, TEXT("endFrame"), 0);

	// ── Validation: nothing is created above the allocation below. ─────────
	if (!PresetPath.IsEmpty() && !GraphPath.IsEmpty())
	{
		return MCPError(TEXT("Pass preset or graph, not both: a job renders through one configuration."));
	}
	if (bHasWidth != bHasHeight)
	{
		return MCPError(TEXT("width and height go together."));
	}
	if (bHasWidth && (Width < 16 || Height < 16 || Width > 16384 || Height > 16384))
	{
		return MCPError(FString::Printf(TEXT("width and height must be between 16 and 16384, got %dx%d."), Width, Height));
	}
	if (bHasStart != bHasEnd)
	{
		return MCPError(TEXT("startFrame and endFrame go together."));
	}
	if (bHasStart && EndFrame <= StartFrame)
	{
		return MCPError(FString::Printf(TEXT("endFrame (%d) must be greater than startFrame (%d); the end is exclusive."), EndFrame, StartFrame));
	}
	if (!Format.IsEmpty() && !MRQFormatClassPath(Format))
	{
		return MCPError(FString::Printf(TEXT("format '%s' is not one of png, jpg, exr, bmp."), *Format));
	}
	const bool bOverrides = !OutputDirectory.IsEmpty() || !FileNameFormat.IsEmpty() || bHasWidth || !Format.IsEmpty() || bHasStart;
	if (!GraphPath.IsEmpty() && bOverrides)
	{
		return MCPError(TEXT("outputDirectory, fileNameFormat, width/height, format and startFrame/endFrame configure a preset job. A graph job takes them from its graph's Global Output Settings and output nodes, so set them there."));
	}
	FString CheckedOutputDirectory = OutputDirectory;
	if (!OutputDirectory.IsEmpty() && !OutputDirectory.Contains(TEXT("{")))
	{
		if (auto Err = MCPRequireWritablePath(OutputDirectory, CheckedOutputDirectory, FPaths::ProjectSavedDir())) return Err;
	}

	UObject* Subsystem = nullptr;
	UObject* Queue = nullptr;
	if (auto Err = MRQResolveQueue(Subsystem, Queue)) return Err;

	ULevelSequence* Sequence = LoadAssetByPath<ULevelSequence>(SequencePath);
	if (!Sequence) return MCPAssetLoadError(SequencePath, TEXT("LevelSequence"));

	FSoftObjectPath MapSoftPath;
	if (!MapPath.IsEmpty())
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(MapPath);
		const FAssetData MapData = MCPFindAssetDataForPath(Forms);
		if (!MapData.IsValid() || MapData.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
		{
			return MCPError(FString::Printf(TEXT("map '%s' is not a World asset the asset registry knows."), *MapPath));
		}
		MapSoftPath = MapData.GetSoftObjectPath();
	}
	else
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const FString PackageName = World ? World->GetOutermost()->GetName() : FString();
		if (PackageName.IsEmpty() || PackageName.StartsWith(TEXT("/Temp/")) || !FPackageName::DoesPackageExist(PackageName))
		{
			return MCPError(TEXT("The level open in the editor has never been saved, so a render cannot load it. Save it, or pass map."));
		}
		MapSoftPath = FSoftObjectPath(World);
	}

	UObject* Preset = nullptr;
	if (!PresetPath.IsEmpty())
	{
		Preset = MCPLoadAssetObject(PresetPath);
		UClass* PresetClass = MRQFindClass(MRQPrimaryConfigClassPath);
		if (!Preset || !PresetClass || !Preset->IsA(PresetClass)) return MCPAssetLoadError(PresetPath, TEXT("MoviePipelinePrimaryConfig"));
	}
	UObject* Graph = nullptr;
	if (!GraphPath.IsEmpty())
	{
		Graph = MCPLoadAssetObject(GraphPath);
		UClass* GraphClass = MRQFindClass(MRQGraphConfigClassPath);
		if (!GraphClass) return MCPError(TEXT("This engine's Movie Render Pipeline has no Movie Render Graph (MovieGraphConfig)."));
		if (!Graph || !Graph->IsA(GraphClass)) return MCPAssetLoadError(GraphPath, TEXT("MovieGraphConfig"));
	}

	if (JobName.IsEmpty()) JobName = Sequence->GetName();

	int32 ExistingIndex = INDEX_NONE;
	if (UObject* Existing = MRQFindJob(Queue, JobName, INDEX_NONE, ExistingIndex))
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetObjectField(TEXT("job"), MRQJobJson(Existing, ExistingIndex));
		Result->SetStringField(TEXT("note"), TEXT("A job with this name is already in the queue and was left as it is. Delete it with mrq_delete_job first to recreate it."));
		MCPSetNoRollback(Result, TEXT("Nothing was created."));
		return MCPResult(Result);
	}

	UClass* JobClass = MRQFindClass(MRQJobClassPath);
	FMRQCall Allocate(Queue, TEXT("AllocateNewJob"));
	Allocate.SetObject(TEXT("InJobType"), JobClass);
	UObject* Job = Allocate.Invoke() ? Allocate.ReturnObject() : nullptr;
	if (!Job) return MCPError(TEXT("The queue did not allocate a job."));

	MRQSetString(Job, TEXT("JobName"), JobName);
	FMRQCall SetSequence(Job, TEXT("SetSequence"));
	if (SetSequence.IsValid())
	{
		SetSequence.SetSoftPath(TEXT("InSequence"), FSoftObjectPath(Sequence));
		SetSequence.Invoke();
	}
	else if (FSoftObjectPath* SequenceProp = MRQStructPtr<FSoftObjectPath>(Job, TEXT("Sequence")))
	{
		*SequenceProp = FSoftObjectPath(Sequence);
	}
	if (FSoftObjectPath* MapProp = MRQStructPtr<FSoftObjectPath>(Job, TEXT("Map"))) *MapProp = MapSoftPath;

	TArray<TSharedPtr<FJsonValue>> Warnings;
	if (Graph)
	{
		FMRQCall SetGraph(Job, TEXT("SetGraphPreset"));
		if (!SetGraph.IsValid())
		{
			FMRQCall Delete(Queue, TEXT("DeleteJob"));
			Delete.SetObject(TEXT("InJob"), Job);
			Delete.Invoke();
			return MCPError(TEXT("This engine's Movie Render Queue jobs cannot take a graph configuration (no SetGraphPreset)."));
		}
		SetGraph.SetObject(TEXT("InGraphPreset"), Graph);
		SetGraph.SetBool(TEXT("bUpdateVariableAssignments"), true);
		SetGraph.Invoke();
	}
	else
	{
		if (Preset)
		{
			FMRQCall SetPreset(Job, TEXT("SetPresetOrigin"));
			SetPreset.SetObject(TEXT("InPreset"), Preset);
			SetPreset.Invoke();
		}
		UObject* Config = MRQCallObject(Job, TEXT("GetConfiguration"));
		if (!Config)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("The job has no configuration object, so no settings were applied.")));
		}
		else
		{
			UObject* Output = MRQFindOrAddSetting(Config, MRQFindClass(MRQOutputSettingClassPath));
			if (Output)
			{
				if (!CheckedOutputDirectory.IsEmpty())
				{
					if (FString* Dir = MRQDirectoryPathPtr(Output, TEXT("OutputDirectory"))) *Dir = CheckedOutputDirectory;
				}
				if (!FileNameFormat.IsEmpty()) MRQSetString(Output, TEXT("FileNameFormat"), FileNameFormat);
				if (bHasWidth)
				{
					if (FIntPoint* Resolution = MRQStructPtr<FIntPoint>(Output, TEXT("OutputResolution"))) *Resolution = FIntPoint(Width, Height);
				}
				if (bHasStart)
				{
					MRQSetBool(Output, TEXT("bUseCustomPlaybackRange"), true);
					MRQSetInt(Output, TEXT("CustomStartFrame"), StartFrame);
					MRQSetInt(Output, TEXT("CustomEndFrame"), EndFrame);
				}
				// A rerun writes over the previous frames instead of versioning a new folder.
				MRQSetBool(Output, TEXT("bOverrideExistingOutput"), true);
			}

			UClass* OutputBaseClass = MRQFindClass(MRQOutputBaseClassPath);
			const bool bHasOutput = MRQFindSetting(Config, OutputBaseClass) != nullptr;
			if (!Format.IsEmpty() || !bHasOutput)
			{
				UClass* FormatClass = MRQFindClass(MRQFormatClassPath(Format.IsEmpty() ? FString(TEXT("png")) : Format));
				if (!FormatClass)
				{
					Warnings.Add(MakeShared<FJsonValueString>(TEXT("The image output classes are not loaded (MovieRenderPipelineRenderPasses), so no output format was set.")));
				}
				else
				{
					for (UObject* Setting : MRQSettings(Config))
					{
						if (Setting && OutputBaseClass && Setting->IsA(OutputBaseClass) && !Setting->IsA(FormatClass)
							&& Setting->GetClass()->GetName().StartsWith(TEXT("MoviePipelineImageSequenceOutput")))
						{
							MRQRemoveSetting(Config, Setting);
						}
					}
					MRQFindOrAddSetting(Config, FormatClass);
				}
			}

			UClass* RenderPassClass = MRQFindClass(MRQRenderPassClassPath);
			if (!MRQFindSetting(Config, RenderPassClass))
			{
				if (UClass* Deferred = MRQFindClass(MRQDeferredPassClassPath)) MRQFindOrAddSetting(Config, Deferred);
			}
		}
	}

	int32 NewIndex = INDEX_NONE;
	MRQFindJob(Queue, JobName, INDEX_NONE, NewIndex);
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetObjectField(TEXT("job"), MRQJobJson(Job, NewIndex));
	if (Warnings.Num() > 0) Result->SetArrayField(TEXT("warnings"), Warnings);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("jobName"), JobName);
	MCPSetRollback(Result, TEXT("mrq_delete_job"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMovieRenderQueueHandlers::ListJobs(const TSharedPtr<FJsonObject>& Params)
{
	UObject* Subsystem = nullptr;
	UObject* Queue = nullptr;
	if (auto Err = MRQResolveQueue(Subsystem, Queue)) return Err;

	const TArray<UObject*> Jobs = MRQJobs(Queue);
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (int32 Index = 0; Index < Jobs.Num(); ++Index)
	{
		if (Jobs[Index]) Rows.Add(MakeShared<FJsonValueObject>(MRQJobJson(Jobs[Index], Index)));
	}
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetArrayField(TEXT("jobs"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	Result->SetBoolField(TEXT("isRendering"), MRQCallBool(Subsystem, TEXT("IsRendering")));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMovieRenderQueueHandlers::DeleteJob(const TSharedPtr<FJsonObject>& Params)
{
	const FString JobName = OptionalString(Params, TEXT("jobName"), TEXT(""));
	const bool bHasIndex = HasParam(Params, TEXT("jobIndex"));
	const int32 JobIndex = OptionalInt(Params, TEXT("jobIndex"), INDEX_NONE);
	if (JobName.IsEmpty() && !bHasIndex)
	{
		return MCPError(TEXT("Pass jobName or jobIndex."));
	}

	UObject* Subsystem = nullptr;
	UObject* Queue = nullptr;
	if (auto Err = MRQResolveQueue(Subsystem, Queue)) return Err;
	if (MRQCallBool(Subsystem, TEXT("IsRendering")))
	{
		return MCPError(TEXT("The queue is rendering; cancel it with mrq_cancel_render before deleting a job."));
	}

	int32 Index = INDEX_NONE;
	UObject* Job = MRQFindJob(Queue, JobName, JobIndex, Index);
	if (!Job)
	{
		if (JobName.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("jobIndex %d is outside the queue (%d jobs)."), JobIndex, MRQJobs(Queue).Num()));
		}
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetStringField(TEXT("jobName"), JobName);
		MCPSetNoRollback(Result, TEXT("No job had that name, so nothing was deleted."));
		return MCPResult(Result);
	}

	TSharedPtr<FJsonObject> Before = MRQJobJson(Job, Index);
	FMRQCall Delete(Queue, TEXT("DeleteJob"));
	Delete.SetObject(TEXT("InJob"), Job);
	Delete.Invoke();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("alreadyRemoved"), false);
	Result->SetObjectField(TEXT("deleted"), Before);
	Result->SetNumberField(TEXT("remaining"), MRQJobs(Queue).Num());

	// The inverse rebuilds the job from what names it. Edits made to the job's
	// own configuration copy beyond those are not in the record.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	FString Value;
	if (Before->TryGetStringField(TEXT("sequence"), Value)) Payload->SetStringField(TEXT("sequence"), Value);
	if (Before->TryGetStringField(TEXT("map"), Value)) Payload->SetStringField(TEXT("map"), Value);
	if (Before->TryGetStringField(TEXT("jobName"), Value)) Payload->SetStringField(TEXT("jobName"), Value);
	if (Before->TryGetStringField(TEXT("graph"), Value)) Payload->SetStringField(TEXT("graph"), Value);
	else
	{
		if (Before->TryGetStringField(TEXT("presetOrigin"), Value)) Payload->SetStringField(TEXT("preset"), Value);
		if (Before->TryGetStringField(TEXT("outputDirectory"), Value)) Payload->SetStringField(TEXT("outputDirectory"), Value);
	}
	MCPSetRollback(Result, TEXT("mrq_create_job"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), TEXT("The inverse recreates the job from its sequence, map, name, preset or graph and output folder; other edits to the job's own configuration copy are not restored."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMovieRenderQueueHandlers::Render(const TSharedPtr<FJsonObject>& Params)
{
	TArray<FString> Wanted;
	const TArray<TSharedPtr<FJsonValue>>* JobNames = nullptr;
	if (TryGetArrayParam(Params, TEXT("jobNames"), JobNames) && JobNames)
	{
		for (const TSharedPtr<FJsonValue>& Value : *JobNames)
		{
			FString Name;
			if (Value.IsValid() && Value->TryGetString(Name) && !Name.IsEmpty()) Wanted.AddUnique(Name);
		}
	}
	const bool bWait = OptionalBool(Params, TEXT("wait"), true);

	UObject* Subsystem = nullptr;
	UObject* Queue = nullptr;
	if (auto Err = MRQResolveQueue(Subsystem, Queue)) return Err;
	if (MRQCallBool(Subsystem, TEXT("IsRendering")) || MRQSessionRunning())
	{
		return MCPError(TEXT("A render is already running. Read it with mrq_get_render_status or stop it with mrq_cancel_render."));
	}
	if (GEditor && GEditor->PlayWorld)
	{
		return MCPError(TEXT("Play In Editor is running, and the in-editor render needs it. Stop PIE first."));
	}
	UClass* ExecutorClass = MRQFindClass(MRQPIEExecutorClassPath);
	if (!ExecutorClass) return MCPError(TEXT("The Movie Render Queue PIE executor class is not loaded (MovieRenderPipelineEditor)."));

	const TArray<UObject*> Jobs = MRQJobs(Queue);
	TSharedRef<FMRQRenderSession> Session = MakeShared<FMRQRenderSession>();
	TArray<FString> Missing = Wanted;
	for (UObject* Job : Jobs)
	{
		if (!Job) continue;
		const FString Name = MRQGetString(Job, TEXT("JobName"));
		const bool bEnabled = MRQCallBool(Job, TEXT("IsEnabled"), true);
		const bool bSelected = Wanted.Num() > 0 ? Wanted.Contains(Name) : bEnabled;
		Missing.Remove(Name);
		if (bSelected)
		{
			const FSoftObjectPath* Sequence = MRQStructPtr<FSoftObjectPath>(Job, TEXT("Sequence"));
			const FSoftObjectPath* Map = MRQStructPtr<FSoftObjectPath>(Job, TEXT("Map"));
			if (!Sequence || Sequence->IsNull() || !Map || Map->IsNull())
			{
				return MCPError(FString::Printf(TEXT("Job '%s' has no sequence or no map, so it cannot render."), *Name));
			}
			Session->Jobs.Add(Job);
			Session->JobNames.Add(Name);
			Session->ScanRoots.AddUnique(MRQScanRoot(MRQOutputDirectory(Job)));
		}
		if (bSelected != bEnabled) Session->RestoreEnabled.Emplace(Job, bEnabled);
	}
	if (Missing.Num() > 0)
	{
		return MCPError(FString::Printf(TEXT("No job named %s in the queue. mrq_list_jobs lists them."), *FString::Join(Missing, TEXT(", "))));
	}
	if (Session->Jobs.Num() == 0)
	{
		return MCPError(TEXT("The queue has no enabled job to render. Add one with mrq_create_job."));
	}

	// Select by enabling exactly the chosen jobs for this run, and let a job that
	// rendered before render again; the previous enabled state comes back after.
	for (const TPair<TWeakObjectPtr<UObject>, bool>& Restore : Session->RestoreEnabled)
	{
		FMRQCall Call(Restore.Key.Get(), TEXT("SetIsEnabled"));
		Call.SetBool(TEXT("bInEnabled"), !Restore.Value);
		Call.Invoke();
	}
	for (const TWeakObjectPtr<UObject>& Job : Session->Jobs)
	{
		FMRQCall Call(Job.Get(), TEXT("SetConsumed"));
		Call.SetBool(TEXT("bInConsumed"), false);
		Call.Invoke();
	}

	Session->StartUtc = FDateTime::UtcNow();
	Session->StartSeconds = FPlatformTime::Seconds();
	Session->Log = MakeShared<FMCPScopedLogCapture>(TArray<FString>{ TEXT("LogMovieRender"), TEXT("LogMoviePipeline") });
	FMRQCall Start(Subsystem, TEXT("RenderQueueWithExecutor"));
	Start.SetObject(TEXT("InExecutorType"), ExecutorClass);
	UObject* Executor = Start.Invoke() ? Start.ReturnObject() : nullptr;
	if (!Executor)
	{
		MRQFinishSession(*Session);
		return MCPError(TEXT("The queue subsystem did not start a render."));
	}
	Session->Executor = Executor;
	Session->ExecutorClass = Executor->GetClass()->GetName();
	GMCPMovieRenderSession = Session;

	FMCPDeferredCallPtr Deferred = bWait ? MCPDeferRequest() : nullptr;
	if (Deferred.IsValid())
	{
		Session->Deferred = Deferred;
		Deferred->SetCancelNote(TEXT("Cancelling the call cancels the render: every job in it stops, and frames already written stay on disk."));
		TWeakPtr<FMRQRenderSession> Weak = Session;
		Deferred->OnCancel([Weak](const FString& Reason)
		{
			const TSharedPtr<FMRQRenderSession> Pinned = Weak.Pin();
			if (!Pinned.IsValid() || Pinned->bFinished) return;
			Pinned->bCancelRequested = true;
			Pinned->CancelReason = Reason;
			if (UObject* Running = Pinned->Executor.Get())
			{
				FMRQCall Cancel(Running, TEXT("CancelAllJobs"));
				Cancel.Invoke();
			}
		});
	}
	MRQWatchSession(Session);
	if (Deferred.IsValid()) return MCPDeferredMarker();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("started"), true);
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetObjectField(TEXT("render"), MRQSessionJson(*Session));
	if (bWait) Result->SetStringField(TEXT("note"), TEXT("Called outside a bridge request, so it returned once the render started. Poll mrq_get_render_status."));
	MCPSetNoRollback(Result, TEXT("A render writes image files to disk; there is no inverse call. mrq_cancel_render stops it, and the files it wrote can be deleted."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMovieRenderQueueHandlers::GetRenderStatus(const TSharedPtr<FJsonObject>& Params)
{
	if (!MRQEnsureLoaded()) return MCPErrorWithCode(TEXT("plugin_not_enabled"), MRQPluginOffError);
	UObject* Subsystem = MRQSubsystem();
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	const bool bRendering = Subsystem && MRQCallBool(Subsystem, TEXT("IsRendering"));
	Result->SetBoolField(TEXT("isRendering"), bRendering);
	if (UObject* Executor = Subsystem ? MRQCallObject(Subsystem, TEXT("GetActiveExecutor")) : nullptr)
	{
		Result->SetStringField(TEXT("activeExecutor"), Executor->GetClass()->GetName());
	}
	if (GMCPMovieRenderSession.IsValid())
	{
		Result->SetObjectField(GMCPMovieRenderSession->bFinished ? TEXT("lastRender") : TEXT("render"), MRQSessionJson(*GMCPMovieRenderSession));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMovieRenderQueueHandlers::CancelRender(const TSharedPtr<FJsonObject>& Params)
{
	if (!MRQEnsureLoaded()) return MCPErrorWithCode(TEXT("plugin_not_enabled"), MRQPluginOffError);
	UObject* Subsystem = MRQSubsystem();
	UObject* Executor = Subsystem ? MRQCallObject(Subsystem, TEXT("GetActiveExecutor")) : nullptr;
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (!Executor || !MRQCallBool(Subsystem, TEXT("IsRendering")))
	{
		Result->SetBoolField(TEXT("alreadyStopped"), true);
		MCPSetNoRollback(Result, TEXT("No render was running, so nothing was cancelled."));
		return MCPResult(Result);
	}
	if (GMCPMovieRenderSession.IsValid() && !GMCPMovieRenderSession->bFinished)
	{
		GMCPMovieRenderSession->bCancelRequested = true;
		GMCPMovieRenderSession->CancelReason = TEXT("mrq_cancel_render");
	}
	FMRQCall Cancel(Executor, TEXT("CancelAllJobs"));
	Cancel.Invoke();
	Result->SetBoolField(TEXT("alreadyStopped"), false);
	Result->SetBoolField(TEXT("cancelled"), true);
	Result->SetStringField(TEXT("note"), TEXT("The executor stops after the frame in flight; mrq_get_render_status reports when it has."));
	MCPSetNoRollback(Result, TEXT("A cancelled render cannot be resumed; mrq_render starts it again from the first frame."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMovieRenderQueueHandlers::ListOutputs(const TSharedPtr<FJsonObject>& Params)
{
	const FString JobName = OptionalString(Params, TEXT("jobName"), TEXT(""));
	const FString OutputDirectory = OptionalString(Params, TEXT("outputDirectory"), TEXT(""));
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 200);
	if (Limit < 1 || Limit > 5000)
	{
		return MCPError(FString::Printf(TEXT("limit must be between 1 and 5000, got %d."), Limit));
	}

	TArray<FString> Roots;
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (!OutputDirectory.IsEmpty())
	{
		Roots.Add(MRQScanRoot(OutputDirectory));
	}
	else
	{
		UObject* Subsystem = nullptr;
		UObject* Queue = nullptr;
		if (auto Err = MRQResolveQueue(Subsystem, Queue)) return Err;
		if (!JobName.IsEmpty())
		{
			int32 Index = INDEX_NONE;
			UObject* Job = MRQFindJob(Queue, JobName, INDEX_NONE, Index);
			if (!Job) return MCPError(FString::Printf(TEXT("No job named '%s' in the queue."), *JobName));
			Roots.Add(MRQScanRoot(MRQOutputDirectory(Job)));
			Result->SetStringField(TEXT("jobName"), JobName);
		}
		else if (GMCPMovieRenderSession.IsValid())
		{
			Roots = GMCPMovieRenderSession->ScanRoots;
		}
		else
		{
			for (UObject* Job : MRQJobs(Queue)) if (Job) Roots.AddUnique(MRQScanRoot(MRQOutputDirectory(Job)));
			if (Roots.Num() == 0) Roots.Add(MRQScanRoot(FString()));
		}
	}

	int32 Total = 0;
	TArray<TSharedPtr<FJsonValue>> Files = MRQScanOutputs(Roots, FDateTime::MinValue(), Limit, Total);
	TArray<TSharedPtr<FJsonValue>> RootValues;
	for (const FString& Root : Roots) RootValues.Add(MakeShared<FJsonValueString>(Root));
	Result->SetArrayField(TEXT("directories"), RootValues);
	Result->SetArrayField(TEXT("files"), Files);
	Result->SetNumberField(TEXT("count"), Total);
	Result->SetNumberField(TEXT("returned"), Files.Num());
	if (Total > Files.Num()) Result->SetBoolField(TEXT("truncated"), true);
	return MCPResult(Result);
}
