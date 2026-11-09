#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

namespace MCPPIE
{
	/**
	 * Pins t.MaxFPS for a recording or replay and puts it back afterwards,
	 * value and set-by priority both (#1690). Left at SetByConsole, the pin
	 * outranks game settings and project config until the editor restarts.
	 * Restore is skipped when something else changed the value meanwhile.
	 * Owners call Restore on finish and on module shutdown.
	 */
	class FPIEMaxFPSPin
	{
	public:
		// Returns false when Hz <= 0 or the variable does not exist.
		bool Apply(int32 Hz)
		{
			if (Hz <= 0) return false;
			IConsoleVariable* CVar = Find();
			if (!CVar) return false;
			if (!bPinned)
			{
				SavedValue = CVar->GetString();
				SavedSetBy = static_cast<uint32>(CVar->GetFlags()) & ECVF_SetByMask;
			}
			PinnedValue = static_cast<float>(Hz);
			CVar->Set(PinnedValue, ECVF_SetByConsole);
			bPinned = true;
			return true;
		}

		void Restore()
		{
			if (!bPinned) return;
			bPinned = false;
			IConsoleVariable* CVar = Find();
			if (!CVar) return;
			if (!FMath::IsNearlyEqual(CVar->GetFloat(), PinnedValue))
			{
				return;
			}
			CVar->Set(*SavedValue, ECVF_SetByConsole);
			// Set never lowers a priority, so put the previous set-by back
			// directly; a lower-priority writer can then change it again.
			const uint32 Others = static_cast<uint32>(CVar->GetFlags()) & ~static_cast<uint32>(ECVF_SetByMask);
			CVar->SetFlags(static_cast<EConsoleVariableFlags>(Others | SavedSetBy));
		}

		bool IsPinned() const { return bPinned; }

	private:
		static IConsoleVariable* Find()
		{
			return IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
		}

		bool bPinned = false;
		float PinnedValue = 0.f;
		FString SavedValue;
		uint32 SavedSetBy = 0;
	};
}
