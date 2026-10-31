#pragma once

#include "CoreMinimal.h"

/**
 * Whether a rendered BGRA picture shows nothing: too small for its size, or every
 * pixel black. A thumbnail scene always draws its sky sphere, so a render with no
 * colour at all is one that never drew, and is not a picture of the asset (#178).
 */
inline bool MCPCkDiffIsBlankImage(const TArray<uint8>& Bgra, int32 Width, int32 Height)
{
	const int64 Pixels = int64(Width) * int64(Height);
	if (Width <= 0 || Height <= 0 || Bgra.Num() < Pixels * 4) return true;
	constexpr uint8 Black = 2;
	const uint8* Data = Bgra.GetData();
	for (int64 I = 0; I < Pixels; ++I)
	{
		const uint8* P = Data + I * 4;
		if (P[0] > Black || P[1] > Black || P[2] > Black) return false;
	}
	return true;
}
