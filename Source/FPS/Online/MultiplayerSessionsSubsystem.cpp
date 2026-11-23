#include "MultiplayerSessionsSubsystem.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "../Lobby/FPSLobbyGameState.h"

DEFINE_LOG_CATEGORY(LogFPSSession);

#if !UE_BUILD_SHIPPING
namespace FPSSessionSmoke { void Tick(UMultiplayerSessionsSubsystem* Sessions); }
#endif

void UMultiplayerSessionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::OnTravelFailure);
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), .1f);
}

void UMultiplayerSessionsSubsystem::Deinitialize()
{
	bDeinitializing = true;
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	ClearDelegates();
	Search.Reset();
	Sessions.Reset();
	Super::Deinitialize();
}

FString UMultiplayerSessionsSubsystem::NormalizeRoomName(const FString& Name)
{
	FString Out = Name.TrimStartAndEnd().Left(40);
	Out.ReplaceInline(TEXT("\r"), TEXT(" "));
	Out.ReplaceInline(TEXT("\n"), TEXT(" "));
	Out.ReplaceInline(TEXT("\t"), TEXT(" "));
	return Out.IsEmpty() ? TEXT("FPS Room") : Out;
}

bool UMultiplayerSessionsSubsystem::IsMenuOperationAllowed(EFPSSessionFlow State)
{
	return State == EFPSSessionFlow::Idle || State == EFPSSessionFlow::Error;
}

bool UMultiplayerSessionsSubsystem::IsBusy() const
{
	return BackendOp != EBackendOp::None || Flow == EFPSSessionFlow::Connecting || Flow == EFPSSessionFlow::Recovering;
}

bool UMultiplayerSessionsSubsystem::EnsureInterface()
{
	if (Sessions.IsValid()) return true;
	IOnlineSubsystem* OSS = GetWorld() ? Online::GetSubsystem(GetWorld()) : nullptr;
	if (OSS) Sessions = OSS->GetSessionInterface();
	if (!Sessions.IsValid())
	{
		LastError = TEXT("Online session interface unavailable. Enable OnlineSubsystemNull.");
		SetFlow(EFPSSessionFlow::Error);
		return false;
	}
	return true;
}

int32 UMultiplayerSessionsSubsystem::GetLocalUserNum() const
{
	const ULocalPlayer* LP = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	return LP ? LP->GetControllerId() : INDEX_NONE;
}

bool UMultiplayerSessionsSubsystem::PrepareMenuOperation()
{
	if (bDeinitializing || IsBusy() || !IsMenuOperationAllowed(Flow)) return false;
	if (GetLocalUserNum() < 0)
	{
		LastError = TEXT("A local player is required. This workflow hosts listen servers, not dedicated registration.");
		SetFlow(EFPSSessionFlow::Error);
		return false;
	}
	if (!EnsureInterface()) return false;
	LastError.Reset();
	return true;
}

void UMultiplayerSessionsSubsystem::SetFlow(EFPSSessionFlow NewFlow)
{
	UE_LOG(LogFPSSession, Log, TEXT("[FPSSession] op=%llu world=%s state=%d->%d backend=%d error='%s'"),
		OperationId, *GetNameSafe(GetWorld()), int32(Flow), int32(NewFlow), int32(BackendOp), *LastError);
	Flow = NewFlow;
	if (!bDeinitializing) OnStateChanged.Broadcast();
}

void UMultiplayerSessionsSubsystem::BeginBackend(EBackendOp Op, EFPSSessionFlow State)
{
	check(BackendOp == EBackendOp::None);
	BackendOp = Op;
	bAbandoned = false;
	++OperationId;
	Deadline = FPlatformTime::Seconds() + FMath::Clamp(OperationTimeoutSeconds, 5.f, 120.f);
	SetFlow(State);
}

void UMultiplayerSessionsSubsystem::ClearDelegates()
{
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}
	CreateHandle.Reset(); FindHandle.Reset(); JoinHandle.Reset(); DestroyHandle.Reset();
}

void UMultiplayerSessionsSubsystem::ClearBackend()
{
	ClearDelegates();
	BackendOp = EBackendOp::None;
	Deadline = 0.;
	bAbandoned = false;
}

