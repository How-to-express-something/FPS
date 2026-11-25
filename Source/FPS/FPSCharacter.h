// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "FPSWeaponAimTypes.h"
#include "FPSAnimationPresentation.h"
#include "FPSCharacter.generated.h"

class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UFPSCombatComponent;
class UFPSHealthComponent;
class UFPSHitReactionComponent;
class UAnimMontage;
class AFPSWeapon;
struct FInputActionValue;
struct FMinimalViewInfo;

class AFPSCharacter;

/** Cosmetic alignment runs after skeletal animation and the camera update. */
USTRUCT()
struct FFPSAimAlignmentTickFunction : public FTickFunction
{
	GENERATED_BODY()

	AFPSCharacter* Target = nullptr;
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType,
		ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
};

template<>
struct TStructOpsTypeTraits<FFPSAimAlignmentTickFunction>
	: public TStructOpsTypeTraitsBase2<FFPSAimAlignmentTickFunction>
{
	enum { WithCopy = false };
};

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UCLASS(config=Game)
class AFPSCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Third-person body, hidden from the owning player. The inherited Mesh is the first-person arms. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* Mesh3P;


	
	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Crouch Input Action. Held to stay crouched, released to stand up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* CrouchAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	/** Handles aim/fire input forwarded from the player controller. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat, meta = (AllowPrivateAccess = "true"))
	UFPSCombatComponent* CombatComponent;

	/** Replicated health, damage and death. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat, meta = (AllowPrivateAccess = "true"))
	UFPSHealthComponent* HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UFPSHitReactionComponent> HitReactionComponent;

	/** True once the local death handling (movement/collision) has run. */
	bool bDeathHandled = false;
	bool bCharacterEndingPlay = false;
	TArray<FFPSCosmeticMontage> ActiveHitReactionMontages;

public:
	AFPSCharacter();

	virtual void BeginPlay() override;
	void RefreshRecordingPresentation();
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnJumped_Implementation() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool IsPresentationReady() const;

	/** Routes damage to the health component. Only applies on the server. */
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const;
	void StartSpawnProtection(float Seconds);
	void CancelSpawnProtection();
	UFUNCTION(BlueprintPure, Category="Respawn") bool IsSpawnProtected() const;
	bool WasSpawnProtectedAt(double ServerTime) const;
	double GetBirthServerTime() const { return BirthServerTime; }
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Respawn") double BirthServerTime = 0.;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Respawn") double ProtectionUntil = 0.;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Respawn") double ProtectionCancelledAt = -1.;

	UFUNCTION(BlueprintPure, Category = "Health")
	UFPSHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category="Hit Reaction")
	UFPSHitReactionComponent* GetHitReactionComponent() const { return HitReactionComponent; }

	/** 24 slots: region * 4 + Front/Right/Back/Left. These are third-person assets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hit Reaction")
	TArray<TObjectPtr<UAnimMontage>> HitReactionMontages;

	/** Optional owning-view arms assets with the identical index contract. Empty entries do nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hit Reaction")
	TArray<TObjectPtr<UAnimMontage>> HitReactionMontages1P;
	void PlayHitReactionMontages(int32 Index);

	/** Always renders through FollowCamera; ADS only moves the arms and changes FOV. */
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	/** Starts or stops ADS on the locally controlled pawn. Called by the combat component. */
	void SetAiming(bool bNewAiming);

	/** Refreshes ADS settings after a weapon switch or late replication. */
	void NotifyWeaponEquipped(AFPSWeapon* NewWeapon);


	UFUNCTION(BlueprintImplementableEvent)
	void OnAim(bool bIsAiming);
	
	UFUNCTION(BlueprintCallable)
	FRotator GetFixedAimRotation() const;

	/**
	 * Change of the character's yaw since the previous frame, in degrees, normalized
	 * to -180..180. Positive is turning right. Readable from animation blueprints.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	float MovementOffsetYaw = 0.f;
protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Starts crouching through the character movement component. */
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StartCrouch();

	/** Stops crouching. */
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StopCrouch();

	/**
	 * ACharacter moves the inherited Mesh up by the crouch height change to keep its
	 * feet on the ground. In this project that Mesh is the first-person arms hanging
	 * off the camera, so the offset has to be redirected to Mesh3P instead.
	 */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	/** Bound to the health component's OnDeath (server and clients). Runs the
	 *  local death visuals and, on the server, reports the kill to the game mode. */
	UFUNCTION(BlueprintNativeEvent)
	void HandleDeath(AController* Killer);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);
			

protected:
	/** The only player camera. The first-person arms hang off it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Camera)
	UCameraComponent* FollowCamera;

	/** Field of view used while not aiming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Camera)
	float DefaultFoV;

	/** Character yaw sampled on the previous frame, used by MovementOffsetYaw. */
	float PreviousYaw = 0.f;

	/** Rest transforms captured at BeginPlay. */
	FVector Mesh1PDefaultRelativeLocation = FVector::ZeroVector;
	FRotator Mesh1PDefaultRelativeRotation = FRotator::ZeroRotator;
	FVector Mesh3PDefaultRelativeLocation = FVector::ZeroVector;

	/** Player is holding aim. */
	bool bWantsAim = false;
	FVector ProtectionOrigin = FVector::ZeroVector;
	FDelegateHandle PoseReadyHandle;
	bool bPoseEvaluated = false;

	/** ADS is blending in, fully aimed, or blending out. */
	bool bAimActive = false;

	/** Linear timeline position and its eased value. */
	float AimAlpha = 0.f;
	float AimBlendAlpha = 0.f;

	friend struct FFPSAimAlignmentTickFunction;
	FFPSAimAlignmentTickFunction AimAlignmentTick;
	virtual void RegisterActorTickFunctions(bool bRegister) override;
	void AlignFirstPersonMesh();

	/** Seconds for a full 0..1 blend of the currently resolved weapon. */
	float AimTime = 0.2f;

	/** Field of view at alpha == 1. */
	float AimTargetFOV = 90.f;

	/** Current alignment weapon; weak so destruction is detected. */
	TWeakObjectPtr<AFPSWeapon> AimWeapon;

	/** Tuning of the currently aimed weapon (curve is referenced by the data asset). */
	UPROPERTY()
	FWeaponAimSettings AimSettings;

	/** Resolves the weapon's aim tuning through the combat component. */
	bool ResolveAimSettings(const AFPSWeapon* Weapon, FWeaponAimSettings& OutSettings) const;

	/** Captures the aim target and starts the blend. */
	void BeginAim();

	/** Advances the arms/FOV blend before the camera is evaluated. */
	void UpdateAim(float DeltaSeconds);

	/** Aborts ADS and restores the arms and FOV. */
	void ResetAimState();


	virtual void NotifyControllerChanged() override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
	// /** Returns CameraBoom subobject **/
	// FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }


	/** Returns the mesh only the owning player sees (first-person arms). **/
	FORCEINLINE USkeletalMeshComponent* GetMesh1P() const { return GetMesh(); }

	/** Returns the mesh everyone else sees (the inherited third-person mesh). **/
	FORCEINLINE USkeletalMeshComponent* GetMesh3P() const { return Mesh3P; }
	/** Returns CombatComponent subobject **/
	FORCEINLINE UFPSCombatComponent* GetCombatComponent() const { return CombatComponent; }
	
	float GetDefaultFOV() const {return DefaultFoV;}
	
};

