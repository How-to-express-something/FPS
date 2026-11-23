#include "FPSLobbyGameMode.h"
#include "FPSLobbyGameState.h"
#include "FPSLobbyPlayerController.h"
#include "../FPSPlayerState.h"
#include "../Online/MultiplayerSessionsSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AFPSLobbyGameMode::AFPSLobbyGameMode()
{
	PlayerControllerClass = AFPSLobbyPlayerController::StaticClass();
	PlayerStateClass = AFPSPlayerState::StaticClass();
	GameStateClass = AFPSLobbyGameState::StaticClass();
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	bStartPlayersAsSpectators = true;
	bUseSeamlessTravel = true;
}

void AFPSLobbyGameMode::InitGame(const FString& MapName, const FString& Options, FString& Error)
{
	Super::InitGame(MapName, Options, Error);
	Capacity = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("MaxPlayers"), 4), 2, FPSSession::MaxCapacity);
	MinimumPlayers = FMath::Clamp(MinimumPlayers, 2, Capacity);
	if (GameSession) { GameSession->MaxPlayers = Capacity; GameSession->MaxSpectators = 0; }
}

void AFPSLobbyGameMode::InitGameState()
{
	Super::InitGameState();
	if (AFPSLobbyGameState* GS = GetGameState<AFPSLobbyGameState>())
	{
		FFPSLobbySnapshot S;
		S.Capacity = Capacity;
		S.MinimumPlayers = MinimumPlayers;
		GS->SetSnapshot(S);
	}
}

int32 AFPSLobbyGameMode::CountConnections() const
{
	int32 Count = 0;
	// Controllers include accepted logins before PostLogin: do not rely only on a stale roster.
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (IsValid(It->Get()) && !It->Get()->IsActorBeingDestroyed() && !It->Get()->PendingSwapConnection) ++Count;
	return Count;
}

FString AFPSLobbyGameMode::AdmissionError(const FString& Options) const
{
	if (UGameplayStatics::GetIntOption(Options, TEXT("SpectatorOnly"), 0) != 0) return TEXT("SpectatorsNotSupported");
	const AFPSLobbyGameState* GS = GetGameState<AFPSLobbyGameState>();
	const EFPSLobbyPhase Phase = GS ? GS->GetLobbySnapshot().Phase : EFPSLobbyPhase::Waiting;
	if (Phase == EFPSLobbyPhase::Traveling || Phase == EFPSLobbyPhase::Closing) return TEXT("RoomNotJoinable");
	return CountConnections() >= Capacity ? TEXT("RoomFull") : FString();
}

void AFPSLobbyGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id, FString& Error)
{
	Super::PreLogin(Options, Address, Id, Error);
	if (Error.IsEmpty()) Error = AdmissionError(Options);
	UE_LOG(LogFPSSession, Log, TEXT("[FPSLobby] PreLogin members=%d capacity=%d outcome='%s'"), CountConnections(), Capacity, *Error);
}

APlayerController* AFPSLobbyGameMode::Login(UPlayer* Player, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& Id, FString& Error)
{
	// Final, synchronous game-thread gate. Two clients can both have passed PreLogin earlier.
	Error = AdmissionError(Options);
	if (!Error.IsEmpty()) return nullptr;
	return Super::Login(Player, InRemoteRole, Portal, Options, Id, Error);
}

void AFPSLobbyGameMode::PostLogin(APlayerController* PC)
{
	Super::PostLogin(PC);
	InitializeLobbyPlayer(PC);
}

void AFPSLobbyGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	Super::HandleSeamlessTravelPlayer(C);
	if (APlayerController* PC = Cast<APlayerController>(C)) InitializeLobbyPlayer(PC);
}

