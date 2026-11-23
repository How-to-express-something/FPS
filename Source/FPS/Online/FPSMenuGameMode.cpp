#include "FPSMenuGameMode.h"
#include "../UI/MainMenuWidget.h"

AFPSMenuGameMode::AFPSMenuGameMode()
{
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	PlayerControllerClass = AFPSMenuPlayerController::StaticClass();
	bStartPlayersAsSpectators = true;
}

AFPSMenuPlayerController::AFPSMenuPlayerController() { MainMenuWidgetClass = UMainMenuWidget::StaticClass(); }

void AFPSMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController() || !GetLocalPlayer()) return;
	MainMenuWidget = CreateWidget<UMainMenuWidget>(this, MainMenuWidgetClass);
	if (MainMenuWidget) MainMenuWidget->AddToPlayerScreen();
	bShowMouseCursor = true;
	FInputModeUIOnly Input;
	if (MainMenuWidget) Input.SetWidgetToFocus(MainMenuWidget->TakeWidget());
	SetInputMode(Input);
}

void AFPSMenuPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (MainMenuWidget) { MainMenuWidget->RemoveFromParent(); MainMenuWidget = nullptr; }
	Super::EndPlay(Reason);
}
