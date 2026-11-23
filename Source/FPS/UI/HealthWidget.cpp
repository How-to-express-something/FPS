// Fill out your copyright notice in the Description page of Project Settings.


#include "HealthWidget.h"
#include "../FPSCharacter.h"
#include "../FPSHealthComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

void UHealthWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
		WidgetTree->RootWidget = Root;
		HealthText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HealthText"));
		HealthBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
		Root->AddChildToVerticalBox(HealthText);
		Root->AddChildToVerticalBox(HealthBar);
	}
}

void UHealthWidget::UnbindHealth()
{
	if (UFPSHealthComponent* Health = ObservedHealth.Get())
		Health->OnHealthChanged.RemoveDynamic(this, &UHealthWidget::HandleHealthChanged);
	if (AFPSCharacter* Character = ObservedCharacter.Get())
		Character->OnDestroyed.RemoveDynamic(this, &UHealthWidget::HandleObservedCharacterDestroyed);
	ObservedHealth.Reset();
}

void UHealthWidget::SetObservedCharacter(AFPSCharacter* NewCharacter)
{
	UnbindHealth();
	ObservedCharacter = IsValid(NewCharacter) ? NewCharacter : nullptr;
	UFPSHealthComponent* Health = ObservedCharacter.IsValid() ? ObservedCharacter->GetHealthComponent() : nullptr;
	ObservedHealth = Health;
	if (Health)
	{
		Health->OnHealthChanged.AddUniqueDynamic(this, &UHealthWidget::HandleHealthChanged);
		ObservedCharacter->OnDestroyed.AddUniqueDynamic(this, &UHealthWidget::HandleObservedCharacterDestroyed);
	}
	RefreshHealth(Health ? Health->GetHealth() : 0.f, Health ? Health->GetMaxHealth() : 0.f);
}

void UHealthWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetObservedCharacter(ObservedCharacter.IsValid() ? ObservedCharacter.Get() : Cast<AFPSCharacter>(GetOwningPlayerPawn()));
}

void UHealthWidget::NativeDestruct()
{
	UnbindHealth();
	Super::NativeDestruct();
}

void UHealthWidget::HandleHealthChanged(float CurrentHealth,float MaxHealth)
{
	RefreshHealth(CurrentHealth, MaxHealth);
}

void UHealthWidget::RefreshHealth(float CurrentHealth, float MaxHealth)
{
	DisplayedHealth = FMath::Max(CurrentHealth, 0.f);
	DisplayedMaxHealth = FMath::Max(MaxHealth, 0.f);
	if (HealthBar) HealthBar->SetPercent(MaxHealth > 0.f ? FMath::Clamp(CurrentHealth / MaxHealth, 0.f, 1.f) : 0.f);
	if (HealthText) HealthText->SetText(FText::Format(NSLOCTEXT("FPSHUD", "Health", "Health: {0} / {1}"),
		FText::AsNumber(FMath::CeilToInt(DisplayedHealth)), FText::AsNumber(FMath::CeilToInt(DisplayedMaxHealth))));
	UpdateHealthDisplay(DisplayedHealth, DisplayedMaxHealth);
}

void UHealthWidget::HandleObservedCharacterDestroyed(AActor* DestroyedActor)
{
	SetObservedCharacter(nullptr);
}
