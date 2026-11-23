#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "../FPSCombatTypes.h"
#include "ConfirmedDamageWidget.generated.h"

class AFPSCharacter;
class APawn;
class APlayerController;
class UFPSCombatComponent;
class UTextBlock;

/** Independent owning-player debug readout. Latest confirmed hit replaces the previous one. */
UCLASS(Blueprintable)
class FPS_API UConfirmedDamageWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Damage Test") void SetObservedCharacter(AFPSCharacter* Character);
	UFUNCTION(BlueprintCallable, Category="Damage Test") void ClearDamage();
	UPROPERTY(BlueprintReadOnly, Transient, Category="Damage Test") float DisplayedDamage = 0.f;
	UPROPERTY(BlueprintReadOnly, Transient, Category="Damage Test") bool bDamageVisible = false;
	UPROPERTY(BlueprintReadOnly, Transient, Category="Damage Test") bool bLastHeadshot = false;
	UPROPERTY(BlueprintReadOnly, Transient, Category="Damage Test") bool bLastKilled = false;
	/** Seconds since the latest valid receipt; uses World game time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage Test", meta=(ClampMin="0.05",ClampMax="30"))
	float DisplaySeconds = 1.5f;
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	/** Optional Designer TextBlock, named DamageText with Is Variable enabled. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Damage Test") TObjectPtr<UTextBlock> DamageText;
	UFUNCTION(BlueprintImplementableEvent, Category="Damage Test")
	void OnDamageShown(float ActualDamage, bool bHeadshot, bool bKilled);
	UFUNCTION(BlueprintImplementableEvent, Category="Damage Test") void OnDamageCleared();
private:
	void UnbindCharacter();
	void CancelHideTimer();
	void ResetDisplay(bool bNotifyBlueprint);
	void RefreshText();
	UFUNCTION() void HandleHit(EFPSShotOutcome Outcome, bool bHeadshot, float ActualDamage);
	UFUNCTION() void HandlePawnChanged(APawn* OldPawn, APawn* NewPawn);
	UFUNCTION() void HandleCharacterDestroyed(AActor* Actor);
	TWeakObjectPtr<AFPSCharacter> ObservedCharacter;
	TWeakObjectPtr<UFPSCombatComponent> ObservedCombat;
	TWeakObjectPtr<APlayerController> ObservedController;
	TWeakObjectPtr<UWorld> HideTimerWorld;
	FTimerHandle HideTimer;
	uint64 DisplayGeneration = 0;
	uint64 BindingGeneration = 0;
	bool bConstructed = false;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSDamageWidgetTestAccess;
#endif
};
