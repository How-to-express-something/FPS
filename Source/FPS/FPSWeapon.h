// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSWeapon.generated.h"

class USkeletalMeshComponent;
class USceneComponent;
class AFPSWeapon;

/** Revision travels with ammo so an older replicated value cannot undo a shot ack. */
USTRUCT()
struct FFPSWeaponAmmoState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Ammo = 0;

	UPROPERTY()
	uint32 Revision = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FFPSWeaponAmmoChanged, AFPSWeapon*, Weapon,
	int32, CurrentAmmo, int32, PreviousAmmo);

/**
 * Minimal weapon actor: owns a first- and a third-person mesh and can be attached
 * to a character's hands.
 *
 * The meshes are the mirror image of the character's own ones: WeaponMesh1P is only
 * drawn for the player holding the weapon and hangs off that player's Mesh1P, while
 * WeaponMesh3P is what every other client sees and hangs off the character's default
 * (third-person) mesh. Both sockets are resolved by the combat component.
 */
UCLASS()
class FPS_API AFPSWeapon : public AActor
{
	GENERATED_BODY()

public:
	AFPSWeapon();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Attaches WeaponMesh3P to the character's default (third-person) mesh and
	 * WeaponMesh1P to its first-person mesh, each at its own socket.
	 */
	void AttachToCharacter(class ACharacter* Character, FName Socket1P, FName Socket3P);

	/** Detaches both meshes and hides them (used when unequipped). */
	void DetachFromCharacter();
	/** Terminal presentation cleanup for this owner's life, including hidden shadows. */
	void HideForOwnerDeath();

	/**
	 * Marks this weapon as its owner's equipped weapon. The flag replicates, so a
	 * client can put the weapon in the hands without relying on the order in which
	 * the combat component's CurrentWeapon and this actor replicate.
	 */
	void SetEquipped(bool bNewEquipped);

	bool IsEquipped() const { return bEquipped; }

	/** Mesh the player holding this weapon sees. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	USkeletalMeshComponent* GetWeaponMesh1P() const { return WeaponMesh1P; }

	/** Mesh every other client sees in the holder's hands. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	USkeletalMeshComponent* GetWeaponMesh3P() const { return WeaponMesh3P; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FGameplayTag WeaponTypeTag;

	const FGameplayTag& GetTags() const { return WeaponTypeTag; }
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	float ZoomRatio = 1.5f;

    float GetZoomRatio() const { return ZoomRatio; }

	/** Authoritative magazine contents, replicated to every client. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetAmmoInMag() const { return AmmoInMag; }

	uint32 GetAmmoRevision() const { return AmmoState.Revision; }

	UPROPERTY(BlueprintAssignable, Category = "Weapon")
	FFPSWeaponAmmoChanged OnAmmoInMagChanged;

	/** Server-only: overwrites the magazine (spawn and reload). */
	void SetAmmoInMag(int32 NewAmmo);
protected:

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Weapon")
	int32 AmmoInMag = 0;

	UPROPERTY(ReplicatedUsing = OnRep_AmmoState)
	FFPSWeaponAmmoState AmmoState;

	UFUNCTION()
	void OnRep_AmmoState();

	/** Replicated so clients can attach the weapon independently of CurrentWeapon. */
	UPROPERTY(ReplicatedUsing = OnRep_Equipped)
	bool bEquipped = false;

	/** Reparents the meshes onto the owning character if this weapon is the equipped one. */
	void TryAttachToOwner();

	/**
	 * A skeletal mesh that has not evaluated a pose yet reports a bogus socket
	 * transform, and SnapToTarget would bake that into the weapon's position. Reject
	 * offsets that cannot be a hand so the attach can be retried once the pose exists.
	 */
	bool IsSocketTransformPlausible(const class USkeletalMeshComponent* Body, FName Socket) const;

	/** Bounds of a plausible socket-to-body offset, in centimetres. */
	static constexpr float MaxSocketOffset = 1000.f;

	/** Remaining retries while waiting for the character's mesh to evaluate a pose. */
	int32 SocketRetriesLeft = 0;

	FTimerHandle SocketRetryHandle;

	UFUNCTION()
	void OnRep_Equipped();

	/** Owner and bEquipped arrive in the same bunch; re-run the attach when either lands. */
	virtual void OnRep_Owner() override;

	/** Plain root: both meshes are re-parented to the character at runtime. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USceneComponent> SceneRoot;

	/** First-person mesh: only the owning player sees it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh1P;

	/** Third-person mesh: everyone except the owning player sees it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh3P;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSHUDTestAccess;
	friend struct FFPSDeathPresentationTestAccess;
#endif
};
