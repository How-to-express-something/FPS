#include "../FPSCombatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../FPSCharacter.h"
#include "../FPSHealthComponent.h"
#include "../FPSWeapon.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

/** Exercise the real component and timer without loading gameplay/animation assets. */
struct FFPSCombatTestAccess
{
	static void Init(UFPSCombatComponent* Combat, AFPSWeapon* First, AFPSWeapon* Second)
	{
		Combat->Inventory = {First, Second};
		Combat->CurrentWeapon = First;
		Combat->DefaultCombatSettings.MagSize = 30;
		Combat->DefaultCombatSettings.ReloadTime = 1.6f;
	}
	static void SetActiveInput(UFPSCombatComponent* Combat)
	{
		Combat->bWantsFire = Combat->bIsFiring = Combat->bIsAiming = true;
	}
	static bool HasActiveInput(const UFPSCombatComponent* Combat)
	{
		return Combat->bWantsFire || Combat->bIsFiring || Combat->bIsAiming;
	}
	static uint32 Generation(const UFPSCombatComponent* Combat) { return Combat->ReloadGeneration; }
	static void OldCallback(UFPSCombatComponent* Combat, uint32 Generation, AFPSWeapon* Weapon)
	{
		Combat->FinishReload(Generation, Weapon);
	}
	static void FireInput(UFPSCombatComponent* Combat, uint32 Sequence)
	{
		Combat->NextLocalFireTime = BIG_NUMBER;
		Combat->Server_CombatInput_Implementation(EFPSCombatInput::StartFire, Sequence, INDEX_NONE);
	}
	static void PredictReload(UFPSCombatComponent* Combat, uint32 Sequence)
	{
		Combat->bHasLocalCombatInputs = true;
		Combat->LocalInputSequence = Sequence;
		Combat->ApplyCombatInput(EFPSCombatInput::Reload, INDEX_NONE, false);
	}
	static void ApplyState(UFPSCombatComponent* Combat, const FFPSCombatState& State)
	{
		Combat->ApplyAuthoritativeCombatState(State);
	}
	static void RemoveWeapon(UFPSCombatComponent* Combat) { Combat->CurrentWeapon = nullptr; }
	static void SetWeapon(UFPSCombatComponent* Combat, AFPSWeapon* Weapon) { Combat->CurrentWeapon = Weapon; }
	static void Shot(UFPSCombatComponent* Combat, AFPSCharacter* Character, int32 Sequence)
	{
		FFPSShotRequest Request;
		Request.Sequence = Sequence;
		Request.Weapon = Combat->CurrentWeapon;
		Request.BurstId = Combat->ActiveFireBurstId;
		Request.PredictedShotIndex = Combat->ServerNextBurstShotIndex;
		Request.ViewLocation = Character->GetPawnViewLocation();
		Request.WeaponTag = Combat->CurrentWeapon->GetTags();
		Combat->Server_FireShot_Implementation(Request);
	}
};

namespace
{
struct FReloadFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AFPSCharacter* Character = nullptr;
	UFPSCombatComponent* Combat = nullptr;
	AFPSWeapon* First = nullptr;
	AFPSWeapon* Second = nullptr;

	FReloadFixture()
	{
		if (!World) return;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(), FTransform::Identity, Params);
		Params.Owner = Character;
		First = World->SpawnActor<AFPSWeapon>(AFPSWeapon::StaticClass(), FTransform::Identity, Params);
		Second = World->SpawnActor<AFPSWeapon>(AFPSWeapon::StaticClass(), FTransform::Identity, Params);
		if (!Character || !First || !Second) return;
		Combat = Character->FindComponentByClass<UFPSCombatComponent>();
		FFPSCombatTestAccess::Init(Combat, First, Second);
		First->SetAmmoInMag(5);
		Second->SetAmmoInMag(8);
	}
	~FReloadFixture()
	{
		if (World) World->DestroyWorld(false);
	}
	bool IsValid() const { return World && Character && Combat && First && Second; }
};

class FReloadLifecycleCommand : public IAutomationLatentCommand
{
public:
	FReloadLifecycleCommand(FAutomationTestBase* InTest, TSharedPtr<FReloadFixture> InFixture)
		: Test(InTest), Fixture(MoveTemp(InFixture)) {}

