#include "../FPSGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../FPSCharacter.h"
#include "../FPSGameState.h"
#include "../FPSHealthComponent.h"
#include "../FPSMatchRules.h"
#include "../FPSLagCompensationSubsystem.h"
#include "../FPSPlayerState.h"
#include "../FPSRecoil.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

struct FFPSMatchTestAccess
{
	static void Setup(AFPSGameMode* GM, AFPSGameState* GS) { GM->GameState = GS; }
	static int32 TaskCount(AFPSGameMode* GM) { return GM->RespawnTasks.Num(); }
	static void Close(AFPSGameMode* GM) { GM->EndPlay(EEndPlayReason::LevelTransition); }
};
struct FFPSLifeHistoryTestAccess
{
	static void Record(UFPSLagCompensationSubsystem* S, int32 Frame) { S->RecordFrame(Frame); }
};

namespace
{
struct FMatchFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AFPSGameMode* GM;
	AFPSGameState* GS;
	APlayerController* Killer;
	APlayerController* Victim;
	FMatchFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GM = World->SpawnActor<AFPSGameMode>();
		GS = World->SpawnActor<AFPSGameState>();
		World->SetGameState(GS);
		FFPSMatchTestAccess::Setup(GM, GS);
		Killer = AddPlayer(TEXT("Winner"));
		Victim = AddPlayer(TEXT("Victim"));
	}
	APlayerController* AddPlayer(const TCHAR* Name)
	{
		APlayerController* PC = World->SpawnActor<APlayerController>();
		PC->PlayerState = World->SpawnActor<AFPSPlayerState>();
		PC->PlayerState->SetPlayerName(Name);
		return PC;
	}
	AFPSCharacter* NewLife()
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AFPSCharacter* C = World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(), FTransform::Identity, P);
		Victim->UnPossess();
		Victim->Possess(C);
		return C;
	}
	~FMatchFixture()
	{
		FFPSMatchTestAccess::Close(GM);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSLifeBoundaryTest, "FPS.Match.LifeAndShieldBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSLifeBoundaryTest::RunTest(const FString&)
{
	TestFalse(TEXT("A new life cannot exist at a historical frame before birth"), FPSMatch::FrameExistsForLife(59, 60));
	TestTrue(TEXT("Birth frame is admitted"), FPSMatch::FrameExistsForLife(60, 60));
	TestFalse(TEXT("Shield cannot extend before birth"), FPSMatch::ProtectedAt(9.9, 10., 13., -1.));
	TestTrue(TEXT("Shield starts at birth"), FPSMatch::ProtectedAt(10., 10., 13., -1.));
	TestTrue(TEXT("Historical shot stays protected even if delivered after expiry"), FPSMatch::ProtectedAt(12., 10., 13., -1.));
	TestFalse(TEXT("Exactly three seconds expires the shield"), FPSMatch::ProtectedAt(13., 10., 13., -1.));
	TestTrue(TEXT("Cancellation does not erase protected history"), FPSMatch::ProtectedAt(10.5, 10., 13., 11.));
	TestFalse(TEXT("Movement cancellation boundary is exclusive"), FPSMatch::ProtectedAt(11., 10., 13., 11.));
	FMatchFixture F;
	AFPSCharacter* C = F.NewLife();
	C->StartSpawnProtection(3.f);
	TestEqual(TEXT("Central health entry rejects direct damage while protected"), C->GetHealthComponent()->ApplyDamage(20.f, F.Killer, nullptr), 0.f);
	C->GetCharacterMovement()->Velocity = FVector(10.f, 0.f, 0.f);
	C->Tick(.016f);
	TestFalse(TEXT("Authoritative horizontal movement cancels protection"), C->IsSpawnProtected());
	TestEqual(TEXT("Damage resumes after cancellation"), C->GetHealthComponent()->ApplyDamage(20.f, F.Killer, nullptr), 20.f);
	C->StartSpawnProtection(3.f);
	C->OnJumped_Implementation();
	TestFalse(TEXT("Confirmed jump cancels protection without treating passive falling as input"), C->IsSpawnProtected());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSpreadRampTest, "FPS.Combat.ProgressiveSpread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSSpreadRampTest::RunTest(const FString&)
{
	FFPSRecoilPattern P;
	float Previous = -1.f;
	for (int32 I = 0; I < 200; ++I)
	{
		const float Spread = FPSRecoil::SpreadAtShot(I, .6f, 8, 1.f);
		if (!TestTrue(TEXT("Spread is monotonic and bounded"), Spread >= Previous && Spread <= .6f)) return false;
		FVector2D Fixed, Offset, Fixed2, Offset2;
		FPSRecoil::Evaluate(P, 123, 22, I, .6f, 2.f, Fixed, Offset, 8, 1.f);
		FPSRecoil::Evaluate(P, 123, 22, I, .6f, 2.f, Fixed2, Offset2, 8, 1.f);
		TestTrue(TEXT("Independent prediction/authority evaluations agree"), Fixed == Fixed2 && Offset == Offset2);
		TestTrue(TEXT("Random offset stays within the ramped radius"), Offset.Size() <= Spread * 2.f + .000001f);
		if (I == 0) TestTrue(TEXT("First shot has zero fixed and random offset"), Fixed.IsZero() && Offset.IsZero());
		Previous = Spread;
	}
	TestEqual(TEXT("Ninth bullet reaches the configured spread, zero based index 8"), FPSRecoil::SpreadAtShot(8, .6f, 8, 1.f), .6f);
	TestTrue(TEXT("Higher exponent delays the curve"), FPSRecoil::SpreadAtShot(4, .6f, 8, 2.f) < FPSRecoil::SpreadAtShot(4, .6f, 8, 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSNewLifeHistoryTest, "FPS.Match.NewLifeHistoryQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSNewLifeHistoryTest::RunTest(const FString&)
{
	FMatchFixture F;
	AFPSCharacter* C = F.NewLife();
	C->SetActorLocation(FVector(500.f, 0.f, 0.f));
	C->BirthServerTime = 1.; // Controlled fixture: birth is frame 60.
	UFPSLagCompensationSubsystem* S = F.World->GetSubsystem<UFPSLagCompensationSubsystem>();
	if (!TestNotNull(TEXT("World history subsystem exists"), S)) return false;
	S->RegisterCharacter(C);
	FFPSShotRequest R;
	R.ViewLocation = FVector::ZeroVector;
	R.ViewDirection = FVector::ForwardVector;
	R.ClientFrame = 59;
	FPSLagComp::FResolvedShot Out;
	S->ResolveShot(nullptr, R, 1000.f, Out);
	TestFalse(TEXT("Current-pose fallback cannot hit a target before birth"), Out.bHit);
	R.ClientFrame = 60;
	S->ResolveShot(nullptr, R, 1000.f, Out);
	TestTrue(TEXT("Fixture ray hits the current target when no history exists"), Out.bHit && Out.Character == C);
	FFPSLifeHistoryTestAccess::Record(S, 61);
	S->ResolveShot(nullptr, R, 1000.f, Out);
	TestFalse(TEXT("Missing historical sample cannot substitute the current pose"), Out.bHit);
	R.ClientFrame = 61;
	S->ResolveShot(nullptr, R, 1000.f, Out);
	TestTrue(TEXT("Recorded post-birth pose is hittable"), Out.bHit && Out.Character == C);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSFortyKillsTest, "FPS.Match.FortyKillsAndDeduplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSFortyKillsTest::RunTest(const FString&)
{
	FMatchFixture F;
	for (int32 I = 1; I <= 40; ++I)
	{
		F.NewLife()->GetHealthComponent()->ApplyDamage(1000.f, F.Killer, nullptr);
		F.GM->NotifyPlayerKilled(F.Victim, F.Killer);
		F.GM->NotifyPlayerKilled(F.Victim, F.Killer);
		TestEqual(TEXT("Duplicate death cannot score twice"), F.Killer->GetPlayerState<AFPSPlayerState>()->GetKills(), I);
		TestEqual(TEXT("Victory occurs at exactly forty"), F.GS->HasFinished(), I == 40);
	}
	TestEqual(TEXT("Winner is authoritative"), F.GS->GetWinner(), F.Killer->PlayerState.Get());
	TestEqual(TEXT("Winner name survives later departure"), F.GS->WinnerName, FString(TEXT("Winner")));
	TestEqual(TEXT("Winning kill clears all pending respawns"), FFPSMatchTestAccess::TaskCount(F.GM), 0);
	AFPSCharacter* C = F.NewLife();
	TestEqual(TEXT("Results phase blocks damage at central health entry"), C->GetHealthComponent()->ApplyDamage(10.f, F.Killer, nullptr), 0.f);
	F.GM->NotifyPlayerKilled(F.Victim, F.Killer);
	TestEqual(TEXT("No scoring after match finishes"), F.Killer->GetPlayerState<AFPSPlayerState>()->GetKills(), 40);
	return true;
}

namespace
{
class FStaleRespawnCommand : public IAutomationLatentCommand
{
public:
	FStaleRespawnCommand(FAutomationTestBase* InTest) : Test(InTest), F(MakeShared<FMatchFixture>()) {}
	virtual bool Update() override
	{
		if (LastFrame == GFrameCounter) return false;
		LastFrame = GFrameCounter;
		switch (Phase++)
		{
		case 0:
			F->NewLife()->GetHealthComponent()->ApplyDamage(1000.f, F->Killer, nullptr);
			F->GM->NotifyPlayerKilled(F->Victim, F->Killer);
			F->World->GetTimerManager().Tick(0.f);
			NewPawn = F->NewLife();
			break;
		case 1:
			F->World->GetTimerManager().Tick(4.f);
			Test->TestEqual(TEXT("Old respawn cannot unpossess a manually replaced life"), F->Victim->GetPawn(), NewPawn.Get());
			Test->TestEqual(TEXT("Stale task is removed"), FFPSMatchTestAccess::TaskCount(F->GM), 0);
			Cast<AFPSCharacter>(NewPawn.Get())->GetHealthComponent()->ApplyDamage(1000.f, F->Killer, nullptr);
			F->GM->NotifyPlayerKilled(F->Victim, F->Killer);
			F->GM->Logout(F->Victim);
			Test->TestEqual(TEXT("Logout removes pending respawn"), FFPSMatchTestAccess::TaskCount(F->GM), 0);
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	TSharedPtr<FMatchFixture> F;
	TWeakObjectPtr<APawn> NewPawn;
	uint64 LastFrame = MAX_uint64;
	int32 Phase = 0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSOldRespawnTest, "FPS.Match.StaleRespawnAndLogout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSOldRespawnTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStaleRespawnCommand>(this));
	return true;
}
#endif
