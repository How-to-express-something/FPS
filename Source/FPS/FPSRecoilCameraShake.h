#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "FPSRecoilCameraShake.generated.h"

/** Short, damped angular impulse. It never translates the gameplay camera. */
UCLASS(EditInlineNew)
class FPS_API UFPSRecoilShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Recoil", meta = (ClampMin = "0.02", ClampMax = "1"))
	float Duration = 0.14f;

	UPROPERTY(EditAnywhere, Category = "Recoil", meta = (ClampMin = "0", ClampMax = "3"))
	float PitchAmplitude = 0.4f;

	UPROPERTY(EditAnywhere, Category = "Recoil", meta = (ClampMin = "0", ClampMax = "3"))
	float YawAmplitude = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Recoil", meta = (ClampMin = "0", ClampMax = "3"))
	float RollAmplitude = 0.12f;

	UPROPERTY(EditAnywhere, Category = "Recoil", meta = (ClampMin = "0", ClampMax = "2"))
	float FOVAmplitude = 0.12f;

	UPROPERTY(EditAnywhere, Category = "Recoil", meta = (ClampMin = "1", ClampMax = "40"))
	float Frequency = 11.f;

	UPROPERTY(EditAnywhere, Category = "Recoil", meta = (ClampMin = "1", ClampMax = "60"))
	float Damping = 20.f;

protected:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual void ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;
	virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override;
	virtual void TeardownShakePatternImpl() override;
	void Evaluate(float BlendWeight, FCameraShakePatternUpdateResult& OutResult) const;
	FCameraShakeState State;
};

/** Native default, also usable as a Blueprint parent for weapon feedback tuning. */
UCLASS()
class FPS_API UFPSRecoilCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()
public:
	UFPSRecoilCameraShake(const FObjectInitializer& ObjectInitializer);
};
