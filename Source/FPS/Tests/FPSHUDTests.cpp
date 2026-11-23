#include "../UI/HUDWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../UI/HealthWidget.h"
#include "../UI/WeaponWidget.h"
#include "../UI/CrosshairWidget.h"
#include "../UI/CombatFeedbackWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "../FPSCharacter.h"
#include "../FPSCombatComponent.h"
#include "../FPSHealthComponent.h"
#include "../FPSPlayerController.h"
#include "../FPSWeapon.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

struct FFPSHUDTestAccess
{
	static UCombatFeedbackWidget* Feedback(UHUDWidget* HUD) { return HUD->CombatFeedbackWidget; }
	static void Equip(UFPSCombatComponent* C, AFPSWeapon* Weapon)
	{
		AFPSWeapon* Old = C->CurrentWeapon;
		C->CurrentWeapon = Weapon;
		C->PendingShots.Reset();
		C->NotifyCurrentWeaponChanged(Old);
	}
	static void Predict(UFPSCombatComponent* C, int32 Sequence)
	{
		C->CacheWeaponAmmoIfIdle();
		FFPSPredictedShot P;
		P.Sequence = Sequence;
		P.Weapon = C->CurrentWeapon;
		C->PendingShots.Add(Sequence, P);
		C->NotifyDisplayAmmoChanged();
	}
	static void Ack(UFPSCombatComponent* C, int32 Sequence, int32 Ammo, uint32 Revision, bool bAccepted = true)
	{
		FFPSShotResult R;
		R.Sequence = Sequence;
		R.Weapon = C->CurrentWeapon;
		R.RemainingAmmo = Ammo;
		R.AmmoRevision = Revision;
		R.ServerShotIndex = bAccepted ? 0 : INDEX_NONE;
		R.RejectReason = bAccepted ? EFPSShotReject::None : EFPSShotReject::FireRate;
		C->Client_ShotResult_Implementation(R);
	}
	static void ReplicateAmmo(AFPSWeapon* Weapon, int32 Ammo, uint32 Revision)
	{
		Weapon->AmmoState.Ammo = Ammo;
		Weapon->AmmoState.Revision = Revision;
		Weapon->OnRep_AmmoState();
	}
	static void Construct(UHUDWidget* W) { W->NativeConstruct(); }
	static void Destruct(UHUDWidget* W) { W->NativeDestruct(); }
	static void Construct(UHealthWidget* W) { W->NativeConstruct(); }
	static void Destruct(UHealthWidget* W) { W->NativeDestruct(); }
	static void Construct(UWeaponWidget* W) { W->NativeConstruct(); }
	static void Destruct(UWeaponWidget* W) { W->NativeDestruct(); }
	static bool HasNativeControls(UHealthWidget* H, UWeaponWidget* W)
	{
		return H->HealthBar && H->HealthText && W->WeaponNameText && W->AmmoText;
	}
};

namespace
{
struct FHUDTestFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AFPSPlayerController* PC = nullptr;
	AFPSCharacter* Character = nullptr;
	AFPSWeapon* Weapon = nullptr;
	UFPSCombatComponent* Combat = nullptr;
	UHUDWidget* HUD = nullptr;

