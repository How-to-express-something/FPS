#include "../FPSRecoilCameraShake.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../FPSCharacter.h"
#include "../FPSCombatComponent.h"
#include "../FPSWeapon.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

struct FFPSCameraShakeTestAccess
{
	static void Init(UFPSCombatComponent* C, AFPSWeapon* Weapon)
	{
		C->CurrentWeapon = Weapon;
		C->ShotSeed = 12345;
	}
	static UCameraShakeBase* Active(UFPSCombatComponent* C) { return C->ActiveShotCameraShake.Get(); }
	static void Play(UFPSCombatComponent* C) { C->PlayShotCameraShake(); }
	static void Due(UFPSCombatComponent* C) { C->NextLocalFireTime = 0.f; }
	static void Aim(UFPSCombatComponent* C, bool bAim) { C->bIsAiming = bAim; }
	static void Enable(UFPSCombatComponent* C, bool bEnable) { C->bEnableShotCameraShake = bEnable; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCameraShakePulseTest, "FPS.Camera.RecoilShakePulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSCameraShakePulseTest::RunTest(const FString& Parameters)
{
	UFPSRecoilCameraShake* Shake = NewObject<UFPSRecoilCameraShake>();
	TestTrue(TEXT("Default shake is single-instance"), Shake->bSingleInstance);
	TestNotNull(TEXT("Default shake has a native pulse pattern"), Cast<UFPSRecoilShakePattern>(Shake->GetRootShakePattern()));
	FMinimalViewInfo Base;
	Base.Location = FVector(100.f, 200.f, 300.f);
	Base.Rotation = FRotator::ZeroRotator;
	Base.FOV = 90.f;
	Shake->StartShake(nullptr, 1.f, ECameraShakePlaySpace::CameraLocal);
	FMinimalViewInfo Full = Base;
	Shake->UpdateAndApplyCameraShake(0.02f, 1.f, Full);
	TestTrue(TEXT("Shot shake changes camera rotation"), !Full.Rotation.IsNearlyZero());
	TestTrue(TEXT("Small FOV impulse is applied"), Full.FOV > Base.FOV);
	TestTrue(TEXT("Native shake never translates the camera"), Full.Location.Equals(Base.Location));
	UFPSRecoilCameraShake* ADS = NewObject<UFPSRecoilCameraShake>();
	ADS->StartShake(nullptr, 0.4f, ECameraShakePlaySpace::CameraLocal);
	FMinimalViewInfo Reduced = Base;
	ADS->UpdateAndApplyCameraShake(0.02f, 1.f, Reduced);
	TestTrue(TEXT("Aiming scales recoil shake down"), FMath::IsNearlyEqual(Reduced.Rotation.Pitch, Full.Rotation.Pitch * 0.4, 0.0001));
	for (int32 I = 0; I < 50; ++I)
	{
		Shake->StartShake(nullptr, 1.f, ECameraShakePlaySpace::CameraLocal);
		FMinimalViewInfo Restarted = Base;
		Shake->UpdateAndApplyCameraShake(0.02f, 1.f, Restarted);
		if (!TestTrue(TEXT("Retriggering never accumulates angular amplitude"), Restarted.Rotation.Equals(Full.Rotation, 0.001f))) return false;
	}
	FMinimalViewInfo Finished = Base;
	Shake->UpdateAndApplyCameraShake(0.3f, 1.f, Finished);
	TestTrue(TEXT("Pulse ends automatically"), Shake->IsFinished());
	TestTrue(TEXT("Finished pulse has no residual rotation or FOV"), Finished.Rotation.Equals(Base.Rotation) && Finished.FOV == Base.FOV);
	ADS->StopShake(false);
	Reduced = Base;
	ADS->UpdateAndApplyCameraShake(0.08f, 1.f, Reduced);
	TestTrue(TEXT("StopFire blend-out finishes promptly"), ADS->IsFinished());
	Shake->StopShake(true);
	Shake->TeardownShake();
	ADS->TeardownShake();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCameraShakeIntegrationTest, "FPS.Camera.RecoilShakeIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSCameraShakeIntegrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Native world created"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFPSCharacter* Character = World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(), FTransform::Identity, Params);
	APlayerController* PC = World->SpawnActor<APlayerController>();
	APlayerCameraManager* Manager = World->SpawnActor<APlayerCameraManager>();
	AFPSWeapon* Weapon = World->SpawnActor<AFPSWeapon>();
	bool bReady = TestNotNull(TEXT("Character spawned"), Character) && TestNotNull(TEXT("Controller spawned"), PC) &&
		TestNotNull(TEXT("Camera manager spawned"), Manager) && TestNotNull(TEXT("Weapon spawned"), Weapon);
	if (bReady)
	{
		PC->Possess(Character);
		PC->PlayerCameraManager = Manager;
		if (!Manager->IsActorInitialized()) Manager->PostInitializeComponents();
		Manager->InitializeFor(PC);
		PC->SetViewTarget(Character);
		UFPSCombatComponent* C = Character->GetCombatComponent();
		FFPSCameraShakeTestAccess::Init(C, Weapon);
		Weapon->SetAmmoInMag(30);
		C->StartFire();
		TestEqual(TEXT("FireOnce emits a round"), Weapon->GetAmmoInMag(), 29);
		TestNotNull(TEXT("FireOnce starts local Camera Shake"), FFPSCameraShakeTestAccess::Active(C));
		UCameraModifier_CameraShake* Modifier = Cast<UCameraModifier_CameraShake>(Manager->FindCameraModifierByClass(UCameraModifier_CameraShake::StaticClass()));
		if (TestNotNull(TEXT("Camera shake modifier is installed"), Modifier))
		{
			for (int32 I = 0; I < 30; ++I) FFPSCameraShakeTestAccess::Play(C);
			TArray<FActiveCameraShakeInfo> Active;
			Modifier->GetActiveCameraShakes(Active);
			TestEqual(TEXT("Automatic fire owns at most one shake"), Active.Num(), 1);
		}
		FFPSCameraShakeTestAccess::Aim(C, true);
		FFPSCameraShakeTestAccess::Play(C);
		if (UCameraShakeBase* Active = FFPSCameraShakeTestAccess::Active(C))
			TestTrue(TEXT("ADS uses 40 percent scale"), FMath::IsNearlyEqual(Active->ShakeScale, 0.4f));
		const FRotator InputAim(10.f, 25.f, 0.f);
		PC->SetControlRotation(InputAim);
		Manager->UpdateCamera(0.02f);
		TestTrue(TEXT("Camera Shake preserves base input aim"), PC->GetControlRotation().Equals(InputAim));
		C->StopFire();
		if (UCameraShakeBase* Active = FFPSCameraShakeTestAccess::Active(C))
		{
			FMinimalViewInfo View;
			Active->UpdateAndApplyCameraShake(0.08f, 1.f, View);
			TestTrue(TEXT("StopFire fades owned pulse"), Active->IsFinished());
		}
		FFPSCameraShakeTestAccess::Play(C);
		C->OnOwnerDied();
		TestNull(TEXT("Death immediately releases owned shake"), FFPSCameraShakeTestAccess::Active(C));
		FFPSCameraShakeTestAccess::Play(C);
		FFPSCameraShakeTestAccess::Enable(C, false);
		C->TickComponent(0.016f, LEVELTICK_All, nullptr);
		TestNull(TEXT("Disabling feedback clears an active shake"), FFPSCameraShakeTestAccess::Active(C));
		FFPSCameraShakeTestAccess::Play(C);
		TestNull(TEXT("Disabled feedback cannot start another shake"), FFPSCameraShakeTestAccess::Active(C));
		FFPSCameraShakeTestAccess::Enable(C, true);
		PC->UnPossess();
		FFPSCameraShakeTestAccess::Play(C);
		TestNull(TEXT("Unpossessed/remote owner cannot shake local camera"), FFPSCameraShakeTestAccess::Active(C));
		C->OnOwnerDied();
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return bReady;
}
#endif
