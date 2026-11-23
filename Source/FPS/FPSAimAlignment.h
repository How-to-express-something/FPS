#pragma once

#include "CoreMinimal.h"
#include "Math/RotationMatrix.h"

namespace FPSAim
{
// Inputs are in arms-component space, independent of the current ADS correction.
// The result is relative to the camera, which directly parents the arms.
inline bool SolveSightAlignment(const FVector& Rear, const FVector& Front,
	const FVector& Up, const FVector& ArmsScale, float EyeRelief, FTransform& OutTarget)
{
	if (Rear.ContainsNaN() || Front.ContainsNaN() || Up.ContainsNaN() ||
		ArmsScale.ContainsNaN() || !FMath::IsFinite(EyeRelief) || EyeRelief < 0.f ||
		ArmsScale.GetAbsMin() <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector ScaledRear = Rear * ArmsScale;
	const FVector Forward = ((Front - Rear) * ArmsScale).GetSafeNormal();
	const FVector ScaledUp = (Up * ArmsScale).GetSafeNormal();
	if (Forward.IsNearlyZero() || ScaledUp.IsNearlyZero() ||
		FVector::CrossProduct(Forward, ScaledUp).IsNearlyZero())
	{
		return false;
	}

	// Map the sight's forward/up frame to camera +X/+Z. Preserve component scale.
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(Forward, ScaledUp).ToQuat().Inverse();
	const FVector Eye = ScaledRear - Forward * EyeRelief;
	OutTarget = FTransform(Rotation, -Rotation.RotateVector(Eye), ArmsScale);
	return true;
}
}
