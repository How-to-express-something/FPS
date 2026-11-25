#include "../FPSCombatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../FPSCharacter.h"
#include "../FPSWeapon.h"
#include "../FPSLagCompensationSubsystem.h"
#include "../FPSRecordingTestGameMode.h"
#include "Engine/GameInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "../UI/NetworkDiagnosticsWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

struct FFPSNetworkDiagnosticsTestAccess
{
	static void Prepare(UFPSCombatComponent* C, AFPSWeapon* W, int32 Sequence)
	{
		C->CurrentWeapon = W;
		FFPSPredictedShot P; P.Sequence = Sequence; P.Weapon = W; P.SentAtSeconds = FPlatformTime::Seconds();
		C->PendingShots.Add(Sequence, P);
	}
	static void Ack(UFPSCombatComponent* C, const FFPSShotResult& R) { C->Client_ShotResult_Implementation(R); }
	static void End(UFPSCombatComponent* C) { C->GetOwner()->Destroy(); }
	static void Equip(UFPSCombatComponent* C, AFPSWeapon* W) { C->CurrentWeapon = W; }
	static void Record(UFPSLagCompensationSubsystem* S, int32 Frame) { S->RecordFrame(Frame); }
	static void Draw(UFPSLagCompensationSubsystem* S) { S->DrawShotSnapshots(); }
	static void Expire(UFPSLagCompensationSubsystem* S)
	{ for (auto& Shot : S->ShotDebugSnapshots) Shot.ExpiresAt = -1.; S->DrawShotSnapshots(); }
	static FString Text(UNetworkDiagnosticsWidget* W)
	{ W->RefreshDiagnostics(); return W->DiagnosticsText ? W->DiagnosticsText->GetText().ToString() : FString(); }
};

