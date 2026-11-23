#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HUDWidget.generated.h"

class AFPSCharacter;
class UHealthWidget;
class UWeaponWidget;
class UCrosshairWidget;
class UTextBlock;
class UCombatFeedbackWidget;

/** A local player's HUD owns child widget instances and their configurable classes. */
UCLASS()
class FPS_API UHUDWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UHUDWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetObservedCharacter(AFPSCharacter* Character);

	UFUNCTION(BlueprintPure, Category = "HUD")
	UHealthWidget* GetHealthWidget() const { return HealthWidget; }

	UFUNCTION(BlueprintPure, Category = "HUD")
	UWeaponWidget* GetWeaponWidget() const { return WeaponWidget; }

	UFUNCTION(BlueprintPure, Category = "HUD")
	UCrosshairWidget* GetCrosshairWidget() const { return CrosshairWidget; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> MatchStatusText;

	/** Used when the HUD Blueprint does not already contain a named child widget. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UHealthWidget> HealthWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UWeaponWidget> WeaponWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UCrosshairWidget> CrosshairWidgetClass;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "HUD")
	TObjectPtr<UHealthWidget> HealthWidget;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "HUD")
	TObjectPtr<UWeaponWidget> WeaponWidget;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "HUD")
	TObjectPtr<UCrosshairWidget> CrosshairWidget;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="HUD") TSubclassOf<UCombatFeedbackWidget> CombatFeedbackWidgetClass;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="HUD") TObjectPtr<UCombatFeedbackWidget> CombatFeedbackWidget;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSHUDTestAccess;
#endif
};
