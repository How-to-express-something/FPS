#include "CombatFeedbackWidget.h"
#include "../FPSCharacter.h"
#include "../FPSCombatComponent.h"
#include "../FPSHealthComponent.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"

void UCombatFeedbackWidget::Unbind()
{
	if (auto* Combat = ObservedCombat.Get()) Combat->OnHitConfirmed.RemoveDynamic(this, &UCombatFeedbackWidget::HandleHit);
	if (auto* Health = ObservedHealth.Get()) Health->OnHealthChanged.RemoveDynamic(this, &UCombatFeedbackWidget::HandleHealthChanged);
	if (auto* C = ObservedCharacter.Get()) C->OnDestroyed.RemoveDynamic(this, &UCombatFeedbackWidget::HandleCharacterDestroyed);
	ObservedCombat.Reset(); ObservedHealth.Reset(); ObservedCharacter.Reset();
	LastHealth = EdgeRemaining = HitRemaining = 0.f;
	MarkerOutcome = EFPSShotOutcome::Miss;
	if (TSharedPtr<SWidget> Widget = GetCachedWidget()) Widget->Invalidate(EInvalidateWidgetReason::Paint);
}

void UCombatFeedbackWidget::SetObservedCharacter(AFPSCharacter* C)
{
	if (!IsValid(C) || !C->IsLocallyControlled() || (GetOwningPlayer() && C->GetController() != GetOwningPlayer())) C = nullptr;
	if (ObservedCharacter.Get() == C && C && ObservedHealth.Get() == C->GetHealthComponent()) return;
	Unbind();
	if (!C) return;
	ObservedCharacter = C;
	ObservedHealth = C->GetHealthComponent();
	ObservedCombat = C->GetCombatComponent();
	// Seed before binding: initial full health / pawn changes are not healing events.
	LastHealth = ObservedHealth.IsValid() ? ObservedHealth->GetHealth() : 0.f;
	if (auto* Health = ObservedHealth.Get()) Health->OnHealthChanged.AddUniqueDynamic(this, &UCombatFeedbackWidget::HandleHealthChanged);
	if (auto* Combat = ObservedCombat.Get()) Combat->OnHitConfirmed.AddUniqueDynamic(this, &UCombatFeedbackWidget::HandleHit);
	C->OnDestroyed.AddUniqueDynamic(this, &UCombatFeedbackWidget::HandleCharacterDestroyed);
}

void UCombatFeedbackWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetObservedCharacter(Cast<AFPSCharacter>(GetOwningPlayerPawn()));
}

void UCombatFeedbackWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UCombatFeedbackWidget::HandleCharacterDestroyed(AActor*) { SetObservedCharacter(nullptr); }

void UCombatFeedbackWidget::HandleHealthChanged(float Health, float)
{
	if (!ObservedCharacter.IsValid() || !ObservedHealth.IsValid() || !FMath::IsFinite(Health)) return;
	if (GetOwningPlayer() && GetOwningPlayer()->GetPawn() != ObservedCharacter.Get()) return;
	const float Delta = Health - LastHealth;
	LastHealth = Health; // Commit even if the same value, before any future presentation extension.
	if (FMath::IsNearlyZero(Delta)) return;
	EdgeColor = Delta > 0.f ? HealColor : DamageColor;
	EdgeRemaining = FMath::Clamp(EdgeFadeSeconds, .05f, 2.f);
}

void UCombatFeedbackWidget::HandleHit(EFPSShotOutcome Outcome, bool bHeadshot, float ActualDamage)
{
	if (!ObservedCharacter.IsValid() || ObservedCharacter->IsDead()) return;
	if (GetOwningPlayer() && GetOwningPlayer()->GetPawn() != ObservedCharacter.Get()) return;
	const bool bDamage = (Outcome == EFPSShotOutcome::Damaged || Outcome == EFPSShotOutcome::Killed) && ActualDamage > 0.f;
	if (!bDamage && Outcome != EFPSShotOutcome::Protected) return;
	// A body confirmation bunched behind a kill must not erase the stronger mark.
	if (HitRemaining > 0.f && MarkerOutcome == EFPSShotOutcome::Killed && Outcome != MarkerOutcome) return;
	MarkerOutcome = Outcome;
	HitDuration = Outcome == EFPSShotOutcome::Killed ? .3f : .12f;
	HitRemaining = HitDuration;
	MarkerColor = Outcome == EFPSShotOutcome::Protected ? FLinearColor(.25f,.65f,1.f)
		: (Outcome == EFPSShotOutcome::Killed ? FLinearColor(1.f,.2f,.1f) : (bHeadshot ? FLinearColor(1.f,.8f,.1f) : FLinearColor::White));
}

void UCombatFeedbackWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	const bool bWasVisible = EdgeRemaining > 0.f || HitRemaining > 0.f;
	const float Step = FMath::Max(0.f, DeltaSeconds);
	EdgeRemaining = FMath::Max(0.f, EdgeRemaining - Step);
	HitRemaining = FMath::Max(0.f, HitRemaining - Step);
	// Also invalidate the last fade-out frame so cached paint cannot retain an old mark.
	if (bWasVisible)
		if (TSharedPtr<SWidget> Widget = GetCachedWidget()) Widget->Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UCombatFeedbackWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Cull,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
	const int32 Base = Super::NativePaint(Args, Geometry, Cull, Elements, Layer, Style, bEnabled);
	const FVector2D Size = Geometry.GetLocalSize();
	const FLinearColor Tint = Style.GetColorAndOpacityTint();
	if (EdgeRemaining > 0.f)
	{
		const float Width = FMath::Min(Size.X, Size.Y) * .075f;
		const float Alpha = FMath::Clamp(EdgeRemaining / FMath::Clamp(EdgeFadeSeconds, .05f, 2.f), 0.f, 1.f) * FMath::Clamp(MaxEdgeOpacity, 0.f, .8f);
		const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
		for (int32 Band = 0; Band < 8; ++Band)
		{
			const float Offset = Width * Band / 8.f, Strip = Width / 8.f;
			FLinearColor Color = EdgeColor * Tint; Color.A *= Alpha * FMath::Square(1.f - Band / 8.f);
			const auto Box = [&](FVector2D Position, FVector2D Extent)
			{
				FSlateDrawElement::MakeBox(Elements, Base + 1, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Position)), Brush, ESlateDrawEffect::None, Color);
			};
			Box(FVector2D(Offset,0), FVector2D(Strip,Size.Y));
			Box(FVector2D(Size.X-Offset-Strip,0), FVector2D(Strip,Size.Y));
			Box(FVector2D(Width,Offset), FVector2D(FMath::Max(0.f,Size.X-2*Width),Strip));
			Box(FVector2D(Width,Size.Y-Offset-Strip), FVector2D(FMath::Max(0.f,Size.X-2*Width),Strip));
		}
	}
	if (HitRemaining > 0.f)
	{
		FLinearColor Color = MarkerColor * Tint; Color.A *= FMath::Clamp(HitRemaining / HitDuration, 0.f, 1.f);
		const FVector2D Center = Size * .5f;
		const float Outer = MarkerOutcome == EFPSShotOutcome::Killed ? 15.f : 11.f;
		for (const FVector2D D : {FVector2D(1,1),FVector2D(1,-1),FVector2D(-1,1),FVector2D(-1,-1)})
		{
			const TArray<FVector2D> Points = {Center + D * 5.f, Center + D * Outer};
			FSlateDrawElement::MakeLines(Elements, Base + 2, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true,
				MarkerOutcome == EFPSShotOutcome::Killed ? 3.f : 2.f);
		}
	}
	return Base + 2;
}
