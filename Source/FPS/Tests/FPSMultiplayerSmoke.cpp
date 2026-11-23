// Opt-in REAL network smoke driver for separate -game processes; never active in Shipping.
#include "../Online/MultiplayerSessionsSubsystem.h"

#if !UE_BUILD_SHIPPING
#include "../Lobby/FPSLobbyGameState.h"
#include "../Lobby/FPSLobbyPlayerController.h"
#include "../FPSCharacter.h"
#include "../FPSPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace FPSSessionSmoke
{
struct FRun
{
	double Start = FPlatformTime::Seconds();
	double NextAction = 0.;
	double MatchTime = 0.;
	bool bCreated = false;
	bool bReady = false;
	bool bLeaving = false;
	bool bObservedMatch = false;
	bool bFinished = false;
	int32 SearchAttempts = 0;
};

void Tick(UMultiplayerSessionsSubsystem* Sessions)
{
	FString Role;
	if (!FParse::Value(FCommandLine::Get(), TEXT("FPSSessionSmoke="), Role)) return;
	UWorld* World = Sessions->GetWorld();
	if (!World || World->WorldType != EWorldType::Game || !World->HasBegunPlay()) return;
	static TMap<TWeakObjectPtr<UMultiplayerSessionsSubsystem>, FRun> Runs;
	FRun& Run = Runs.FindOrAdd(Sessions);
	if (Run.bFinished) return;
	FString Room = TEXT("FPS_SMOKE");
	FParse::Value(FCommandLine::Get(), TEXT("FPSSmokeRoom="), Room);
	const bool bHost = Role.Equals(TEXT("Host"), ESearchCase::IgnoreCase);
	const double Now = FPlatformTime::Seconds();
	if (Now - Run.Start > 100.)
	{
		UE_LOG(LogFPSSession, Error, TEXT("[FPSSmoke] FAIL role=%s flow=%d error='%s'"), *Role, int32(Sessions->GetFlow()), *Sessions->GetLastError());
		Run.bFinished = true;
		FPlatformMisc::RequestExitWithStatus(false, 1);
		return;
	}
	if (Now < Run.NextAction) return;
	Run.NextAction = Now + .5;
	APlayerController* PC = Sessions->GetGameInstance()->GetFirstLocalPlayerController();
	if (!PC) return;
	if (Run.bLeaving)
	{
		if (!Sessions->IsBusy() && (Sessions->GetFlow() == EFPSSessionFlow::Idle || Sessions->GetFlow() == EFPSSessionFlow::Error)
			&& UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == Sessions->MenuMap)
		{
			UE_LOG(LogFPSSession, Log, TEXT("[FPSSmoke] PASS role=%s match=%d returnedMenu=1"), *Role, Run.bObservedMatch);
			Run.bFinished = true;
			FPlatformMisc::RequestExitWithStatus(false, 0);
		}
		return;
	}
	if (AFPSLobbyGameState* GS = World->GetGameState<AFPSLobbyGameState>())
	{
		AFPSLobbyPlayerController* LobbyPC = Cast<AFPSLobbyPlayerController>(PC);
		if (!LobbyPC || !PC->PlayerState) return;
		if (!Run.bReady)
		{
			UE_LOG(LogFPSSession, Log, TEXT("[FPSSmoke] Lobby role=%s player=%d roster=%d"), *Role, PC->PlayerState->GetPlayerId(), GS->GetLobbyPlayers().Num());
			LobbyPC->Server_SetReady(true);
			Run.bReady = true;
		}
		const TArray<FFPSLobbyPlayerInfo> Players = GS->GetLobbyPlayers();
		bool bAllReady = Players.Num() == 2;
		for (const FFPSLobbyPlayerInfo& P : Players) bAllReady &= P.bReady;
		if (bHost && bAllReady && GS->GetLobbySnapshot().Phase == EFPSLobbyPhase::Waiting) LobbyPC->Server_StartMatch();
		return;
	}
	if (Sessions->GetFlow() == EFPSSessionFlow::InMatch && Cast<AFPSCharacter>(PC->GetPawn()))
	{
		if (!Run.bObservedMatch)
		{
			UE_LOG(LogFPSSession, Log, TEXT("[FPSSmoke] Match role=%s map=%s pawn=%s controller=%s"), *Role, *World->GetMapName(), *GetNameSafe(PC->GetPawn()), *PC->GetClass()->GetName());
			Run.bObservedMatch = true; Run.MatchTime = Now;
		}
		// Client exits first; the host remains long enough to observe that disconnect.
		if (Now - Run.MatchTime > (bHost ? 8. : 2.))
		{
			Run.bLeaving = true;
			Sessions->LeaveRoom();
		}
		return;
	}
	if (!Sessions->IsBusy() && UMultiplayerSessionsSubsystem::IsMenuOperationAllowed(Sessions->GetFlow()))
	{
		if (bHost && !Run.bCreated)
		{
			Run.bCreated = true;
			Sessions->CreateRoom(Room, 2);
		}
		else if (!bHost)
		{
			for (const FFPSRoomSearchResult& R : Sessions->GetRooms())
			{
				if (R.RoomName == Room) { Sessions->JoinRoom(R.Generation, R.ResultIndex); return; }
			}
			if (++Run.SearchAttempts <= 12) { Sessions->FindRooms(); Run.NextAction = Now + 2.; }
		}
	}
}
}
#endif
