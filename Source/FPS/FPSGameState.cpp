// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSGameState.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

AFPSGameState::AFPSGameState()
{
}

void AFPSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSGameState, TeamScore);
	DOREPLIFETIME(AFPSGameState, bFinished);
	DOREPLIFETIME(AFPSGameState, Winner);
	DOREPLIFETIME(AFPSGameState, KillTarget);
	DOREPLIFETIME(AFPSGameState, ReturnToLobbyTime);
	DOREPLIFETIME(AFPSGameState, WinnerName);
}

void AFPSGameState::Finish(APlayerState* InWinner)
{
	if (!HasAuthority() || bFinished) return;
	Winner = InWinner;
	WinnerName = InWinner ? InWinner->GetPlayerName() : TEXT("Unknown");
	bFinished = true;
	ForceNetUpdate();
}
