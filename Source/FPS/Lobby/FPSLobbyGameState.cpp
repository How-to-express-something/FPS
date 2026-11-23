#include "FPSLobbyGameState.h"
#include "../FPSPlayerState.h"
#include "Net/UnrealNetwork.h"

void AFPSLobbyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFPSLobbyGameState, Snapshot);
}

void AFPSLobbyGameState::SetSnapshot(FFPSLobbySnapshot NewSnapshot)
{
	if (!HasAuthority()) return;
	NewSnapshot.Revision = Snapshot.Revision + 1;
	Snapshot = NewSnapshot;
	ForceNetUpdate();
	OnRep_Snapshot(); // C++ writes on the listen host do not receive a network OnRep.
}

void AFPSLobbyGameState::OnRep_Snapshot() { OnLobbyStateChanged.Broadcast(); }

TArray<FFPSLobbyPlayerInfo> AFPSLobbyGameState::GetLobbyPlayers() const
{
	TArray<FFPSLobbyPlayerInfo> Out;
	for (const APlayerState* Base : PlayerArray)
	{
		const AFPSPlayerState* PS = Cast<AFPSPlayerState>(Base);
		if (!IsValid(PS) || PS->IsInactive() || PS->IsOnlyASpectator()) continue;
		FFPSLobbyPlayerInfo Row;
		Row.PlayerId = PS->GetPlayerId();
		Row.PlayerName = PS->GetPlayerName();
		Row.PingMs = PS->GetPingInMilliseconds();
		Row.bReady = PS->IsLobbyReady();
		Row.bHost = Row.PlayerId == Snapshot.HostPlayerId;
		Out.Add(Row);
	}
	Out.Sort([](const FFPSLobbyPlayerInfo& A, const FFPSLobbyPlayerInfo& B) { return A.PlayerId < B.PlayerId; });
	return Out;
}

float AFPSLobbyGameState::GetCountdownRemaining() const
{
	return Snapshot.Phase == EFPSLobbyPhase::Countdown
		? FMath::Max(0., Snapshot.CountdownEndServerTime - GetServerWorldTimeSeconds()) : 0.f;
}