	virtual bool Update() override
	{
		// TimerManager advances at most once per engine frame. Never change GFrameCounter.
		if (LastFrame == GFrameCounter) return false;
		LastFrame = GFrameCounter;
		UFPSCombatComponent* C = Fixture->Combat;
		AFPSWeapon* A = Fixture->First;
		AFPSWeapon* B = Fixture->Second;
		FTimerManager& Timers = Fixture->World->GetTimerManager();
		switch (Phase++)
		{
		case 0:
			FFPSCombatTestAccess::SetActiveInput(C);
			C->Reload();
			Test->TestTrue(TEXT("Reload starts"), C->IsReloading());
			Test->TestFalse(TEXT("Reload clears held fire, firing and aiming"), FFPSCombatTestAccess::HasActiveInput(C));
			Timers.Tick(0.f); // Activate pending timers.
			break;
		case 1:
			Timers.Tick(2.f);
			Test->TestFalse(TEXT("Real timer completes reload"), C->IsReloading());
			Test->TestEqual(TEXT("Authority fills the magazine"), A->GetAmmoInMag(), 30);
			A->SetAmmoInMag(5);
			C->Reload();
			C->StartAim();
			Test->TestFalse(TEXT("Aim press interrupts reload"), C->IsReloading());
			break;
		case 2:
			Timers.Tick(2.f);
			Test->TestEqual(TEXT("Cancelled timer cannot fill ammo"), A->GetAmmoInMag(), 5);
			C->Reload();
			OldGeneration = FFPSCombatTestAccess::Generation(C);
			C->Reload();
			Test->TestEqual(TEXT("Repeated reload does not restart"), FFPSCombatTestAccess::Generation(C), OldGeneration);
			C->StopFire();
			C->StopAim();
			Test->TestTrue(TEXT("Input releases preserve reload"), C->IsReloading());
			C->SwitchWeapon(1);
			Test->TestFalse(TEXT("Weapon switch interrupts reload"), C->IsReloading());
			Test->TestTrue(TEXT("Second weapon equipped"), C->GetCurrentWeapon() == B);
			break;
		case 3:
			Timers.Tick(2.f);
			Test->TestEqual(TEXT("Old weapon not filled after switch"), A->GetAmmoInMag(), 5);
			Test->TestEqual(TEXT("New weapon not filled by old timer"), B->GetAmmoInMag(), 8);
			C->Reload();
			FFPSCombatTestAccess::OldCallback(C, OldGeneration, A);
			Test->TestTrue(TEXT("Stale callback cannot cancel a newer reload"), C->IsReloading());
			C->OnOwnerDied();
			Test->TestFalse(TEXT("Death stops reload"), C->IsReloading());
			break;
		case 4:
			Timers.Tick(2.f);
			Test->TestEqual(TEXT("Death cancels pending fill"), B->GetAmmoInMag(), 8);
			B->SetAmmoInMag(0);
			C->Reload();
			OldGeneration = FFPSCombatTestAccess::Generation(C);
			FFPSCombatTestAccess::FireInput(C, 1);
			Test->TestTrue(TEXT("Empty trigger automatically reloads even under shot cooldown"), C->IsReloading());
			Test->TestTrue(TEXT("Empty trigger replaces old reload task"), FFPSCombatTestAccess::Generation(C) != OldGeneration);
			Test->TestFalse(TEXT("Auto reload does not retain firing intent"), FFPSCombatTestAccess::HasActiveInput(C));
			break;
		case 5:
			Timers.Tick(0.8f);
			FFPSCombatTestAccess::FireInput(C, 2);
			break;
		case 6:
			Timers.Tick(1.f);
			Test->TestEqual(TEXT("Restart waits a complete new reload duration"), B->GetAmmoInMag(), 0);
			break;
		case 7:
			Timers.Tick(0.7f);
			Test->TestEqual(TEXT("Restarted timer eventually fills ammo"), B->GetAmmoInMag(), 30);
			FFPSCombatTestAccess::SetActiveInput(C);
			C->Reload();
			Test->TestFalse(TEXT("Full magazine does not reload"), C->IsReloading());
			Test->TestFalse(TEXT("Full magazine reload still clears combat intent"), FFPSCombatTestAccess::HasActiveInput(C));
			B->SetAmmoInMag(5);
			C->Reload();
			C->SwitchWeapon(99);
			Test->TestFalse(TEXT("Invalid switch input still interrupts"), C->IsReloading());
			C->Reload();
			C->InterruptReload();
			Test->TestFalse(TEXT("Future combat hook interrupts"), C->IsReloading());
			FFPSCombatTestAccess::RemoveWeapon(C);
			C->Reload();
			Test->TestFalse(TEXT("No weapon cannot reload"), C->IsReloading());
			FFPSCombatTestAccess::SetWeapon(C, B);
			Fixture->Character->FindComponentByClass<UFPSHealthComponent>()->ApplyDamage(1000.f, nullptr, nullptr);
			C->Reload();
			Test->TestFalse(TEXT("Dead owner cannot reload"), C->IsReloading());
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	TSharedPtr<FReloadFixture> Fixture;
	uint64 LastFrame = MAX_uint64;
	uint32 OldGeneration = 0;
	int32 Phase = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSReloadLifecycleTest, "FPS.Combat.ReloadLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSReloadLifecycleTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FReloadFixture> Fixture = MakeShared<FReloadFixture>();
	if (!TestTrue(TEXT("Native test world and actors spawn"), Fixture->IsValid())) return false;
	// The asset-free character deliberately has no hand sockets. Expect exactly
	// these two attachment diagnostics; all other errors still fail the test.
	AddExpectedMessagePlain(TEXT("Weapon attach socket 'hand_r' does not exist"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 2);
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FReloadLifecycleCommand>(this, Fixture));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSReloadReconciliationTest, "FPS.Combat.ReloadReconciliation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSReloadReconciliationTest::RunTest(const FString& Parameters)
{
	FReloadFixture F;
	if (!TestTrue(TEXT("Native test fixture spawns"), F.IsValid())) return false;
	FFPSCombatTestAccess::SetActiveInput(F.Combat);
	FFPSCombatTestAccess::PredictReload(F.Combat, 2);
	TestTrue(TEXT("Owner predicts reload immediately"), F.Combat->IsReloading());
	TestFalse(TEXT("Owner prediction clears fire and aim"), FFPSCombatTestAccess::HasActiveInput(F.Combat));
	FFPSCombatState State;
	State.Revision = 10;
	State.LastProcessedInput = 1;
	FFPSCombatTestAccess::ApplyState(F.Combat, State);
	TestTrue(TEXT("Older input acknowledgement cannot undo new prediction"), F.Combat->IsReloading());
	State.Revision = 11;
	State.LastProcessedInput = 2;
	FFPSCombatTestAccess::ApplyState(F.Combat, State);
	TestFalse(TEXT("Latest acknowledgement rolls back rejected reload"), F.Combat->IsReloading());
	State.Revision = 10;
	State.bReloading = true;
	FFPSCombatTestAccess::ApplyState(F.Combat, State);
	TestFalse(TEXT("Delayed replication cannot restore stale reload"), F.Combat->IsReloading());
	State.Revision = 12;
	State.bAiming = State.bFiring = true;
	FFPSCombatTestAccess::ApplyState(F.Combat, State);
	TestTrue(TEXT("New authority reload applies"), F.Combat->IsReloading());
	TestFalse(TEXT("Reload view always clears fire and aim"), FFPSCombatTestAccess::HasActiveInput(F.Combat));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSReloadShotIntegrationTest, "FPS.Combat.ReloadShotIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSReloadShotIntegrationTest::RunTest(const FString& Parameters)
{
	FReloadFixture F;
	if (!TestTrue(TEXT("Native test fixture spawns"), F.IsValid())) return false;
	F.First->SetAmmoInMag(2);
	F.Combat->Reload();
	FFPSCombatTestAccess::FireInput(F.Combat, 1);
	TestFalse(TEXT("Loaded trigger cancels reload before shot validation"), F.Combat->IsReloading());
	FFPSCombatTestAccess::Shot(F.Combat, F.Character, 1);
	TestEqual(TEXT("First shot consumes one round"), F.First->GetAmmoInMag(), 1);
	TestFalse(TEXT("Nonempty magazine does not auto reload"), F.Combat->IsReloading());
	F.Combat->StartAim();
	FFPSCombatTestAccess::Shot(F.Combat, F.Character, 2);
	TestEqual(TEXT("Last round is consumed"), F.First->GetAmmoInMag(), 0);
	TestTrue(TEXT("Last round starts authority auto reload"), F.Combat->IsReloading());
	TestFalse(TEXT("Authority auto reload clears aim and fire"), FFPSCombatTestAccess::HasActiveInput(F.Combat));
	const uint32 Generation = FFPSCombatTestAccess::Generation(F.Combat);
	// Simulate an independent equipment change which bypassed SwitchWeapon.
	FFPSCombatTestAccess::SetWeapon(F.Combat, F.Second);
	FFPSCombatTestAccess::OldCallback(F.Combat, Generation, F.First);
	TestEqual(TEXT("Task does not fill its unequipped original weapon"), F.First->GetAmmoInMag(), 0);
	TestEqual(TEXT("Task does not fill a replacement weapon"), F.Second->GetAmmoInMag(), 8);
	TestFalse(TEXT("Mismatched current weapon ends the task safely"), F.Combat->IsReloading());
	return true;
}
#endif
