#include "../FPSCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../FPSCombatComponent.h"
#include "../FPSHealthComponent.h"
#include "../FPSWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

struct FFPSDeathPresentationTestAccess
{
	static void Inventory(UFPSCombatComponent* C, AFPSWeapon* First, AFPSWeapon* Second)
	{
		C->CurrentWeapon=First; C->Inventory={First,Second};
	}
	static void Retry(AFPSWeapon* W) { W->GetWorldTimerManager().SetTimer(W->SocketRetryHandle,W,&AFPSWeapon::TryAttachToOwner,1.f,false); }
	static bool Pending(AFPSWeapon* W) { return W->GetWorldTimerManager().IsTimerActive(W->SocketRetryHandle); }
	static void LateEquipped(AFPSWeapon* W) { W->bEquipped=true; W->OnRep_Equipped(); }
	static void LateOwner(AFPSWeapon* W) { W->bEquipped=true; W->OnRep_Owner(); }
};

namespace
{
struct FDeathPresentationFixture
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	AFPSCharacter* Character;
	FDeathPresentationFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		// Dynamic Actor delegates and destruction need a World initialized for play;
		// a raw CreateWorld fixture is sufficient only for direct component calls.
		World->InitializeActorsForPlay(FURL());
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character=World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(),FTransform::Identity,P);
		Character->DispatchBeginPlay();
	}
	AFPSWeapon* Weapon()
	{
		FActorSpawnParameters P; P.Owner=Character;
		auto* W=World->SpawnActor<AFPSWeapon>(AFPSWeapon::StaticClass(),FTransform::Identity,P);
		W->SetEquipped(true);
		// This test concerns lifecycle/visibility, not animation socket resources.
		W->GetWeaponMesh1P()->AttachToComponent(Character->GetMesh1P(),FAttachmentTransformRules::KeepRelativeTransform);
		W->GetWeaponMesh3P()->AttachToComponent(Character->GetMesh3P(),FAttachmentTransformRules::KeepRelativeTransform);
		W->GetWeaponMesh1P()->SetHiddenInGame(false); W->GetWeaponMesh3P()->SetHiddenInGame(false);
		W->GetWeaponMesh3P()->SetCastHiddenShadow(true);
		return W;
	}
	~FDeathPresentationFixture() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSDeathMeshTest,"FPS.Presentation.DeathHidesBodyArmsAndInventory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFPSDeathMeshTest::RunTest(const FString&)
{
	FDeathPresentationFixture F;
	auto* First=F.Weapon(); auto* Second=F.Weapon();
	FFPSDeathPresentationTestAccess::Inventory(F.Character->GetCombatComponent(),First,Second);
	TestTrue(TEXT("Fixture actor initialized"),F.Character->IsActorInitialized());
	TestTrue(TEXT("Fixture routed real BeginPlay"),F.Character->HasActorBegunPlay());
	TestTrue(TEXT("Health death delegate subscribed"),F.Character->GetHealthComponent()->OnDeath.IsBound());
	First->SetAmmoInMag(7);
	FFPSDeathPresentationTestAccess::Retry(First);
	TestTrue(TEXT("Attach retry exists before death"),FFPSDeathPresentationTestAccess::Pending(First));
	auto* Child=NewObject<UStaticMeshComponent>(F.Character);
	F.Character->AddInstanceComponent(Child); Child->RegisterComponent();
	Child->AttachToComponent(F.Character->GetMesh3P(),FAttachmentTransformRules::KeepRelativeTransform);
	TestEqual(TEXT("Fatal damage actually applied"),F.Character->GetHealthComponent()->ApplyDamage(999.f,nullptr,nullptr),100.f);
	TestTrue(TEXT("Real fatal health event hides arms"),F.Character->GetMesh1P()->bHiddenInGame);
	TestTrue(TEXT("Other players' body is hidden too"),F.Character->GetMesh3P()->bHiddenInGame);
	TestTrue(TEXT("Attached presentation child inherits death hiding"),Child->bHiddenInGame);
	TestFalse(TEXT("Hidden corpse casts no ghost shadow"),F.Character->GetMesh3P()->bCastHiddenShadow);
	TestTrue(TEXT("Invisible body cannot block traces"),F.Character->GetMesh3P()->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
	for (AFPSWeapon* W : {First,Second})
	{
		TestFalse(TEXT("Inventory weapon unequipped"),W->IsEquipped());
		TestTrue(TEXT("1P weapon hidden"),W->GetWeaponMesh1P()->bHiddenInGame);
		TestTrue(TEXT("3P weapon hidden"),W->GetWeaponMesh3P()->bHiddenInGame);
		TestNull(TEXT("Weapon detached from dying body"),W->GetWeaponMesh3P()->GetAttachParent());
		TestFalse(TEXT("Weapon ghost shadow disabled"),W->GetWeaponMesh3P()->bCastHiddenShadow);
	}
	TestFalse(TEXT("Death cancels attach retry"),FFPSDeathPresentationTestAccess::Pending(First));
	TestFalse(TEXT("Pawn remains alive as an Actor for camera and respawn"),F.Character->IsActorBeingDestroyed());
	TestFalse(TEXT("Actor bHidden is not used to break network relevancy"),F.Character->IsHidden());
	TestEqual(TEXT("Visual cleanup does not change magazine"),First->GetAmmoInMag(),7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSDeathLateAttachTest,"FPS.Presentation.LateAttachAndNewLifeIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFPSDeathLateAttachTest::RunTest(const FString&)
{
	FDeathPresentationFixture F;
	auto* W=F.Weapon();
	FFPSDeathPresentationTestAccess::Inventory(F.Character->GetCombatComponent(),W,nullptr);
	F.Character->GetHealthComponent()->ApplyDamage(999.f,nullptr,nullptr);
	FFPSDeathPresentationTestAccess::LateEquipped(W);
	FFPSDeathPresentationTestAccess::LateOwner(W);
	W->AttachToCharacter(F.Character,NAME_None,NAME_None);
	F.Character->GetCombatComponent()->AttachWeaponToOwner(W);
	W->SetEquipped(true);
	TestTrue(TEXT("All late attach paths keep 1P hidden"),W->GetWeaponMesh1P()->bHiddenInGame);
	TestTrue(TEXT("All late attach paths keep 3P hidden"),W->GetWeaponMesh3P()->bHiddenInGame);
	TestFalse(TEXT("Dead owner cannot re-equip"),W->IsEquipped());
	TestFalse(TEXT("Dead owner cannot schedule another socket retry"),FFPSDeathPresentationTestAccess::Pending(W));
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* NewLife=F.World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(),FTransform::Identity,P);
	TestFalse(TEXT("New life arms do not inherit hidden state"),NewLife->GetMesh1P()->bHiddenInGame);
	TestFalse(TEXT("New life body does not inherit hidden state"),NewLife->GetMesh3P()->bHiddenInGame);
	TestTrue(TEXT("New life retains normal hidden-shadow configuration"),NewLife->GetMesh3P()->bCastHiddenShadow);
	auto* LateCharacter=F.World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(),FTransform::Identity,P);
	auto* LateHealth=LateCharacter->GetHealthComponent();
	// Model a component receiving its initial dead state before the character
	// subscribes. Component BeginPlay first prevents authority health reseeding.
	LateHealth->RegisterAllComponentTickFunctions(true);
	LateHealth->BeginPlay();
	LateHealth->ApplyDamage(999.f,nullptr,nullptr);
	TestFalse(TEXT("Initial-death fixture has not begun character play"),LateCharacter->HasActorBegunPlay());
	TestTrue(TEXT("Persistent initial dead state exists"),LateCharacter->IsDead());
	LateCharacter->DispatchBeginPlay();
	TestTrue(TEXT("BeginPlay seeds hidden body from an already-dead health state"),LateCharacter->GetMesh3P()->bHiddenInGame);
	TestTrue(TEXT("BeginPlay seeds hidden arms too"),LateCharacter->GetMesh1P()->bHiddenInGame);
	auto* Unresolved=F.World->SpawnActor<AFPSWeapon>();
	FFPSDeathPresentationTestAccess::LateEquipped(Unresolved);
	TestTrue(TEXT("Unresolved owner preserves equipped state for later replication"),Unresolved->IsEquipped());
	TestTrue(TEXT("Unresolved owner weapon stays hidden meanwhile"),Unresolved->GetWeaponMesh3P()->bHiddenInGame);
	W->DispatchBeginPlay(); FFPSDeathPresentationTestAccess::Retry(W); W->Destroy();
	TestFalse(TEXT("Weapon EndPlay clears retry explicitly"),FFPSDeathPresentationTestAccess::Pending(W));
	return true;
}
#endif
