#pragma once

#include "CoreMinimal.h"
#include "FPSRecoil.generated.h"

/** Absolute angular offset from the player's unrecoiled aim, in degrees. */
USTRUCT(BlueprintType)
struct FPS_API FFPSRecoilPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float Pitch = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float Yaw = 0.f;

	FFPSRecoilPoint() = default;
	FFPSRecoilPoint(float InPitch, float InYaw) : Pitch(InPitch), Yaw(InYaw) {}
	FVector2D AsVector() const { return FVector2D(Pitch, Yaw); }
};

/** Zero-based rising prefix followed by six repeating points (three right/left). */
USTRUCT(BlueprintType)
struct FPS_API FFPSRecoilPattern
{
	GENERATED_BODY()

	FFPSRecoilPattern();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "1", ClampMax = "128"))
	int32 BaseShotCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	TMap<int32, FFPSRecoilPoint> Points;
};

namespace FPSRecoil
{
	FPS_API int32 MapShotIndex(int32 ShotIndex, int32 BaseShotCount);
	FPS_API bool ValidatePattern(const FFPSRecoilPattern& Pattern);
	FPS_API float ShotInterval(float RPM);
	FPS_API float SpreadAtShot(int32 ShotIndex, float MaxSpread, int32 RampShots, float Exponent);
	/** Uniform area distribution in an angular disk; no mutable RNG stream to desync. */
	FPS_API FVector2D SampleSpread(int32 Seed, uint32 BurstId, int32 ShotIndex, float RadiusDegrees);
	FPS_API bool Evaluate(const FFPSRecoilPattern& Pattern, int32 Seed, uint32 BurstId,
		int32 ShotIndex, float Spread, float MaxRadiusDegrees, FVector2D& OutPattern, FVector2D& OutSpread,
		int32 RampShots = 8, float RampExponent = 1.f);
	FPS_API FRotator ApplyOffset(const FRotator& BaseAim, const FVector2D& OffsetDegrees);
	FPS_API FVector2D Interpolate(const FVector2D& Current, const FVector2D& Target, float Speed, float DeltaTime);
}