bool UMultiplayerSessionsSubsystem::CreateRoom(const FString& RoomName, int32 Capacity)
{
	if (!PrepareMenuOperation()) return false;
	if (Capacity < 2 || Capacity > FPSSession::MaxCapacity)
	{
		LastError = TEXT("Room capacity must be between 2 and 16 (including the host).");
		SetFlow(EFPSSessionFlow::Error);
		return false;
	}
	if (!FPackageName::DoesPackageExist(LobbyMap))
	{
		LastError = TEXT("Lobby map is missing. Run Tools/Create-SessionMaps.py in the editor.");
		SetFlow(EFPSSessionFlow::Error);
		return false;
	}
	PendingRoomName = NormalizeRoomName(RoomName);
	PendingCapacity = Capacity;
	if (Sessions->GetNamedSession(NAME_GameSession)) BeginSessionDestroy(EAfterDestroy::Recreate);
	else IssueCreate();
	return true;
}

void UMultiplayerSessionsSubsystem::IssueCreate()
{
	BeginBackend(EBackendOp::Create, EFPSSessionFlow::Creating);
	const FOnlineSessionSettings Settings = MakeLANSettings(PendingRoomName, PendingCapacity);
	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreateComplete));
	if (!Sessions->CreateSession(GetLocalUserNum(), NAME_GameSession, Settings) && BackendOp == EBackendOp::Create)
		OnCreateComplete(NAME_GameSession, false);
}

FOnlineSessionSettings UMultiplayerSessionsSubsystem::MakeLANSettings(const FString& RoomName, int32 Capacity)
{
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true; // Final admission belongs to the authoritative GameMode.
	Settings.bUsesPresence = false;
	Settings.bUseLobbiesIfAvailable = false;
	Settings.NumPublicConnections = Capacity;
	// Keep the engine build ID. Null filters replies against GetBuildUniqueId().
	// Project protocol version is advertised separately, never put into BuildUniqueId.
	Settings.Set(FPSSession::RoomNameKey, NormalizeRoomName(RoomName), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(FPSSession::ProtocolKey, FPSSession::ProtocolVersion, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(FPSSession::ModeKey, FPSSession::Mode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	return Settings;
}

void UMultiplayerSessionsSubsystem::OnCreateComplete(FName Name, bool bSuccess)
{
	if (Name != NAME_GameSession || BackendOp != EBackendOp::Create || bDeinitializing) return;
	const bool bWasAbandoned = bAbandoned;
	ClearBackend();
	if (bWasAbandoned) { BeginSessionDestroy(EAfterDestroy::Recover); return; }
	if (!bSuccess) { Recover(TEXT("CreateSession failed.")); return; }
	Deadline = FPlatformTime::Seconds() + FMath::Clamp(OperationTimeoutSeconds, 5.f, 120.f);
	SetFlow(EFPSSessionFlow::Connecting);
	const FString Options = FString::Printf(TEXT("listen?MaxPlayers=%d"), PendingCapacity);
	UGameplayStatics::OpenLevel(this, FName(*LobbyMap), true, Options);
}

bool UMultiplayerSessionsSubsystem::FindRooms()
{
	if (!PrepareMenuOperation()) return false;
	if (Sessions->GetNamedSession(NAME_GameSession)) { Recover(TEXT("Leave the previous session before searching.")); return false; }
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 100;
	++SearchGeneration;
	Rooms.Reset();
	BeginBackend(EBackendOp::Find, EFPSSessionFlow::Searching);
	OnRoomsChanged.Broadcast();
	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindComplete));
	if (!Sessions->FindSessions(GetLocalUserNum(), Search.ToSharedRef()) && BackendOp == EBackendOp::Find) OnFindComplete(false);
	return true;
}

void UMultiplayerSessionsSubsystem::OnFindComplete(bool bSuccess)
{
	if (BackendOp != EBackendOp::Find || bDeinitializing) return;
	const bool bWasAbandoned = bAbandoned;
	ClearBackend();
	if (bWasAbandoned) { Search.Reset(); SetFlow(EFPSSessionFlow::Idle); return; }
	if (!bSuccess || !Search.IsValid()) { Recover(TEXT("FindSessions failed.")); return; }
	for (int32 Index = 0; Index < Search->SearchResults.Num(); ++Index)
	{
		const FOnlineSessionSearchResult& Result = Search->SearchResults[Index];
		if (!Result.IsValid() || Result.Session.SessionSettings.NumPublicConnections < 2 || Result.Session.SessionSettings.NumPublicConnections > FPSSession::MaxCapacity) continue;
		FString Mode;
		Result.Session.SessionSettings.Get(FPSSession::ModeKey, Mode);
		if (Mode != FPSSession::Mode) continue;
		FFPSRoomSearchResult Row;
		Row.Generation = SearchGeneration;
		Row.ResultIndex = Index;
		Result.Session.SessionSettings.Get(FPSSession::RoomNameKey, Row.RoomName);
		Row.RoomName = NormalizeRoomName(Row.RoomName);
		Row.Capacity = Result.Session.SessionSettings.NumPublicConnections;
		Row.PlayerCount = FMath::Clamp(Row.Capacity - Result.Session.NumOpenPublicConnections, 0, Row.Capacity);
		Row.PingMs = Result.PingInMs;
		int32 Protocol = 0;
		Result.Session.SessionSettings.Get(FPSSession::ProtocolKey, Protocol);
		Row.bCompatible = Protocol == FPSSession::ProtocolVersion;
		Rooms.Add(Row);
	}
	// Commit state BEFORE broadcasting; a UI may request the next operation in its callback.
	SetFlow(EFPSSessionFlow::Idle);
	OnRoomsChanged.Broadcast();
}

bool UMultiplayerSessionsSubsystem::JoinRoom(int64 Generation, int32 ResultIndex)
{
	if (!PrepareMenuOperation()) return false;
	const FFPSRoomSearchResult* Row = Rooms.FindByPredicate([=](const FFPSRoomSearchResult& R) { return R.Generation == Generation && R.ResultIndex == ResultIndex; });
	if (Generation != SearchGeneration || !Search.IsValid() || !Search->SearchResults.IsValidIndex(ResultIndex) || !Row || !Row->bCompatible)
	{
		LastError = TEXT("The selected result is stale or incompatible. Search again.");
		SetFlow(EFPSSessionFlow::Error);
		return false;
	}
	if (Sessions->GetNamedSession(NAME_GameSession)) { Recover(TEXT("A local session already exists. Leave before joining.")); return false; }
	BeginBackend(EBackendOp::Join, EFPSSessionFlow::Joining);
	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinComplete));
	if (!Sessions->JoinSession(GetLocalUserNum(), NAME_GameSession, Search->SearchResults[ResultIndex]) && BackendOp == EBackendOp::Join)
		OnJoinComplete(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
	return true;
}

