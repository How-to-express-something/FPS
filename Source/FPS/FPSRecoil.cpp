#include "FPSRecoil.h"

FFPSRecoilPattern::FFPSRecoilPattern()
{
	const FFPSRecoilPoint Defaults[] = {
		{0.f, 0.f}, {0.5f, 0.1f}, {1.05f, 0.25f}, {1.65f, 0.45f},
		{2.3f, 0.65f}, {2.9f, 0.45f}, {3.45f, 0.2f}, {3.9f, 0.f},
		{4.3f, 0.8f}, {4.3f, 1.6f}, {4.3f, 2.4f},
		{4.3f, -0.8f}, {4.3f, -1.6f}, {4.3f, -2.4f}
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Defaults); ++Index) Points.Add(Index, Defaults[Index]);
}

int32 FPSRecoil::MapShotIndex(int32 ShotIndex, int32 BaseShotCount)
{
	if (ShotIndex < 0 || BaseShotCount < 1 || BaseShotCount > 128) return INDEX_NONE;
	return ShotIndex < BaseShotCount ? ShotIndex : BaseShotCount + (ShotIndex - BaseShotCount) % 6;
}

bool FPSRecoil::ValidatePattern(const FFPSRecoilPattern& Pattern)
{
	if (Pattern.BaseShotCount < 1 || Pattern.BaseShotCount > 128) return false;
	for (int32 Index = 0; Index < Pattern.BaseShotCount + 6; ++Index)
	{
		const FFPSRecoilPoint* Point = Pattern.Points.Find(Index);
		if (!Point || !FMath::IsFinite(Point->Pitch) || !FMath::IsFinite(Point->Yaw) ||
			FMath::Abs(Point->Pitch) > 30.f || FMath::Abs(Point->Yaw) > 30.f) return false;
	}
	return true;
}

float FPSRecoil::ShotInterval(float RPM)
{
	return 60.f / (FMath::IsFinite(RPM) ? FMath::Clamp(RPM, 1.f, 3000.f) : 600.f);
}

FVector2D FPSRecoil::SampleSpread(int32 Seed, uint32 BurstId, int32 ShotIndex, float RadiusDegrees)
{
	if (!FMath::IsFinite(RadiusDegrees) || RadiusDegrees <= 0.f || ShotIndex < 0) return FVector2D::ZeroVector;
	// Explicit integer mix, independent of TMap iteration and the global random stream.
	uint32 Key = uint32(Seed) ^ (BurstId * 0x9e3779b9u) ^ (uint32(ShotIndex) * 0x85ebca6bu);
	Key ^= Key >> 16;
	Key *= 0x7feb352du;
	Key ^= Key >> 15;
	Key *= 0x846ca68bu;
	Key ^= Key >> 16;
	FRandomStream Random(int32(Key & 0x7fffffffu));
	const float Radius = FMath::Sqrt(Random.GetFraction()) * RadiusDegrees;
	const float Angle = Random.GetFraction() * 2.f * PI;
	return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
}

float FPSRecoil::SpreadAtShot(int32 ShotIndex, float MaxSpread, int32 RampShots, float Exponent)
{
	const float T = FMath::Clamp(float(ShotIndex) / FMath::Clamp(RampShots, 1, 128), 0.f, 1.f);
	const float SafeMax = FMath::IsFinite(MaxSpread) ? FMath::Clamp(MaxSpread, 0.f, 1.f) : 0.f;
	const float SafeExponent = FMath::IsFinite(Exponent) ? FMath::Clamp(Exponent, .1f, 8.f) : 1.f;
	return SafeMax * FMath::Pow(T * T * (3.f - 2.f * T), SafeExponent);
}

bool FPSRecoil::Evaluate(const FFPSRecoilPattern& Pattern, int32 Seed, uint32 BurstId,
	int32 ShotIndex, float Spread, float MaxRadiusDegrees, FVector2D& OutPattern, FVector2D& OutSpread,
	int32 RampShots, float RampExponent)
{
	OutPattern = OutSpread = FVector2D::ZeroVector;
	const FFPSRecoilPoint* Point = Pattern.Points.Find(MapShotIndex(ShotIndex, Pattern.BaseShotCount));
	if (!Point || !ValidatePattern(Pattern)) return false;
	OutPattern = Point->AsVector();
	const float SafeSpread = SpreadAtShot(ShotIndex, Spread, RampShots, RampExponent);
	const float SafeRadius = FMath::IsFinite(MaxRadiusDegrees) ? FMath::Clamp(MaxRadiusDegrees, 0.f, 15.f) : 0.f;
	OutSpread = SampleSpread(Seed, BurstId, ShotIndex, SafeSpread * SafeRadius);
	return true;
}

FRotator FPSRecoil::ApplyOffset(const FRotator& BaseAim, const FVector2D& OffsetDegrees)
{
	// Canonicalize exactly as the server reconstructs the submitted base vector.
	const FRotator CanonicalAim = BaseAim.Vector().Rotation();
	return FRotator(FMath::Clamp(CanonicalAim.Pitch + OffsetDegrees.X, -89.f, 89.f),
		FRotator::NormalizeAxis(CanonicalAim.Yaw + OffsetDegrees.Y), 0.f);
}

FVector2D FPSRecoil::Interpolate(const FVector2D& Current, const FVector2D& Target, float Speed, float DeltaTime)
{
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f) return Current;
	const float SafeSpeed = FMath::IsFinite(Speed) ? FMath::Max(Speed, 0.01f) : 10.f;
	const float Alpha = 1.f - FMath::Exp(-SafeSpeed * DeltaTime);
	const FVector2D Result = FMath::Lerp(Current, Target, Alpha);
	return Result.Equals(Target, 0.0001) ? Target : Result;
}
