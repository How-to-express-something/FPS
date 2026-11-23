#include "ConfirmedDamageWidget.h"
#include "../FPSCharacter.h"
#include "../FPSCombatComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

void UConfirmedDamageWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		DamageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DamageText"));
		WidgetTree->RootWidget = DamageText;
	}
	RefreshText();
}

void UConfirmedDamageWidget::NativeConstruct()
{
	bConstructed = true;
	Super::NativeConstruct(); // Blueprint Construct runs here and may remove the widget.
	if (!bConstructed) return;
	APlayerController* PC = GetOwningPlayer();
	if (auto* Previous = ObservedController.Get(); Previous && Previous != PC)
		Previous->OnPossessedPawnChanged.RemoveDynamic(this, &UConfirmedDamageWidget::HandlePawnChanged);
	ObservedController = PC;
	if (PC && PC->IsLocalController())
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UConfirmedDamageWidget::HandlePawnChanged);
	SetObservedCharacter(PC ? Cast<AFPSCharacter>(PC->GetPawn()) : nullptr);
}

void UConfirmedDamageWidget::UnbindCharacter()
{
	if (auto* Combat = ObservedCombat.Get()) Combat->OnHitConfirmed.RemoveDynamic(this, &UConfirmedDamageWidget::HandleHit);
	if (auto* Character = ObservedCharacter.Get()) Character->OnDestroyed.RemoveDynamic(this, &UConfirmedDamageWidget::HandleCharacterDestroyed);
	ObservedCharacter.Reset();
	ObservedCombat.Reset();
}

void UConfirmedDamageWidget::SetObservedCharacter(AFPSCharacter* Character)
{
	APlayerController* PC = GetOwningPlayer();
	// PC's Pawn is the binding identity. Character.Controller can replicate later.
	if (!bConstructed || !PC || !PC->IsLocalController() || !IsValid(Character) || PC->GetPawn() != Character) Character = nullptr;
	if (Character && ObservedCharacter.Get() == Character && ObservedCombat.Get() == Character->GetCombatComponent()) return;
	const uint64 Binding = ++BindingGeneration;
	UnbindCharacter();
	ResetDisplay(true);
	if (!bConstructed || Binding != BindingGeneration || !Character) return;
	ObservedCharacter = Character;
	ObservedCombat = Character->GetCombatComponent();
	if (auto* Combat = ObservedCombat.Get()) Combat->OnHitConfirmed.AddUniqueDynamic(this, &UConfirmedDamageWidget::HandleHit);
	Character->OnDestroyed.AddUniqueDynamic(this, &UConfirmedDamageWidget::HandleCharacterDestroyed);
}

void UConfirmedDamageWidget::HandlePawnChanged(APawn*, APawn* NewPawn)
{
	SetObservedCharacter(Cast<AFPSCharacter>(NewPawn));
}

void UConfirmedDamageWidget::HandleCharacterDestroyed(AActor*) { SetObservedCharacter(nullptr); }

void UConfirmedDamageWidget::CancelHideTimer()
{
	// Clear on the World that scheduled it, even if owning player/World changed.
	if (auto* World = HideTimerWorld.Get()) World->GetTimerManager().ClearTimer(HideTimer);
	HideTimer.Invalidate();
	HideTimerWorld.Reset();
}

void UConfirmedDamageWidget::ResetDisplay(bool bNotifyBlueprint)
{
	const bool bHadDisplay = bDamageVisible;
	++DisplayGeneration;
	CancelHideTimer();
	DisplayedDamage = 0.f;
	bDamageVisible = bLastHeadshot = bLastKilled = false;
	RefreshText();
	if (bNotifyBlueprint && bConstructed && bHadDisplay) OnDamageCleared(); // Last operation: may re-enter/remove UI.
}

void UConfirmedDamageWidget::ClearDamage() { ResetDisplay(true); }

void UConfirmedDamageWidget::RefreshText()
{
	if (!DamageText) return;
	FNumberFormattingOptions Format;
	Format.SetMaximumFractionalDigits(2);
	DamageText->SetText(bDamageVisible ? FText::Format(NSLOCTEXT("FPSDamageTest", "Damage", "Damage: {0}"),
		FText::AsNumber(DisplayedDamage, &Format)) : FText::GetEmpty());
	DamageText->SetVisibility(bDamageVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UConfirmedDamageWidget::HandleHit(EFPSShotOutcome Outcome, bool bHeadshot, float ActualDamage)
{
	APlayerController* PC = GetOwningPlayer();
	if (!bConstructed || !PC || !PC->IsLocalController() || !ObservedCharacter.IsValid() ||
		PC->GetPawn() != ObservedCharacter.Get() || !ObservedCombat.IsValid() ||
		!FMath::IsFinite(ActualDamage) || ActualDamage <= 0.f ||
		(Outcome != EFPSShotOutcome::Damaged && Outcome != EFPSShotOutcome::Killed)) return;
	UWorld* World = GetWorld();
	if (!World) return;
	CancelHideTimer();
	const uint64 Generation = ++DisplayGeneration;
	DisplayedDamage = ActualDamage;
	bDamageVisible = true;
	bLastHeadshot = bHeadshot;
	bLastKilled = Outcome == EFPSShotOutcome::Killed;
	RefreshText();
	const float Duration = FMath::IsFinite(DisplaySeconds) ? FMath::Clamp(DisplaySeconds, .05f, 30.f) : 1.5f;
	HideTimerWorld = World;
	World->GetTimerManager().SetTimer(HideTimer, FTimerDelegate::CreateWeakLambda(this, [this, Generation]()
	{
		if (bConstructed && Generation == DisplayGeneration) ResetDisplay(true);
	}), Duration, false);
	// Timer and state committed before any Blueprint code may leave/travel/rebind.
	OnDamageShown(DisplayedDamage, bLastHeadshot, bLastKilled);
}

void UConfirmedDamageWidget::NativeDestruct()
{
	bConstructed = false;
	++BindingGeneration;
	UnbindCharacter();
	if (auto* PC = ObservedController.Get()) PC->OnPossessedPawnChanged.RemoveDynamic(this, &UConfirmedDamageWidget::HandlePawnChanged);
	ObservedController.Reset();
	ResetDisplay(false);
	Super::NativeDestruct(); // Blueprint Destruct sees already-clean state.
}
