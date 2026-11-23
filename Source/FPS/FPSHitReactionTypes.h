#pragma once

#include "CoreMinimal.h"
#include "FPSHitReactionTypes.generated.h"

/** Index order is an asset contract. Do not reorder without migrating arrays. */
UENUM(BlueprintType)
enum class EFPSHitDirection : uint8 { Front, Right, Back, Left };

UENUM(BlueprintType)
enum class EFPSHitRegion : uint8 { Torso, Head, LeftArm, RightArm, LeftLeg, RightLeg };

/** Transient accepted-damage cue; never used to apply damage or move a character. */
USTRUCT(BlueprintType)
struct FFPSHitReactionCue
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 EventId = 0;
	UPROPERTY(BlueprintReadOnly) int32 MontageIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) EFPSHitDirection Direction = EFPSHitDirection::Front;
	UPROPERTY(BlueprintReadOnly) EFPSHitRegion Region = EFPSHitRegion::Torso;
	/** Where the bullet came FROM: local +X front, +Y right. */
	UPROPERTY(BlueprintReadOnly) FVector2D LocalSourceDirection = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FName Bone;
	UPROPERTY(BlueprintReadOnly) FVector ImpactPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float ActualDamage = 0.f;
	UPROPERTY(BlueprintReadOnly) double ServerTime = 0.;
};

namespace FPSHitReaction
{
	constexpr int32 DirectionCount = 4;
	constexpr int32 MontageCount = 24;
	FPS_API EFPSHitDirection ClassifyDirection(const FVector2D& LocalSource);
	FPS_API int32 MontageIndex(EFPSHitRegion Region, EFPSHitDirection Direction);
}
