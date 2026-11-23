#include "../FPSCombatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../FPSCharacter.h"
#include "../FPSWeapon.h"
#include "../FPSWeaponDataAsset.h"
#include "Camera/CameraTypes.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include <limits>

struct FFPSRecoilTestAccess
{
	static void Init(UFPSCombatComponent* C, AFPSWeapon* Weapon)
	{
		C->CurrentWeapon = Weapon;
		C->Inventory = {Weapon};
		C->ShotSeed = 123456;
		C->DefaultCombatSettings.MagSize = 30;
		C->DefaultCombatSettings.RoundsPerMinute = 600.f;
		C->DefaultCombatSettings.Spread = 0.35f;
	}
	static bool TimerActive(UFPSCombatComponent* C)
	{
		return C->GetWorld()->GetTimerManager().IsTimerActive(C->FireTimerHandle);
	}
	static float TimerRemaining(UFPSCombatComponent* C)
	{
		return C->GetWorld()->GetTimerManager().GetTimerRemaining(C->FireTimerHandle);
	}
	static void MakeDue(UFPSCombatComponent* C) { C->NextLocalFireTime = 0.f; }
	static uint32 Burst(const UFPSCombatComponent* C) { return C->ActiveFireBurstId; }
	static int32 ServerIndex(const UFPSCombatComponent* C) { return C->ServerNextBurstShotIndex; }
	static FVector2D Target(const UFPSCombatComponent* C) { return C->CameraRecoilTarget; }
	static void SetTokens(UFPSCombatComponent* C, float Tokens) { C->ServerFireTokens = Tokens; }
	static void SetAutomatic(UFPSCombatComponent* C, bool bAutomatic) { C->DefaultCombatSettings.bAutomatic = bAutomatic; }
	static void SetData(UFPSCombatComponent* C, UFPSWeaponDataAsset* Data) { C->WeaponData = Data; }
	static void BeginServerBurst(UFPSCombatComponent* C, uint32 Id)
	{
		C->ApplyCombatInput(EFPSCombatInput::StartFire, INDEX_NONE, true, Id);
		C->bWantsFire = true; // Permit synchronous owner reconciliation in the isolated world.
	}
	static FFPSShotRequest Request(UFPSCombatComponent* C, int32 Sequence, int32 PredictedIndex)
	{
		FFPSShotRequest R;
		R.Sequence = Sequence;
		R.Weapon = C->CurrentWeapon;
		R.WeaponTag = C->CurrentWeapon->GetTags();
		R.BurstId = C->ActiveFireBurstId;
		R.PredictedShotIndex = PredictedIndex;
		R.ViewLocation = Cast<AFPSCharacter>(C->GetOwner())->GetPawnViewLocation();
		return R;
	}
	static void Predict(UFPSCombatComponent* C, const FFPSShotRequest& R)
	{
		FFPSPredictedShot P;
		P.Sequence = R.Sequence;
		P.Weapon = R.Weapon;
		P.BurstId = R.BurstId;
		P.BurstShotIndex = R.PredictedShotIndex;
		FPSRecoil::Evaluate(C->RecoilPattern, C->ShotSeed, R.BurstId, R.PredictedShotIndex,
			C->DefaultCombatSettings.Spread, C->MaxSpreadRadiusDegrees, P.PatternOffsetDegrees, P.SpreadOffsetDegrees);
		C->PendingShots.Add(P.Sequence, P);
		C->NextLocalBurstShotIndex = P.BurstShotIndex + 1;
	}
	static int32 PendingIndex(UFPSCombatComponent* C, int32 Sequence) { return C->PendingShots.FindChecked(Sequence).BurstShotIndex; }
	static void ServerShot(UFPSCombatComponent* C, const FFPSShotRequest& R) { C->Server_FireShot_Implementation(R); }
	static void Ack(UFPSCombatComponent* C, const FFPSShotResult& R) { C->Client_ShotResult_Implementation(R); }
};

namespace
{
struct FRecoilFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AFPSCharacter* Character = nullptr;
	AFPSWeapon* Weapon = nullptr;
	UFPSCombatComponent* Combat = nullptr;
	APlayerController* PC = nullptr;

