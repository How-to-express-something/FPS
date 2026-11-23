// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSWeaponAnimationTypes.generated.h"

class UAnimSequence;
class UAnimMontage;

/** Per-weapon-type animation sequences (idle/fire/aim). */
USTRUCT(BlueprintType)
struct FWeaponAnimSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim")
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim")
	TObjectPtr<UAnimSequence> AimIdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim")
	TObjectPtr<UAnimSequence> CrouchIdleAnim;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim")
    TObjectPtr<UAnimSequence> SprintAnim;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim")
	TObjectPtr<UBlendSpace> AimOffset_Hip;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim")
	TObjectPtr<UBlendSpace> AimOffset_Aim;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim")
	TObjectPtr<UBlendSpace> Strafe_Standing;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim")
	TObjectPtr<UBlendSpace> Strafe_Crouching;
		
};

/** One view's character/arms and weapon montages. Fire/Reload target the character mesh. */
USTRUCT(BlueprintType)
struct FWeaponMontageSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	TObjectPtr<UAnimMontage> Fire;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	TObjectPtr<UAnimMontage> Reload;

	/** Optional animation of the weapon's own skeleton, not the character/arms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	TObjectPtr<UAnimMontage> ReloadWeaponMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	TObjectPtr<UAnimMontage> FireWeaponMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	TObjectPtr<UAnimMontage> Aim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	TObjectPtr<UAnimMontage> Equip;
};
