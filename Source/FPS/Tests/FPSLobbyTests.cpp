#include "../Lobby/FPSLobbyGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../Lobby/FPSLobbyGameState.h"
#include "../Lobby/FPSLobbyPlayerController.h"
#include "../FPSPlayerState.h"
#include "../FPSGameMode.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Player.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

struct FFPSLobbyTestAccess
{
	static void Setup(AFPSLobbyGameMode* GM, AFPSLobbyGameState* GS, APlayerController* Host)
	{
		GM->GameState = GS; GM->Host = Host; GM->Capacity = 2; GM->MinimumPlayers = 2;
	}
	static FString Admission(AFPSLobbyGameMode* GM, const FString& Options = FString()) { return GM->AdmissionError(Options); }
	static uint32 Generation(AFPSLobbyGameMode* GM) { return GM->CountdownGeneration; }
	static void Complete(AFPSLobbyGameMode* GM, uint32 Generation) { GM->CompleteCountdown(Generation); }
	static FString TravelError(AFPSLobbyGameMode* GM) { return GM->GetTravelConfigurationError(); }
	static void SessionMatch(AFPSGameMode* GM, bool bMatch) { GM->bSessionMatch = bMatch; }
	static bool BlocksLogin(AFPSGameMode* GM, const UPlayer* Player) { return GM->ShouldRejectSessionLogin(Player); }
};

