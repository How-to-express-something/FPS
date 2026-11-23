#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "../FPSCombatTypes.h"
#include "CombatFeedbackWidget.generated.h"

class AFPSCharacter;
class UFPSCombatComponent;
class UFPSHealthComponent;

/** Local-only persistent overlay. HP changes show net damage/heal, not every damage event. */
UCLASS()
class FPS_API UCombatFeedbackWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Feedback") void SetObservedCharacter(AFPSCharacter* Character);
	UFUNCTION(BlueprintPure, Category="Feedback") float GetLastObservedHealth() const { return LastHealth; }
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Cull,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
	UPROPERTY(EditDefaultsOnly, Category="Feedback") FLinearColor DamageColor = FLinearColor(1.f,.04f,.02f,1.f);
	UPROPERTY(EditDefaultsOnly, Category="Feedback") FLinearColor HealColor = FLinearColor(.1f,1.f,.25f,1.f);
	UPROPERTY(EditDefaultsOnly, Category="Feedback", meta=(ClampMin=".05",ClampMax="2")) float EdgeFadeSeconds = .25f;
	UPROPERTY(EditDefaultsOnly, Category="Feedback", meta=(ClampMin="0",ClampMax=".8")) float MaxEdgeOpacity = .35f;
private:
	void Unbind();
	UFUNCTION() void HandleHealthChanged(float Health, float Maximum);
	UFUNCTION() void HandleHit(EFPSShotOutcome Outcome, bool bHeadshot, float ActualDamage);
	UFUNCTION() void HandleCharacterDestroyed(AActor* Actor);
	TWeakObjectPtr<AFPSCharacter> ObservedCharacter;
	TWeakObjectPtr<UFPSCombatComponent> ObservedCombat;
	TWeakObjectPtr<UFPSHealthComponent> ObservedHealth;
	float LastHealth = 0.f;
	float EdgeRemaining = 0.f;
	float HitRemaining = 0.f;
	float HitDuration = .12f;
	FLinearColor EdgeColor = FLinearColor::Red;
	FLinearColor MarkerColor = FLinearColor::White;
	EFPSShotOutcome MarkerOutcome = EFPSShotOutcome::Miss;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSFeedbackTestAccess;
#endif
};