namespace
{
	struct FDiagnosticFixture
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FDiagnosticFixture() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); World->InitializeActorsForPlay(FURL()); }
		~FDiagnosticFixture() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		AFPSCharacter* Character()
		{
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(), FTransform::Identity, P);
		}
	};
	struct FScopedDiagnosticCVar
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("fps.Net.DrawShots"));
		int32 Previous = Var->GetInt();
		FScopedDiagnosticCVar() { Var->Set(1, ECVF_SetByCode); }
		~FScopedDiagnosticCVar() { Var->Set(Previous, ECVF_SetByCode); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSDiagnosticReceiptTest, "FPS.Diagnostics.RealReceiptsAndIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSDiagnosticReceiptTest::RunTest(const FString&)
{
	FDiagnosticFixture F;
	auto* Character = F.Character();
	Character->DispatchBeginPlay();
	auto* C = Character->GetCombatComponent();
	auto* W = F.World->SpawnActor<AFPSWeapon>();
	FFPSNetworkDiagnosticsTestAccess::Prepare(C, W, 101);
	FFPSShotResult R; R.Sequence = 101; R.Weapon = W; R.ServerShotIndex = 0; R.RemainingAmmo = 9;
	R.RequestedFrame = 61; R.ResolvedFrame = 61; R.HitSampleFrame = 60; R.NewestHistoryFrame = 70; R.bUsedHistory = true;
	R.bQueryPerformed = true;
	FFPSNetworkDiagnosticsTestAccess::Ack(C, R);
	TestEqual(TEXT("Actual receipt accepted"), C->GetNetworkDiagnostics().AcceptedCount, 1);
	TestEqual(TEXT("Actual target sample differs from requested frame"), C->GetNetworkDiagnostics().SampleFrame, 60);
	TestTrue(TEXT("Ack measured at source"), C->GetNetworkDiagnostics().AckMilliseconds >= 0.);
	TestTrue(TEXT("Ammo labelled as receipt snapshot"), C->GetNetworkDiagnosticsText().Contains(TEXT("ammo(at ACK)=9")));
	FFPSNetworkDiagnosticsTestAccess::Ack(C, R);
	TestEqual(TEXT("Duplicate classified ignored, not accepted twice"), C->GetNetworkDiagnostics().IgnoredCount, 1);
	FFPSNetworkDiagnosticsTestAccess::Prepare(C, W, 102);
	R.Sequence = 102; R.ServerShotIndex = INDEX_NONE; R.RejectReason = EFPSShotReject::BadOrigin; R.Outcome = EFPSShotOutcome::Rejected;
	FFPSNetworkDiagnosticsTestAccess::Ack(C, R);
	TestEqual(TEXT("Rejection from real handler counted"), C->GetNetworkDiagnostics().RejectedCount, 1);
	TestEqual(TEXT("Rejection corrects prediction"), C->GetNetworkDiagnostics().CorrectedCount, 1);
	FFPSNetworkDiagnosticsTestAccess::Prepare(C, W, 103);
	R.Sequence = 103; R.BurstId = 99;
	FFPSNetworkDiagnosticsTestAccess::Ack(C, R);
	TestEqual(TEXT("Stale context does not replace last receipt"), C->GetNetworkDiagnostics().LastAckSequence, 102);
	TestEqual(TEXT("Context mismatch counted ignored"), C->GetNetworkDiagnostics().IgnoredCount, 2);
	FFPSNetworkDiagnosticsTestAccess::End(C);
	FFPSNetworkDiagnosticsTestAccess::Ack(C, R);
	TestFalse(TEXT("EndPlay clears telemetry and ignores late callbacks"), C->GetNetworkDiagnostics().bHasReceipt);
	TestEqual(TEXT("Late receipt cannot create events"), C->GetNetworkDiagnostics().Events.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSDiagnosticSnapshotTest, "FPS.Diagnostics.ActualPoseSnapshotsAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSDiagnosticSnapshotTest::RunTest(const FString&)
{
	FScopedDiagnosticCVar Enabled;
	FDiagnosticFixture F;
	auto* Target = F.Character(); Target->SetActorLocation(FVector(500, 0, 0));
	auto* S = F.World->GetSubsystem<UFPSLagCompensationSubsystem>();
	S->RegisterCharacter(Target);
	FFPSNetworkDiagnosticsTestAccess::Record(S, 60);
	Target->SetActorLocation(FVector(500, 300, 0));
	FFPSNetworkDiagnosticsTestAccess::Record(S, 70);
	FFPSShotRequest R; R.Sequence = 1; R.BurstId = 4; R.ClientFrame = 61;
	FPSLagComp::FResolvedShot Out;
	S->ResolveShot(nullptr, R, 1000.f, Out);
	TestTrue(TEXT("Real ray hits historical pose, despite current miss"), Out.bHit);
	TestEqual(TEXT("Resolved frame preserved"), Out.ResolvedFrame, 61);
	TestEqual(TEXT("Actual earlier sampled frame preserved"), Out.HitSampleFrame, 60);
	if (!TestEqual(TEXT("One actual query recorded"), S->GetShotDebugSnapshots().Num(), 1)) return false;
	const auto Saved = S->GetShotDebugSnapshots()[0];
	if (!TestEqual(TEXT("One target captured"), Saved.Targets.Num(), 1)) return false;
	TestEqual(TEXT("Historical pose copied"), Saved.Targets[0].QueryPose.CapsuleA.Y, 0.);
	TestEqual(TEXT("Current-at-query pose copied"), Saved.Targets[0].CurrentPose.CapsuleA.Y, 300.);
	Target->SetActorLocation(FVector(500, 600, 0));
	FFPSNetworkDiagnosticsTestAccess::Record(S, 124); // Same ring slot as frame 60.
	FFPSNetworkDiagnosticsTestAccess::Record(S, 200);
	TestEqual(TEXT("Overwriting history cannot mutate captured geometry"), S->GetShotDebugSnapshots()[0].Targets[0].QueryPose.CapsuleA.Y, 0.);
	for (int32 N = 2; N <= 12; ++N) { R.Sequence = N; R.ClientFrame = 200; S->ResolveShot(nullptr, R, 1000.f, Out); }
	TestEqual(TEXT("Bounded to last eight actual queries"), S->GetShotDebugSnapshots().Num(), 8);
	TestEqual(TEXT("Oldest retained query identity"), S->GetShotDebugSnapshots()[0].Sequence, 5);
	Target->Destroy();
	FFPSNetworkDiagnosticsTestAccess::Draw(S); // Only copied geometry; no dereference of the destroyed target.
	TestEqual(TEXT("Target destruction does not invalidate captured shot"), S->GetShotDebugSnapshots().Num(), 8);
	FFPSNetworkDiagnosticsTestAccess::Expire(S);
	TestEqual(TEXT("Expiry removes all captured shots"), S->GetShotDebugSnapshots().Num(), 0);
	S->ResolveShot(nullptr, R, 1000.f, Out);
	Enabled.Var->Set(0, ECVF_SetByCode);
	FFPSNetworkDiagnosticsTestAccess::Draw(S);
	TestEqual(TEXT("Disabling clears snapshots"), S->GetShotDebugSnapshots().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSDiagnosticWidgetTest, "FPS.Diagnostics.PassiveWidgetFollowsPossession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSDiagnosticWidgetTest::RunTest(const FString&)
{
	FDiagnosticFixture F;
	auto* PC = F.World->SpawnActor<APlayerController>();
	auto* LP = NewObject<ULocalPlayer>(GEngine); LP->PlayerController = PC; PC->Player = LP; PC->SetAsLocalPlayerController();
	auto* First = F.Character(); PC->Possess(First);
	auto* Widget = CreateWidget<UNetworkDiagnosticsWidget>(PC);
	TestTrue(TEXT("Default C++ widget reads current pawn without BP setup"), FFPSNetworkDiagnosticsTestAccess::Text(Widget).Contains(First->GetName()));
	auto* Second = F.Character(); PC->Possess(Second);
	const FString Text = FFPSNetworkDiagnosticsTestAccess::Text(Widget);
	TestTrue(TEXT("Possession immediately resolves new pawn"), Text.Contains(Second->GetName()));
	TestFalse(TEXT("No old-life pawn retained"), Text.Contains(First->GetName()));
	PC->UnPossess();
	TestTrue(TEXT("Replication gap shows waiting, not stale telemetry"), FFPSNetworkDiagnosticsTestAccess::Text(Widget).Contains(TEXT("waiting for local pawn")));
	FFPSNetworkDiagnostics D;
	for (int32 N = 0; N < 30; ++N) D.AddEvent(FString::FromInt(N));
	TestEqual(TEXT("Event list stays bounded"), D.Events.Num(), 8);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRecordingObserverTest, "FPS.Diagnostics.TestListenHostObserver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSRecordingObserverTest::RunTest(const FString&)
{
	FScopedDiagnosticCVar Enabled;
	FDiagnosticFixture F;
	F.World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	FURL URL; URL.AddOption(TEXT("game=/Script/FPS.FPSRecordingTestGameMode"));
	if (!TestTrue(TEXT("Dedicated sandbox GameMode created"), F.World->SetGameMode(URL))) return false;
	auto* Mode = F.World->GetAuthGameMode<AFPSRecordingTestGameMode>();
	auto* PC = F.World->SpawnActor<APlayerController>();
	auto* LP = NewObject<ULocalPlayer>(GEngine); LP->PlayerController = PC; PC->Player = LP; PC->SetAsLocalPlayerController();
	auto* Host = F.Character(); PC->Possess(Host);
	TestFalse(TEXT("Standalone preview is not suppressed"), AFPSRecordingTestGameMode::IsListenHostObserver(Host));
	// Derive ListenServer mode from a pending listen URL without opening sockets.
	F.World->NextURL = TEXT("/Game/FPS/Maps/Test?listen");
	TestEqual(TEXT("Fixture models listen World"), F.World->GetNetMode(), NM_ListenServer);
	auto* Weapon = F.World->SpawnActor<AFPSWeapon>();
	FFPSNetworkDiagnosticsTestAccess::Equip(Host->GetCombatComponent(), Weapon);
	Weapon->GetWeaponMesh1P()->SetHiddenInGame(false);
	Host->RefreshRecordingPresentation();
	TestTrue(TEXT("Listen host arms hidden"), Host->GetMesh1P()->bHiddenInGame);
	TestTrue(TEXT("Listen host weapon hidden"), Weapon->GetWeaponMesh1P()->bHiddenInGame);
	auto* Target = F.Character(); Target->SetActorLocation(FVector(500, 0, 0));
	Host->SetActorLocation(FVector(500, 300, 0));
	TestFalse(TEXT("Unpossessed target is not a host observer"), AFPSRecordingTestGameMode::IsListenHostObserver(Target));
	auto* S = F.World->GetSubsystem<UFPSLagCompensationSubsystem>();
	S->RegisterCharacter(Host); S->RegisterCharacter(Target);
	FFPSNetworkDiagnosticsTestAccess::Record(S, 1);
	FFPSShotRequest R; R.ClientFrame = 1;
	FPSLagComp::FResolvedShot Out;
	S->ResolveShot(nullptr, R, 1000, Out);
	TestTrue(TEXT("Target remains hittable"), Out.bHit && Out.Character == Target);
	if (!TestEqual(TEXT("One shot captured"), S->GetShotDebugSnapshots().Num(), 1)) return false;
	TestEqual(TEXT("Observer omitted; only target pose retained"), S->GetShotDebugSnapshots()[0].Targets.Num(), 1);
	R.ViewDirection = FVector(500, 300, 0).GetSafeNormal();
	S->ResolveShot(nullptr, R, 1000, Out);
	TestTrue(TEXT("Visual exclusion does not remove host from authoritative hits"), Out.bHit && Out.Character == Host);
	Mode->bHideListenHostFirstPerson = false;
	TestFalse(TEXT("Explicit opt-out disables observer policy"), AFPSRecordingTestGameMode::IsListenHostObserver(Host));
	return true;
}
#endif
