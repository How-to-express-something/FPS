// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FPSWeaponAnimationTypes.h"
#include "FPSWeaponAimTypes.h"
#include "FPSCombatTypes.h"
#include "FPSWeaponDataAsset.generated.h"

/**
 * Central lookup table for weapon data, keyed by weapon type tag (Weapon.Type.*).
 * Holds animation sequences, attach sockets, and montages per weapon type.
 */
UCLASS(BlueprintType)
class FPS_API UFPSWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Animation sequences per weapon type. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TMap<FGameplayTag, FWeaponAnimSet> WeaponAnimSets;
	
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TMap<FGameplayTag, FWeaponAnimSet> WeaponAnimSets1P;
	
	
	/** Attach socket on the third-person character mesh per weapon type. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TMap<FGameplayTag, FName> WeaponAttachSockets3P;

	/** Attach socket on the first-person character mesh per weapon type. Empty
	 *  entries fall back to the third-person socket. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TMap<FGameplayTag, FName> WeaponAttachSockets1P;
	
	
	
	/** Third-person body/weapon montages. Existing Fire/Reload entries remain compatible. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TMap<FGameplayTag, FWeaponMontageSet> WeaponMontageSets;

	/** First-person arms/weapon montages. No fallback to 3P: skeletons can differ. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TMap<FGameplayTag, FWeaponMontageSet> WeaponMontageSets1P;

	/** ADS tuning (aim socket, FOV, blend time) per weapon type. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TMap<FGameplayTag, FWeaponAimSettings> WeaponAimSettings;

	/** Damage, RPM, spread (0..1), magazine and reload tuning per weapon type. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TMap<FGameplayTag, FWeaponCombatSettings> WeaponCombatSettings;
};
