#include "../UI/ConfirmedDamageWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../FPSCharacter.h"
#include "../FPSCombatComponent.h"
#include "../FPSWeapon.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include <limits>

struct FFPSDamageWidgetTestAccess
{
	static void Construct(UConfirmedDamageWidget* W) { W->NativeConstruct(); }
	static void Destruct(UConfirmedDamageWidget* W) { W->NativeDestruct(); }
	static void Weapon(UFPSCombatComponent* C, AFPSWeapon* W) { C->CurrentWeapon=W; }
	static FFPSShotResult Receipt(UFPSCombatComponent* C)
	{
		FFPSPredictedShot P; P.Sequence=1; P.Weapon=C->CurrentWeapon; P.bHitAnything=P.bHitCharacter=true;
		C->PendingShots.Add(1,P);
		FFPSShotResult R; R.Sequence=1; R.Weapon=C->CurrentWeapon; R.ServerShotIndex=0; R.NextShotIndex=1;
		R.Outcome=EFPSShotOutcome::Damaged; R.ActualDamage=17.5f; R.bHeadshot=true; R.bHit=R.bBlockingHit=true;
		R.RemainingAmmo=29; R.AmmoRevision=1;
		C->Client_ShotResult_Implementation(R);
		return R;
	}
	static void Repeat(UFPSCombatComponent* C, const FFPSShotResult& R) { C->Client_ShotResult_Implementation(R); }
	static uint64 Generation(UConfirmedDamageWidget* W) { return W->DisplayGeneration; }
	static int32 Listeners(UFPSCombatComponent* C) { return C->OnHitConfirmed.GetAllObjects().Num(); }
	static bool TextVisible(UConfirmedDamageWidget* W) { return W->DamageText && W->DamageText->GetVisibility()!=ESlateVisibility::Collapsed; }
};

namespace
{
struct FDamageWidgetFixture
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	APlayerController* PC;
	AFPSCharacter* Character;
	UConfirmedDamageWidget* Widget;
	FDamageWidgetFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		PC=World->SpawnActor<APlayerController>();
		Character=SpawnCharacter(); PC->Possess(Character);
		auto* LP=NewObject<ULocalPlayer>(GEngine); LP->PlayerController=PC; PC->Player=LP; PC->SetAsLocalPlayerController();
		Widget=CreateWidget<UConfirmedDamageWidget>(PC);
		FFPSDamageWidgetTestAccess::Construct(Widget);
	}
	AFPSCharacter* SpawnCharacter()
	{
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(),FTransform::Identity,P);
	}
	~FDamageWidgetFixture()
	{
		FFPSDamageWidgetTestAccess::Destruct(Widget);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
	UFPSCombatComponent* Combat() const { return Character->GetCombatComponent(); }
	void Hit(float Damage) { Combat()->OnHitConfirmed.Broadcast(EFPSShotOutcome::Damaged,false,Damage); }
};

