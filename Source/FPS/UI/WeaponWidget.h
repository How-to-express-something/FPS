#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WeaponWidget.generated.h"

class AFPSCharacter;
class AFPSWeapon;
class UFPSCombatComponent;
class UTextBlock;

/** Event-driven HUD for the owning player's pawn only. */
UCLASS()
class FPS_API UWeaponWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetObservedCharacter(AFPSCharacter* NewCharacter);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void UpdateWeaponDisplay(AFPSWeapon* Weapon);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void UpdateAmmoDisplay(int32 Ammo, int32 MagazineCapacity);

	/** Includes downward corrections, not only accepted shots. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void PlayAmmoDecreasedFeedback(int32 CurrentAmmo, int32 PreviousAmmo);

	UPROPERTY(BlueprintReadOnly, Transient, Category = "HUD")
	TObjectPtr<AFPSWeapon> DisplayedWeapon;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "HUD")
	int32 DisplayedAmmo = 0;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "HUD")
	int32 DisplayedMagazineCapacity = 0;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	void UnbindCombat();
	void RefreshWeapon();
	void RefreshAmmo();

	UFUNCTION()
	void HandleCurrentWeaponChanged(AFPSWeapon* Weapon, AFPSWeapon* PreviousWeapon);

	UFUNCTION()
	void HandleAmmoChanged(AFPSWeapon* Weapon, int32 CurrentAmmo, int32 PreviousAmmo);

	UFUNCTION()
	void HandleAmmoDecreased(AFPSWeapon* Weapon, int32 CurrentAmmo, int32 PreviousAmmo);

	UFUNCTION()
	void HandleObservedCharacterDestroyed(AActor* DestroyedActor);

	UPROPERTY(BlueprintReadOnly, Transient, Category = "HUD")
	TWeakObjectPtr<AFPSCharacter> ObservedCharacter;

	TWeakObjectPtr<UFPSCombatComponent> ObservedCombat;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "HUD")
	TObjectPtr<UTextBlock> WeaponNameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "HUD")
	TObjectPtr<UTextBlock> AmmoText;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSHUDTestAccess;
#endif
};
