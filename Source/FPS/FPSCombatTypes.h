// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FPSCombatTypes.generated.h"

class AFPSWeapon;

/** Input edges, independent of whether a shot passes cooldown/ammo checks. */
UENUM()
enum class EFPSCombatInput : uint8
{
	StartFire,
	StopFire,
	StartAim,
	StopAim,
	Reload,
	SwitchWeapon,
	InterruptReload,
	Count UMETA(Hidden)
};

/** One coherent authoritative view; owner input acknowledgements also use it. */
USTRUCT()
struct FFPSCombatState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bReloading = false;

	UPROPERTY()
	bool bAiming = false;

	UPROPERTY()
	bool bFiring = false;

	UPROPERTY()
	uint32 Revision = 0;

	UPROPERTY()
	uint32 LastProcessedInput = 0;

	/** Distinguishes cancelling and restarting within the same replication frame. */
	UPROPERTY()
	uint32 ReloadGeneration = 0;

	/** Reload presentation must not depend on CurrentWeapon replication order. */
	UPROPERTY()
	TObjectPtr<AFPSWeapon> ReloadWeapon;

	UPROPERTY()
	double ReloadEndServerTime = 0.;
};

/** Why the server refused a fire request. None means the shot was processed. */
UENUM(BlueprintType)
enum class EFPSShotReject : uint8
{
	None,
	Dead,
	NoWeapon,
	WrongWeapon,
	FireRate,
	NoAmmo,
	FrameInFuture,
	BadOrigin,
	BadDirection,
	Sequence,
	NotFiring,
	BurstMismatch,
	RecoilConfig,
	MatchFinished
};

UENUM(BlueprintType)
enum class EFPSShotOutcome : uint8
{
	Miss, WorldImpact, Protected, Damaged, Killed, NoDamage, Rejected
};

/**
 * What the client asks the server to resolve. Only the ray and the client's
 * render frame are sent: the server never trusts a client-side hit result.
 */
USTRUCT()
struct FFPSShotRequest
{
	GENERATED_BODY()

	/** Client-side incrementing id, used to confirm or correct the prediction. */
	UPROPERTY()
	int32 Sequence = 0;

	/** Weapon instance and input edge identify the prediction context. */
	UPROPERTY()
	TObjectPtr<AFPSWeapon> Weapon = nullptr;

	UPROPERTY()
	uint32 BurstId = 0;

	/** Diagnostic hint only; the server assigns its own index. */
	UPROPERTY()
	int32 PredictedShotIndex = 0;

	/** Server frame the client was rendering when the shot was taken. */
	UPROPERTY()
	int32 ClientFrame = 0;

	UPROPERTY()
	FVector ViewLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector ViewDirection = FVector::ForwardVector; // Base aim, BEFORE camera recoil/spread.

	/** Weapon type the client fired, so a switch mid-flight is detected. */
	UPROPERTY()
	FGameplayTag WeaponTag;
};

/** Server verdict for a shot, sent back to the shooter only. */
USTRUCT()
struct FFPSShotResult
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Sequence = 0;

	UPROPERTY()
	TObjectPtr<AFPSWeapon> Weapon = nullptr;

	UPROPERTY()
	uint32 BurstId = 0;

	/** INDEX_NONE on rejection. Accepted bullets alone advance this index. */
	UPROPERTY()
	int32 ServerShotIndex = INDEX_NONE;

	UPROPERTY()
	int32 NextShotIndex = 0;

	UPROPERTY()
	FVector2D PatternOffsetDegrees = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D SpreadOffsetDegrees = FVector2D::ZeroVector;

	UPROPERTY()
	FVector ShotDirection = FVector::ForwardVector;

	UPROPERTY()
	EFPSShotReject RejectReason = EFPSShotReject::None;
	UPROPERTY() EFPSShotOutcome Outcome = EFPSShotOutcome::Miss;
	/** Actual health removed, not nominal weapon damage. Zero for protection/world/rejection. */
	UPROPERTY() float ActualDamage = 0.f;

	UPROPERTY()
	bool bHit = false;

	/** Includes world impacts, unlike bHit (damageable character). */
	UPROPERTY()
	bool bBlockingHit = false;

	UPROPERTY()
	bool bHeadshot = false;

	UPROPERTY()
	bool bKilled = false;

	/** True when the requested frame fell outside the history window. */
	UPROPERTY()
	bool bFrameClamped = false;

	UPROPERTY()
	int32 ResolvedFrame = 0;

	/** Diagnostic metadata from the actual server query. No target transforms replicate. */
	UPROPERTY() int32 RequestedFrame = INDEX_NONE;
	UPROPERTY() int32 NewestHistoryFrame = INDEX_NONE;
	/** Actual pose frame of the hit target; INDEX_NONE for miss/world/current fallback. */
	UPROPERTY() int32 HitSampleFrame = INDEX_NONE;
	UPROPERTY() bool bUsedHistory = false;
	UPROPERTY() bool bQueryPerformed = false;

	UPROPERTY()
	FVector HitLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector HitNormal = FVector::UpVector;

	UPROPERTY()
	FName HitBone;

	UPROPERTY()
	int32 RemainingAmmo = 0;

	UPROPERTY()
	uint32 AmmoRevision = 0;
};

/** Cosmetic shot data broadcast to every client except the shooter. */
USTRUCT()
struct FFPSShotFX
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AFPSWeapon> Weapon;

	UPROPERTY()
	int32 Sequence = 0;

	UPROPERTY()
	FVector MuzzleLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector ImpactLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector ImpactNormal = FVector::UpVector;

	UPROPERTY()
	bool bHit = false;
};

/** Per-weapon firing tuning, keyed by weapon type tag in UFPSWeaponDataAsset. */
USTRUCT(BlueprintType)
struct FWeaponCombatSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float Damage = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float HeadshotMultiplier = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "1", ClampMax = "3000"))
	float RoundsPerMinute = 600.f;

	/** 0 = fixed track only; 1 = Combat's full angular spread radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0", ClampMax = "1"))
	float Spread = 0.15f;

	/** Smooth curve from zero to Spread over this many shot intervals (index 0 is exact). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="1", ClampMax="128"))
	int32 SpreadRampShots = 8;

	/** 1 = smoothstep; >1 delays growth, <1 grows earlier. Shared deterministic formula. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.1", ClampMax="8"))
	float SpreadRampExponent = 1.f;

	/** Maximum hitscan distance in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float Range = 15000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 MagSize = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bAutomatic = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float ReloadTime = 1.6f;

	/** Socket on the first-person weapon mesh used for muzzle FX. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	FName MuzzleSocket = TEXT("Muzzle");
};
