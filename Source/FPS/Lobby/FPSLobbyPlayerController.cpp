#include "FPSLobbyPlayerController.h"
#include "FPSLobbyGameMode.h"
#include "FPSLobbyGameState.h"
#include "../UI/LobbyWidget.h"
#include "../Online/MultiplayerSessionsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

AFPSLobbyPlayerController::AFPSLobbyPlayerController()
{
	LobbyWidgetClass = ULobbyWidget::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void AFPSLobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController() || !GetLocalPlayer()) return;
	if (LobbyWidgetClass)
	{
		LobbyWidget = CreateWidget<ULobbyWidget>(this, LobbyWidgetClass);
		if (LobbyWidget) LobbyWidget->AddToPlayerScreen();
	}
	bShowMouseCursor = true;
	FInputModeUIOnly Input;
	if (LobbyWidget) Input.SetWidgetToFocus(LobbyWidget->TakeWidget());
	SetInputMode(Input);
}

void AFPSLobbyPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsLocalController() && PlayerState && GetWorld()->GetGameState<AFPSLobbyGameState>())
		if (UMultiplayerSessionsSubsystem* SS = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>()) SS->NotifyLobbyReady();
}

void AFPSLobbyPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (LobbyWidget) { LobbyWidget->RemoveFromParent(); LobbyWidget = nullptr; }
	if (IsLocalController()) { SetInputMode(FInputModeGameOnly()); bShowMouseCursor = false; }
	Super::EndPlay(Reason);
}

void AFPSLobbyPlayerController::Server_SetReady_Implementation(bool bReady)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastReadyRequestTime < .1) return;
	LastReadyRequestTime = Now;
	if (AFPSLobbyGameMode* GM = GetWorld()->GetAuthGameMode<AFPSLobbyGameMode>()) GM->SetPlayerReady(this, bReady);
}

void AFPSLobbyPlayerController::Server_StartMatch_Implementation()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastStartRequestTime < .25) return;
	LastStartRequestTime = Now;
	if (AFPSLobbyGameMode* GM = GetWorld()->GetAuthGameMode<AFPSLobbyGameMode>()) GM->RequestStart(this);
}

void AFPSLobbyPlayerController::Client_LobbyError_Implementation(const FString& Error) { LobbyError = Error; }

void AFPSLobbyPlayerController::LeaveLobby()
{
	if (!IsLocalController() || !GetGameInstance()) return;
	if (AFPSLobbyGameMode* GM = GetWorld()->GetAuthGameMode<AFPSLobbyGameMode>()) GM->CloseRoom();
	if (UMultiplayerSessionsSubsystem* SS = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>()) SS->LeaveRoom();
}

void AFPSLobbyPlayerController::Client_RoomClosing_Implementation()
{
	LobbyError = TEXT("The host closed the room.");
	if (GetGameInstance())
		if (UMultiplayerSessionsSubsystem* SS = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>()) SS->LeaveRoom();
}
