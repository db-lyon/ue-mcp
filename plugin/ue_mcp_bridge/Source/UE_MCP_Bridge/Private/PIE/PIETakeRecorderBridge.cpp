#include "PIETakeRecorderBridge.h"
#include "UE_MCP_BridgeModule.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace UEMCPPIE
{
	namespace TakeRecorderBridge
	{
		namespace
		{
			// Resolve the static blueprint library class. Returns nullptr when
			// the TakeRecorder plugin is not loaded.
			UClass* GetBPLibClass()
			{
				return FindObject<UClass>(nullptr, TEXT("/Script/TakeRecorder.TakeRecorderBlueprintLibrary"));
			}

			// Invoke a no-arg static UFunction on the BP library CDO. ReturnAddr
			// must point at a buffer big enough to receive the function's return
			// value (or be nullptr for void-returning functions).
			bool CallStatic(const TCHAR* FunctionName, void* ReturnAddr, size_t ReturnSize)
			{
				UClass* BPLib = GetBPLibClass();
				if (!BPLib) return false;
				UFunction* Fn = BPLib->FindFunctionByName(FunctionName);
				if (!Fn) return false;
				UObject* CDO = BPLib->GetDefaultObject();
				if (!CDO) return false;
				// Build a parameter buffer matching the UFunction's layout.
				// For no-arg functions with a return value, the only param is
				// the return value at the end of the param frame.
				const int32 ParmsSize = Fn->ParmsSize;
				TArray<uint8> Frame;
				Frame.SetNumZeroed(ParmsSize);
				CDO->ProcessEvent(Fn, Frame.GetData());
				if (ReturnAddr && ReturnSize > 0 && ParmsSize >= static_cast<int32>(ReturnSize))
				{
					FMemory::Memcpy(ReturnAddr, Frame.GetData() + (ParmsSize - ReturnSize), ReturnSize);
				}
				return true;
			}

			UObject* GetPanel()
			{
				UObject* Panel = nullptr;
				if (!CallStatic(TEXT("GetTakeRecorderPanel"), &Panel, sizeof(Panel)))
				{
					return nullptr;
				}
				return Panel;
			}

			bool CallPanelVoid(UObject* Panel, const TCHAR* FunctionName)
			{
				if (!Panel) return false;
				UFunction* Fn = Panel->FindFunction(FunctionName);
				if (!Fn) return false;
				const int32 ParmsSize = Fn->ParmsSize;
				TArray<uint8> Frame;
				Frame.SetNumZeroed(ParmsSize);
				Panel->ProcessEvent(Fn, Frame.GetData());
				return true;
			}
		}

		bool IsAvailable()
		{
			return GetBPLibClass() != nullptr;
		}

		bool IsPanelOpen()
		{
			return GetPanel() != nullptr;
		}

		bool IsRecording()
		{
			bool bRecording = false;
			CallStatic(TEXT("IsRecording"), &bRecording, sizeof(bRecording));
			return bRecording;
		}

		bool StartFromPanel(FString& OutMessage)
		{
			if (!IsAvailable())
			{
				OutMessage = TEXT("Take Recorder plugin not loaded (enable it in Edit > Plugins to use take_record)");
				return false;
			}
			if (IsRecording())
			{
				OutMessage = TEXT("Take Recorder is already recording");
				return true;
			}
			UObject* Panel = GetPanel();
			if (!Panel)
			{
				OutMessage = TEXT("No Take Recorder panel open. Open Window > Cinematics > Take Recorder, configure sources, then re-arm.");
				return false;
			}
			if (!CallPanelVoid(Panel, TEXT("StartRecording")))
			{
				OutMessage = TEXT("Take Recorder panel rejected StartRecording (check the panel for the reason)");
				return false;
			}
			OutMessage = TEXT("Take Recorder StartRecording dispatched");
			UE_LOG(LogMCPBridge, Log, TEXT("[PIE-REC] Take Recorder StartRecording dispatched"));
			return true;
		}

		bool StopFromPanel(FString& OutMessage)
		{
			if (!IsAvailable())
			{
				OutMessage = TEXT("Take Recorder plugin not loaded");
				return false;
			}
			if (!IsRecording())
			{
				OutMessage = TEXT("Take Recorder is not recording");
				return false;
			}
			UObject* Panel = GetPanel();
			if (!Panel)
			{
				OutMessage = TEXT("Take Recorder panel closed; cannot stop");
				return false;
			}
			if (!CallPanelVoid(Panel, TEXT("StopRecording")))
			{
				OutMessage = TEXT("Take Recorder panel rejected StopRecording");
				return false;
			}
			OutMessage = TEXT("Take Recorder StopRecording dispatched");
			UE_LOG(LogMCPBridge, Log, TEXT("[PIE-REC] Take Recorder StopRecording dispatched"));
			return true;
		}
	}
}