void UMultiplayerSessionsSubsystem::OnJoinComplete(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
	if (Name != NAME_GameSession || BackendOp != EBackendOp::Join || bDeinitializing) return;
	const bool bWasAbandoned = bAbandoned;
	ClearBackend();
	if (bWasAbandoned) { BeginSessionDestroy(EAfterDestroy::Recover); return; }
	if (Result != EOnJoinSessionCompleteResult::Success) { Recover(FString::Printf(TEXT("JoinSession: %s"), LexToString(Result))); return; }
	FString Address;
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
	if (!PC || !Sessions->GetResolvedConnectString(NAME_GameSession, Address) || Address.IsEmpty())
	{ Recover(TEXT("Joined the session record, but could not resolve a connection address.")); return; }
	Deadline = FPlatformTime::Seconds() + FMath::Clamp(OperationTimeoutSeconds, 5.f, 120.f);
	SetFlow(EFPSSessionFlow::Connecting);
	PC->ClientTravel(Address, TRAVEL_Absolute);
}

void UMultiplayerSessionsSubsystem::BeginSessionDestroy(EAfterDestroy Action)
{
	check(BackendOp == EBackendOp::None);
	AfterDestroy = Action;
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession)) { FinishSessionDestroy(true); return; }
	BeginBackend(EBackendOp::Destroy, EFPSSessionFlow::Destroying);
	DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::OnDestroyComplete));
	if (!Sessions->DestroySession(NAME_GameSession) && BackendOp == EBackendOp::Destroy) OnDestroyComplete(NAME_GameSession, false);
}

void UMultiplayerSessionsSubsystem::OnDestroyComplete(FName Name, bool bSuccess)
{
	if (Name != NAME_GameSession || BackendOp != EBackendOp::Destroy || bDeinitializing) return;
	const bool bWasAbandoned = bAbandoned;
	ClearBackend();
	if (bWasAbandoned) AfterDestroy = EAfterDestroy::Recover;
	FinishSessionDestroy(bSuccess);
}

