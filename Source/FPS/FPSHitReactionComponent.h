#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FPSHitReactionTypes.h"
#include "FPSHitReactionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFPSHitReactionReceived, const FFPSHitReactionCue&, Cue);

/** Server selects a stable index; each recipient plays its own preconfigured asset. */
UCLASS(ClassGroup=(FPS), meta=(BlueprintSpawnableComponent))
class FPS_API UFPSHitReactionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UFPSHitReactionComponent();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void HandleAcceptedPointDamage(float ActualDamage, const FVector& ShotDirection,
		const FVector& ImpactPoint, FName Bone);
	UFUNCTION(BlueprintPure, Category="Hit Reaction") EFPSHitRegion ResolveRegion(FName Bone) const;
	/** Last locally accepted transient cue, not a replicated persistent damage history. */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Hit Reaction") FFPSHitReactionCue LastCue;
	/** Cosmetic cue only: unreliable, relevance-limited, may be lost. */
	UPROPERTY(BlueprintAssignable, Category="Hit Reaction") FFPSHitReactionReceived OnHitReaction;
	/** Exact bone aliases. Unknown bones use Torso; adjust for your skeleton. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hit Reaction") TMap<FName, EFPSHitRegion> BoneRegions;
	UPROPERTY(EditDefaultsOnly, Category="Hit Reaction", meta=(ClampMin="0",ClampMax="1")) float MinMontageInterval = .1f;
	UPROPERTY(EditDefaultsOnly, Category="Hit Reaction", meta=(ClampMin=".1",ClampMax="2")) float MaxCueAge = .5f;
private:
	UFUNCTION(NetMulticast, Unreliable) void Multicast_HitReaction(const FFPSHitReactionCue& Cue);
	uint32 NextEventId = 0;
	uint32 LastReceivedEventId = 0;
	bool bHasReceivedCue = false;
	bool bEndingPlay = false;
	double LastMontageTime = -1.e10;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSAnimationTestAccess;
#endif
};