void AFPSLobbyGameMode::InitializeLobbyPlayer(APlayerController* PC)
{
	if (!PC) return;
	if (AFPSPlayerState* PS = PC->GetPlayerState<AFPSPlayerState>()) PS->SetLobbyReady(false);
	if (PC->IsLocalController() && !Host.IsValid())
	{
		Host = PC;
		if (AFPSLobbyGameState* GS = GetGameState<AFPSLobbyGameState>())
		{
			FFPSLobbySnapshot S = GS->GetLobbySnapshot();
			S.HostPlayerId = PC->PlayerState ? PC->PlayerState->GetPlayerId() : INDEX_NONE;
			GS->SetSnapshot(S);
		}
	}
	CancelCountdown();
	UE_LOG(LogFPSSession, Log, TEXT("[FPSLobby] PostLogin player=%s members=%d capacity=%d host=%d"), *GetNameSafe(PC), CountConnections(), Capacity, IsHost(PC));
}

void AFPSLobbyGameMode::Logout(AController* Exiting)
{
	CancelCountdown();
	Super::Logout(Exiting);
}

bool AFPSLobbyGameMode::IsHost(const APlayerController* PC) const { return PC && Host.Get() == PC; }

bool AFPSLobbyGameMode::AllPlayersReady() const
{
	const AFPSLobbyGameState* GS = GetGameState<AFPSLobbyGameState>();
	if (!GS) return false;
	const TArray<FFPSLobbyPlayerInfo> Players = GS->GetLobbyPlayers();
	int32 Ready = 0;
	for (const FFPSLobbyPlayerInfo& P : Players) Ready += P.bReady ? 1 : 0;
	return Players.Num() == CountConnections() && FPSLobby::CanStart(GS->GetLobbySnapshot().Phase, Players.Num(), Ready, MinimumPlayers, Capacity);
}

void AFPSLobbyGameMode::SetPlayerReady(AFPSLobbyPlayerController* PC, bool bReady)
{
	AFPSLobbyGameState* GS = GetGameState<AFPSLobbyGameState>();
	AFPSPlayerState* PS = IsValid(PC) ? PC->GetPlayerState<AFPSPlayerState>() : nullptr;
	if (!GS || !PS || PC->GetWorld() != GetWorld()) return;
	if (GS->GetLobbySnapshot().Phase != EFPSLobbyPhase::Waiting && GS->GetLobbySnapshot().Phase != EFPSLobbyPhase::Countdown) return;
	if (PS->IsLobbyReady() == bReady) return;
	PS->SetLobbyReady(bReady);
	if (!bReady) CancelCountdown();
	UE_LOG(LogFPSSession, Log, TEXT("[FPSLobby] Ready player=%d ready=%d"), PS->GetPlayerId(), bReady);
}

void AFPSLobbyGameMode::RequestStart(AFPSLobbyPlayerController* PC)
{
	AFPSLobbyGameState* GS = GetGameState<AFPSLobbyGameState>();
	if (!GS || !IsHost(PC)) { if (PC) PC->Client_LobbyError(TEXT("Only the host can start.")); return; }
	if (GS->GetLobbySnapshot().Phase != EFPSLobbyPhase::Waiting) return;
	const FString TravelError = GetTravelConfigurationError();
	if (!TravelError.IsEmpty()) { PC->Client_LobbyError(TravelError); return; }
	if (!AllPlayersReady()) { PC->Client_LobbyError(TEXT("At least two players must join, and everyone must be ready.")); return; }
	if (!FPackageName::DoesPackageExist(MatchMap)) { PC->Client_LobbyError(TEXT("Match map is missing.")); return; }
	FFPSLobbySnapshot S = GS->GetLobbySnapshot();
	S.Phase = EFPSLobbyPhase::Countdown;
	S.CountdownEndServerTime = GS->GetServerWorldTimeSeconds() + FMath::Clamp(CountdownSeconds, .1f, 30.f);
	GS->SetSnapshot(S);
	const uint32 Generation = ++CountdownGeneration;
	GetWorldTimerManager().SetTimer(CountdownTimer, FTimerDelegate::CreateWeakLambda(this, [this, Generation]() { CompleteCountdown(Generation); }), FMath::Clamp(CountdownSeconds, .1f, 30.f), false);
	UE_LOG(LogFPSSession, Log, TEXT("[FPSLobby] Countdown generation=%u deadline=%.3f"), Generation, S.CountdownEndServerTime);
}

