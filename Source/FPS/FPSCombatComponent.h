// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FPSCombatTypes.h"
#include "FPSWeaponAimTypes.h"
#include "FPSRecoil.h"
#include "FPSAnimationPresentation.h"
#include "FPSNetworkDiagnostics.h"
#include "FPSCombatComponent.generated.h"

class AFPSWeapon;
class ACharacter;
class APlayerController;
class UFPSWeaponDataAsset;
class UDecalComponent;
class UMaterialInterface;
class UParticleSystem;
class USoundBase;
class UCameraShakeBase;
class APlayerCameraManager;
struct FWeaponMontageSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFPSReloadStateChanged, bool, bIsReloading);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FFPSHitConfirmed, EFPSShotOutcome, Outcome, bool, bHeadshot, float, ActualDamage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSCurrentWeaponChanged, AFPSWeapon*, CurrentWeapon, AFPSWeapon*, PreviousWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FFPSDisplayAmmoChanged, AFPSWeapon*, Weapon, int32, CurrentAmmo, int32, PreviousAmmo);

/** A client-side shot that has not been confirmed by the server yet. */
struct FFPSPredictedShot
{
	int32 Sequence = 0;
	/** Local-only wall clock for prediction-to-confirmation diagnostics. */
	double SentAtSeconds = 0.;
	int32 TracePhase = 0;
	TWeakObjectPtr<AFPSWeapon> Weapon;
	uint32 BurstId = 0;
	int32 BurstShotIndex = 0;
	FVector2D PatternOffsetDegrees = FVector2D::ZeroVector;
	FVector2D SpreadOffsetDegrees = FVector2D::ZeroVector;
	bool bHitAnything = false;
	bool bHitCharacter = false;
	FVector HitLocation = FVector::ZeroVector;
	FVector HitNormal = FVector::UpVector;
	TWeakObjectPtr<UDecalComponent> Decal;
};

/**
 * Receives aim/fire input forwarded from AFPSPlayerController and drives the
 * owner's weapon.
 *
 * Firing is predicted on the owning client (immediate FX and hit feedback) and
 * resolved authoritatively on the server, which rewinds the world to the frame
 * the client was rendering (see UFPSLagCompensationSubsystem).
 */