namespace
{
struct FLobbyFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AFPSLobbyGameMode* GM = nullptr;
	AFPSLobbyGameState* GS = nullptr;
	AFPSLobbyPlayerController* Host = nullptr;
	AFPSLobbyPlayerController* Client = nullptr;
	FLobbyFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GM = World->SpawnActor<AFPSLobbyGameMode>();
		GS = World->SpawnActor<AFPSLobbyGameState>();
		World->SetGameState(GS);
		Host = AddPlayer(10, TEXT("Host"));
		Client = AddPlayer(20, TEXT("Client"));
		FFPSLobbyTestAccess::Setup(GM, GS, Host);
	}
	AFPSLobbyPlayerController* AddPlayer(int32 Id, const FString& Name)
	{
		AFPSLobbyPlayerController* PC = World->SpawnActor<AFPSLobbyPlayerController>();
		World->AddController(PC);
		AFPSPlayerState* PS = World->SpawnActor<AFPSPlayerState>();
		PS->SetPlayerId(Id); PS->SetPlayerName(Name); PC->PlayerState = PS;
		GS->AddPlayerState(PS);
		return PC;
	}
	~FLobbyFixture()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSLobbyAdmissionTest, "FPS.Lobby.AdmissionAndRoster", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSLobbyAdmissionTest::RunTest(const FString&)
{
	FLobbyFixture F;
	TestEqual(TEXT("Accepted controllers consume the last slot before any further PostLogin"), FFPSLobbyTestAccess::Admission(F.GM), FString(TEXT("RoomFull")));
	TestEqual(TEXT("No spectator bypass of capacity"), FFPSLobbyTestAccess::Admission(F.GM, TEXT("?SpectatorOnly=1")), FString(TEXT("SpectatorsNotSupported")));
	TestEqual(TEXT("Roster comes from PlayerState"), F.GS->GetLobbyPlayers().Num(), 2);
	TestEqual(TEXT("Replicated names remain associated with stable player IDs"), F.GS->GetLobbyPlayers()[1].PlayerName, FString(TEXT("Client")));
	F.World->RemoveController(F.Client);
	F.GS->RemovePlayerState(F.Client->PlayerState);
	TestTrue(TEXT("A freed connection slot can be admitted"), FFPSLobbyTestAccess::Admission(F.GM).IsEmpty());
	FFPSLobbySnapshot S = F.GS->GetLobbySnapshot(); S.Phase = EFPSLobbyPhase::Traveling; F.GS->SetSnapshot(S);
	TestEqual(TEXT("Travel locks admission even when slots exist"), FFPSLobbyTestAccess::Admission(F.GM), FString(TEXT("RoomNotJoinable")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMatchLocalLoginTest, "FPS.Lobby.LocalBootstrapAdmission", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSMatchLocalLoginTest::RunTest(const FString&)
{
	FLobbyFixture F;
	AFPSGameMode* Match = F.World->SpawnActor<AFPSGameMode>();
	ULocalPlayer* Local = NewObject<ULocalPlayer>(GEngine);
	UPlayer* Remote = NewObject<UPlayer>();
	FFPSLobbyTestAccess::SessionMatch(Match, true);
	TestFalse(TEXT("LoadMap must be able to recreate its listen host LocalPlayer"), FFPSLobbyTestAccess::BlocksLogin(Match, Local));
	TestTrue(TEXT("A remote UPlayer cannot bypass match admission"), FFPSLobbyTestAccess::BlocksLogin(Match, Remote));
	FString Error;
	TestNull(TEXT("The actual Login entry rejects remote late joining before spawning"), Match->Login(Remote, ROLE_AutonomousProxy, FString(), FString(), FUniqueNetIdRepl(), Error));
	TestEqual(TEXT("Rejection remains explicit"), Error, FString(TEXT("MatchAlreadyStarted")));
	FFPSLobbyTestAccess::SessionMatch(Match, false);
	TestFalse(TEXT("Direct sandbox play retains remote login"), FFPSLobbyTestAccess::BlocksLogin(Match, Remote));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPIEConfigurationTest, "FPS.Lobby.PIETravelConfiguration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSPIEConfigurationTest::RunTest(const FString&)
{
	FLobbyFixture F;
	IConsoleVariable* Allow = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
	if (!TestNotNull(TEXT("UE 5.5.4 PIE seamless CVar exists"), Allow)) return false;
	const int32 SavedValue = Allow->GetInt();
	F.World->WorldType = EWorldType::PIE;
	Allow->SetWithCurrentPriority(0);
	TestFalse(TEXT("Detect unsafe hard-travel fallback BEFORE countdown/travel"), FFPSLobbyTestAccess::TravelError(F.GM).IsEmpty());
	F.GM->SetPlayerReady(F.Host, true);
	F.GM->SetPlayerReady(F.Client, true);
	F.GM->RequestStart(F.Host);
	TestEqual(TEXT("Unsafe PIE configuration keeps all members in the lobby"), F.GS->GetLobbySnapshot().Phase, EFPSLobbyPhase::Waiting);
	Allow->SetWithCurrentPriority(1);
	TestTrue(TEXT("Explicitly enabled PIE can use this session flow"), FFPSLobbyTestAccess::TravelError(F.GM).IsEmpty());
	Allow->SetWithCurrentPriority(SavedValue);
	F.World->WorldType = EWorldType::Game;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSLobbyCountdownTest, "FPS.Lobby.ReadyAuthorityAndCountdown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSLobbyCountdownTest::RunTest(const FString&)
{
	FLobbyFixture F;
	F.GM->SetPlayerReady(F.Host, true);
	TestFalse(TEXT("One unready member blocks the match"), F.GM->AllPlayersReady());
	F.GM->SetPlayerReady(F.Client, true);
	TestTrue(TEXT("All ready satisfies the server rule"), F.GM->AllPlayersReady());
	F.GM->RequestStart(F.Client);
	TestEqual(TEXT("Remote player cannot start even if every member is ready"), F.GS->GetLobbySnapshot().Phase, EFPSLobbyPhase::Waiting);
	F.GM->RequestStart(F.Host);
	TestEqual(TEXT("Host starts the server-owned countdown"), F.GS->GetLobbySnapshot().Phase, EFPSLobbyPhase::Countdown);
	const uint32 OldGeneration = FFPSLobbyTestAccess::Generation(F.GM);
	F.GM->SetPlayerReady(F.Client, false);
	TestEqual(TEXT("Cancelling ready invalidates countdown"), F.GS->GetLobbySnapshot().Phase, EFPSLobbyPhase::Waiting);
	FFPSLobbyTestAccess::Complete(F.GM, OldGeneration);
	TestEqual(TEXT("Stale callback cannot trigger travel"), F.GS->GetLobbySnapshot().Phase, EFPSLobbyPhase::Waiting);
	F.GM->SetPlayerReady(F.Client, true);
	F.GM->RequestStart(F.Host);
	TestEqual(TEXT("Countdown can be started again"), F.GS->GetLobbySnapshot().Phase, EFPSLobbyPhase::Countdown);
	F.GM->CloseRoom();
	TestEqual(TEXT("Room close supersedes the countdown"), F.GS->GetLobbySnapshot().Phase, EFPSLobbyPhase::Closing);
	F.GM->SetPlayerReady(F.Client, false);
	TestTrue(TEXT("Closing room rejects further ready changes"), F.Client->GetPlayerState<AFPSPlayerState>()->IsLobbyReady());
	return true;
}
#endif
