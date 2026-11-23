// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSWeaponAimTypes.generated.h"

class UCurveFloat;

/**
 * Per-weapon ADS tuning: two sight points aligned to the fixed camera, its FOV and how the
 * hip-to-aim transition feels. Keyed by weapon type tag in UFPSWeaponDataAsset.
 */
USTRUCT(BlueprintType)
struct FWeaponAimSettings
{
	GENERATED_BODY()

	/** Rear aperture/notch centre on the weapon's first-person mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	FName RearSightSocket = TEXT("SightRear");

	/** Front sight tip/optical centre. Rear -> front defines the forward sight axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	FName FrontSightSocket = TEXT("SightFront");

	/** Eye-to-rear-sight distance in cm at normal camera scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = "0.0", Units = "cm"))
	float EyeRelief = 10.f;

	/** Up direction in weapon component space, used to resolve roll. Must not be
	 *  parallel to the sight axis. Change this if the imported weapon uses another up axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	FVector SightUpDirection = FVector::UpVector;

	/** Aim field of view in degrees. Zero or less falls back to the weapon's ZoomRatio. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = "0.0", ClampMax = "170.0"))
	float AimFOV = 0.f;

	/** Seconds for a full hip-to-aim blend. With an AimCurve this is also the
	 *  curve's time span: the curve is sampled from 0 to AimTime seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = "0.01"))
	float AimTime = 0.2f;

	/** Optional easing curve. X axis is seconds (sampled 0..AimTime), Y should go
	 *  0..1. Leave empty for a linear blend. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	TObjectPtr<UCurveFloat> AimCurve = nullptr;
};