class FDamageWidgetTimerCommand : public IAutomationLatentCommand
{
public:
	FDamageWidgetTimerCommand(FAutomationTestBase* T) : Test(T), F(MakeShared<FDamageWidgetFixture>()) {}
	bool Update() override
	{
		if (Frame==GFrameCounter) return false;
		Frame=GFrameCounter;
		auto* W=F->Widget; auto& Timers=F->World->GetTimerManager();
		switch (Phase++)
		{
		case 0:
			W->DisplaySeconds=.4f; F->Hit(10.f); Timers.Tick(0.f); break;
		case 1:
			Timers.Tick(.2f); F->Hit(25.f); break;
		case 2:
			Timers.Tick(0.f); break; // activate replacement timer
		case 3:
			Timers.Tick(.25f);
			Test->TestTrue(TEXT("Old hide deadline cannot erase latest receipt"),W->bDamageVisible);
			Test->TestEqual(TEXT("Latest damage replaces prior amount, not cumulative"),W->DisplayedDamage,25.f); break;
		case 4:
			Timers.Tick(.2f);
			Test->TestFalse(TEXT("Actual World timer hides after latest deadline"),W->bDamageVisible);
			Test->TestFalse(TEXT("Fallback text collapsed"),FFPSDamageWidgetTestAccess::TextVisible(W));
			F->Hit(30.f); FFPSDamageWidgetTestAccess::Destruct(W); break;
		case 5:
			Timers.Tick(3.f);
			Test->TestFalse(TEXT("Destruct clears display and cancels timer"),W->bDamageVisible);
			Test->TestEqual(TEXT("Destruct unbinds combat"),FFPSDamageWidgetTestAccess::Listeners(F->Combat()),0);
			FFPSDamageWidgetTestAccess::Construct(W); F->Hit(9.f);
			Test->TestEqual(TEXT("Same UObject can reconstruct and subscribe again"),W->DisplayedDamage,9.f);
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	TSharedPtr<FDamageWidgetFixture> F;
	uint64 Frame=MAX_uint64;
	int32 Phase=0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSDamageReceiptWidgetTest,"FPS.UI.ConfirmedDamageReceiptAndRebind",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFPSDamageReceiptWidgetTest::RunTest(const FString&)
{
	FDamageWidgetFixture F;
	FFPSDamageWidgetTestAccess::Construct(F.Widget);
	TestEqual(TEXT("Repeated construct binds once"),FFPSDamageWidgetTestAccess::Listeners(F.Combat()),1);
	TestFalse(TEXT("Initial display is empty"),F.Widget->bDamageVisible);
	FActorSpawnParameters P; P.Owner=F.Character;
	auto* Weapon=F.World->SpawnActor<AFPSWeapon>(AFPSWeapon::StaticClass(),FTransform::Identity,P);
	FFPSDamageWidgetTestAccess::Weapon(F.Combat(),Weapon);
	const auto Receipt=FFPSDamageWidgetTestAccess::Receipt(F.Combat());
	TestEqual(TEXT("Real shot receipt reaches widget via OnHitConfirmed"),F.Widget->DisplayedDamage,17.5f);
	TestTrue(TEXT("Headshot metadata retained"),F.Widget->bLastHeadshot);
	const uint64 Before=FFPSDamageWidgetTestAccess::Generation(F.Widget);
	FFPSDamageWidgetTestAccess::Repeat(F.Combat(),Receipt);
	TestEqual(TEXT("Duplicate ack cannot refresh hide delay"),FFPSDamageWidgetTestAccess::Generation(F.Widget),Before);
	for (auto Outcome : {EFPSShotOutcome::Protected,EFPSShotOutcome::Rejected,EFPSShotOutcome::WorldImpact,EFPSShotOutcome::NoDamage})
		F.Combat()->OnHitConfirmed.Broadcast(Outcome,false,20.f);
	F.Hit(0.f); F.Hit(std::numeric_limits<float>::quiet_NaN());
	TestEqual(TEXT("No-damage/invalid events cannot replace last valid damage"),F.Widget->DisplayedDamage,17.5f);
	auto* Next=F.SpawnCharacter(); F.PC->Possess(Next);
	TestFalse(TEXT("Possession change clears old life display"),F.Widget->bDamageVisible);
	TestEqual(TEXT("Old combat unbound automatically"),FFPSDamageWidgetTestAccess::Listeners(F.Combat()),0);
	F.Hit(50.f); TestFalse(TEXT("Old life cannot feed new widget binding"),F.Widget->bDamageVisible);
	Next->GetCombatComponent()->OnHitConfirmed.Broadcast(EFPSShotOutcome::Killed,true,3.f);
	TestEqual(TEXT("New life is bound without HUD modifications"),F.Widget->DisplayedDamage,3.f);
	TestTrue(TEXT("Killing blow shows actual remaining HP removed"),F.Widget->bLastKilled);
	Next->DispatchBeginPlay(); Next->Destroy();
	TestFalse(TEXT("Character destruction clears display"),F.Widget->bDamageVisible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSDamageWidgetTimerTest,"FPS.UI.ConfirmedDamageTimerLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFPSDamageWidgetTimerTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FDamageWidgetTimerCommand>(this));
	return true;
}
#endif
