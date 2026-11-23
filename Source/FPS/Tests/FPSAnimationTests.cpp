#include "../FPSHitReactionComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../FPSAnimationPresentation.h"
#include "../FPSCharacter.h"
#include "../FPSCombatComponent.h"
#include "../FPSHealthComponent.h"
#include "../FPSWeapon.h"
#include "../FPSWeaponDataAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Misc/AutomationTest.h"
#include <limits>

struct FFPSAnimationTestAccess
{
	static void Deliver(UFPSHitReactionComponent* C, const FFPSHitReactionCue& Cue) { C->Multicast_HitReaction_Implementation(Cue); }
	static void Data(UFPSCombatComponent* C, UFPSWeaponDataAsset* Data, AFPSWeapon* W) { C->WeaponData=Data; C->CurrentWeapon=W; }
	static const FWeaponMontageSet* Resolve(UFPSCombatComponent* C, AFPSWeapon* W, bool b1P) { return C->ResolveMontages(W,b1P); }
	static void State(UFPSCombatComponent* C, const FFPSCombatState& S) { C->ApplyAuthoritativeCombatState(S); }
	static AFPSWeapon* ReloadWeapon(UFPSCombatComponent* C) { return C->ReloadAnimationWeapon.Get(); }
	static double ReloadEnd(UFPSCombatComponent* C) { return C->ReloadAnimationEndServerTime; }
	static void UpdateReload(UFPSCombatComponent* C) { C->UpdateReloadMontages(); }
	static int32 ReloadAttempts(UFPSCombatComponent* C) { return C->ReloadMontages.Num(); }
};