void UMultiplayerSessionsSubsystem::FinishSessionDestroy(bool bSuccess)
{
	if (!bSuccess && Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		if (LastError.IsEmpty()) LastError = TEXT("Session cleanup failed; retry Leave or restart this game instance.");
		SetFlow(EFPSSessionFlow::Error);
		ReturnToMenu();
		return;
	}
	if (AfterDestroy == EAfterDestroy::Recreate) { IssueCreate(); return; }
	SetFlow(LastError.IsEmpty() ? EFPSSessionFlow::Idle : EFPSSessionFlow::Error);
	ReturnToMenu();
}

void UMultiplayerSessionsSubsystem::LeaveRoom()
{
	if (bDeinitializing) return;
	if (BackendOp != EBackendOp::None) { CancelPendingOperation(); return; }
	LastError.Reset();
	BeginSessionDestroy(EAfterDestroy::Menu);
}

void UMultiplayerSessionsSubsystem::CancelPendingOperation()
{
	if (BackendOp == EBackendOp::Destroy) return;
	if (BackendOp != EBackendOp::None)
	{
		// Do NOT clear and immediately rebind: OSS completions have no per-request ID.
		bAbandoned = true;
		Deadline = 0.;
		LastError = TEXT("Cancelled locally; waiting for the online task to settle. Restart the instance if it never completes.");
		SetFlow(EFPSSessionFlow::Recovering);
		return;
	}
	if (Flow == EFPSSessionFlow::Connecting) Recover(TEXT("Connection cancelled."));
}

void UMultiplayerSessionsSubsystem::Recover(const FString& Error)
{
	if (bDeinitializing || Flow == EFPSSessionFlow::Destroying || Flow == EFPSSessionFlow::Recovering) return;
	LastError = Error;
	if (BackendOp != EBackendOp::None)
	{
		bAbandoned = true;
		Deadline = 0.;
		SetFlow(EFPSSessionFlow::Recovering);
		return;
	}
	BeginSessionDestroy(EAfterDestroy::Recover);
}

void UMultiplayerSessionsSubsystem::ReturnToMenu()
{
	if (!GetWorld() || !FPackageName::DoesPackageExist(MenuMap)) return;
	// Always browse, even if the current map IS the menu: a pending ClientTravel
	// can still be in flight from that world. A map-name shortcut would not cancel it.
	UGameplayStatics::OpenLevel(this, FName(*MenuMap), true);
}

bool UMultiplayerSessionsSubsystem::OwnsWorld(const UWorld* World) const
{
	return World && World->GetGameInstance() == GetGameInstance();
}

void UMultiplayerSessionsSubsystem::OnNetworkFailure(UWorld* World, UNetDriver*, ENetworkFailure::Type Type, const FString& Error)
{
	if (OwnsWorld(World)) Recover(FString::Printf(TEXT("Network failure %d: %s"), int32(Type), *Error));
}

void UMultiplayerSessionsSubsystem::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	if (OwnsWorld(World)) Recover(FString::Printf(TEXT("Travel failure %d: %s"), int32(Type), *Error));
}

void UMultiplayerSessionsSubsystem::NotifyLobbyReady()
{
	if (Flow == EFPSSessionFlow::Connecting || Flow == EFPSSessionFlow::InMatch)
	{
		Deadline = 0.;
		SetFlow(EFPSSessionFlow::InLobby);
	}
}

bool UMultiplayerSessionsSubsystem::Tick(float)
{
	if (bDeinitializing) return false;
	if (Deadline > 0. && FPlatformTime::Seconds() >= Deadline)
	{
		if (BackendOp != EBackendOp::None)
		{
			bAbandoned = true;
			Deadline = 0.;
			LastError = TEXT("Online task timed out; draining its completion before allowing another operation.");
			SetFlow(EFPSSessionFlow::Recovering);
		}
		else Recover(TEXT("Connection/map initialization timed out."));
	}
	if (Flow == EFPSSessionFlow::InLobby && GetWorld() && GetWorld()->HasBegunPlay() && !GetWorld()->GetGameState<AFPSLobbyGameState>())
		SetFlow(EFPSSessionFlow::InMatch);
#if !UE_BUILD_SHIPPING
	FPSSessionSmoke::Tick(this);
#endif
	return true;
}
