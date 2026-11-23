#pragma once
#include "CoreMinimal.h"

namespace FPSMatch
{
	inline bool FrameExistsForLife(int32 Requested, int32 Birth) { return Requested >= Birth; }
	inline bool ProtectedAt(double Time, double Birth, double Until, double Cancelled)
	{
		return Time >= Birth && Time < Until && (Cancelled < 0. || Time < Cancelled);
	}
}
