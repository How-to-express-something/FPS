#include "HUDWidget.h"
#include "HealthWidget.h"
#include "WeaponWidget.h"
#include "CrosshairWidget.h"
#include "CombatFeedbackWidget.h"
#include "../FPSCharacter.h"
#include "../FPSGameState.h"
#include "../FPSPlayerState.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "GameFramework/PlayerController.h"

UHUDWidget::UHUDWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	HealthWidgetClass = UHealthWidget::StaticClass();
	WeaponWidgetClass = UWeaponWidget::StaticClass();
	CrosshairWidgetClass = UCrosshairWidget::StaticClass();
	CombatFeedbackWidgetClass = UCombatFeedbackWidget::StaticClass();
}

void UHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree) return;
	if (!WidgetTree->RootWidget) WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	// Custom Designer layouts provide the named children via BindWidgetOptional.
	if (!HealthWidget && HealthWidgetClass && Canvas)
	{
		HealthWidget = GetOwningPlayer() ? CreateWidget<UHealthWidget>(GetOwningPlayer(), HealthWidgetClass)
			: CreateWidget<UHealthWidget>(GetWorld(), HealthWidgetClass);
		if (HealthWidget)
		{
			UCanvasPanelSlot* ChildSlot = Canvas->AddChildToCanvas(HealthWidget);
			ChildSlot->SetAnchors(FAnchors(0.f, 1.f));
			ChildSlot->SetAlignment(FVector2D(0.f, 1.f));
			ChildSlot->SetPosition(FVector2D(24.f, -24.f));
			ChildSlot->SetSize(FVector2D(260.f, 64.f));
		}
	}
	if (!WeaponWidget && WeaponWidgetClass && Canvas)
	{
		WeaponWidget = GetOwningPlayer() ? CreateWidget<UWeaponWidget>(GetOwningPlayer(), WeaponWidgetClass)
			: CreateWidget<UWeaponWidget>(GetWorld(), WeaponWidgetClass);
		if (WeaponWidget)
		{
			UCanvasPanelSlot* ChildSlot = Canvas->AddChildToCanvas(WeaponWidget);
			ChildSlot->SetAnchors(FAnchors(1.f, 1.f));
			ChildSlot->SetAlignment(FVector2D(1.f, 1.f));
			ChildSlot->SetPosition(FVector2D(-24.f, -24.f));
			ChildSlot->SetSize(FVector2D(280.f, 64.f));
		}
	}
	if (!Canvas && ((!CrosshairWidget && CrosshairWidgetClass) || (!CombatFeedbackWidget && CombatFeedbackWidgetClass)))
	{
		// Preserve the Designer layout and add a full-player-viewport layer above it.
		UWidget* PreviousRoot = WidgetTree->RootWidget;
		UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
		WidgetTree->RootWidget = Overlay;
		UOverlaySlot* ContentSlot = Overlay->AddChildToOverlay(PreviousRoot);
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UOverlaySlot* LayerSlot = Overlay->AddChildToOverlay(Canvas);
		LayerSlot->SetHorizontalAlignment(HAlign_Fill);
		LayerSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (!CrosshairWidget && CrosshairWidgetClass && Canvas)
	{
		CrosshairWidget = GetOwningPlayer() ? CreateWidget<UCrosshairWidget>(GetOwningPlayer(), CrosshairWidgetClass)
			: CreateWidget<UCrosshairWidget>(GetWorld(), CrosshairWidgetClass);
		if (CrosshairWidget)
		{
			UCanvasPanelSlot* CrosshairSlot = Canvas->AddChildToCanvas(CrosshairWidget);
			CrosshairSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CrosshairSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CrosshairSlot->SetPosition(FVector2D::ZeroVector);
			CrosshairSlot->SetSize(FVector2D(32.f, 32.f));
			CrosshairSlot->SetZOrder(10);
		}
	}
	if (!CombatFeedbackWidget && CombatFeedbackWidgetClass && Canvas)
	{
		CombatFeedbackWidget = GetOwningPlayer() ? CreateWidget<UCombatFeedbackWidget>(GetOwningPlayer(), CombatFeedbackWidgetClass)
			: CreateWidget<UCombatFeedbackWidget>(GetWorld(), CombatFeedbackWidgetClass);
		if (CombatFeedbackWidget)
		{
			UCanvasPanelSlot* FeedbackSlot = Canvas->AddChildToCanvas(CombatFeedbackWidget);
			FeedbackSlot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f));
			FeedbackSlot->SetOffsets(FMargin(0));
			FeedbackSlot->SetZOrder(20);
		}
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (!MatchStatusText && Canvas)
	{
		MatchStatusText = WidgetTree->ConstructWidget<UTextBlock>();
		MatchStatusText->SetJustification(ETextJustify::Center);
		UCanvasPanelSlot* StatusSlot = Canvas->AddChildToCanvas(MatchStatusText);
		StatusSlot->SetAnchors(FAnchors(.5f, 0.f));
		StatusSlot->SetAlignment(FVector2D(.5f, 0.f));
		StatusSlot->SetPosition(FVector2D(0.f, 30.f));
		StatusSlot->SetSize(FVector2D(700.f, 90.f));
	}
}

void UHUDWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	if (!MatchStatusText || !GetWorld()) return;
	const AFPSGameState* GS = GetWorld()->GetGameState<AFPSGameState>();
	if (!GS) { MatchStatusText->SetText(FText::GetEmpty()); return; }
	FString Status;
	if (GS->HasFinished())
		Status = FString::Printf(TEXT("Winner: %s | Returning to lobby in %ds"), *GS->WinnerName,
			FMath::Max(0, FMath::CeilToInt(GS->ReturnToLobbyTime - GS->GetServerWorldTimeSeconds())));
	else
	{
		const AFPSPlayerState* PS = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<AFPSPlayerState>() : nullptr;
		Status = FString::Printf(TEXT("DeathMatch | Kills %d / %d"), PS ? PS->GetKills() : 0, GS->KillTarget);
		if (const AFPSCharacter* C = Cast<AFPSCharacter>(GetOwningPlayerPawn()); C && C->IsSpawnProtected())
			Status += FString::Printf(TEXT("\nSpawn protection %.1fs (move/fire to cancel)"),
				FMath::Max(0., C->ProtectionUntil - GS->GetServerWorldTimeSeconds()));
	}
	MatchStatusText->SetText(FText::FromString(Status));
}

void UHUDWidget::SetObservedCharacter(AFPSCharacter* Character)
{
	const APlayerController* OwnerPC = GetOwningPlayer();
	if (!IsValid(Character) || !Character->IsLocallyControlled() ||
		(OwnerPC && Character->GetController() != OwnerPC)) Character = nullptr;
	if (HealthWidget) HealthWidget->SetObservedCharacter(Character);
	if (WeaponWidget) WeaponWidget->SetObservedCharacter(Character);
	if (CombatFeedbackWidget) CombatFeedbackWidget->SetObservedCharacter(Character);
}

void UHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetObservedCharacter(Cast<AFPSCharacter>(GetOwningPlayerPawn()));
}

void UHUDWidget::NativeDestruct()
{
	SetObservedCharacter(nullptr);
	Super::NativeDestruct();
}