	FHUDTestFixture()
	{
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PC = World->SpawnActor<AFPSPlayerController>();
		Character = World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(), FTransform::Identity, Params);
		if (!PC || !Character) return;
		PC->Possess(Character);
		// Supply an actual local-player context without creating a viewport or
		// running the full online/input initialization of SetPlayer().
		ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
		Player->PlayerController = PC;
		PC->Player = Player;
		PC->SetAsLocalPlayerController();
		// CreateWorld does not initialize actors for play; UMG resolves its local
		// player through the world's controller list, normally filled at that stage.
		World->AddController(PC);
		Params.Owner = Character;
		Weapon = World->SpawnActor<AFPSWeapon>(AFPSWeapon::StaticClass(), FTransform::Identity, Params);
		if (!Weapon) return;
		Weapon->SetAmmoInMag(30);
		Combat = Character->GetCombatComponent();
		FFPSHUDTestAccess::Equip(Combat, Weapon);
		HUD = CreateWidget<UHUDWidget>(PC);
		if (HUD) FFPSHUDTestAccess::Construct(HUD);
	}
	~FHUDTestFixture()
	{
		if (HUD) FFPSHUDTestAccess::Destruct(HUD);
		if (Character) Character->SetRole(ROLE_Authority);
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}
	bool IsValid() const { return HUD && HUD->GetHealthWidget() && HUD->GetWeaponWidget() && Weapon; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHUDLifecycleTest, "FPS.UI.HUDLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSHUDLifecycleTest::RunTest(const FString& Parameters)
{
	FHUDTestFixture F;
	if (!TestTrue(TEXT("HUD owns native health and weapon children"), F.IsValid())) return false;
	UHealthWidget* H = F.HUD->GetHealthWidget();
	UWeaponWidget* W = F.HUD->GetWeaponWidget();
	TestTrue(TEXT("Native controls exist without Blueprint assets"), FFPSHUDTestAccess::HasNativeControls(H, W));
	UCrosshairWidget* Crosshair = F.HUD->GetCrosshairWidget();
	if (!TestNotNull(TEXT("HUD creates a native crosshair"), Crosshair)) return false;
	UCanvasPanelSlot* CrosshairSlot = Cast<UCanvasPanelSlot>(Crosshair->Slot);
	if (!TestNotNull(TEXT("Crosshair has a canvas slot"), CrosshairSlot)) return false;
	TestTrue(TEXT("Crosshair anchors stay at the player viewport centre"),
		CrosshairSlot->GetAnchors().Minimum.Equals(FVector2D(0.5, 0.5)) &&
		CrosshairSlot->GetAnchors().Maximum.Equals(FVector2D(0.5, 0.5)) &&
		CrosshairSlot->GetAlignment().Equals(FVector2D(0.5, 0.5)) && CrosshairSlot->GetPosition().IsZero());
	TestEqual(TEXT("Initial health is pulled without waiting for damage"), H->DisplayedHealth, 100.f);
	TestEqual(TEXT("Initial ammo is pulled without waiting for a shot"), W->DisplayedAmmo, 30);
	TestTrue(TEXT("Current weapon is pulled on binding"), W->DisplayedWeapon == F.Weapon);
	F.HUD->SetObservedCharacter(F.Character);
	F.HUD->SetObservedCharacter(F.Character);
	const auto HealthListeners = F.Character->GetHealthComponent()->OnHealthChanged.GetAllObjects();
	TestEqual(TEXT("Repeated pawn callbacks bind the health display once"), HealthListeners.FilterByPredicate([H](const UObject* O){return O==H;}).Num(), 1);
	TestEqual(TEXT("Repeated pawn callbacks bind the feedback overlay once"), HealthListeners.FilterByPredicate([&F](const UObject* O){return O==FFPSHUDTestAccess::Feedback(F.HUD);}).Num(), 1);
	TestEqual(TEXT("Exactly the two intended health consumers are bound"), HealthListeners.Num(), 2);
	TestEqual(TEXT("Repeated pawn callbacks bind weapon once"), F.Combat->OnCurrentWeaponChanged.GetAllObjects().Num(), 1);
	TestEqual(TEXT("Ammo decrease callback is bound once"), F.Combat->OnAmmoDecreased.GetAllObjects().Num(), 1);
	F.Character->GetHealthComponent()->ApplyDamage(25.f, nullptr, nullptr);
	F.Weapon->SetAmmoInMag(28);
	TestEqual(TEXT("Health event updates UI"), H->DisplayedHealth, 75.f);
	TestEqual(TEXT("Ammo event updates UI"), W->DisplayedAmmo, 28);
	TestTrue(TEXT("Ammo changes never expand or move crosshair"), CrosshairSlot->GetSize().Equals(FVector2D(32, 32)) && CrosshairSlot->GetPosition().IsZero());
	F.Weapon->SetAmmoInMag(30);
	TestEqual(TEXT("Reload/increase event also updates UI"), W->DisplayedAmmo, 30);

	F.HUD->SetObservedCharacter(nullptr);
	TestEqual(TEXT("Null pawn clears health"), H->DisplayedHealth, 0.f);
	TestEqual(TEXT("Null pawn clears ammo"), W->DisplayedAmmo, 0);
	TestFalse(TEXT("Null pawn clears weapon"), IsValid(W->DisplayedWeapon));
	TestEqual(TEXT("Old health delegate is removed"), F.Character->GetHealthComponent()->OnHealthChanged.GetAllObjects().Num(), 0);
	TestEqual(TEXT("Old combat delegates are removed"), F.Combat->OnAmmoChanged.GetAllObjects().Num(), 0);
	F.Weapon->SetAmmoInMag(20);
	TestEqual(TEXT("Old pawn cannot update detached UI"), W->DisplayedAmmo, 0);

	FFPSHUDTestAccess::Construct(H);
	FFPSHUDTestAccess::Construct(W);
	TestEqual(TEXT("Reconstruction refreshes current ammo"), W->DisplayedAmmo, 20);
	FFPSHUDTestAccess::Destruct(H);
	FFPSHUDTestAccess::Destruct(W);
	TestEqual(TEXT("Destruct removes health subscription"), F.Character->GetHealthComponent()->OnHealthChanged.GetAllObjects().Num(), 0);
	TestEqual(TEXT("Destruct removes weapon subscription"), F.Combat->OnCurrentWeaponChanged.GetAllObjects().Num(), 0);
	F.HUD->SetObservedCharacter(F.Character);
	F.PC->UnPossess();
	F.HUD->SetObservedCharacter(Cast<AFPSCharacter>(F.PC->GetPawn()));
	TestEqual(TEXT("Unpossession clears persistent HUD"), W->DisplayedAmmo, 0);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFPSCharacter* Respawned = F.World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(), FTransform::Identity, Params);
	F.PC->Possess(Respawned);
	F.HUD->SetObservedCharacter(Respawned);
	TestEqual(TEXT("Respawn reads new health"), H->DisplayedHealth, 100.f);
	TestEqual(TEXT("Old character remains unbound after respawn"), F.Character->GetHealthComponent()->OnHealthChanged.GetAllObjects().Num(), 0);
	W->SetObservedCharacter(F.Character);
	TestEqual(TEXT("Uncontrolled/remote pawn is rejected by weapon HUD"), W->DisplayedAmmo, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHUDAmmoPredictionTest, "FPS.UI.AmmoPrediction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSHUDAmmoPredictionTest::RunTest(const FString& Parameters)
{
	FHUDTestFixture F;
	if (!TestTrue(TEXT("HUD fixture initializes"), F.IsValid())) return false;
	// Exercise the real owning-client view while explicitly driving replicated
	// weapon snapshots and RPC implementations in one isolated native world.
	F.Character->SetRole(ROLE_AutonomousProxy);
	UWeaponWidget* W = F.HUD->GetWeaponWidget();
	FFPSHUDTestAccess::Predict(F.Combat, 1);
	TestEqual(TEXT("Prediction immediately decrements HUD"), W->DisplayedAmmo, 29);
	FFPSHUDTestAccess::ReplicateAmmo(F.Weapon, 29, 2);
	TestEqual(TEXT("Replication before acknowledgement does not double subtract"), W->DisplayedAmmo, 29);
	FFPSHUDTestAccess::Ack(F.Combat, 1, 29, F.Weapon->GetAmmoRevision());
	TestEqual(TEXT("Confirmation does not subtract again"), W->DisplayedAmmo, 29);
	FFPSHUDTestAccess::Predict(F.Combat, 2);
	FFPSHUDTestAccess::Predict(F.Combat, 3);
	FFPSHUDTestAccess::ReplicateAmmo(F.Weapon, 28, 3);
	const uint32 Revision2 = F.Weapon->GetAmmoRevision();
	FFPSHUDTestAccess::ReplicateAmmo(F.Weapon, 27, 4);
	const uint32 Revision3 = F.Weapon->GetAmmoRevision();
	TestEqual(TEXT("Newer raw snapshot with two in-flight shots stays stable"), W->DisplayedAmmo, 27);
	FFPSHUDTestAccess::Ack(F.Combat, 2, 28, Revision2);
	TestEqual(TEXT("Older ordered reply plus remaining prediction stays stable"), W->DisplayedAmmo, 27);
	FFPSHUDTestAccess::Ack(F.Combat, 3, 27, Revision3);
	TestEqual(TEXT("Last confirmation preserves display"), W->DisplayedAmmo, 27);
	FFPSHUDTestAccess::Predict(F.Combat, 4);
	TestEqual(TEXT("Next prediction decrements HUD"), W->DisplayedAmmo, 26);
	FFPSHUDTestAccess::Ack(F.Combat, 4, 27, Revision3, false);
	TestEqual(TEXT("Rejected shot restores ammo"), W->DisplayedAmmo, 27);
	FFPSHUDTestAccess::Predict(F.Combat, 5);
	FFPSHUDTestAccess::Ack(F.Combat, 5, 26, Revision3 + 1);
	TestEqual(TEXT("Reply before raw replication cannot bounce HUD back up"), W->DisplayedAmmo, 26);
	FFPSHUDTestAccess::ReplicateAmmo(F.Weapon, 26, Revision3 + 1);
	TestEqual(TEXT("Delayed matching raw snapshot is deduplicated"), W->DisplayedAmmo, 26);
	FFPSHUDTestAccess::ReplicateAmmo(F.Weapon, 30, Revision3 + 2);
	TestEqual(TEXT("New reload revision raises ammo"), W->DisplayedAmmo, 30);
	FFPSHUDTestAccess::Predict(F.Combat, 6);
	FFPSHUDTestAccess::Ack(F.Combat, 6, 29, Revision3 + 3);
	TestEqual(TEXT("Confirmed shot holds lower value while raw snapshot is still thirty"), W->DisplayedAmmo, 29);
	FFPSHUDTestAccess::ReplicateAmmo(F.Weapon, 30, Revision3 + 4);
	TestEqual(TEXT("Same raw value with newer reload revision must still notify HUD"), W->DisplayedAmmo, 30);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHUDWeaponLifecycleTest, "FPS.UI.WeaponLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSHUDWeaponLifecycleTest::RunTest(const FString& Parameters)
{
	FHUDTestFixture F;
	if (!TestTrue(TEXT("HUD fixture initializes"), F.IsValid())) return false;
	FActorSpawnParameters Params;
	Params.Owner = F.Character;
	AFPSWeapon* Other = F.World->SpawnActor<AFPSWeapon>(AFPSWeapon::StaticClass(), FTransform::Identity, Params);
	Other->SetAmmoInMag(8);
	FFPSHUDTestAccess::Equip(F.Combat, Other);
	UWeaponWidget* W = F.HUD->GetWeaponWidget();
	TestTrue(TEXT("Weapon callback updates identity"), W->DisplayedWeapon == Other);
	TestEqual(TEXT("Weapon callback updates new ammo"), W->DisplayedAmmo, 8);
	F.Weapon->SetAmmoInMag(1);
	TestEqual(TEXT("Unequipped weapon ammo cannot update HUD"), W->DisplayedAmmo, 8);
	Other->SetAmmoInMag(7);
	TestEqual(TEXT("Current weapon ammo still updates HUD"), W->DisplayedAmmo, 7);
	Other->Destroy();
	TestEqual(TEXT("Destroyed weapon clears ammo"), W->DisplayedAmmo, 0);
	TestFalse(TEXT("Destroyed weapon clears identity"), IsValid(W->DisplayedWeapon));
	TestNull(TEXT("Destroyed weapon clears combat reference"), F.Combat->GetCurrentWeapon());
	return true;
}
#endif
