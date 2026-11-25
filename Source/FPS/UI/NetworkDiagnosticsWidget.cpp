#include "NetworkDiagnosticsWidget.h"
#include "../FPSCombatComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void UNetworkDiagnosticsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(.015f, .025f, .045f, .88f));
		Background->SetPadding(FMargin(12.f));
		DiagnosticsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DiagnosticsText"));
		FSlateFontInfo Font = DiagnosticsText->GetFont(); Font.Size = 12;
		DiagnosticsText->SetFont(Font);
		DiagnosticsText->SetColorAndOpacity(FSlateColor(FLinearColor(.8f, .95f, 1.f)));
		Background->SetContent(DiagnosticsText);
		WidgetTree->RootWidget = Background;
	}
}

void UNetworkDiagnosticsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshElapsed = 0.f;
	RefreshDiagnostics();
}

void UNetworkDiagnosticsWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	RefreshElapsed += DeltaTime;
	if (RefreshElapsed >= .1f) { RefreshElapsed = 0.f; RefreshDiagnostics(); }
}

void UNetworkDiagnosticsWidget::RefreshDiagnostics()
{
	// Resolve possession afresh; respawn/travel cannot retain an old combat component.
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC && PC->IsLocalController() && PC->GetWorld() == GetWorld() ? PC->GetPawn() : nullptr;
	const UFPSCombatComponent* Combat = Pawn && !Pawn->IsActorBeingDestroyed() && Pawn->GetWorld() == GetWorld()
		? Pawn->FindComponentByClass<UFPSCombatComponent>() : nullptr;
	if (DiagnosticsText) DiagnosticsText->SetText(FText::FromString(Combat ? Combat->GetNetworkDiagnosticsText() :
		TEXT("FPS NETWORK | waiting for local pawn / CombatComponent\nNo receipt from a previous life is displayed.")));
}
