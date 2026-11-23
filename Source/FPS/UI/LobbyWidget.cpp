#include "LobbyWidget.h"
#include "SessionWidgetHelpers.h"
#include "../Lobby/FPSLobbyGameState.h"
#include "../Lobby/FPSLobbyPlayerController.h"
#include "../FPSPlayerState.h"
#include "Engine/World.h"
#include "TimerManager.h"

void ULobbyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Box = FPSSessionUI::BuildPanel(WidgetTree);
		FPSSessionUI::AddText(WidgetTree, Box, TEXT("FPS - Lobby"));
		PlayersText = FPSSessionUI::AddText(WidgetTree, Box, TEXT("Waiting for players..."));
		StatusText = FPSSessionUI::AddText(WidgetTree, Box, TEXT("Synchronizing..."));
		ReadyButton = FPSSessionUI::AddButton(WidgetTree, Box, TEXT("ReadyButton"), TEXT("Ready"));
		UnreadyButton = FPSSessionUI::AddButton(WidgetTree, Box, TEXT("UnreadyButton"), TEXT("Not ready"));
		StartButton = FPSSessionUI::AddButton(WidgetTree, Box, TEXT("StartButton"), TEXT("Start match (host)"));
		LeaveButton = FPSSessionUI::AddButton(WidgetTree, Box, TEXT("LeaveButton"), TEXT("Leave room"));
	}
	if (ReadyButton) ReadyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ReadyClicked);
	if (UnreadyButton) UnreadyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::UnreadyClicked);
	if (StartButton) StartButton->OnClicked.AddUniqueDynamic(this, &ThisClass::StartClicked);
	if (LeaveButton) LeaveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::LeaveClicked);
}

void ULobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Refresh();
	// Low frequency local presentation refresh also reads engine-owned replicated ping.
	if (GetWorld()) GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this, &ThisClass::Refresh, .25f, true);
}

void ULobbyWidget::NativeDestruct()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
	Super::NativeDestruct();
}

void ULobbyWidget::ReadyClicked() { if (AFPSLobbyPlayerController* PC = GetOwningPlayer<AFPSLobbyPlayerController>()) PC->Server_SetReady(true); }
void ULobbyWidget::UnreadyClicked() { if (AFPSLobbyPlayerController* PC = GetOwningPlayer<AFPSLobbyPlayerController>()) PC->Server_SetReady(false); }
void ULobbyWidget::StartClicked() { if (AFPSLobbyPlayerController* PC = GetOwningPlayer<AFPSLobbyPlayerController>()) PC->Server_StartMatch(); }
void ULobbyWidget::LeaveClicked() { if (AFPSLobbyPlayerController* PC = GetOwningPlayer<AFPSLobbyPlayerController>()) PC->LeaveLobby(); }

void ULobbyWidget::Refresh()
{
	const AFPSLobbyGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSLobbyGameState>() : nullptr;
	const AFPSLobbyPlayerController* PC = GetOwningPlayer<AFPSLobbyPlayerController>();
	const AFPSPlayerState* PS = PC ? PC->GetPlayerState<AFPSPlayerState>() : nullptr;
	const bool bUsable = GS && PS;
	const FFPSLobbySnapshot S = GS ? GS->GetLobbySnapshot() : FFPSLobbySnapshot();
	const bool bEditable = bUsable && (S.Phase == EFPSLobbyPhase::Waiting || S.Phase == EFPSLobbyPhase::Countdown);
	if (ReadyButton) ReadyButton->SetIsEnabled(bEditable && !PS->IsLobbyReady());
	if (UnreadyButton) UnreadyButton->SetIsEnabled(bEditable && PS->IsLobbyReady());
	if (StartButton) StartButton->SetIsEnabled(bUsable && S.Phase == EFPSLobbyPhase::Waiting && PS->GetPlayerId() == S.HostPlayerId);
	if (LeaveButton) LeaveButton->SetIsEnabled(bUsable && S.Phase != EFPSLobbyPhase::Traveling);
	if (!bUsable) return;
	const TArray<FFPSLobbyPlayerInfo> Players = GS->GetLobbyPlayers();
	FString Text;
	for (const FFPSLobbyPlayerInfo& P : Players)
		Text += FString::Printf(TEXT("%s%s  |  %.0f ms  |  %s\n"), *P.PlayerName, P.bHost ? TEXT(" [Host]") : TEXT(""), P.PingMs, P.bReady ? TEXT("Ready") : TEXT("Not ready"));
	if (PlayersText) PlayersText->SetText(FText::FromString(Text));
	if (StatusText) StatusText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d players | Minimum %d | %s | %.1fs\n%s"),
		Players.Num(), S.Capacity, S.MinimumPlayers, *UEnum::GetValueAsString(S.Phase), GS->GetCountdownRemaining(), *PC->GetLobbyError())));
}
