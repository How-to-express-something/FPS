#include "../Online/MultiplayerSessionsSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "OnlineSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSessionSettingsTest, "FPS.Session.LANBuildCompatibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSSessionSettingsTest::RunTest(const FString&)
{
	const FOnlineSessionSettings Settings = UMultiplayerSessionsSubsystem::MakeLANSettings(TEXT(" Room "), 4);
	TestEqual(TEXT("Null LAN discoverability requires the engine's actual build ID"), Settings.BuildUniqueId, GetBuildUniqueId());
	int32 Protocol = 0;
	Settings.Get(FPSSession::ProtocolKey, Protocol);
	TestEqual(TEXT("Project protocol compatibility is a separate advertised field"), Protocol, FPSSession::ProtocolVersion);
	TestTrue(TEXT("LAN rooms are advertised"), Settings.bIsLANMatch && Settings.bShouldAdvertise);
	TestFalse(TEXT("Null search does not require a platform lobby"), Settings.bUseLobbiesIfAvailable);
	return true;
}

struct FFPSSessionTestAccess
{
	static void BeginCreate(UMultiplayerSessionsSubsystem* S) { S->BeginBackend(UMultiplayerSessionsSubsystem::EBackendOp::Create, EFPSSessionFlow::Creating); }
	static void BeginFind(UMultiplayerSessionsSubsystem* S) { S->BeginBackend(UMultiplayerSessionsSubsystem::EBackendOp::Find, EFPSSessionFlow::Searching); }
	static void Expire(UMultiplayerSessionsSubsystem* S) { S->Deadline = FPlatformTime::Seconds() - 1.; S->Tick(0.f); }
	static void Created(UMultiplayerSessionsSubsystem* S, FName Name, bool bSuccess) { S->OnCreateComplete(Name, bSuccess); }
	static void Found(UMultiplayerSessionsSubsystem* S, bool bSuccess) { S->OnFindComplete(bSuccess); }
	static void EmptySearch(UMultiplayerSessionsSubsystem* S) { S->Search = MakeShared<FOnlineSessionSearch>(); }
	static void Stop(UMultiplayerSessionsSubsystem* S) { S->bDeinitializing = true; }
	static void Connecting(UMultiplayerSessionsSubsystem* S) { S->SetFlow(EFPSSessionFlow::Connecting); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSessionConnectionCancelTest, "FPS.Session.ConnectionCancel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSSessionConnectionCancelTest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>();
	UMultiplayerSessionsSubsystem* S = NewObject<UMultiplayerSessionsSubsystem>(GI);
	FFPSSessionTestAccess::Connecting(S);
	S->CancelPendingOperation();
	TestFalse(TEXT("Cancelling connection releases local Busy after cleanup"), S->IsBusy());
	TestEqual(TEXT("Cancellation reaches recoverable menu error"), S->GetFlow(), EFPSSessionFlow::Error);
	S->NotifyLobbyReady();
	TestEqual(TEXT("A late lobby-ready signal cannot revive the cancelled operation"), S->GetFlow(), EFPSSessionFlow::Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSessionDrainTest, "FPS.Session.CancelAndLateCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSSessionDrainTest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>();
	UMultiplayerSessionsSubsystem* S = NewObject<UMultiplayerSessionsSubsystem>(GI);
	FFPSSessionTestAccess::BeginCreate(S);
	S->CancelPendingOperation();
	TestEqual(TEXT("Cancellation drains the native operation"), S->GetFlow(), EFPSSessionFlow::Recovering);
	TestTrue(TEXT("No conflicting new operation while original completion is possible"), S->IsBusy());
	TestFalse(TEXT("Create cannot replace an abandoned outstanding request"), S->CreateRoom(TEXT("Other"), 4));
	FFPSSessionTestAccess::Created(S, TEXT("UnrelatedSession"), true);
	TestTrue(TEXT("Foreign session completions cannot unlock our operation"), S->IsBusy());
	FFPSSessionTestAccess::Created(S, NAME_GameSession, true);
	TestFalse(TEXT("Late successful create has been discarded and cleaned"), S->IsBusy());
	TestEqual(TEXT("No travel into a cancelled lobby"), S->GetFlow(), EFPSSessionFlow::Error);
	FFPSSessionTestAccess::Created(S, NAME_GameSession, true);
	TestEqual(TEXT("Duplicate completion is ignored"), S->GetFlow(), EFPSSessionFlow::Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSessionTimeoutTest, "FPS.Session.TimeoutAndFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSSessionTimeoutTest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>();
	UMultiplayerSessionsSubsystem* S = NewObject<UMultiplayerSessionsSubsystem>(GI);
	FFPSSessionTestAccess::BeginFind(S);
	FFPSSessionTestAccess::Expire(S);
	TestEqual(TEXT("Timeout is not mistaken for native completion"), S->GetFlow(), EFPSSessionFlow::Recovering);
	TestTrue(TEXT("Still serialized until the underlying task completes"), S->IsBusy());
	FFPSSessionTestAccess::Found(S, false);
	TestFalse(TEXT("Late failure releases the abandoned request"), S->IsBusy());
	TestFalse(TEXT("Timeout diagnosis is retained"), S->GetLastError().IsEmpty());
	FFPSSessionTestAccess::BeginCreate(S);
	FFPSSessionTestAccess::Created(S, NAME_GameSession, false);
	TestEqual(TEXT("Immediate/async failure both reach an actionable error"), S->GetFlow(), EFPSSessionFlow::Error);
	TestFalse(TEXT("Failure does not leave Busy set"), S->IsBusy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSessionEmptyAndShutdownTest, "FPS.Session.EmptySearchAndShutdown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSSessionEmptyAndShutdownTest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>();
	UMultiplayerSessionsSubsystem* S = NewObject<UMultiplayerSessionsSubsystem>(GI);
	FFPSSessionTestAccess::BeginFind(S);
	FFPSSessionTestAccess::EmptySearch(S);
	FFPSSessionTestAccess::Found(S, true);
	TestEqual(TEXT("No rooms is success"), S->GetFlow(), EFPSSessionFlow::Idle);
	TestTrue(TEXT("Empty results have an explicit empty snapshot"), S->GetRooms().IsEmpty());
	TestEqual(TEXT("Whitespace-only room has a usable default"), UMultiplayerSessionsSubsystem::NormalizeRoomName(TEXT(" \r\n ")), FString(TEXT("FPS Room")));
	TestFalse(TEXT("Room text cannot inject new log lines"), UMultiplayerSessionsSubsystem::NormalizeRoomName(TEXT("room\nnext")).Contains(TEXT("\n")));
	FFPSSessionTestAccess::BeginCreate(S);
	FFPSSessionTestAccess::Stop(S);
	FFPSSessionTestAccess::Created(S, NAME_GameSession, true);
	TestEqual(TEXT("Shutdown ignores late travel-triggering completions"), S->GetFlow(), EFPSSessionFlow::Creating);
	return true;
}
#endif
