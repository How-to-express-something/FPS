#include "../FPSHealthPickup.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../FPSCharacter.h"
#include "../FPSCombatComponent.h"
#include "../FPSGameMode.h"
#include "../FPSGameState.h"
#include "../FPSHealthComponent.h"
#include "../FPSWeapon.h"
#include "../UI/CombatFeedbackWidget.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/AutomationTest.h"
#include <limits>

struct FFPSFeedbackTestAccess
{
	static void Setup(AFPSGameMode* GM, AFPSGameState* GS) { GM->GameState = GS; }
	static int32 Pickups(AFPSGameMode* GM) { return GM->HealthPickupPositions.Num(); }
	static void Close(AFPSGameMode* GM) { GM->EndPlay(EEndPlayReason::LevelTransition); }
	static float Edge(UCombatFeedbackWidget* W) { return W->EdgeRemaining; }
	static float Hit(UCombatFeedbackWidget* W) { return W->HitRemaining; }
	static bool IsHealing(UCombatFeedbackWidget* W) { return W->EdgeColor.G > W->EdgeColor.R; }
	static void Tick(UCombatFeedbackWidget* W, float DT) { W->NativeTick(FGeometry(), DT); }
	static void Destruct(UCombatFeedbackWidget* W) { W->NativeDestruct(); }
	static void Weapon(UFPSCombatComponent* C, AFPSWeapon* W) { C->CurrentWeapon = W; C->Inventory = {W}; C->ActiveFireBurstId = 9; }
	static void Predict(UFPSCombatComponent* C, int32 Sequence)
	{
		FFPSPredictedShot P; P.Sequence=Sequence; P.Weapon=C->CurrentWeapon; P.BurstId=9;
		C->PendingShots.Add(Sequence, P);
	}
	static void Ack(UFPSCombatComponent* C, const FFPSShotResult& R) { C->Client_ShotResult_Implementation(R); }
	static void InvalidWeaponDamage(UFPSCombatComponent* C) { C->DefaultCombatSettings.Damage=-20.f; C->DefaultCombatSettings.HeadshotMultiplier=-2.f; }
	static EFPSShotOutcome Marker(UCombatFeedbackWidget* W) { return W->MarkerOutcome; }
	static void SetFiring(UFPSCombatComponent* C) { C->bWantsFire=C->bIsFiring=true; }
	static void ServerShot(UFPSCombatComponent* C, AFPSCharacter* Shooter, int32 Sequence)
	{
		C->bIsFiring=true;
		FFPSShotRequest R; R.Sequence=Sequence; R.BurstId=9; R.Weapon=C->CurrentWeapon;
		R.WeaponTag=R.Weapon->GetTags(); R.ViewLocation=Shooter->GetPawnViewLocation(); R.ViewDirection=FVector::ForwardVector;
		Predict(C,Sequence);
		C->Server_FireShot_Implementation(R);
	}
};

