// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HealthWidget.generated.h"

/**
 * 
 */

class AFPSCharacter;
class UFPSHealthComponent;
class UProgressBar;
class UTextBlock;

UCLASS()
class FPS_API UHealthWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void UpdateHealthDisplay(float CurrentHealth, float MaxHealth);
	
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetObservedCharacter(AFPSCharacter* NewCharacter);

	UPROPERTY(BlueprintReadOnly, Transient, Category = "HUD")
	float DisplayedHealth = 0.f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "HUD")
	float DisplayedMaxHealth = 0.f;
	
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	void UnbindHealth();
	void RefreshHealth(float CurrentHealth, float MaxHealth);
	
	
	
	// 委托回调
	UFUNCTION()
	void HandleHealthChanged(float CurrentHealth,float MaxHealth);
	
	
	UFUNCTION()
	void HandleObservedCharacterDestroyed(AActor* DestroyedActor);

	UPROPERTY(BlueprintReadOnly, Transient, Category = "HUD")
	TWeakObjectPtr<AFPSCharacter> ObservedCharacter;

	TWeakObjectPtr<UFPSHealthComponent> ObservedHealth;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "HUD")
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "HUD")
	TObjectPtr<UTextBlock> HealthText;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSHUDTestAccess;
#endif
	
};
