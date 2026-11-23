// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSPlayerState.h"
#include "Net/UnrealNetwork.h"

AFPSPlayerState::AFPSPlayerState()
{
	SetShouldUpdateReplicatedPing(true);
}

void AFPSPlayerState::SetLobbyReady(bool bReady)
{
	if (HasAuthority() && bLobbyReady != bReady)
	{
		bLobbyReady = bReady;
		ForceNetUpdate();
	}
}

void AFPSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSPlayerState, Kills);
	DOREPLIFETIME(AFPSPlayerState, Deaths);
	DOREPLIFETIME(AFPSPlayerState, bLobbyReady);
}