namespace
{
struct FAnimationFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game,false);
	AFPSCharacter* Character;
	FAnimationFixture()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character=World->SpawnActor<AFPSCharacter>(AFPSCharacter::StaticClass(),FTransform::Identity,P);
	}
	~FAnimationFixture() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	float Point(float Damage, FVector Shot, FName Bone)
	{
		FHitResult Hit; Hit.BoneName=Bone; Hit.ImpactPoint=Character->GetActorLocation();
		return UGameplayStatics::ApplyPointDamage(Character,Damage,Shot,Hit,nullptr,nullptr,UDamageType::StaticClass());
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSDirectionalHitTest,"FPS.Animation.AuthoritativePointDamageDirection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFPSDirectionalHitTest::RunTest(const FString&)
{
	FAnimationFixture F;
	auto* C=F.Character->GetHitReactionComponent();
	F.Character->SetActorRotation(FRotator(0,90,0));
	// Global -Y flies into a victim facing +Y: source is in front of that victim.
	TestEqual(TEXT("Real PointDamage returns actual damage"),F.Point(5.f,FVector(0,-1,0),TEXT("head")),5.f);
	TestTrue(TEXT("Actor yaw transforms source to local front"),C->LastCue.Direction==EFPSHitDirection::Front);
	TestEqual(TEXT("Head front uses configured contract index"),C->LastCue.MontageIndex,4);
	TestTrue(TEXT("Local source X points toward attacker"),C->LastCue.LocalSourceDirection.Equals(FVector2D(1,0),.001));
	F.Point(5.f,-F.Character->GetActorRightVector(),TEXT("upperarm_l"));
	TestEqual(TEXT("Right source hitting left arm uses index 9"),C->LastCue.MontageIndex,9);
	F.Point(5.f,F.Character->GetActorForwardVector(),TEXT("unknown_custom_bone"));
	TestEqual(TEXT("Unknown bone uses torso, rear source index 2"),C->LastCue.MontageIndex,2);
	F.Point(5.f,F.Character->GetActorRightVector(),TEXT("calf_r"));
	TestEqual(TEXT("Left source right leg uses index 23"),C->LastCue.MontageIndex,23);
	TestEqual(TEXT("Only point damage contributes these four cue IDs"),C->LastCue.EventId,4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHitCueGatesTest,"FPS.Animation.ProtectedHealFatalDoNotReact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFPSHitCueGatesTest::RunTest(const FString&)
{
	FAnimationFixture F; auto* C=F.Character->GetHitReactionComponent();
	F.Character->StartSpawnProtection(3.f);
	TestEqual(TEXT("Protected point hit causes no damage"),F.Point(5.f,FVector::ForwardVector,TEXT("head")),0.f);
	TestEqual(TEXT("Protected hit has no reaction"),C->LastCue.EventId,0);
	F.Character->CancelSpawnProtection();
	F.Point(5.f,FVector::ForwardVector,TEXT("head"));
	const int32 First=C->LastCue.EventId;
	F.Character->GetHealthComponent()->Heal(5.f);
	F.Point(0.f,FVector::ForwardVector,TEXT("head"));
	F.Point(-5.f,FVector::ForwardVector,TEXT("head"));
	TestEqual(TEXT("Heal and zero damage do not issue cues"),C->LastCue.EventId,First);
	F.Point(999.f,FVector::ForwardVector,TEXT("head"));
	TestTrue(TEXT("Fatal damage still kills"),F.Character->IsDead());
	TestEqual(TEXT("Death has priority over a normal hit reaction"),C->LastCue.EventId,First);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHitCueLifecycleTest,"FPS.Animation.CueIdentityAgeAndEndPlay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFPSHitCueLifecycleTest::RunTest(const FString&)
{
	FAnimationFixture F; auto* C=F.Character->GetHitReactionComponent();
	F.Character->DispatchBeginPlay();
	FFPSHitReactionCue Cue; Cue.EventId=10; Cue.ActualDamage=5; Cue.MontageIndex=0;
	FFPSAnimationTestAccess::Deliver(C,Cue);
	TestEqual(TEXT("Valid cue accepted despite unconfigured animations"),C->LastCue.EventId,10);
	Cue.Bone=TEXT("different"); FFPSAnimationTestAccess::Deliver(C,Cue);
	TestTrue(TEXT("Duplicate ID cannot replace or replay old cue"),C->LastCue.Bone.IsNone());
	Cue.EventId=9; FFPSAnimationTestAccess::Deliver(C,Cue);
	TestEqual(TEXT("Older ID ignored"),C->LastCue.EventId,10);
	Cue.EventId=11; Cue.ServerTime=-10; FFPSAnimationTestAccess::Deliver(C,Cue);
	TestEqual(TEXT("Expired reaction ignored"),C->LastCue.EventId,10);
	Cue.EventId=12; Cue.ServerTime=0; Cue.MontageIndex=24; FFPSAnimationTestAccess::Deliver(C,Cue);
	TestEqual(TEXT("Invalid index/semantic mismatch ignored"),C->LastCue.EventId,10);
	Cue.MontageIndex=0; Cue.ActualDamage=std::numeric_limits<float>::quiet_NaN(); FFPSAnimationTestAccess::Deliver(C,Cue);
	TestEqual(TEXT("Non-finite damage ignored"),C->LastCue.EventId,10);
	F.Character->Destroy(); // Real Actor EndPlay dispatch, not EndPlay on an unbegun component.
	Cue.ActualDamage=5; Cue.EventId=13; FFPSAnimationTestAccess::Deliver(C,Cue);
	TestEqual(TEXT("Delivery after EndPlay cannot play/update state"),C->LastCue.EventId,10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSWeaponViewDataTest,"FPS.Animation.WeaponViewDataAndReloadIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFPSWeaponViewDataTest::RunTest(const FString&)
{
	FAnimationFixture F; auto* Combat=F.Character->GetCombatComponent();
	auto* W=F.World->SpawnActor<AFPSWeapon>(); auto* Other=F.World->SpawnActor<AFPSWeapon>();
	auto* Data=NewObject<UFPSWeaponDataAsset>();
	FWeaponMontageSet Body,Arms; Body.Fire=NewObject<UAnimMontage>(); Arms.Fire=NewObject<UAnimMontage>();
	Body.ReloadWeaponMontage=NewObject<UAnimMontage>(); Arms.ReloadWeaponMontage=NewObject<UAnimMontage>();
	Data->WeaponMontageSets.Add(W->GetTags(),Body); Data->WeaponMontageSets1P.Add(W->GetTags(),Arms);
	FFPSAnimationTestAccess::Data(Combat,Data,W);
	TestTrue(TEXT("3P uses original body map"),FFPSAnimationTestAccess::Resolve(Combat,W,false)->Fire==Body.Fire);
	TestTrue(TEXT("1P selects distinct arms map"),FFPSAnimationTestAccess::Resolve(Combat,W,true)->Fire==Arms.Fire);
	Data->WeaponMontageSets1P.Reset();
	TestNull(TEXT("Missing arms never fall back to body skeleton"),FFPSAnimationTestAccess::Resolve(Combat,W,true));
	FFPSCombatState S; S.Revision=1; S.bReloading=true; S.ReloadEndServerTime=1.6;
	FFPSAnimationTestAccess::State(Combat,S);
	TestNull(TEXT("Unresolved authority weapon waits without choosing old weapon"),FFPSAnimationTestAccess::ReloadWeapon(Combat));
	S.ReloadWeapon=Other; // Same revision: NetGUID resolves later, not a new reload.
	FFPSAnimationTestAccess::State(Combat,S); FFPSAnimationTestAccess::UpdateReload(Combat);
	TestTrue(TEXT("Reload remembers authority weapon, not old CurrentWeapon"),FFPSAnimationTestAccess::ReloadWeapon(Combat)==Other);
	TestEqual(TEXT("Wrong current weapon does not start reload animation"),FFPSAnimationTestAccess::ReloadAttempts(Combat),0);
	TestEqual(TEXT("Authoritative deadline retained"),FFPSAnimationTestAccess::ReloadEnd(Combat),1.6);
	Combat->InterruptReload();
	FFPSAnimationTestAccess::State(Combat,S);
	TestFalse(TEXT("Same revision repair cannot resurrect a locally cancelled reload"),Combat->IsReloading());
	S.Revision=2; S.bReloading=false; S.ReloadWeapon=nullptr; S.ReloadEndServerTime=0;
	FFPSAnimationTestAccess::State(Combat,S);
	TestFalse(TEXT("Cancellation clears state"),Combat->IsReloading());
	TestNull(TEXT("Cancellation clears old presentation weapon"),FFPSAnimationTestAccess::ReloadWeapon(Combat));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMontagePlaybackTest,"FPS.Animation.RealMontagePlaybackAndScopedStop",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFPSMontagePlaybackTest::RunTest(const FString&)
{
	FAnimationFixture F;
	auto* Mesh=F.Character->GetMesh1P();
	auto* Arms=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FirstPersonArms/Character/Mesh/SK_Mannequin_Arms.SK_Mannequin_Arms"));
	auto* Montage=LoadObject<UAnimMontage>(nullptr,TEXT("/Game/FirstPersonArms/Animations/FP_Rifle_Shoot_Montage.FP_Rifle_Shoot_Montage"));
	if (!TestNotNull(TEXT("Existing arms mesh"),Arms) || !TestNotNull(TEXT("Existing shooting montage"),Montage)) return false;
	Mesh->SetSkeletalMesh(Arms); Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass()); Mesh->InitAnim(true);
	if (!TestNotNull(TEXT("AnimInstance initialized for real montage"),Mesh->GetAnimInstance())) return false;
	TestTrue(TEXT("Real asset starts on its matching skeleton"),FPSAnimation::Play(Mesh,Montage));
	TestTrue(TEXT("Montage instance is playing"),Mesh->GetAnimInstance()->Montage_IsPlaying(Montage));
	TestFalse(TEXT("Invalid playback rate rejected"),FPSAnimation::Play(Mesh,Montage,0.f));
	TestFalse(TEXT("Unready mesh rejected"),FPSAnimation::Play(F.Character->GetMesh3P(),Montage));
	TArray<FFPSCosmeticMontage> Started; Started.Add({Mesh->GetAnimInstance(),Montage});
	FPSAnimation::Stop(Started,0.f);
	TestTrue(TEXT("Owned playback records cleared before callbacks"),Started.IsEmpty());
	TestFalse(TEXT("Recorded montage stopped"),Mesh->GetAnimInstance()->Montage_IsPlaying(Montage));
	return true;
}
#endif