	explicit FRecoilFixture(bool bPossess = false)
	{
		if (!World) return;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(), FTransform::Identity, Params);
		Params.Owner = Character;
		Weapon = World->SpawnActor<AFPSWeapon>(AFPSWeapon::StaticClass(), FTransform::Identity, Params);
		if (!Character || !Weapon) return;
		Combat = Character->GetCombatComponent();
		FFPSRecoilTestAccess::Init(Combat, Weapon);
		Weapon->SetAmmoInMag(30);
		if (bPossess)
		{
			PC = World->SpawnActor<APlayerController>();
			if (PC) PC->Possess(Character);
		}
	}
	~FRecoilFixture() { if (World) World->DestroyWorld(false); }
	bool IsValid() const { return World && Character && Weapon && Combat; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRecoilMathTest, "FPS.Combat.RecoilMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSRecoilMathTest::RunTest(const FString& Parameters)
{
	FFPSRecoilPattern Pattern;
	TestTrue(TEXT("Default pattern is complete"), FPSRecoil::ValidatePattern(Pattern));
	for (int32 I = 0; I < 8; ++I) TestEqual(TEXT("Rising prefix does not wrap"), FPSRecoil::MapShotIndex(I, 8), I);
	for (int32 I = 8; I < 1000; ++I)
	{
		TestEqual(TEXT("Six-shot loop maps with no off-by-one"), FPSRecoil::MapShotIndex(I, 8), 8 + (I - 8) % 6);
		TestEqual(TEXT("Loop height remains fixed"), Pattern.Points.FindChecked(FPSRecoil::MapShotIndex(I, 8)).Pitch, 4.3f);
	}
	TestEqual(TEXT("Negative shot index rejected"), FPSRecoil::MapShotIndex(-1, 8), INDEX_NONE);
	TestTrue(TEXT("Large index maps inside loop"), FPSRecoil::MapShotIndex(MAX_int32, 8) >= 8 && FPSRecoil::MapShotIndex(MAX_int32, 8) <= 13);
	for (int32 I = 8; I < 14; ++I)
		TestTrue(TEXT("Three right then three left"), I < 11 ? Pattern.Points[I].Yaw > 0.f : Pattern.Points[I].Yaw < 0.f);
	TestTrue(TEXT("600 RPM = 0.1 seconds"), FMath::IsNearlyEqual(FPSRecoil::ShotInterval(600.f), 0.1f));
	TestTrue(TEXT("1200 RPM = 0.05 seconds"), FMath::IsNearlyEqual(FPSRecoil::ShotInterval(1200.f), 0.05f));
	TestTrue(TEXT("Zero RPM is clamped"), FMath::IsNearlyEqual(FPSRecoil::ShotInterval(0.f), 60.f));
	TestTrue(TEXT("Extreme RPM bounded to 3000"), FMath::IsNearlyEqual(FPSRecoil::ShotInterval(1.e9f), 0.02f));

	FVector2D Fixed, Spread;
	FPSRecoil::Evaluate(Pattern, 17, 23, 10, 0.f, 2.f, Fixed, Spread);
	TestTrue(TEXT("Spread zero gives exactly the fixed track"), Spread.IsZero() && Fixed.Equals(FVector2D(4.3f, 2.4f)));
	const FVector2D Sample = FPSRecoil::SampleSpread(17, 23, 10, 2.f);
	TestTrue(TEXT("Seed/burst/index reproduce the same point"), Sample == FPSRecoil::SampleSpread(17, 23, 10, 2.f));
	TestFalse(TEXT("New burst changes random spread"), Sample == FPSRecoil::SampleSpread(17, 24, 10, 2.f));
	TestFalse(TEXT("New bullet changes random spread"), Sample == FPSRecoil::SampleSpread(17, 23, 11, 2.f));
	double MeanRadiusSquared = 0.;
	FVector2D Mean = FVector2D::ZeroVector;
	for (int32 I = 0; I < 10000; ++I)
	{
		const FVector2D Offset = FPSRecoil::SampleSpread(123, 456, I, 2.f);
		if (!TestTrue(TEXT("Every sample stays within circular radius"), Offset.SizeSquared() <= 4.000001)) return false;
		MeanRadiusSquared += Offset.SizeSquared() / 10000.;
		Mean += Offset / 10000.;
	}
	TestTrue(TEXT("Disk sampling has uniform area, E[r^2] = R^2/2"), FMath::Abs(MeanRadiusSquared - 2.) < 0.06);
	TestTrue(TEXT("Spread is centred"), Mean.Size() < 0.05);
	TestTrue(TEXT("Positive pitch raises the shot"), FPSRecoil::ApplyOffset(FRotator::ZeroRotator, FVector2D(4.f, 0.f)).Vector().Z > 0.f);
	TestTrue(TEXT("Vertical camera/ray rotation is bounded"), FPSRecoil::ApplyOffset(FRotator(88.f, 179.f, 0.f), FVector2D(20.f, 4.f)).Equals(FRotator(89.f, -177.f, 0.f), 0.001f));

	const FVector2D Target(4.f, -2.f);
	FVector2D Split = FVector2D::ZeroVector;
	for (int32 I = 0; I < 60; ++I) Split = FPSRecoil::Interpolate(Split, Target, 10.f, 1.f / 60.f);
	TestTrue(TEXT("Exponential camera blend is frame-rate independent"), Split.Equals(FPSRecoil::Interpolate(FVector2D::ZeroVector, Target, 10.f, 1.f), 0.00001));
	TestTrue(TEXT("Recovery converges to base aim"), FPSRecoil::Interpolate(Target, FVector2D::ZeroVector, 10.f, 2.f).IsNearlyZero());
	Pattern.Points.Remove(9);
	TestFalse(TEXT("Missing map key rejected"), FPSRecoil::ValidatePattern(Pattern));
	TestFalse(TEXT("Broken pattern cannot fire"), FPSRecoil::Evaluate(Pattern, 1, 1, 9, 1.f, 2.f, Fixed, Spread));
	Pattern = FFPSRecoilPattern();
	Pattern.Points[0].Pitch = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Nonfinite point rejected"), FPSRecoil::ValidatePattern(Pattern));
	Pattern.BaseShotCount = 0;
	TestEqual(TEXT("Invalid prefix cannot divide or wrap"), FPSRecoil::MapShotIndex(10, 0), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRecoilAuthorityTest, "FPS.Combat.RecoilAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSRecoilAuthorityTest::RunTest(const FString& Parameters)
{
	FRecoilFixture F;
	if (!TestTrue(TEXT("Native authority fixture spawns"), F.IsValid())) return false;
	UFPSCombatComponent* C = F.Combat;
	FFPSRecoilTestAccess::BeginServerBurst(C, 77);
	FFPSShotRequest First = FFPSRecoilTestAccess::Request(C, 1, 999);
	FFPSRecoilTestAccess::Predict(C, First);
	FFPSRecoilTestAccess::ServerShot(C, First);
	TestEqual(TEXT("Claimed index 999 cannot choose server recoil point"), FFPSRecoilTestAccess::ServerIndex(C), 1);
	TestEqual(TEXT("Reply reconciles to accepted index zero"), C->GetNextPredictedShotIndex(), 1);
	TestEqual(TEXT("Accepted shot consumes ammo"), F.Weapon->GetAmmoInMag(), 29);
	TestTrue(TEXT("First track point is zero regardless of client index"), FFPSRecoilTestAccess::Target(C).IsZero());

	FFPSShotRequest Rejected = FFPSRecoilTestAccess::Request(C, 2, 1);
	FFPSShotRequest InFlight = FFPSRecoilTestAccess::Request(C, 3, 2);
	FFPSRecoilTestAccess::Predict(C, Rejected);
	FFPSRecoilTestAccess::Predict(C, InFlight);
	FFPSRecoilTestAccess::SetTokens(C, 0.f);
	FFPSRecoilTestAccess::ServerShot(C, Rejected);
	TestEqual(TEXT("Rejected shot does not advance authority index"), FFPSRecoilTestAccess::ServerIndex(C), 1);
	TestEqual(TEXT("Rejected shot does not consume ammo"), F.Weapon->GetAmmoInMag(), 29);
	TestEqual(TEXT("Original in-flight index retained for honest reply comparison"), FFPSRecoilTestAccess::PendingIndex(C, 3), 2);
	TestEqual(TEXT("Next prediction rebased to accepted plus pending count"), C->GetNextPredictedShotIndex(), 2);
	FFPSRecoilTestAccess::SetTokens(C, 3.f);
	FFPSRecoilTestAccess::ServerShot(C, InFlight);
	TestEqual(TEXT("Server assigns index one despite stale in-flight claim"), FFPSRecoilTestAccess::ServerIndex(C), 2);
	TestEqual(TEXT("Client continues at index two"), C->GetNextPredictedShotIndex(), 2);
	TestTrue(TEXT("Camera target follows fixed point at 80 percent"), FFPSRecoilTestAccess::Target(C).Equals(FVector2D(0.4f, 0.08f), 0.0001));

	FFPSShotRequest WrongBurst = FFPSRecoilTestAccess::Request(C, 4, 0);
	WrongBurst.BurstId = 888;
	FFPSRecoilTestAccess::ServerShot(C, WrongBurst);
	TestEqual(TEXT("Wrong burst cannot consume ammo"), F.Weapon->GetAmmoInMag(), 28);
	FFPSRecoilTestAccess::ServerShot(C, InFlight);
	TestEqual(TEXT("Replayed request cannot consume ammo"), F.Weapon->GetAmmoInMag(), 28);
	C->StopFire();
	TestTrue(TEXT("Stop clears camera target"), FFPSRecoilTestAccess::Target(C).IsZero());
	FFPSShotRequest AfterStop = FFPSRecoilTestAccess::Request(C, 5, 2);
	FFPSRecoilTestAccess::ServerShot(C, AfterStop);
	TestEqual(TEXT("Shot after stop cannot consume ammo"), F.Weapon->GetAmmoInMag(), 28);

	// A cleared/foreign acknowledgement must not alter current recoil or index.
	FFPSRecoilTestAccess::BeginServerBurst(C, 99);
	FFPSShotResult Old;
	Old.Sequence = 1;
	Old.Weapon = F.Weapon;
	Old.BurstId = 77;
	Old.NextShotIndex = 100;
	FFPSRecoilTestAccess::Ack(C, Old);
	TestEqual(TEXT("Unknown old reply ignored"), C->GetNextPredictedShotIndex(), 0);
	FFPSShotRequest BadOrigin = FFPSRecoilTestAccess::Request(C, 6, 0);
	BadOrigin.ViewLocation += FVector(10000.f, 0.f, 0.f);
	FFPSRecoilTestAccess::Predict(C, BadOrigin);
	FFPSRecoilTestAccess::ServerShot(C, BadOrigin);
	TestEqual(TEXT("Invalid-origin probe cannot consume authoritative ammo"), F.Weapon->GetAmmoInMag(), 28);
	TestEqual(TEXT("Invalid-origin probe cannot advance authority track"), FFPSRecoilTestAccess::ServerIndex(C), 0);
	TestEqual(TEXT("Rejected probe restores predicted next index"), C->GetNextPredictedShotIndex(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRecoilSettingsTest, "FPS.Combat.RecoilSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSRecoilSettingsTest::RunTest(const FString& Parameters)
{
	FRecoilFixture F;
	if (!TestTrue(TEXT("Native fixture spawns"), F.IsValid())) return false;
	UFPSWeaponDataAsset* Data = NewObject<UFPSWeaponDataAsset>(F.Combat);
	FWeaponCombatSettings Custom;
	Custom.RoundsPerMinute = 900.f;
	Custom.Spread = 0.6f;
	Data->WeaponCombatSettings.Add(F.Weapon->GetTags(), Custom);
	FFPSRecoilTestAccess::SetData(F.Combat, Data);
	FWeaponCombatSettings Resolved;
	F.Combat->ResolveCombatSettings(F.Weapon, Resolved);
	TestEqual(TEXT("RPM comes from weapon data entry"), Resolved.RoundsPerMinute, 900.f);
	TestEqual(TEXT("Spread comes from weapon data entry"), Resolved.Spread, 0.6f);
	Data->WeaponCombatSettings[F.Weapon->GetTags()].Spread = -5.f;
	Data->WeaponCombatSettings[F.Weapon->GetTags()].RoundsPerMinute = std::numeric_limits<float>::quiet_NaN();
	F.Combat->ResolveCombatSettings(F.Weapon, Resolved);
	TestEqual(TEXT("Spread below zero is clamped"), Resolved.Spread, 0.f);
	TestEqual(TEXT("Nonfinite RPM uses safe default"), Resolved.RoundsPerMinute, 600.f);
	Data->WeaponCombatSettings[F.Weapon->GetTags()].Spread = 9.f;
	F.Combat->ResolveCombatSettings(F.Weapon, Resolved);
	TestEqual(TEXT("Spread above one is clamped"), Resolved.Spread, 1.f);
	return true;
}

namespace
{
class FFireTimerCommand : public IAutomationLatentCommand
{
public:
	FFireTimerCommand(FAutomationTestBase* InTest, TSharedPtr<FRecoilFixture> InFixture)
		: Test(InTest), F(MoveTemp(InFixture)) {}
	virtual bool Update() override
	{
		if (LastFrame == GFrameCounter) return false;
		LastFrame = GFrameCounter;
		UFPSCombatComponent* C = F->Combat;
		FTimerManager& Timers = F->World->GetTimerManager();
		switch (Phase++)
		{
		case 0:
			C->StartFire();
			Test->TestEqual(TEXT("Started fires first bullet immediately"), F->Weapon->GetAmmoInMag(), 29);
			Test->TestTrue(TEXT("Automatic fire arms actual timer"), FFPSRecoilTestAccess::TimerActive(C));
			Test->TestTrue(TEXT("600 RPM timer waits 0.1 seconds"), FMath::IsNearlyEqual(FFPSRecoilTestAccess::TimerRemaining(C), 0.1f, 0.001f));
			Timers.Tick(0.f);
			break;
		case 1:
			// This isolated world is not engine-ticked: make its game-time gate due
			// while advancing the real TimerManager across separate engine frames.
			FFPSRecoilTestAccess::MakeDue(C);
			Timers.Tick(0.1001f);
			Test->TestEqual(TEXT("Timer callback fires next bullet"), F->Weapon->GetAmmoInMag(), 28);
			Test->TestEqual(TEXT("Index advances per shot, not per render Tick"), C->GetNextPredictedShotIndex(), 2);
			C->TickComponent(0.2f, LEVELTICK_All, nullptr);
			F->PC->SetControlRotation(FRotator(10.f, 20.f, 0.f));
			{
				FMinimalViewInfo View;
				F->Character->CalcCamera(0.016f, View);
				const FRotator Offset = C->GetCameraRecoilOffset();
				Test->TestTrue(TEXT("Camera adds recoil once to current player aim"), View.Rotation.Equals(FRotator(10.f + Offset.Pitch, 20.f + Offset.Yaw, 0.f), 0.001f));
				Test->TestTrue(TEXT("Camera does not modify base control rotation"), F->PC->GetControlRotation().Equals(FRotator(10.f, 20.f, 0.f), 0.001f));
			}
			C->StopFire();
			Test->TestFalse(TEXT("StopFire cancels automatic timer"), FFPSRecoilTestAccess::TimerActive(C));
			break;
		case 2:
			Timers.Tick(2.f);
			Test->TestEqual(TEXT("Cancelled fire callback cannot emit another round"), F->Weapon->GetAmmoInMag(), 28);
			C->TickComponent(2.f, LEVELTICK_All, nullptr);
			F->PC->SetControlRotation(FRotator(15.f, 30.f, 0.f));
			{
				FMinimalViewInfo View;
				F->Character->CalcCamera(0.016f, View);
				Test->TestTrue(TEXT("Recovery preserves mouse input instead of snapping to old aim"), View.Rotation.Equals(FRotator(15.f, 30.f, 0.f), 0.001f));
			}
			FFPSRecoilTestAccess::MakeDue(C);
			C->StartFire();
			Test->TestEqual(TEXT("New burst starts from first track point"), C->GetNextPredictedShotIndex(), 1);
			Test->TestEqual(TEXT("New burst emits one round"), F->Weapon->GetAmmoInMag(), 27);
			C->Reload();
			Test->TestFalse(TEXT("Reload cancels fire timer"), FFPSRecoilTestAccess::TimerActive(C));
			C->OnOwnerDied();
			break;
		case 3:
			Timers.Tick(3.f);
			Test->TestEqual(TEXT("Death leaves no delayed fire/reload"), F->Weapon->GetAmmoInMag(), 27);
			FFPSRecoilTestAccess::MakeDue(C);
			FFPSRecoilTestAccess::SetTokens(C, 3.f);
			FFPSRecoilTestAccess::SetAutomatic(C, false);
			C->StartFire();
			Test->TestEqual(TEXT("Semiautomatic trigger emits one round"), F->Weapon->GetAmmoInMag(), 26);
			Test->TestFalse(TEXT("Semiautomatic trigger does not arm automatic timer"), FFPSRecoilTestAccess::TimerActive(C));
			C->StopFire();
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	TSharedPtr<FRecoilFixture> F;
	uint64 LastFrame = MAX_uint64;
	int32 Phase = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRecoilTimerCameraTest, "FPS.Combat.RecoilTimerCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSRecoilTimerCameraTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FRecoilFixture> F = MakeShared<FRecoilFixture>(true);
	if (!TestTrue(TEXT("Native world and local possessed pawn spawn"), F->IsValid() && F->PC && F->Character->IsLocallyControlled())) return false;
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFireTimerCommand>(this, F));
	return true;
}
#endif
