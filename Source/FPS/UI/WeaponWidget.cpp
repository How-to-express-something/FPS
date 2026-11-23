#include "WeaponWidget.h"
#include "../FPSCharacter.h"
#include "../FPSCombatComponent.h"
#include "../FPSWeapon.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "GameFramework/PlayerController.h"

void UWeaponWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
		WidgetTree->RootWidget = Root;
		WeaponNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WeaponNameText"));
		AmmoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AmmoText"));
		Root->AddChildToVerticalBox(WeaponNameText);
		Root->AddChildToVerticalBox(AmmoText);
	}
}

void UWeaponWidget::UnbindCombat()
{
	if (UFPSCombatComponent* Combat = ObservedCombat.Get())
	{
		Combat->OnCurrentWeaponChanged.RemoveDynamic(this, &UWeaponWidget::HandleCurrentWeaponChanged);
		Combat->OnAmmoChanged.RemoveDynamic(this, &UWeaponWidget::HandleAmmoChanged);
		Combat->OnAmmoDecreased.RemoveDynamic(this, &UWeaponWidget::HandleAmmoDecreased);
	}
	if (AFPSCharacter* Character = ObservedCharacter.Get())
		Character->OnDestroyed.RemoveDynamic(this, &UWeaponWidget::HandleObservedCharacterDestroyed);
	ObservedCombat.Reset();
}

void UWeaponWidget::SetObservedCharacter(AFPSCharacter* NewCharacter)
{
	UnbindCombat();
	const APlayerController* OwnerPC = GetOwningPlayer();
	if (!IsValid(NewCharacter) || !NewCharacter->IsLocallyControlled() ||
		(OwnerPC && NewCharacter->GetController() != OwnerPC)) NewCharacter = nullptr;
	ObservedCharacter = NewCharacter;
	UFPSCombatComponent* Combat = NewCharacter ? NewCharacter->GetCombatComponent() : nullptr;
	ObservedCombat = Combat;
	if (Combat)
	{
		Combat->OnCurrentWeaponChanged.AddUniqueDynamic(this, &UWeaponWidget::HandleCurrentWeaponChanged);
		Combat->OnAmmoChanged.AddUniqueDynamic(this, &UWeaponWidget::HandleAmmoChanged);
		Combat->OnAmmoDecreased.AddUniqueDynamic(this, &UWeaponWidget::HandleAmmoDecreased);
		NewCharacter->OnDestroyed.AddUniqueDynamic(this, &UWeaponWidget::HandleObservedCharacterDestroyed);
	}
	RefreshWeapon();
}

void UWeaponWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetObservedCharacter(Cast<AFPSCharacter>(GetOwningPlayerPawn()));
}

void UWeaponWidget::NativeDestruct()
{
	UnbindCombat();
	DisplayedWeapon = nullptr;
	Super::NativeDestruct();
}

void UWeaponWidget::RefreshWeapon()
{
	UFPSCombatComponent* Combat = ObservedCombat.Get();
	DisplayedWeapon = Combat ? Combat->GetCurrentWeapon() : nullptr;
	if (WeaponNameText)
	{
		const FText Name = DisplayedWeapon
			? FText::FromString(DisplayedWeapon->GetTags().IsValid() ? DisplayedWeapon->GetTags().ToString() : DisplayedWeapon->GetClass()->GetName())
			: NSLOCTEXT("FPSHUD", "NoWeapon", "No weapon");
		WeaponNameText->SetText(Name);
	}
	UpdateWeaponDisplay(DisplayedWeapon);
	RefreshAmmo();
}

void UWeaponWidget::RefreshAmmo()
{
	UFPSCombatComponent* Combat = ObservedCombat.Get();
	FWeaponCombatSettings Settings;
	DisplayedAmmo = Combat ? Combat->GetDisplayAmmo() : 0;
	DisplayedMagazineCapacity = Combat && Combat->ResolveCombatSettings(Combat->GetCurrentWeapon(), Settings) ? Settings.MagSize : 0;
	if (AmmoText) AmmoText->SetText(FText::Format(NSLOCTEXT("FPSHUD", "Ammo", "Ammo: {0} / {1}"),
		FText::AsNumber(DisplayedAmmo), FText::AsNumber(DisplayedMagazineCapacity)));
	UpdateAmmoDisplay(DisplayedAmmo, DisplayedMagazineCapacity);
}

void UWeaponWidget::HandleCurrentWeaponChanged(AFPSWeapon* Weapon, AFPSWeapon* PreviousWeapon)
{
	RefreshWeapon();
}

void UWeaponWidget::HandleAmmoChanged(AFPSWeapon* Weapon, int32 CurrentAmmo, int32 PreviousAmmo)
{
	if (ObservedCombat.IsValid() && Weapon == ObservedCombat->GetCurrentWeapon()) RefreshAmmo();
}

void UWeaponWidget::HandleAmmoDecreased(AFPSWeapon* Weapon, int32 CurrentAmmo, int32 PreviousAmmo)
{
	if (ObservedCombat.IsValid() && Weapon == ObservedCombat->GetCurrentWeapon())
		PlayAmmoDecreasedFeedback(CurrentAmmo, PreviousAmmo);
}

void UWeaponWidget::HandleObservedCharacterDestroyed(AActor* DestroyedActor)
{
	SetObservedCharacter(nullptr);
}
