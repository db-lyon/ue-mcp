#pragma once

#include "CoreMinimal.h"

/** Most a colour channel may vary across a picture that still counts as one flat colour. */
constexpr int32 MCPCkDiffBlankTolerance = 6;

/**
 * Whether a rendered BGRA picture shows nothing: too small for its size, or one flat
 * colour. A renderer that drew nothing leaves the cleared target (black) or only its
 * background, and a picture of nothing is not a picture of the version (studio #178).
 */
inline bool MCPCkDiffIsBlankImage(const TArray<uint8>& Bgra, int32 Width, int32 Height)
{
	const int64 Pixels = int64(Width) * int64(Height);
	if (Width <= 0 || Height <= 0 || Bgra.Num() < Pixels * 4) return true;
	uint8 Min[3] = { 255, 255, 255 };
	uint8 Max[3] = { 0, 0, 0 };
	const uint8* Data = Bgra.GetData();
	for (int64 I = 0; I < Pixels; ++I)
	{
		const uint8* P = Data + I * 4;
		for (int32 C = 0; C < 3; ++C)
		{
			Min[C] = FMath::Min(Min[C], P[C]);
			Max[C] = FMath::Max(Max[C], P[C]);
		}
		if (Max[0] - Min[0] > MCPCkDiffBlankTolerance || Max[1] - Min[1] > MCPCkDiffBlankTolerance
			|| Max[2] - Min[2] > MCPCkDiffBlankTolerance) return false;
	}
	return true;
}