void AFPSLobbyGameMode::CancelCountdown()
{
	++CountdownGeneration;
	GetWorldTimerManager().ClearTimer(CountdownTimer);
	if (AFPSLobbyGameState* GS = GetGameState<AFPSLobbyGameState>())
	{
		FFPSLobbySnapshot S = GS->GetLobbySnapshot();
		if (S.Phase == EFPSLobbyPhase::Countdown)
		{
			S.Phase = EFPSLobbyPhase::Waiting;
			S.CountdownEndServerTime = 0.;
			GS->SetSnapshot(S);
			UE_LOG(LogFPSSession, Log, TEXT("[FPSLobby] CountdownCancelled generation=%u"), CountdownGeneration);
		}
	}
}

void AFPSLobbyGameMode::CompleteCountdown(uint32 Generation)
{
	AFPSLobbyGameState* GS = GetGameState<AFPSLobbyGameState>();
	if (Generation != CountdownGeneration || !GS || GS->GetLobbySnapshot().Phase != EFPSLobbyPhase::Countdown) return;
	if (!AllPlayersReady()) { CancelCountdown(); return; }
	const FString TravelError = GetTravelConfigurationError();
	if (!TravelError.IsEmpty())
	{
		CancelCountdown();
		if (AFPSLobbyPlayerController* PC = Cast<AFPSLobbyPlayerController>(Host.Get())) PC->Client_LobbyError(TravelError);
		return;
	}
	FFPSLobbySnapshot S = GS->GetLobbySnapshot();
	S.Phase = EFPSLobbyPhase::Traveling;
	S.CountdownEndServerTime = 0.;
	GS->SetSnapshot(S);
	// Reset the lobby's game= URL override so the arena uses its own gameplay GameMode.
	const FString URL = FString::Printf(TEXT("%s?SessionMatch=1?MaxPlayers=%d"), *MatchMap, Capacity);
	UE_LOG(LogFPSSession, Log, TEXT("[FPSTravel] ServerTravel generation=%u map=%s members=%d"), Generation, *URL, CountConnections());
	if (!GetWorld()->ServerTravel(URL, true))
	{
		S.Phase = EFPSLobbyPhase::Waiting;
		GS->SetSnapshot(S);
		if (AFPSLobbyPlayerController* PC = Cast<AFPSLobbyPlayerController>(Host.Get())) PC->Client_LobbyError(TEXT("ServerTravel could not start."));
	}
}

FString AFPSLobbyGameMode::GetTravelConfigurationError() const
{
	if (!bUseSeamlessTravel || GetWorld()->TimeSeconds >= 172800.f)
		return TEXT("This session flow requires seamless travel; enable it or restart this server instance.");
	if (GetWorld()->WorldType == EWorldType::PIE && !FParse::Param(FCommandLine::Get(), TEXT("MultiprocessOSS")))
	{
		const IConsoleVariable* Allow = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
		if (!Allow || Allow->GetInt() == 0)
			return TEXT("PIE seamless travel is disabled. Set net.AllowPIESeamlessTravel 1, then start again.");
	}
	return FString();
}

void AFPSLobbyGameMode::CloseRoom()
{
	CancelCountdown();
	if (AFPSLobbyGameState* GS = GetGameState<AFPSLobbyGameState>())
	{
		FFPSLobbySnapshot S = GS->GetLobbySnapshot(); S.Phase = EFPSLobbyPhase::Closing; GS->SetSnapshot(S);
	}
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (AFPSLobbyPlayerController* PC = Cast<AFPSLobbyPlayerController>(It->Get()))
			if (!PC->IsLocalController()) PC->Client_RoomClosing();
}

void AFPSLobbyGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	++CountdownGeneration;
	GetWorldTimerManager().ClearTimer(CountdownTimer);
	Super::EndPlay(Reason);
}