namespace
{
struct FFeedbackFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AFPSGameMode* GM;
	AFPSGameState* GS;
	AFPSCharacter* C;
	APlayerController* PC;
	UCombatFeedbackWidget* Widget;
	FFeedbackFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		GM = World->SpawnActor<AFPSGameMode>();
		GS = World->SpawnActor<AFPSGameState>(); World->SetGameState(GS);
		FFPSFeedbackTestAccess::Setup(GM, GS);
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		C = World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(), FTransform::Identity, P);
		PC = World->SpawnActor<APlayerController>(); PC->Possess(C);
		ULocalPlayer* LP = NewObject<ULocalPlayer>(GEngine); LP->PlayerController = PC; PC->Player=LP;
		Widget = CreateWidget<UCombatFeedbackWidget>(PC);
		Widget->SetObservedCharacter(C);
	}
	AFPSHealthPickup* Pickup(FVector Position=FVector::ZeroVector)
	{
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AFPSHealthPickup>(AFPSHealthPickup::StaticClass(), Position, FRotator::ZeroRotator, P);
	}
	~FFeedbackFixture()
	{
		FFPSFeedbackTestAccess::Destruct(Widget);
		FFPSFeedbackTestAccess::Close(GM);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHealingRulesTest, "FPS.Feedback.SignedDamageAndHealing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSHealingRulesTest::RunTest(const FString&)
{
	FFeedbackFixture F;
	UFPSHealthComponent* H = F.C->GetHealthComponent();
	TestEqual(TEXT("Damage returns actual health removed"), H->ApplyDamage(30.f,nullptr,nullptr), 30.f);
	TestEqual(TEXT("Negative damage restores and clamps at maximum"), H->ApplyDamage(-999.f,nullptr,nullptr), -30.f);
	TestEqual(TEXT("Full health healing is a no-op"), H->Heal(5.f), 0.f);
	TestEqual(TEXT("NaN rejected"), H->ApplyDamage(std::numeric_limits<float>::quiet_NaN(),nullptr,nullptr), 0.f);
	TestEqual(TEXT("Infinity rejected"), H->ApplyDamage(-std::numeric_limits<float>::infinity(),nullptr,nullptr), 0.f);
	H->ApplyDamage(10.f,nullptr,nullptr); F.C->StartSpawnProtection(3.f);
	TestEqual(TEXT("Shield blocks damage"), H->ApplyDamage(10.f,nullptr,nullptr), 0.f);
	TestEqual(TEXT("Shield does not block healing"), H->Heal(10.f), 10.f);
	F.C->CancelSpawnProtection(); H->ApplyDamage(999.f,nullptr,nullptr);
	TestTrue(TEXT("Fatal hit commits death"), H->IsDead());
	TestEqual(TEXT("Negative damage does not resurrect"), H->ApplyDamage(-999.f,nullptr,nullptr), 0.f);
	AFPSWeapon* W = F.World->SpawnActor<AFPSWeapon>();
	FFPSFeedbackTestAccess::InvalidWeaponDamage(F.C->GetCombatComponent());
	FWeaponCombatSettings Settings;
	F.C->GetCombatComponent()->ResolveCombatSettings(W,Settings);
	TestEqual(TEXT("Negative weapon damage cannot accidentally use healing path"),Settings.Damage,0.f);
	TestEqual(TEXT("Negative head multiplier cannot turn a shot into healing"),Settings.HeadshotMultiplier,0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHealthOverlayTest, "FPS.Feedback.HealthDeltaAndRebind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSHealthOverlayTest::RunTest(const FString&)
{
	FFeedbackFixture F;
	TestEqual(TEXT("Binding initial health does not flash"), FFPSFeedbackTestAccess::Edge(F.Widget), 0.f);
	F.C->GetHealthComponent()->ApplyDamage(20.f,nullptr,nullptr);
	TestTrue(TEXT("Damage starts red edge"), FFPSFeedbackTestAccess::Edge(F.Widget)>0.f && !FFPSFeedbackTestAccess::IsHealing(F.Widget));
	F.C->GetHealthComponent()->ApplyDamage(-20.f,nullptr,nullptr);
	TestTrue(TEXT("Healing starts green edge"), FFPSFeedbackTestAccess::IsHealing(F.Widget));
	FFPSFeedbackTestAccess::Tick(F.Widget, 1.f);
	TestEqual(TEXT("Pulse expires without a timer"), FFPSFeedbackTestAccess::Edge(F.Widget), 0.f);
	F.Widget->SetObservedCharacter(nullptr);
	F.C->GetHealthComponent()->ApplyDamage(5.f,nullptr,nullptr);
	TestEqual(TEXT("Old health no longer drives detached overlay"), FFPSFeedbackTestAccess::Edge(F.Widget), 0.f);
	F.Widget->SetObservedCharacter(F.C);
	TestEqual(TEXT("Rebind seeds current health without a false heal"), F.Widget->GetLastObservedHealth(), 95.f);
	TestEqual(TEXT("Rebind is quiet"), FFPSFeedbackTestAccess::Edge(F.Widget), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHitAckFeedbackTest, "FPS.Feedback.ConfirmedOutcomeDeduplication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSHitAckFeedbackTest::RunTest(const FString&)
{
	FFeedbackFixture F;
	AFPSWeapon* W = F.World->SpawnActor<AFPSWeapon>();
	UFPSCombatComponent* C = F.C->GetCombatComponent();
	FFPSFeedbackTestAccess::Weapon(C,W); W->SetAmmoInMag(10);
	FFPSFeedbackTestAccess::Predict(C,7);
	FFPSShotResult R; R.Sequence=7; R.Weapon=W; R.BurstId=9; R.ServerShotIndex=0; R.NextShotIndex=1;
	R.RemainingAmmo=9; R.Outcome=EFPSShotOutcome::Damaged; R.ActualDamage=20.f;
	FFPSFeedbackTestAccess::Ack(C,R);
	TestTrue(TEXT("Confirmation after release still shows X"), FFPSFeedbackTestAccess::Hit(F.Widget)>0.f);
	FFPSFeedbackTestAccess::Tick(F.Widget,.05f);
	const float Remaining = FFPSFeedbackTestAccess::Hit(F.Widget);
	FFPSFeedbackTestAccess::Ack(C,R);
	TestEqual(TEXT("Duplicate receipt cannot replay X/audio path"), FFPSFeedbackTestAccess::Hit(F.Widget), Remaining);
	FFPSFeedbackTestAccess::Tick(F.Widget,1.f);
	FFPSFeedbackTestAccess::Predict(C,8); R.Sequence=8; R.Outcome=EFPSShotOutcome::NoDamage; R.ActualDamage=0;
	FFPSFeedbackTestAccess::Ack(C,R);
	TestEqual(TEXT("Zero damage no-damage outcome cannot show damage X"), FFPSFeedbackTestAccess::Hit(F.Widget),0.f);
	FFPSFeedbackTestAccess::Predict(C,9); R.Sequence=9; R.Outcome=EFPSShotOutcome::Protected;
	FFPSFeedbackTestAccess::Ack(C,R);
	TestTrue(TEXT("Protection gets a separate visual cue"), FFPSFeedbackTestAccess::Hit(F.Widget)>0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPickupAuthorityTest, "FPS.Feedback.PickupAuthorityAndConsumption", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSPickupAuthorityTest::RunTest(const FString&)
{
	FFeedbackFixture F;
	AFPSHealthPickup* P = F.Pickup();
	F.C->GetHealthComponent()->ApplyDamage(40.f,nullptr,nullptr);
	TestFalse(TEXT("Unregistered object cannot heal"), F.GM->TryCollectHealthPickup(P,F.C));
	TestTrue(TEXT("Server registers object and position"),F.GM->RegisterHealthPickup(P));
	P->SetActorLocation(FVector(5,0,0));
	TestFalse(TEXT("Moved object mismatching registry rejected"),F.GM->TryCollectHealthPickup(P,F.C));
	P->SetActorLocation(FVector::ZeroVector);
	F.C->SetActorLocation(FVector(500,0,0));
	TestFalse(TEXT("Remote player rejected"),F.GM->TryCollectHealthPickup(P,F.C));
	F.C->SetActorLocation(FVector::ZeroVector);
	TestTrue(TEXT("Real server shape overlap heals"),F.GM->TryCollectHealthPickup(P,F.C));
	TestEqual(TEXT("Health restored to maximum"),F.C->GetHealthComponent()->GetHealth(),100.f);
	TestEqual(TEXT("Consumed registry removed before destroy"),FFPSFeedbackTestAccess::Pickups(F.GM),0);
	TestFalse(TEXT("Second consume cannot heal"),F.GM->TryCollectHealthPickup(P,F.C));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSOutcomeDamageTest, "FPS.Feedback.AuthoritativeDamageOutcomes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSOutcomeDamageTest::RunTest(const FString&)
{
	FFeedbackFixture F;
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFPSCharacter* Target=F.World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(),FVector(500,0,0),FRotator::ZeroRotator,P);
	AFPSWeapon* W=F.World->SpawnActor<AFPSWeapon>(); W->SetAmmoInMag(30);
	UFPSCombatComponent* Combat=F.C->GetCombatComponent(); FFPSFeedbackTestAccess::Weapon(Combat,W);
	FFPSFeedbackTestAccess::ServerShot(Combat,F.C,1);
	TestEqual(TEXT("Real trace and point damage remove twenty health"),Target->GetHealthComponent()->GetHealth(),80.f);
	TestTrue(TEXT("Actual server reply drives damaged X"),FFPSFeedbackTestAccess::Marker(F.Widget)==EFPSShotOutcome::Damaged);
	Target->StartSpawnProtection(3.f);
	FFPSFeedbackTestAccess::ServerShot(Combat,F.C,2);
	TestEqual(TEXT("Shield hit removes zero health"),Target->GetHealthComponent()->GetHealth(),80.f);
	TestTrue(TEXT("Zero-damage protection reply has its own Outcome"),FFPSFeedbackTestAccess::Marker(F.Widget)==EFPSShotOutcome::Protected);
	Target->CancelSpawnProtection(); Target->GetHealthComponent()->ApplyDamage(75.f,nullptr,nullptr);
	FFPSFeedbackTestAccess::ServerShot(Combat,F.C,3);
	TestTrue(TEXT("Real point damage reaches death"),Target->IsDead());
	TestTrue(TEXT("Fatal reply drives kill rather than body feedback"),FFPSFeedbackTestAccess::Marker(F.Widget)==EFPSShotOutcome::Killed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPickupGateTest, "FPS.Feedback.PickupFullHealthAndFinishedMatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSPickupGateTest::RunTest(const FString&)
{
	FFeedbackFixture F;
	AFPSHealthPickup* P=F.Pickup(); F.GM->RegisterHealthPickup(P);
	TestFalse(TEXT("Full health preserves pack"),F.GM->TryCollectHealthPickup(P,F.C));
	TestFalse(TEXT("Not consumed at full health"),P->IsConsumed());
	F.C->GetHealthComponent()->ApplyDamage(20.f,nullptr,nullptr); F.GS->Finish(nullptr);
	TestFalse(TEXT("Results phase rejects pickup"),F.GM->TryCollectHealthPickup(P,F.C));
	TestFalse(TEXT("Results phase rejects new registry entries"),F.GM->RegisterHealthPickup(F.Pickup(FVector(500,0,0))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRandomSpawnTest, "FPS.Feedback.RandomPlayerStarts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSRandomSpawnTest::RunTest(const FString&)
{
	FFeedbackFixture F;
	APlayerStart* A=F.World->SpawnActor<APlayerStart>(FVector(3000,0,200),FRotator::ZeroRotator);
	APlayerStart* B=F.World->SpawnActor<APlayerStart>(FVector(-3000,0,200),FRotator::ZeroRotator);
	F.PC->StartSpot=A;
	TestFalse(TEXT("Cached start is not blindly reused"),F.GM->ShouldSpawnAtStartSpot(F.PC));
	for (int32 I=0;I<12;++I) TestTrue(TEXT("Alternative free start selected"),F.GM->ChoosePlayerStart_Implementation(F.PC)==B);
	F.PC->StartSpot=B;
	TestTrue(TEXT("Selection follows last used point, not a fixed index"),F.GM->ChoosePlayerStart_Implementation(F.PC)==A);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSWinningReceiptTest, "FPS.Feedback.WinningReceiptAfterMatchStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSWinningReceiptTest::RunTest(const FString&)
{
	FFeedbackFixture F;
	AFPSWeapon* W=F.World->SpawnActor<AFPSWeapon>();
	UFPSCombatComponent* C=F.C->GetCombatComponent();
	FFPSFeedbackTestAccess::Weapon(C,W); FFPSFeedbackTestAccess::SetFiring(C);
	FFPSFeedbackTestAccess::Predict(C,42);
	F.GS->Finish(nullptr);
	C->TickComponent(.016f,LEVELTICK_All,nullptr);
	FFPSShotResult R; R.Sequence=42; R.Weapon=W; R.BurstId=9; R.ServerShotIndex=0;
	R.Outcome=EFPSShotOutcome::Killed; R.ActualDamage=5.f; R.RemainingAmmo=29;
	FFPSFeedbackTestAccess::Ack(C,R);
	TestTrue(TEXT("Results stop preserves the winning in-flight confirmation"),FFPSFeedbackTestAccess::Marker(F.Widget)==EFPSShotOutcome::Killed);
	return true;
}
#endif