UCLASS(ClassGroup = (FPS), meta = (BlueprintSpawnableComponent))
class FPS_API UFPSCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSCombatComponent();
	UPROPERTY(BlueprintAssignable, Category="Combat|Feedback") FFPSHitConfirmed OnHitConfirmed;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Feedback") TObjectPtr<USoundBase> BodyHitSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Feedback") TObjectPtr<USoundBase> HeadHitSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Feedback") TObjectPtr<USoundBase> KillHitSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Feedback", meta=(ClampMin="0",ClampMax="1")) float HitSoundVolume = .45f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void StartAim();
	void StopAim();
	void StartFire();
	void StopFire();

	/** Requests a reload from the server (no-op while already reloading). */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void Reload();

	/** New combat actions can use this; movement/look/jump/crouch must not call it. */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void InterruptReload();

	/** Start/stop only the reload montage. Runs on predicted owner and remote views. */
	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FFPSReloadStateChanged OnReloadStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FFPSCurrentWeaponChanged OnCurrentWeaponChanged;

	/** Display ammo: prediction, correction, replication and reload, deduplicated. */
	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FFPSDisplayAmmoChanged OnAmmoChanged;

	/** Same weapon only; equipping a lower-capacity weapon is not a fired round. */
	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FFPSDisplayAmmoChanged OnAmmoDecreased;

	/** Called by the owning character when it dies. */
	void OnOwnerDied();
	/** Stop input/FX on results, preserving in-flight receipts (including the winning shot). */
	void StopForMatchEnd();

	/** Spawns the default pistol and rifle into the inventory and equips the pistol. Server only. */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SpawnInventory();

	/** Equips the weapon at the given inventory index. Routes to the server when called on a client. */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SwitchWeapon(int32 WeaponIndex);

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetFovByCurrentWeaponZoomRatio() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetFovByWeaponZoomRatio(const AFPSWeapon* Weapon) const;

	/** Fills OutSettings with the weapon's ADS tuning (data asset entry, else defaults). */
	bool ResolveAimSettings(const AFPSWeapon* Weapon, FWeaponAimSettings& OutSettings) const;

	/** Fills OutSettings with the weapon's firing tuning (data asset entry, else defaults). */
	bool ResolveCombatSettings(const AFPSWeapon* Weapon, FWeaponCombatSettings& OutSettings) const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAiming() const { return bIsAiming; }

	/** Owning-client display: versioned confirmation minus pending predictions;
	 *  authority/observers use the weapon's ammo snapshot. */
	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetDisplayAmmo() const;

	/** Reads current state plus the most recent real receipt; never drives gameplay. */
	UFUNCTION(BlueprintPure, Category = "Combat|Diagnostics")
	FString GetNetworkDiagnosticsText() const;
	const FFPSNetworkDiagnostics& GetNetworkDiagnostics() const { return NetworkDiagnostics; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsReloading() const { return bReloading; }

	/** Cosmetic offset only: never feed this back into the shot's base aim. */
	UFUNCTION(BlueprintPure, Category = "Combat|Recoil")
	FRotator GetCameraRecoilOffset() const { return FRotator(CameraRecoil.X, CameraRecoil.Y, 0.f); }

	UFUNCTION(BlueprintPure, Category = "Combat|Recoil")
	int32 GetNextPredictedShotIndex() const { return NextLocalBurstShotIndex; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	AFPSWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	UFPSWeaponDataAsset* GetWeaponData() const { return WeaponData; }

	/**
	 * Attaches the given weapon to the owner's first- and third-person meshes.
	 * Also called by AFPSWeapon on clients, once it learns that it is equipped.
	 */
	void AttachWeaponToOwner(AFPSWeapon* Weapon);

protected:
	FFPSNetworkDiagnostics NetworkDiagnostics;
	void PresentHitConfirmation(const FFPSShotResult& Result);
	double LastBodyHitSoundTime = -1.;
	/** Per-weapon-type anims, attach sockets, and montages. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UFPSWeaponDataAsset> WeaponData;

	/** Weapon classes spawned into the inventory on BeginPlay. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TSubclassOf<AFPSWeapon> DefaultPistolClass;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TSubclassOf<AFPSWeapon> DefaultRifleClass;

	/** ADS tuning used when the data asset has no entry for a weapon type. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	FWeaponAimSettings DefaultAimSettings;

	/** Firing tuning used when the data asset has no entry for a weapon type. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	FWeaponCombatSettings DefaultCombatSettings;

	/** Shared by all weapons; points are absolute offsets, not per-shot increments. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Recoil")
	FFPSRecoilPattern RecoilPattern;

	/** Angular disk radius in degrees at weapon Spread == 1. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Recoil", meta = (ClampMin = "0", ClampMax = "15"))
	float MaxSpreadRadiusDegrees = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Recoil", meta = (ClampMin = "0", ClampMax = "1"))
	float CameraRecoilRatio = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Recoil", meta = (ClampMin = "0.01"))
	float CameraRecoilFollowSpeed = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Recoil", meta = (ClampMin = "0.01"))
	float CameraRecoilReturnSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Recoil|Shake")
	TSubclassOf<UCameraShakeBase> ShotCameraShakeClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Recoil|Shake")
	bool bEnableShotCameraShake = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Recoil|Shake", meta = (ClampMin = "0", ClampMax = "3"))
	float ShotCameraShakeScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Recoil|Shake", meta = (ClampMin = "0", ClampMax = "1"))
	float AimingCameraShakeMultiplier = 0.4f;

	/** Muzzle flash played locally on prediction and by Multicast_ShotFX. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UParticleSystem> MuzzleFX;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UParticleSystem> ImpactFX;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UMaterialInterface> ImpactDecalMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	FVector ImpactDecalSize = FVector(6.f, 6.f, 6.f);

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<USoundBase> FireSound;

	/** Fallback socket on the owner's first-person mesh when the data asset has no entry. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	FName WeaponAttachSocket1P = TEXT("hand_r");

	/** Fallback socket on the owner's third-person mesh when the data asset has no entry. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	FName WeaponAttachSocket3P = TEXT("hand_r");

	/** Server-side inventory of spawned weapons. Not replicated; clients only need CurrentWeapon. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TArray<TObjectPtr<AFPSWeapon>> Inventory;

	/** Replicated to every client; OnRep performs the cosmetic attach. */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AFPSWeapon> CurrentWeapon;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Combat")
	bool bIsAiming = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Combat")
	bool bIsFiring = false;

	/** Shared view, including owner prediction; the server alone owns its timer/ammo. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Combat")
	bool bReloading = false;

	UPROPERTY(ReplicatedUsing = OnRep_CombatState)
	FFPSCombatState CombatState;

	void SubmitCombatInput(EFPSCombatInput Input, int32 WeaponIndex = INDEX_NONE);
	void ApplyCombatInput(EFPSCombatInput Input, int32 WeaponIndex, bool bAuthority, uint32 InputId = 0);
	void SetAimingState(bool bAiming);
	void SetReloadingState(bool bNewReloading, AFPSWeapon* AnimationWeapon = nullptr, double EndServerTime = 0.);
	void PlayFireMontages(AFPSWeapon* Weapon);
	void UpdateReloadMontages();
	const FWeaponMontageSet* ResolveMontages(const AFPSWeapon* Weapon, bool bFirstPerson) const;
	TArray<FFPSCosmeticMontage> FireMontages;
	TArray<FFPSCosmeticMontage> ReloadMontages;
	TWeakObjectPtr<AFPSWeapon> ReloadAnimationWeapon;
	double ReloadAnimationEndServerTime = 0.;
	uint32 ReloadPresentationGeneration = 0;
	bool bEndingPlay = false;
	void StopFireAndAimForReload();
	void CancelReloadInternal();
	void PublishCombatState();
	void ApplyAuthoritativeCombatState(const FFPSCombatState& State);

	UFUNCTION(Server, Reliable)
	void Server_CombatInput(EFPSCombatInput Input, uint32 InputSequence, int32 WeaponIndex);

	/** Explicit acknowledgement also corrects rejected predictions with unchanged flags. */
	UFUNCTION(Client, Reliable)
	void Client_CombatState(const FFPSCombatState& State);

	UFUNCTION()
	void OnRep_CombatState();

	/** Clients attach the newly replicated weapon here. */
	UFUNCTION()
	void OnRep_CurrentWeapon(AFPSWeapon* OldWeapon);
	void NotifyCurrentWeaponChanged(AFPSWeapon* OldWeapon);
	void NotifyDisplayAmmoChanged(bool bForce = false);
	void CacheWeaponAmmoIfIdle();
	void AcceptAmmoBaseline(int32 Ammo, uint32 Revision);

	UFUNCTION()
	void HandleWeaponAmmoChanged(AFPSWeapon* Weapon, int32 CurrentAmmo, int32 PreviousAmmo);

	UFUNCTION()
	void HandleCurrentWeaponDestroyed(AActor* DestroyedWeapon);

	/** Authority-only: swaps CurrentWeapon and updates the local attachment. */
	void ApplyWeaponSwitch(int32 WeaponIndex);

	/** Resolves the first-person attach socket for a weapon, by weapon tag. */
	FName ResolveAttachSocket1P(const AFPSWeapon* Weapon) const;

	/** Resolves the third-person attach socket for a weapon, by weapon tag. */
	FName ResolveAttachSocket3P(const AFPSWeapon* Weapon) const;

	ACharacter* GetOwningCharacter() const;

	//////////////////////////////////////////////////////////////////////
	// Firing

	/** Local client -> server. The server never trusts a client-side hit. */
	UFUNCTION(Server, Reliable)
	void Server_FireShot(const FFPSShotRequest& Request);

	/** Server -> shooter: confirms or corrects the prediction. */
	UFUNCTION(Client, Reliable)
	void Client_ShotResult(const FFPSShotResult& Result);

	/** Server -> everyone (the shooter skips it; it already predicted). */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ShotFX(const FFPSShotFX& FX);

	/** Fires one shot locally and sends it to the server. Local client only. */
	void FireOnce();
	void ScheduleNextShot();
	void EndFireBurst();
	void PlayShotCameraShake();
	void StopShotCameraShake(bool bImmediately);
	void InitializeShotSeed();

	UPROPERTY(ReplicatedUsing = OnRep_ShotSeed)
	int32 ShotSeed = 0;

	UFUNCTION()
	void OnRep_ShotSeed();

	bool IsLocallyControlledOwner() const;

	/** Local hitscan against the currently visible characters and the world. */
	void PredictShot(const FVector& Start, const FVector& Dir, float Range,
		FFPSPredictedShot& OutPrediction) const;

	FTransform GetMuzzleTransform(const AFPSWeapon* Weapon, FName Socket) const;
	void SpawnMuzzleFX(const FVector& Location, const FRotator& Rotation) const;
	UDecalComponent* SpawnImpactFX(const FVector& Location, const FVector& Normal) const;

	/** Server-side reload timer; ignored when one is already running. */
	void StartReloadTimer(const FWeaponCombatSettings& Settings);
	void FinishReload(uint32 ExpectedGeneration, TWeakObjectPtr<AFPSWeapon> ExpectedWeapon);

	/** Client prediction state. */
	bool bWantsFire = false;
	float NextLocalFireTime = 0.f;
	int32 ShotSequence = 0;
	int32 PredictedAmmo = 0;
	TMap<int32, FFPSPredictedShot> PendingShots;
	TWeakObjectPtr<AFPSWeapon> AmmoBaselineWeapon;
	int32 ConfirmedDisplayAmmo = 0;
	uint32 ConfirmedAmmoRevision = 0;
	bool bHasAmmoBaseline = false;
	TWeakObjectPtr<AFPSWeapon> AmmoNotificationWeapon;
	int32 LastNotifiedAmmo = 0;
	bool bHasAmmoNotification = false;
	FTimerHandle FireTimerHandle;
	uint32 ActiveFireBurstId = 0;
	int32 NextLocalBurstShotIndex = 0;
	int32 ServerNextBurstShotIndex = 0;
	FVector2D CameraRecoil = FVector2D::ZeroVector;
	FVector2D CameraRecoilTarget = FVector2D::ZeroVector;
	TWeakObjectPtr<UCameraShakeBase> ActiveShotCameraShake;
	TWeakObjectPtr<APlayerCameraManager> ShotShakeCameraManager;

	/** Server-side replay and rate-limit state. */
	int32 LastServerSequence = 0;
	float ServerFireTokens = 3.f;
	float ServerFireRateLastUpdate = 0.f;

	uint32 LocalInputSequence = 0;
	uint32 LastReceivedStateRevision = 0;
	bool bHasReceivedCombatState = false;
	bool bHasLocalCombatInputs = false;
	uint32 ViewedReloadGeneration = 0;

	/** Server-only task identity prevents a stale callback filling a different weapon. */
	uint32 ReloadGeneration = 0;
	TWeakObjectPtr<AFPSWeapon> ReloadWeapon;
	int32 ReloadMagSize = 0;
	FTimerHandle ReloadTimerHandle;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSCombatTestAccess;
	friend struct FFPSRecoilTestAccess;
	friend struct FFPSHUDTestAccess;
	friend struct FFPSCameraShakeTestAccess;
	friend struct FFPSFeedbackTestAccess;
	friend struct FFPSAnimationTestAccess;
	friend struct FFPSDeathPresentationTestAccess;
	friend struct FFPSDamageWidgetTestAccess;
	friend struct FFPSNetworkDiagnosticsTestAccess;
#endif
};
