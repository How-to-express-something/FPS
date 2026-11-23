#pragma once

#include "CoreMinimal.h"

class UAnimInstance;
class UAnimMontage;
class USkeletalMeshComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogFPSAnimation, Log, All);

/** No gameplay ownership: weak references to the exact instances we started. */
struct FFPSCosmeticMontage
{
	TWeakObjectPtr<UAnimInstance> AnimInstance;
	TWeakObjectPtr<UAnimMontage> Montage;
};

namespace FPSAnimation
{
	/** Reject root motion and incompatible skeletons; never stop every montage. */
	bool Play(USkeletalMeshComponent* Mesh, UAnimMontage* Montage, float Rate = 1.f, float Position = 0.f);
	void Stop(TArray<FFPSCosmeticMontage>& Started, float BlendOut = .08f);
}
