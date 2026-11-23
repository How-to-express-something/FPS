#include "FPSRecoilCameraShake.h"

namespace
{
float SafeValue(float Value, float Default, float Minimum, float Maximum)
{
	return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Default;
}
}

UFPSRecoilCameraShake::UFPSRecoilCameraShake(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bSingleInstance = true;
	SetRootShakePattern(CreateDefaultSubobject<UFPSRecoilShakePattern>(TEXT("RecoilImpulse")));
}

void UFPSRecoilShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = SafeValue(Duration, 0.14f, 0.02f, 1.f);
	OutInfo.BlendIn = 0.005f;
	OutInfo.BlendOut = FMath::Min(0.04f, OutInfo.Duration.Get() * 0.5f);
}

void UFPSRecoilShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	State.Start(this, Params);
}

void UFPSRecoilShakePattern::Evaluate(float BlendWeight, FCameraShakePatternUpdateResult& OutResult) const
{
	const float Time = State.GetElapsedTime();
	const float Phase = 2.f * PI * SafeValue(Frequency, 11.f, 1.f, 40.f) * Time;
	const float Envelope = FMath::Exp(-SafeValue(Damping, 20.f, 1.f, 60.f) * Time) * BlendWeight;
	OutResult.Rotation = FRotator(
		SafeValue(PitchAmplitude, 0.4f, 0.f, 3.f) * FMath::Sin(Phase),
		SafeValue(YawAmplitude, 0.08f, 0.f, 3.f) * FMath::Sin(Phase * 1.3f),
		SafeValue(RollAmplitude, 0.12f, 0.f, 3.f) * FMath::Sin(Phase * 0.8f)) * Envelope;
	OutResult.FOV = SafeValue(FOVAmplitude, 0.12f, 0.f, 2.f) * FMath::Sin(Phase) * Envelope;
}

void UFPSRecoilShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	Evaluate(State.Update(FMath::Max(Params.DeltaTime, 0.f)), OutResult);
}

void UFPSRecoilShakePattern::ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	Evaluate(State.Scrub(FMath::Max(Params.AbsoluteTime, 0.f)), OutResult);
}

bool UFPSRecoilShakePattern::IsFinishedImpl() const { return !State.IsPlaying(); }
void UFPSRecoilShakePattern::StopShakePatternImpl(const FCameraShakePatternStopParams& Params) { State.Stop(Params.bImmediately); }
void UFPSRecoilShakePattern::TeardownShakePatternImpl() { State = FCameraShakeState(); }
