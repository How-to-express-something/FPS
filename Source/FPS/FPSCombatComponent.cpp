// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSCombatComponent.h"

#include "FPS.h"
#include "FPSCharacter.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "FPSGameState.h"
#include "FPSRecoilCameraShake.h"
#include "FPSLagCompensationSubsystem.h"
#include "FPSWeapon.h"
#include "FPSWeaponDataAsset.h"
#include "FPSHealthComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/DecalComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarNetTrace(TEXT("fps.Net.Trace"), 0,
		TEXT("1 logs each local prediction, server verdict and owning-client acknowledgement."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarNetTracePhase(TEXT("fps.Net.Phase"), 0,
		TEXT("Local diagnostic stage label (e.g. 1=baseline, 2=lag, 3=recovery)."), ECVF_Default);

	FString NetTraceContext(const UFPSCombatComponent* Combat)
	{
		const AActor* Owner = Combat->GetOwner();
		const APawn* Pawn = Cast<APawn>(Owner);
		const APlayerState* State = Pawn ? Pawn->GetPlayerState() : nullptr;
		const UWorld* World = Combat->GetWorld();
		const ENetMode Mode = World ? World->GetNetMode() : NM_Standalone;
		const TCHAR* ModeName = Mode == NM_Client ? TEXT("Client") : Mode == NM_ListenServer ? TEXT("ListenServer")
			: Mode == NM_DedicatedServer ? TEXT("DedicatedServer") : TEXT("Standalone");
		return FString::Printf(TEXT("mode=%s world=%s player=%d owner=%s authority=%d"), ModeName,
			*GetNameSafe(World), State ? State->GetPlayerId() : -1, *GetNameSafe(Owner), Owner && Owner->HasAuthority() ? 1 : 0);
	}

#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<UWorld> BadOriginTestWorld;
	FAutoConsoleCommandWithWorld BadOriginTestCommand(TEXT("fps.Net.BadOriginOnce"),
		TEXT("Development test: offset only the next remote client's submitted origin; predict normally. Server should reject BadOrigin."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!World || World->GetNetMode() != NM_Client)
			{
				UE_LOG(LogFPSCombat, Warning, TEXT("[FPSNet][Probe] Run fps.Net.BadOriginOnce in a remote CLIENT game console."));
				return;
			}
			BadOriginTestWorld = World;
			UE_LOG(LogFPSCombat, Log, TEXT("[FPSNet][Probe] armed world=%s; next shot request only will use an invalid origin."), *GetNameSafe(World));
		}));
#endif

	constexpr int32 MaxPendingPredictions = 32;

	/** Client/server disagreement tolerance for a predicted hit point. */
	constexpr float PredictionLocationToleranceSq = 75.f * 75.f;

	/** Origin validation radius on the server, in centimetres. */
	constexpr float MaxOriginErrorSq = 200.f * 200.f;

	/** Shot gun token bucket capacity. Tolerates reliable RPC bunching. */
	constexpr float ServerFireTokenCapacity = 3.f;
}

UFPSCombatComponent::UFPSCombatComponent()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> Body(TEXT("/Game/FPS/Audio/S_HitBody.S_HitBody"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Head(TEXT("/Game/FPS/Audio/S_HitHead.S_HitHead"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Kill(TEXT("/Game/FPS/Audio/S_HitKill.S_HitKill"));
	BodyHitSound = Body.Object;
	HeadHitSound = Head.Object;
	KillHitSound = Kill.Object;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
	ShotCameraShakeClass = UFPSRecoilCameraShake::StaticClass();
}

void UFPSCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFPSCombatComponent, CurrentWeapon);
	DOREPLIFETIME(UFPSCombatComponent, CombatState);
	DOREPLIFETIME_CONDITION(UFPSCombatComponent, ShotSeed, COND_OwnerOnly);
}

void UFPSCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		InitializeShotSeed();
		SpawnInventory();
	}
	if (!FPSRecoil::ValidatePattern(RecoilPattern))
	{
		UE_LOG(LogFPSCombat, Error, TEXT("Invalid recoil pattern on '%s': need finite points 0..BaseShotCount+5."),
			*GetNameSafe(GetOwner()));
	}
}

void UFPSCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	FPSAnimation::Stop(FireMontages, 0.f);
	EndFireBurst();
	StopShotCameraShake(true);
	CancelReloadInternal();
	AFPSWeapon* OldWeapon = CurrentWeapon;
	CurrentWeapon = nullptr;
	NotifyCurrentWeaponChanged(OldWeapon);

	// Weapons are separate actors owned by the pawn; destroying the pawn does
	// not destroy them, so do it here on the authority.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		for (AFPSWeapon* Weapon : Inventory)
		{
			if (IsValid(Weapon))
			{
				Weapon->Destroy();
			}
		}
	}
	Inventory.Reset();
	PendingShots.Reset();

	Super::EndPlay(EndPlayReason);
}

void UFPSCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bEndingPlay) return;
	if (bReloading) UpdateReloadMontages();
	if (const AFPSGameState* GS = GetWorld()->GetGameState<AFPSGameState>();
		GS && GS->HasFinished() && (bWantsFire || bIsFiring || bIsAiming || bReloading)) StopForMatchEnd();

	if (IsLocallyControlledOwner())
	{
		const float Speed = bWantsFire ? CameraRecoilFollowSpeed : CameraRecoilReturnSpeed;
		CameraRecoil = FPSRecoil::Interpolate(CameraRecoil, CameraRecoilTarget, Speed, DeltaTime);
	}
	if (!IsLocallyControlledOwner() || !bEnableShotCameraShake) StopShotCameraShake(true);
}

ACharacter* UFPSCombatComponent::GetOwningCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

bool UFPSCombatComponent::IsLocallyControlledOwner() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled();
}

void UFPSCombatComponent::SetAimingState(bool bAiming)
{
	if (bIsAiming == bAiming) return;
	bIsAiming = bAiming;
	if (IsLocallyControlledOwner())
	{
		if (AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner()))
		{
			Character->SetAiming(bAiming);
			Character->OnAim(bAiming);
		}
	}
}

void UFPSCombatComponent::SetReloadingState(bool bNewReloading, AFPSWeapon* AnimationWeapon, double EndServerTime)
{
	const bool bStateChanged = bReloading != bNewReloading;
	const bool bContextChanged = ReloadAnimationWeapon != AnimationWeapon;
	// Commit first: montage stop delegates may re-enter combat, die or travel.
	bReloading = bNewReloading;
	ReloadAnimationWeapon = bNewReloading ? AnimationWeapon : nullptr;
	ReloadAnimationEndServerTime = bNewReloading ? EndServerTime : 0.;
	const uint32 Generation = ++ReloadPresentationGeneration;
	if (!bNewReloading || bContextChanged)
	{
		FPSAnimation::Stop(ReloadMontages);
	}
	if (Generation != ReloadPresentationGeneration) return;
	if (bReloading && bStateChanged) FPSAnimation::Stop(FireMontages);
	if (Generation == ReloadPresentationGeneration && bStateChanged) OnReloadStateChanged.Broadcast(bReloading);
}

const FWeaponMontageSet* UFPSCombatComponent::ResolveMontages(const AFPSWeapon* Weapon, bool bFirstPerson) const
{
	if (!WeaponData || !IsValid(Weapon)) return nullptr;
	return (bFirstPerson ? WeaponData->WeaponMontageSets1P : WeaponData->WeaponMontageSets).Find(Weapon->GetTags());
}

void UFPSCombatComponent::PlayFireMontages(AFPSWeapon* Weapon)
{
	AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	if (bEndingPlay || bReloading || !Character || Character->IsDead() || !GetWorld() ||
		!IsValid(Weapon) || Weapon != CurrentWeapon) return;
	FPSAnimation::Stop(FireMontages, .03f);
	if (bEndingPlay || bReloading || Character->IsDead() || Weapon != CurrentWeapon) return;
	auto Play = [this](USkeletalMeshComponent* Mesh, UAnimMontage* Montage)
	{
		if (!Mesh || !Mesh->GetAnimInstance() || !Montage) return;
		FireMontages.Add({Mesh->GetAnimInstance(), Montage});
		FPSAnimation::Play(Mesh, Montage);
	};
	if (const auto* Set = ResolveMontages(Weapon, false))
	{
		Play(Character->GetMesh3P(), Set->Fire);
		if (bEndingPlay || Character->IsDead() || Weapon != CurrentWeapon) return;
		if (GetWorld()->GetNetMode() != NM_DedicatedServer) Play(Weapon->GetWeaponMesh3P(), Set->FireWeaponMontage);
	}
	if (GetWorld()->GetNetMode() == NM_DedicatedServer || !IsLocallyControlledOwner() || bEndingPlay || Character->IsDead() || Weapon != CurrentWeapon) return;
	if (const auto* Set = ResolveMontages(Weapon, true))
	{
		Play(Character->GetMesh1P(), Set->Fire);
		if (bEndingPlay || Character->IsDead() || Weapon != CurrentWeapon) return;
		Play(Weapon->GetWeaponMesh1P(), Set->FireWeaponMontage);
	}
}

void UFPSCombatComponent::UpdateReloadMontages()
{
	AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	AFPSWeapon* Weapon = ReloadAnimationWeapon.Get();
	if (bEndingPlay || !bReloading || !Character || Character->IsDead() || !GetWorld() ||
		!IsValid(Weapon) || Weapon != CurrentWeapon) return;
	FWeaponCombatSettings Settings;
	if (!ResolveCombatSettings(Weapon, Settings)) return;
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const double Remaining = ReloadAnimationEndServerTime - Now;
	if (Remaining <= 0.) return;
	const uint32 Generation = ReloadPresentationGeneration;
	auto Play = [this, &Settings, Remaining, Generation, Weapon](USkeletalMeshComponent* Mesh, UAnimMontage* Montage)
	{
		if (bEndingPlay || !bReloading || Generation != ReloadPresentationGeneration || Weapon != CurrentWeapon ||
			!Mesh || !Mesh->GetAnimInstance() || !Montage) return;
		UAnimInstance* Anim = Mesh->GetAnimInstance();
		if (ReloadMontages.ContainsByPredicate([Anim, Montage](const auto& Entry)
			{ return Entry.AnimInstance == Anim && Entry.Montage == Montage; })) return;
		// Record even a failed attempt: missing slots/root motion must not retry every frame.
		ReloadMontages.Add({Anim, Montage});
		const float Rate = Montage->GetPlayLength() / Settings.ReloadTime;
		const float Position = FMath::Max(0.f, float(Settings.ReloadTime - Remaining)) * Rate;
		FPSAnimation::Play(Mesh, Montage, Rate, Position);
	};
	if (const auto* Set = ResolveMontages(Weapon, false))
	{
		Play(Character->GetMesh3P(), Set->Reload);
		if (GetWorld()->GetNetMode() != NM_DedicatedServer) Play(Weapon->GetWeaponMesh3P(), Set->ReloadWeaponMontage);
	}
	if (GetWorld()->GetNetMode() != NM_DedicatedServer && IsLocallyControlledOwner()) if (const auto* Set = ResolveMontages(Weapon, true))
	{
		Play(Character->GetMesh1P(), Set->Reload);
		Play(Weapon->GetWeaponMesh1P(), Set->ReloadWeaponMontage);
	}
}

void UFPSCombatComponent::StopFireAndAimForReload()
{
	EndFireBurst();
	SetAimingState(false);
}

void UFPSCombatComponent::EndFireBurst()
{
	bWantsFire = bIsFiring = false;
	CameraRecoilTarget = FVector2D::ZeroVector;
	StopShotCameraShake(false);
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(FireTimerHandle);
}

void UFPSCombatComponent::StopShotCameraShake(bool bImmediately)
{
	if (APlayerCameraManager* Manager = ShotShakeCameraManager.Get())
	{
		if (UCameraShakeBase* Shake = ActiveShotCameraShake.Get()) Manager->StopCameraShake(Shake, bImmediately);
	}
	if (bImmediately)
	{
		ActiveShotCameraShake.Reset();
		ShotShakeCameraManager.Reset();
	}
}

void UFPSCombatComponent::PlayShotCameraShake()
{
	UWorld* World = GetWorld();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer || !IsLocallyControlledOwner() ||
		!PC || !PC->IsLocalController() || !PC->PlayerCameraManager || !bEnableShotCameraShake || !ShotCameraShakeClass) return;
	float Scale = FMath::IsFinite(ShotCameraShakeScale) ? FMath::Clamp(ShotCameraShakeScale, 0.f, 3.f) : 0.f;
	if (bIsAiming) Scale *= FMath::IsFinite(AimingCameraShakeMultiplier) ? FMath::Clamp(AimingCameraShakeMultiplier, 0.f, 1.f) : 0.f;
	if (Scale <= 0.f) return;
	// Own at most one impulse, even when a custom Blueprint disables SingleInstance.
	StopShotCameraShake(true);
	ShotShakeCameraManager = PC->PlayerCameraManager;
	ActiveShotCameraShake = PC->PlayerCameraManager->StartCameraShake(ShotCameraShakeClass, Scale, ECameraShakePlaySpace::CameraLocal);
}

void UFPSCombatComponent::InitializeShotSeed()
{
	if (GetOwner() && GetOwner()->HasAuthority() && ShotSeed == 0)
	{
		ShotSeed = FMath::RandRange(1, MAX_int32);
		GetOwner()->ForceNetUpdate();
	}
}

void UFPSCombatComponent::OnRep_ShotSeed()
{
	if (bWantsFire && IsLocallyControlledOwner() && !bReloading) FireOnce();
}

void UFPSCombatComponent::ScheduleNextShot()
{
	UWorld* World = GetWorld();
	FWeaponCombatSettings Settings;
	if (!World || !bWantsFire || bReloading || !IsLocallyControlledOwner() ||
		!ResolveCombatSettings(CurrentWeapon, Settings) || !Settings.bAutomatic) return;
	// A one-shot timer avoids catch-up volleys when the game thread stalls.
	const float Delay = FMath::Max(NextLocalFireTime - World->GetTimeSeconds(), 0.001f);
	World->GetTimerManager().SetTimer(FireTimerHandle, this, &UFPSCombatComponent::FireOnce, Delay, false);
}

void UFPSCombatComponent::CancelReloadInternal()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	}
	++ReloadGeneration;
	ReloadWeapon.Reset();
	ReloadMagSize = 0;
	SetReloadingState(false);
}

void UFPSCombatComponent::SubmitCombatInput(EFPSCombatInput Input, int32 WeaponIndex)
{
	if (bEndingPlay || !GetOwner()) return;
	if (GetOwner()->HasAuthority())
	{
		ApplyCombatInput(Input, WeaponIndex, true, ++LocalInputSequence);
		PublishCombatState();
	}
	else if (IsLocallyControlledOwner())
	{
		bHasLocalCombatInputs = true;
		++LocalInputSequence;
		ApplyCombatInput(Input, WeaponIndex, false, LocalInputSequence);
		Server_CombatInput(Input, LocalInputSequence, WeaponIndex);
	}
}

void UFPSCombatComponent::ApplyCombatInput(EFPSCombatInput Input, int32 WeaponIndex, bool bAuthority, uint32 InputId)
{
	if (bEndingPlay) return;
	// Releases clear intent without interrupting a reload started while held.
	if (Input == EFPSCombatInput::StopFire)
	{
		EndFireBurst();
		return;
	}
	if (Input == EFPSCombatInput::StopAim)
	{
		SetAimingState(false);
		return;
	}

	AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	if (!Character || Character->IsDead()) return;
	if (const AFPSGameState* GS = GetWorld()->GetGameState<AFPSGameState>(); GS && GS->HasFinished()) return;
	if (Input != EFPSCombatInput::Reload) CancelReloadInternal();

	switch (Input)
	{
	case EFPSCombatInput::StartFire:
		if (!CurrentWeapon) break;
		if (bAuthority) Character->CancelSpawnProtection();
		// Duplicate Started events while held must not reset the authoritative track.
		if (bIsFiring) break;
		ActiveFireBurstId = InputId;
		NextLocalBurstShotIndex = 0;
		if (bAuthority) ServerNextBurstShotIndex = 0;
		bIsFiring = true;
		bWantsFire = IsLocallyControlledOwner();
		// An empty trigger restarts reload exactly once, even during shot cooldown.
		if ((bAuthority ? CurrentWeapon->GetAmmoInMag() : GetDisplayAmmo()) > 0) break;
		// fall through: automatic reload uses the same rules as the reload key.
		[[fallthrough]];
	case EFPSCombatInput::Reload:
	{
		StopFireAndAimForReload();
		FWeaponCombatSettings Settings;
		if (!CurrentWeapon || bReloading || !ResolveCombatSettings(CurrentWeapon, Settings)) break;
		const int32 Ammo = bAuthority ? CurrentWeapon->GetAmmoInMag() : GetDisplayAmmo();
		if (Ammo >= Settings.MagSize) break;
		if (bAuthority) StartReloadTimer(Settings);
		else
		{
			const AGameStateBase* GS = GetWorld()->GetGameState();
			SetReloadingState(true, CurrentWeapon,
				(GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds()) + Settings.ReloadTime);
		}
		break;
	}
	case EFPSCombatInput::StartAim:
		SetAimingState(CurrentWeapon != nullptr);
		break;
	case EFPSCombatInput::SwitchWeapon:
		EndFireBurst();
		if (bAuthority) ApplyWeaponSwitch(WeaponIndex);
		break;
	default:
		break;
	}
}

void UFPSCombatComponent::Server_CombatInput_Implementation(
	EFPSCombatInput Input, uint32 InputSequence, int32 WeaponIndex)
{
	// Reliable RPCs on this component preserve input order, including shot RPCs.
	if (Input < EFPSCombatInput::Count && InputSequence == CombatState.LastProcessedInput + 1u)
	{
		CombatState.LastProcessedInput = InputSequence;
		ApplyCombatInput(Input, WeaponIndex, true, InputSequence);
	}
	PublishCombatState();
}

void UFPSCombatComponent::PublishCombatState()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	CombatState.bReloading = bReloading;
	CombatState.bAiming = bIsAiming;
	CombatState.bFiring = bIsFiring;
	CombatState.ReloadGeneration = ReloadGeneration;
	CombatState.ReloadWeapon = bReloading ? ReloadWeapon.Get() : nullptr;
	CombatState.ReloadEndServerTime = ReloadAnimationEndServerTime;
	++CombatState.Revision;
	GetOwner()->ForceNetUpdate();
	Client_CombatState(CombatState);
}

void UFPSCombatComponent::Client_CombatState_Implementation(const FFPSCombatState& State)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) ApplyAuthoritativeCombatState(State);
}

void UFPSCombatComponent::OnRep_CombatState()
{
	ApplyAuthoritativeCombatState(CombatState);
}

void UFPSCombatComponent::ApplyAuthoritativeCombatState(const FFPSCombatState& State)
{
	// Compare with a half-range window so uint32 wrap remains well defined.
	const uint32 RevisionDelta = State.Revision - LastReceivedStateRevision;
	if (bHasReceivedCombatState && RevisionDelta == 0)
	{
		// Object references can resolve after the state revision was first received.
		// Repair presentation context only: never replay input or resurrect a cancelled reload.
		if (bReloading && State.bReloading && ViewedReloadGeneration == State.ReloadGeneration &&
			(!bHasLocalCombatInputs || State.LastProcessedInput == LocalInputSequence) &&
			IsValid(State.ReloadWeapon) && ReloadAnimationWeapon != State.ReloadWeapon.Get())
			SetReloadingState(true, State.ReloadWeapon, State.ReloadEndServerTime);
		return;
	}
	if (bHasReceivedCombatState && (RevisionDelta == 0 || RevisionDelta >= 0x80000000u)) return;
	bHasReceivedCombatState = true;
	LastReceivedStateRevision = State.Revision;
	// Do not let an older acknowledgement undo a newer locally predicted input.
	if (bHasLocalCombatInputs && State.LastProcessedInput != LocalInputSequence) return;

	if (const AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner()); Character && Character->IsDead())
	{
		StopFireAndAimForReload();
		SetReloadingState(false);
		return;
	}
	if (!bHasLocalCombatInputs && State.bReloading && bReloading &&
		ViewedReloadGeneration != State.ReloadGeneration)
	{
		SetReloadingState(false);
	}
	ViewedReloadGeneration = State.ReloadGeneration;
	bIsFiring = State.bFiring;
	if (!bIsFiring) EndFireBurst();
	SetAimingState(State.bAiming);
	if (State.bReloading) StopFireAndAimForReload();
	SetReloadingState(State.bReloading, State.ReloadWeapon, State.ReloadEndServerTime);
}

//////////////////////////////////////////////////////////////////////////
// Attachment

FName UFPSCombatComponent::ResolveAttachSocket1P(const AFPSWeapon* Weapon) const
{
	if (WeaponData && Weapon)
	{
		if (const FName* Socket = WeaponData->WeaponAttachSockets1P.Find(Weapon->GetTags()))
		{
			return *Socket;
		}

		// No first-person socket configured: reuse the third-person one, since both
		// skeletons usually name the grip the same way.
		if (const FName* Socket = WeaponData->WeaponAttachSockets3P.Find(Weapon->GetTags()))
		{
			return *Socket;
		}
	}

	return WeaponAttachSocket1P;
}

FName UFPSCombatComponent::ResolveAttachSocket3P(const AFPSWeapon* Weapon) const
{
	if (WeaponData && Weapon)
	{
		if (const FName* Socket = WeaponData->WeaponAttachSockets3P.Find(Weapon->GetTags()))
		{
			return *Socket;
		}
	}

	return WeaponAttachSocket3P;
}

void UFPSCombatComponent::AttachWeaponToOwner(AFPSWeapon* Weapon)
{
	ACharacter* Character = GetOwningCharacter();
	if (!Character || !Weapon)
	{
		return;
	}
	if (const AFPSCharacter* FPSCharacter = Cast<AFPSCharacter>(Character);
		FPSCharacter && (FPSCharacter->IsDead() || FPSCharacter->IsActorBeingDestroyed()))
	{
		Weapon->HideForOwnerDeath();
		return;
	}

	// Drives the replicated flag the weapon uses to attach itself on clients.
	Weapon->SetEquipped(true);

	Weapon->AttachToCharacter(Character, ResolveAttachSocket1P(Weapon), ResolveAttachSocket3P(Weapon));
}

//////////////////////////////////////////////////////////////////////////
// Inventory

void UFPSCombatComponent::SpawnInventory()
{
	ACharacter* Character = GetOwningCharacter();
	UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Character;
	SpawnParams.Instigator = Character;

	auto SpawnWeapon = [&](TSubclassOf<AFPSWeapon> WeaponClass) -> AFPSWeapon*
	{
		if (!WeaponClass)
		{
			return nullptr;
		}

		AFPSWeapon* Weapon = World->SpawnActor<AFPSWeapon>(WeaponClass, FTransform::Identity, SpawnParams);
		if (Weapon)
		{
			// Seed the authoritative magazine from the data asset.
			FWeaponCombatSettings Settings;
			if (ResolveCombatSettings(Weapon, Settings))
			{
				Weapon->SetAmmoInMag(Settings.MagSize);
			}
			Inventory.Add(Weapon);
		}
		return Weapon;
	};

	SpawnWeapon(DefaultPistolClass);
	AFPSWeapon* Rifle  = SpawnWeapon(DefaultRifleClass);

	// The rifle is equipped by default.
	if (Rifle)
	{
		AFPSWeapon* OldWeapon = CurrentWeapon;
		CurrentWeapon = Rifle;
		AttachWeaponToOwner(Rifle);
		NotifyCurrentWeaponChanged(OldWeapon);
	}
}

void UFPSCombatComponent::SwitchWeapon(int32 WeaponIndex)
{
	SubmitCombatInput(EFPSCombatInput::SwitchWeapon, WeaponIndex);
}

void UFPSCombatComponent::ApplyWeaponSwitch(int32 WeaponIndex)
{
	if (!Inventory.IsValidIndex(WeaponIndex))
	{
		return;
	}

	AFPSWeapon* NewWeapon = Inventory[WeaponIndex];
	if (!IsValid(NewWeapon) || NewWeapon == CurrentWeapon)
	{
		return;
	}

	if (CurrentWeapon)
	{
		CurrentWeapon->SetEquipped(false);
		CurrentWeapon->DetachFromCharacter();
	}

	// OnRep does not fire on the authority, so apply the swap locally here.
	AFPSWeapon* OldWeapon = CurrentWeapon;
	CurrentWeapon = NewWeapon;
	AttachWeaponToOwner(NewWeapon);

	// Predictions still in flight belong to the old weapon and will be
	// rejected by its tag; drop them locally.
	PendingShots.Reset();
	NextLocalFireTime = 0.f;
	PredictedAmmo = NewWeapon->GetAmmoInMag();

	// Re-anchor the ADS camera on the new weapon without dropping out of aim.
	if (AFPSCharacter* FPSCharacter = Cast<AFPSCharacter>(GetOwningCharacter()))
	{
		FPSCharacter->NotifyWeaponEquipped(NewWeapon);
	}
	NotifyCurrentWeaponChanged(OldWeapon);
}

void UFPSCombatComponent::OnRep_CurrentWeapon(AFPSWeapon* OldWeapon)
{
	EndFireBurst();
	if (OldWeapon && OldWeapon != CurrentWeapon)
	{
		OldWeapon->SetEquipped(false);
		OldWeapon->DetachFromCharacter();
	}

	AttachWeaponToOwner(CurrentWeapon);

	PendingShots.Reset();
	PredictedAmmo = CurrentWeapon ? CurrentWeapon->GetAmmoInMag() : 0;

	if (AFPSCharacter* FPSCharacter = Cast<AFPSCharacter>(GetOwningCharacter()))
	{
		FPSCharacter->NotifyWeaponEquipped(CurrentWeapon);
	}
	NotifyCurrentWeaponChanged(OldWeapon);
}

void UFPSCombatComponent::NotifyCurrentWeaponChanged(AFPSWeapon* OldWeapon)
{
	if (OldWeapon != CurrentWeapon)
	{
		FPSAnimation::Stop(FireMontages);
		FPSAnimation::Stop(ReloadMontages);
		++ReloadPresentationGeneration;
	}
	if (OldWeapon)
	{
		OldWeapon->OnAmmoInMagChanged.RemoveDynamic(this, &UFPSCombatComponent::HandleWeaponAmmoChanged);
		OldWeapon->OnDestroyed.RemoveDynamic(this, &UFPSCombatComponent::HandleCurrentWeaponDestroyed);
	}
	if (CurrentWeapon)
	{
		CurrentWeapon->OnAmmoInMagChanged.AddUniqueDynamic(this, &UFPSCombatComponent::HandleWeaponAmmoChanged);
		CurrentWeapon->OnDestroyed.AddUniqueDynamic(this, &UFPSCombatComponent::HandleCurrentWeaponDestroyed);
	}
	AmmoBaselineWeapon = CurrentWeapon;
	bHasAmmoBaseline = false;
	CacheWeaponAmmoIfIdle();
	if (OldWeapon != CurrentWeapon) OnCurrentWeaponChanged.Broadcast(CurrentWeapon, OldWeapon);
	NotifyDisplayAmmoChanged(true);
}

void UFPSCombatComponent::HandleCurrentWeaponDestroyed(AActor* DestroyedWeapon)
{
	if (CurrentWeapon != DestroyedWeapon) return;
	AFPSWeapon* OldWeapon = CurrentWeapon;
	CurrentWeapon = nullptr;
	EndFireBurst();
	CancelReloadInternal();
	PendingShots.Reset();
	NotifyCurrentWeaponChanged(OldWeapon);
}

void UFPSCombatComponent::AcceptAmmoBaseline(int32 Ammo, uint32 Revision)
{
	const uint32 Delta = Revision - ConfirmedAmmoRevision;
	if (!bHasAmmoBaseline || AmmoBaselineWeapon != CurrentWeapon || Delta == 0 || Delta < 0x80000000u)
	{
		AmmoBaselineWeapon = CurrentWeapon;
		ConfirmedDisplayAmmo = FMath::Max(Ammo, 0);
		ConfirmedAmmoRevision = Revision;
		bHasAmmoBaseline = true;
	}
}

void UFPSCombatComponent::CacheWeaponAmmoIfIdle()
{
	if (!CurrentWeapon) return;
	// A replicated ammo snapshot can already include unacknowledged shots. While
	// any are pending, only ordered shot replies advance the prediction baseline.
	if (PendingShots.IsEmpty() || !bHasAmmoBaseline || AmmoBaselineWeapon != CurrentWeapon)
	{
		AcceptAmmoBaseline(CurrentWeapon->GetAmmoInMag(), CurrentWeapon->GetAmmoRevision());
	}
}

void UFPSCombatComponent::HandleWeaponAmmoChanged(AFPSWeapon* Weapon, int32 CurrentAmmo, int32 PreviousAmmo)
{
	if (Weapon != CurrentWeapon) return;
	CacheWeaponAmmoIfIdle();
	NotifyDisplayAmmoChanged();
}

void UFPSCombatComponent::NotifyDisplayAmmoChanged(bool bForce)
{
	AFPSWeapon* Weapon = CurrentWeapon;
	const int32 Ammo = GetDisplayAmmo();
	const bool bSameWeapon = bHasAmmoNotification && AmmoNotificationWeapon == TWeakObjectPtr<AFPSWeapon>(Weapon);
	if (!bForce && bSameWeapon && Ammo == LastNotifiedAmmo) return;
	const int32 Previous = bSameWeapon ? LastNotifiedAmmo : Ammo;
	AmmoNotificationWeapon = Weapon;
	LastNotifiedAmmo = Ammo;
	bHasAmmoNotification = true;
	OnAmmoChanged.Broadcast(Weapon, Ammo, Previous);
	if (bSameWeapon && Ammo < Previous) OnAmmoDecreased.Broadcast(Weapon, Ammo, Previous);
}

//////////////////////////////////////////////////////////////////////////
// Settings

bool UFPSCombatComponent::ResolveAimSettings(const AFPSWeapon* Weapon, FWeaponAimSettings& OutSettings) const
{
	if (!Weapon)
	{
		return false;
	}

	OutSettings = DefaultAimSettings;

	if (WeaponData)
	{
		if (const FWeaponAimSettings* Found = WeaponData->WeaponAimSettings.Find(Weapon->GetTags()))
		{
			OutSettings = *Found;
		}
	}

	// Fill anything the designer left empty.
	if (OutSettings.RearSightSocket.IsNone())
	{
		OutSettings.RearSightSocket = TEXT("SightRear");
	}
	if (OutSettings.FrontSightSocket.IsNone())
	{
		OutSettings.FrontSightSocket = TEXT("SightFront");
	}
	if (OutSettings.AimTime <= 0.f)
	{
		OutSettings.AimTime = 0.22f;
	}
	if (OutSettings.AimFOV <= 0.f)
	{
		OutSettings.AimFOV = GetFovByWeaponZoomRatio(Weapon);
	}

	return true;
}

bool UFPSCombatComponent::ResolveCombatSettings(const AFPSWeapon* Weapon,
	FWeaponCombatSettings& OutSettings) const
{
	if (!Weapon)
	{
		return false;
	}

	OutSettings = DefaultCombatSettings;

	if (WeaponData)
	{
		if (const FWeaponCombatSettings* Found = WeaponData->WeaponCombatSettings.Find(Weapon->GetTags()))
		{
			OutSettings = *Found;
		}
	}

	// Sanitize so a misconfigured asset cannot divide by zero or produce a
	// zero-length trace.
	OutSettings.RoundsPerMinute = FMath::IsFinite(OutSettings.RoundsPerMinute)
		? FMath::Clamp(OutSettings.RoundsPerMinute, 1.f, 3000.f) : 600.f;
	OutSettings.Spread = FMath::IsFinite(OutSettings.Spread) ? FMath::Clamp(OutSettings.Spread, 0.f, 1.f) : 0.f;
	// Signed health damage supports explicit healing; a bad weapon asset must not heal enemies.
	OutSettings.Damage = FMath::IsFinite(OutSettings.Damage) ? FMath::Max(0.f, OutSettings.Damage) : 0.f;
	OutSettings.HeadshotMultiplier = FMath::IsFinite(OutSettings.HeadshotMultiplier) ? FMath::Max(0.f, OutSettings.HeadshotMultiplier) : 0.f;
	OutSettings.SpreadRampShots = FMath::Clamp(OutSettings.SpreadRampShots, 1, 128);
	OutSettings.SpreadRampExponent = FMath::IsFinite(OutSettings.SpreadRampExponent)
		? FMath::Clamp(OutSettings.SpreadRampExponent, .1f, 8.f) : 1.f;
	OutSettings.MagSize = FMath::Max(OutSettings.MagSize, 1);
	OutSettings.Range = FMath::Max(OutSettings.Range, 100.f);
	OutSettings.ReloadTime = FMath::Max(OutSettings.ReloadTime, 0.1f);
	if (OutSettings.MuzzleSocket.IsNone())
	{
		OutSettings.MuzzleSocket = TEXT("Muzzle");
	}

	return true;
}

float UFPSCombatComponent::GetFovByCurrentWeaponZoomRatio() const
{
	return GetFovByWeaponZoomRatio(CurrentWeapon);
}

float UFPSCombatComponent::GetFovByWeaponZoomRatio(const AFPSWeapon* Weapon) const
{
	float DefaultFOV = 90.0f;
	if (const AFPSCharacter* FPSCharacter = Cast<AFPSCharacter>(GetOwner()))
	{
		DefaultFOV = FPSCharacter->GetDefaultFOV();
	}

	if (!Weapon) return DefaultFOV;

	float WeaponZoomRatio = Weapon->GetZoomRatio();

	float ZoomFov = FMath::RadiansToDegrees(2 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(DefaultFOV/2.0f))/WeaponZoomRatio));

	return ZoomFov;
}

int32 UFPSCombatComponent::GetDisplayAmmo() const
{
	if (!CurrentWeapon)
	{
		return 0;
	}

	// While predictions are in flight the replicated value lags behind what
	// the player has already spent; show the predicted count instead.
	if (IsLocallyControlledOwner() && GetOwner() && !GetOwner()->HasAuthority())
	{
		int32 PendingForWeapon = 0;
		for (const auto& Pair : PendingShots) if (Pair.Value.Weapon.Get() == CurrentWeapon) ++PendingForWeapon;
		int32 Baseline = CurrentWeapon->GetAmmoInMag();
		if (bHasAmmoBaseline && AmmoBaselineWeapon == CurrentWeapon)
		{
			const uint32 RawDelta = CurrentWeapon->GetAmmoRevision() - ConfirmedAmmoRevision;
			if (PendingForWeapon > 0 || RawDelta == 0 || RawDelta >= 0x80000000u) Baseline = ConfirmedDisplayAmmo;
		}
		return FMath::Max(Baseline - PendingForWeapon, 0);
	}

	return CurrentWeapon->GetAmmoInMag();
}

//////////////////////////////////////////////////////////////////////////
// Aim

void UFPSCombatComponent::StartAim()
{
	SubmitCombatInput(EFPSCombatInput::StartAim);
}

void UFPSCombatComponent::StopAim()
{
	SubmitCombatInput(EFPSCombatInput::StopAim);
}

//////////////////////////////////////////////////////////////////////////
// Firing

void UFPSCombatComponent::StartFire()
{
	if (!IsLocallyControlledOwner())
	{
		return;
	}

	// Send the input even if local weapon replication is not ready for a shot.
	SubmitCombatInput(EFPSCombatInput::StartFire);

	// Attempt an immediate shot, then use a one-shot RPM timer for automatic fire.
	FireOnce();
}

void UFPSCombatComponent::StopFire()
{
	SubmitCombatInput(EFPSCombatInput::StopFire);
}

void UFPSCombatComponent::Reload()
{
	SubmitCombatInput(EFPSCombatInput::Reload);
}

void UFPSCombatComponent::InterruptReload()
{
	SubmitCombatInput(EFPSCombatInput::InterruptReload);
}

void UFPSCombatComponent::OnOwnerDied()
{
	// Inventory is server-owned; clients may only know CurrentWeapon. Hide both
	// sets explicitly, including meshes that were not attached when death arrived.
	TArray<AFPSWeapon*> DeathWeapons;
	for (AFPSWeapon* Weapon : Inventory) if (IsValid(Weapon)) DeathWeapons.AddUnique(Weapon);
	if (IsValid(CurrentWeapon)) DeathWeapons.AddUnique(CurrentWeapon);
	for (AFPSWeapon* Weapon : DeathWeapons)
	{
		Weapon->HideForOwnerDeath();
	}
	FPSAnimation::Stop(FireMontages);
	StopFireAndAimForReload();
	CameraRecoil = FVector2D::ZeroVector;
	StopShotCameraShake(true);
	CancelReloadInternal();
	PendingShots.Reset();
	CacheWeaponAmmoIfIdle();
	NotifyDisplayAmmoChanged();
	PublishCombatState();
}

void UFPSCombatComponent::StopForMatchEnd()
{
	FPSAnimation::Stop(FireMontages);
	StopFireAndAimForReload();
	CameraRecoil = FVector2D::ZeroVector;
	StopShotCameraShake(true);
	CancelReloadInternal();
	// The server can finish the match inside ApplyPointDamage before sending its
	// winning ShotResult. Pending records remain until receipt or EndPlay cleanup.
	PublishCombatState();
}

void UFPSCombatComponent::FireOnce()
{
	AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwningCharacter());
	AFPSWeapon* Weapon = CurrentWeapon;
	UWorld* World = GetWorld();
	if (bEndingPlay || !Character || !Weapon || !World || Character->IsDead() || bReloading || !bWantsFire)
	{
		return;
	}

	FWeaponCombatSettings Settings;
	if (!ResolveCombatSettings(Weapon, Settings))
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now < NextLocalFireTime)
	{
		ScheduleNextShot();
		return;
	}
	InitializeShotSeed();
	if (ShotSeed == 0) return; // Wait for the owner-only seed before predicting.
	if (PendingShots.Num() >= MaxPendingPredictions)
	{
		NextLocalFireTime = Now + FPSRecoil::ShotInterval(Settings.RoundsPerMinute);
		ScheduleNextShot();
		return;
	}
	if (ShotSequence == MAX_int32 || NextLocalBurstShotIndex == MAX_int32)
	{
		StopFire();
		return;
	}

	// Nothing in flight means the replicated ammo is up to date (a reload may
	// have completed since the last shot).
	CacheWeaponAmmoIfIdle();
	PredictedAmmo = GetDisplayAmmo();

	if (PredictedAmmo <= 0)
	{
		Reload();
		return;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(Character->GetController());
	if (!PlayerController) return;

	FFPSPredictedShot Prediction;
	Prediction.Weapon = Weapon;
	Prediction.BurstId = ActiveFireBurstId;
	Prediction.BurstShotIndex = NextLocalBurstShotIndex;
	if (!FPSRecoil::Evaluate(RecoilPattern, ShotSeed, ActiveFireBurstId, NextLocalBurstShotIndex,
		Settings.Spread, MaxSpreadRadiusDegrees, Prediction.PatternOffsetDegrees, Prediction.SpreadOffsetDegrees,
		Settings.SpreadRampShots, Settings.SpreadRampExponent))
	{
		StopFire();
		return;
	}
	NextLocalFireTime = Now + FPSRecoil::ShotInterval(Settings.RoundsPerMinute);
	PredictedAmmo = FMath::Max(PredictedAmmo - 1, 0);

	// Exclude all PlayerCameraManager shake translations from gameplay rays.
	const UCameraComponent* BaseCamera = Character->GetFollowCamera();
	const FVector Start = BaseCamera ? BaseCamera->GetComponentLocation() : Character->GetPawnViewLocation();
	// GetPlayerViewPoint includes cosmetic recoil; only the input control rotation
	// is the base aim. Otherwise the 80% camera offset would be counted twice.
	const FRotator BaseAim = PlayerController->GetControlRotation();
	const FVector Direction = FPSRecoil::ApplyOffset(BaseAim,
		Prediction.PatternOffsetDegrees + Prediction.SpreadOffsetDegrees).Vector();
	Prediction.Sequence = ++ShotSequence;
	Prediction.SentAtSeconds = FPlatformTime::Seconds();
	Prediction.TracePhase = CVarNetTracePhase.GetValueOnGameThread();
	++NextLocalBurstShotIndex;
	CameraRecoilTarget = Prediction.PatternOffsetDegrees * FMath::Clamp(CameraRecoilRatio, 0.f, 1.f);
	PredictShot(Start, Direction, Settings.Range, Prediction);

	// Immediate local feedback. The server may correct it seconds later.
	// 特效部分
	
	const FTransform Muzzle = GetMuzzleTransform(Weapon, Settings.MuzzleSocket);
	SpawnMuzzleFX(Muzzle.GetLocation(), Direction.Rotation());
	PlayShotCameraShake();
	PlayFireMontages(Weapon);
	if (Prediction.bHitAnything)
	{
		Prediction.Decal = SpawnImpactFX(Prediction.HitLocation, Prediction.HitNormal);
	}

	PendingShots.Add(Prediction.Sequence, Prediction);
	NotifyDisplayAmmoChanged();

	FFPSShotRequest Request;
	Request.Sequence = Prediction.Sequence;
	Request.Weapon = Weapon;
	Request.BurstId = ActiveFireBurstId;
	Request.PredictedShotIndex = Prediction.BurstShotIndex;
	Request.ViewLocation = Start;
	Request.ViewDirection = BaseAim.Vector();
	Request.WeaponTag = Weapon->GetTags();
	Request.ClientFrame = 0;
	if (const UFPSLagCompensationSubsystem* LagComp = UFPSLagCompensationSubsystem::Get(this))
	{
		Request.ClientFrame = LagComp->EstimateClientRenderFrame(PlayerController);
	}
	if (CVarNetTrace.GetValueOnGameThread() > 0)
	{
		UE_LOG(LogFPSCombat, Log,
			TEXT("[FPSNet][Predict] phase=%d %s seq=%d burst=%u shot=%d frame=%d predictedAmmo=%d pending=%d hit=%d pattern=(%.4f,%.4f) spread=(%.4f,%.4f)"),
			Prediction.TracePhase, *NetTraceContext(this), Request.Sequence, Request.BurstId, Prediction.BurstShotIndex,
			Request.ClientFrame, PredictedAmmo, PendingShots.Num(), Prediction.bHitCharacter ? 1 : 0,
			Prediction.PatternOffsetDegrees.X, Prediction.PatternOffsetDegrees.Y, Prediction.SpreadOffsetDegrees.X, Prediction.SpreadOffsetDegrees.Y);
	}
#if !UE_BUILD_SHIPPING
	if (BadOriginTestWorld.Get() == World && World->GetNetMode() == NM_Client && IsLocallyControlledOwner())
	{
		BadOriginTestWorld.Reset();
		Request.ViewLocation += FVector(10000.f, 0.f, 0.f);
		UE_LOG(LogFPSCombat, Log, TEXT("[FPSNet][Probe] %s seq=%d injected=BadOrigin clientPredictionUnchanged=1"),
			*NetTraceContext(this), Request.Sequence);
	}
#endif

	Server_FireShot(Request);
	if (PredictedAmmo <= 0 && !bReloading) Reload();
	ScheduleNextShot();
}

void UFPSCombatComponent::PredictShot(const FVector& Start, const FVector& Dir, float Range,
	FFPSPredictedShot& OutPrediction) const
{
	OutPrediction.bHitAnything = false;
	OutPrediction.bHitCharacter = false;
	OutPrediction.HitLocation = Start + Dir * Range;
	OutPrediction.HitNormal = -Dir;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Direction = Dir.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const FVector End = Start + Direction * Range;

	// The world trace ignores characters; they are tested against their
	// visible bone hitboxes below.
	FHitResult WorldHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FPSShotPredict), false, GetOwner());
	for (TActorIterator<AFPSCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	const bool bWorldHit = World->LineTraceSingleByChannel(
		WorldHit, Start, End, FPSTraceChannels::ECC_Weapon, Params);

	float BestDistance = bWorldHit ? WorldHit.Distance : Range;
	if (bWorldHit)
	{
		OutPrediction.bHitAnything = true;
		OutPrediction.HitLocation = WorldHit.Location;
		OutPrediction.HitNormal = WorldHit.ImpactNormal;
	}

	for (TActorIterator<AFPSCharacter> It(World); It; ++It)
	{
		AFPSCharacter* Other = *It;
		if (!Other || Other == GetOwner() || Other->IsDead())
		{
			continue;
		}

		FPSLagComp::FSampleFrame Sample;
		FPSLagComp::SampleCharacter(Other, Sample);
		if (!Sample.bValid)
		{
			continue;
		}

		float CapsuleDistance = 0.f;
		if (!FPSLagComp::RayCapsule(Start, Direction, Sample.CapsuleA, Sample.CapsuleB,
			Sample.CapsuleRadius, BestDistance, CapsuleDistance))
		{
			continue;
		}

		FPSLagComp::FHitboxHit Hit;
		if (!FPSLagComp::RaycastHitboxes(Sample, Start, Direction, BestDistance, Hit))
		{
			continue;
		}

		BestDistance = Hit.Distance;
		OutPrediction.bHitAnything = true;
		OutPrediction.bHitCharacter = true;
		OutPrediction.HitLocation = Hit.Location;
		OutPrediction.HitNormal = -Direction;
	}
}

FTransform UFPSCombatComponent::GetMuzzleTransform(const AFPSWeapon* Weapon, FName Socket) const
{
	if (!Weapon)
	{
		return FTransform::Identity;
	}

	if (const USkeletalMeshComponent* Mesh = Weapon->GetWeaponMesh1P())
	{
		if (Mesh->GetSkinnedAsset() && !Socket.IsNone() && Mesh->DoesSocketExist(Socket))
		{
			return Mesh->GetSocketTransform(Socket, RTS_World);
		}

		if (Mesh->GetAttachParent())
		{
			return Mesh->GetComponentTransform();
		}
	}

	if (const USkeletalMeshComponent* Mesh = Weapon->GetWeaponMesh3P())
	{
		if (Mesh->GetSkinnedAsset() && !Socket.IsNone() && Mesh->DoesSocketExist(Socket))
		{
			return Mesh->GetSocketTransform(Socket, RTS_World);
		}

		if (Mesh->GetAttachParent())
		{
			return Mesh->GetComponentTransform();
		}
	}

	// The weapon actor itself sits at the origin (only its meshes are attached
	// to the character), so prefer the owner as the last fallback.
	return GetOwner() ? GetOwner()->GetActorTransform() : Weapon->GetActorTransform();
}

void UFPSCombatComponent::SpawnMuzzleFX(const FVector& Location, const FRotator& Rotation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (MuzzleFX)
	{
		UGameplayStatics::SpawnEmitterAtLocation(World, MuzzleFX, Location, Rotation,
			FVector(1.f), true);
	}

	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, FireSound, Location);
	}

#if ENABLE_DRAW_DEBUG
	if (!MuzzleFX)
	{
		DrawDebugPoint(World, Location, 8.f, FColor::Yellow, false, 0.1f);
	}
#endif
}

UDecalComponent* UFPSCombatComponent::SpawnImpactFX(const FVector& Location, const FVector& Normal) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (ImpactFX)
	{
		UGameplayStatics::SpawnEmitterAtLocation(World, ImpactFX, Location, Normal.Rotation(),
			FVector(1.f), true);
	}

	UDecalComponent* Decal = nullptr;
	if (ImpactDecalMaterial)
	{
		Decal = UGameplayStatics::SpawnDecalAtLocation(World, ImpactDecalMaterial,
			ImpactDecalSize, Location, Normal.Rotation(), 10.f);
	}

#if ENABLE_DRAW_DEBUG
	if (!ImpactFX && !ImpactDecalMaterial)
	{
		DrawDebugPoint(World, Location, 6.f, FColor::Red, false, 0.3f);
	}
#endif

	return Decal;
}

//////////////////////////////////////////////////////////////////////////
// Client -> server

void UFPSCombatComponent::Server_FireShot_Implementation(const FFPSShotRequest& Request)
{
	AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwningCharacter());
	UWorld* World = GetWorld();

	FFPSShotResult Result;
	Result.Sequence = Request.Sequence;
	Result.Weapon = Request.Weapon;
	Result.BurstId = Request.BurstId;
	Result.NextShotIndex = ServerNextBurstShotIndex;
	Result.ResolvedFrame = Request.ClientFrame;
	Result.RemainingAmmo = CurrentWeapon ? CurrentWeapon->GetAmmoInMag() : 0;
	Result.AmmoRevision = CurrentWeapon ? CurrentWeapon->GetAmmoRevision() : 0;

	const auto Reject = [this, &Result](EFPSShotReject Reason)
	{
		Result.Outcome = EFPSShotOutcome::Rejected;
		Result.RejectReason = Reason;
		if (CVarNetTrace.GetValueOnGameThread() > 0)
		{
			UE_LOG(LogFPSCombat, Log, TEXT("[FPSNet][ServerReject] phase=%d %s seq=%d burst=%u reason=%s ammo=%d ammoRevision=%u next=%d"),
				CVarNetTracePhase.GetValueOnGameThread(), *NetTraceContext(this), Result.Sequence, Result.BurstId,
				*UEnum::GetValueAsString(Reason), Result.RemainingAmmo, Result.AmmoRevision, Result.NextShotIndex);
		}
		Client_ShotResult(Result);
		UE_LOG(LogFPSCombat, Verbose, TEXT("[FPSShotReject] seq=%d reason=%d"),
			Result.Sequence, static_cast<int32>(Reason));
	};

	if (!Character || !World)
	{
		return;
	}

	if (Character->IsDead())
	{
		Reject(EFPSShotReject::Dead);
		return;
	}
	if (const AFPSGameState* GS = World->GetGameState<AFPSGameState>(); GS && GS->HasFinished())
	{
		Reject(EFPSShotReject::MatchFinished);
		return;
	}

	// Defensive path for actual shot requests; input edges also interrupt when
	// cooldown or an empty magazine prevents the client sending any shot at all.
	if (bReloading)
	{
		CancelReloadInternal();
		PublishCombatState();
	}

	AFPSWeapon* Weapon = CurrentWeapon;
	if (!Weapon)
	{
		Reject(EFPSShotReject::NoWeapon);
		return;
	}

	if (Weapon != Request.Weapon || Weapon->GetTags() != Request.WeaponTag)
	{
		Reject(EFPSShotReject::WrongWeapon);
		return;
	}

	// Reliable RPCs are ordered; a non-increasing sequence is a replay.
	if (Request.Sequence <= LastServerSequence)
	{
		Reject(EFPSShotReject::Sequence);
		return;
	}
	LastServerSequence = Request.Sequence;
	if (!bIsFiring)
	{
		Reject(EFPSShotReject::NotFiring);
		return;
	}
	if (Request.BurstId != ActiveFireBurstId)
	{
		Reject(EFPSShotReject::BurstMismatch);
		return;
	}

	if (Request.ViewDirection.ContainsNaN() || Request.ViewDirection.IsNearlyZero() ||
		FMath::Abs(Request.ViewDirection.SizeSquared() - 1.f) > 0.05f)
	{
		Reject(EFPSShotReject::BadDirection);
		return;
	}
	const FVector BaseDirection = Request.ViewDirection.GetSafeNormal();

	if (Request.ViewLocation.ContainsNaN() ||
		FVector::DistSquared(Request.ViewLocation, Character->GetPawnViewLocation()) > MaxOriginErrorSq)
	{
		Reject(EFPSShotReject::BadOrigin);
		return;
	}

	FWeaponCombatSettings Settings;
	if (!ResolveCombatSettings(Weapon, Settings))
	{
		Reject(EFPSShotReject::NoWeapon);
		return;
	}
	InitializeShotSeed();
	if (ServerNextBurstShotIndex == MAX_int32 ||
		!FPSRecoil::Evaluate(RecoilPattern, ShotSeed, ActiveFireBurstId, ServerNextBurstShotIndex,
			Settings.Spread, MaxSpreadRadiusDegrees, Result.PatternOffsetDegrees, Result.SpreadOffsetDegrees,
			Settings.SpreadRampShots, Settings.SpreadRampExponent))
	{
		Reject(EFPSShotReject::RecoilConfig);
		return;
	}
	// The claimed index is only for prediction diagnostics. The server's accepted
	// bullet count and server-created seed determine the actual shot direction.
	const FVector Direction = FPSRecoil::ApplyOffset(BaseDirection.Rotation(),
		Result.PatternOffsetDegrees + Result.SpreadOffsetDegrees).Vector();
	Result.ShotDirection = Direction;

	// Token bucket: regenerates at the weapon's real rate and tolerates a small
	// burst so bunched reliable packets do not read as cheating.
	const float Now = World->GetTimeSeconds();
	if (ServerFireRateLastUpdate <= 0.f)
	{
		ServerFireRateLastUpdate = Now;
	}
	const float RatePerSecond = Settings.RoundsPerMinute / 60.f;
	ServerFireTokens = FMath::Min(
		ServerFireTokens + (Now - ServerFireRateLastUpdate) * RatePerSecond,
		ServerFireTokenCapacity);
	ServerFireRateLastUpdate = Now;

	if (ServerFireTokens < 1.f)
	{
		Reject(EFPSShotReject::FireRate);
		return;
	}
	ServerFireTokens -= 1.f;

	if (Weapon->GetAmmoInMag() <= 0)
	{
		StartReloadTimer(Settings);
		PublishCombatState();
		Reject(EFPSShotReject::NoAmmo);
		return;
	}

	UFPSLagCompensationSubsystem* LagComp = UFPSLagCompensationSubsystem::Get(this);
	if (LagComp)
	{
		const int32 NewestFrame = LagComp->GetNewestRecordedFrame();
		if (NewestFrame >= 0 && Request.ClientFrame > NewestFrame + FPSLagComp::FutureFrameTolerance)
		{
			Reject(EFPSShotReject::FrameInFuture);
			return;
		}
	}

	FPSLagComp::FResolvedShot Resolved;
	FFPSShotRequest AuthoritativeRequest = Request;
	AuthoritativeRequest.ViewDirection = Direction;
	const bool bResolved = LagComp &&
		LagComp->ResolveShot(Character, AuthoritativeRequest, Settings.Range, Resolved);

	const int32 AmmoBefore = Weapon->GetAmmoInMag();
	Character->CancelSpawnProtection();
	float AppliedDamage = 0.f;
	float VictimHealthBefore = -1.f;
	float VictimHealthAfter = -1.f;
	Weapon->SetAmmoInMag(Weapon->GetAmmoInMag() - 1);
	Result.ServerShotIndex = ServerNextBurstShotIndex++;
	Result.NextShotIndex = ServerNextBurstShotIndex;

	Result.RemainingAmmo = Weapon->GetAmmoInMag();
	Result.AmmoRevision = Weapon->GetAmmoRevision();
	Result.ResolvedFrame = bResolved ? Resolved.ResolvedFrame : Request.ClientFrame;
	Result.bFrameClamped = bResolved && Resolved.bFrameClamped;

	// Shot FX for everyone else.
	FFPSShotFX FX;
	FX.Weapon = Weapon;
	FX.Sequence = Request.Sequence;
	FX.MuzzleLocation = GetMuzzleTransform(Weapon, Settings.MuzzleSocket).GetLocation();
	FX.ImpactLocation = Request.ViewLocation + Direction * Settings.Range;
	if (bResolved && (Resolved.bHit || Resolved.bBlockedByWorld))
	{
		Result.Outcome = EFPSShotOutcome::WorldImpact;
		Result.bBlockingHit = true;
		Result.HitLocation = Resolved.Location;
		Result.HitNormal = Resolved.Normal;
		FX.bHit = true;
		FX.ImpactLocation = Resolved.Location;
		FX.ImpactNormal = Resolved.Normal;
	}
	Multicast_ShotFX(FX);

	if (bResolved && Resolved.bHit && Resolved.Character)
	{
		const float Damage = Settings.Damage *
			(Resolved.bHeadshot ? Settings.HeadshotMultiplier : 1.f);

		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.Location = Resolved.Location;
		Hit.ImpactPoint = Resolved.Location;
		Hit.ImpactNormal = Resolved.Normal;
		Hit.TraceStart = Request.ViewLocation;
		Hit.TraceEnd = Request.ViewLocation + Direction * Settings.Range;
		Hit.Distance = Resolved.Distance;
		Hit.BoneName = Resolved.Bone;
		Hit.Component = Resolved.Character->GetMesh3P();

		UFPSHealthComponent* Health = Resolved.Character->GetHealthComponent();
		VictimHealthBefore = Health ? Health->GetHealth() : -1.f;
		// A delayed packet cannot damage a target whose shield was active at shot time.
		const bool bProtected = Resolved.Character->WasSpawnProtectedAt(double(Resolved.ResolvedFrame) / FPSLagComp::ServerFrameRate) ||
			Resolved.Character->IsSpawnProtected();
		if (!bProtected)
			AppliedDamage = UGameplayStatics::ApplyPointDamage(Resolved.Character, Damage, Direction, Hit,
				Character->GetController(), Weapon, UDamageType::StaticClass());
		VictimHealthAfter = Health ? Health->GetHealth() : -1.f;

		Result.bHit = true;
		Result.bHeadshot = Resolved.bHeadshot;
		Result.HitLocation = Resolved.Location;
		Result.HitNormal = Resolved.Normal;
		Result.HitBone = Resolved.Bone;
		Result.ActualDamage = FMath::Max(0.f, AppliedDamage);
		Result.bKilled = Result.ActualDamage > 0.f && Resolved.Character->IsDead();
		Result.Outcome = Result.ActualDamage > 0.f ? (Result.bKilled ? EFPSShotOutcome::Killed : EFPSShotOutcome::Damaged)
			: (bProtected ? EFPSShotOutcome::Protected : EFPSShotOutcome::NoDamage);

		if (const UFPSLagCompensationSubsystem* Resolver = LagComp)
		{
			const double RewindMs = (Resolver->GetNewestRecordedFrame() - Resolved.ResolvedFrame)
				* (1000.0 / FPSLagComp::ServerFrameRate);
			UE_LOG(LogFPSCombat, Log,
				TEXT("[FPSHit] victim='%s' bone='%s' headshot=%d dmg=%.1f frame=%d rewind=%.0fms clamped=%d"),
				*GetNameSafe(Resolved.Character), *Resolved.Bone.ToString(),
				Resolved.bHeadshot ? 1 : 0, AppliedDamage, Resolved.ResolvedFrame,
				RewindMs, Resolved.bFrameClamped ? 1 : 0);
		}
	}
	else
	{
		UE_LOG(LogFPSCombat, Verbose,
			TEXT("[FPSShot] seq=%d frame=%d resolved=%d clamped=%d world=%d"),
			Request.Sequence, Request.ClientFrame, Result.ResolvedFrame,
			Result.bFrameClamped ? 1 : 0, Resolved.bBlockedByWorld ? 1 : 0);
	}

	if (CVarNetTrace.GetValueOnGameThread() > 0)
	{
		const int32 Newest = LagComp ? LagComp->GetNewestRecordedFrame() : INDEX_NONE;
		const double RewindMs = Newest >= 0 ? (Newest - Result.ResolvedFrame) * (1000.0 / FPSLagComp::ServerFrameRate) : -1.;
		UE_LOG(LogFPSCombat, Log,
			TEXT("[FPSNet][ServerAccept] phase=%d %s seq=%d burst=%u claimed=%d used=%d ammoBefore=%d ammoAfter=%d ammoRevision=%u hit=%d victim=%s damage=%.1f healthBefore=%.1f healthAfter=%.1f history=%d requestedFrame=%d resolvedFrame=%d rewindMs=%.1f clamped=%d pattern=(%.4f,%.4f) spread=(%.4f,%.4f)"),
			CVarNetTracePhase.GetValueOnGameThread(), *NetTraceContext(this), Result.Sequence, Result.BurstId,
			Request.PredictedShotIndex, Result.ServerShotIndex, AmmoBefore, Result.RemainingAmmo, Result.AmmoRevision,
			Result.bHit ? 1 : 0, *GetNameSafe(Resolved.Character), AppliedDamage, VictimHealthBefore, VictimHealthAfter,
			Resolved.bHaveHistory ? 1 : 0, Request.ClientFrame, Result.ResolvedFrame, RewindMs, Result.bFrameClamped ? 1 : 0,
			Result.PatternOffsetDegrees.X, Result.PatternOffsetDegrees.Y, Result.SpreadOffsetDegrees.X, Result.SpreadOffsetDegrees.Y);
	}
	Client_ShotResult(Result);

	if (Weapon->GetAmmoInMag() <= 0)
	{
		StartReloadTimer(Settings);
		PublishCombatState();
	}
}

//////////////////////////////////////////////////////////////////////////
// Server -> client

void UFPSCombatComponent::Client_ShotResult_Implementation(const FFPSShotResult& Result)
{
	FFPSPredictedShot* Predicted = PendingShots.Find(Result.Sequence);
	// Old weapon / already cleared predictions must not change current ammo or recoil.
	if (!Predicted)
	{
		if (CVarNetTrace.GetValueOnGameThread() > 0)
			UE_LOG(LogFPSCombat, Log, TEXT("[FPSNet][AckIgnored] %s seq=%d burst=%u reason=noPendingPrediction"),
				*NetTraceContext(this), Result.Sequence, Result.BurstId);
		return;
	}
	const bool bContextMatches = Predicted->Weapon.Get() == Result.Weapon && Predicted->BurstId == Result.BurstId;
	const bool bAccepted = Result.RejectReason == EFPSShotReject::None && Result.ServerShotIndex >= 0;
	const bool bOffsetsAgree = bAccepted && Predicted->BurstShotIndex == Result.ServerShotIndex &&
		Predicted->PatternOffsetDegrees.Equals(Result.PatternOffsetDegrees, 0.001) &&
		Predicted->SpreadOffsetDegrees.Equals(Result.SpreadOffsetDegrees, 0.001);
	const bool bHitAgrees = Predicted->bHitAnything == Result.bBlockingHit &&
		Predicted->bHitCharacter == Result.bHit && (!Result.bBlockingHit ||
		FVector::DistSquared(Predicted->HitLocation, Result.HitLocation) <= PredictionLocationToleranceSq);
	const double AckMs = Predicted->SentAtSeconds > 0. ? (FPlatformTime::Seconds() - Predicted->SentAtSeconds) * 1000. : -1.;
	const int32 TracePhase = Predicted->TracePhase;
	const int32 PredictedShotIndex = Predicted->BurstShotIndex;
	const int32 DisplayBefore = GetDisplayAmmo();
	if (!bContextMatches || !bOffsetsAgree || !bHitAgrees)
	{
		if (Predicted->Decal.IsValid()) Predicted->Decal->DestroyComponent();
		if (bAccepted && Result.bBlockingHit) SpawnImpactFX(Result.HitLocation, Result.HitNormal);
		UE_LOG(LogFPSCombat, Verbose,
			TEXT("[FPSRecoilCorrect] seq=%d burst=%u predicted=%d server=%d offsetMatch=%d reject=%d"),
			Result.Sequence, Result.BurstId, Predicted->BurstShotIndex, Result.ServerShotIndex,
			bOffsetsAgree ? 1 : 0, int32(Result.RejectReason));
	}
	PendingShots.Remove(Result.Sequence);
	if (!bContextMatches || CurrentWeapon != Result.Weapon)
	{
		if (CVarNetTrace.GetValueOnGameThread() > 0)
			UE_LOG(LogFPSCombat, Log, TEXT("[FPSNet][AckIgnored] %s seq=%d burst=%u reason=changedContext ackMs=%.2f"),
				*NetTraceContext(this), Result.Sequence, Result.BurstId, AckMs);
		return;
	}

	int32 PendingForWeapon = 0;
	int32 PendingForBurst = 0;
	for (const TPair<int32, FFPSPredictedShot>& Pair : PendingShots)
	{
		if (Pair.Value.Weapon.Get() == CurrentWeapon)
		{
			++PendingForWeapon;
			if (Pair.Value.BurstId == Result.BurstId) ++PendingForBurst;
		}
	}
	PredictedAmmo = FMath::Max(Result.RemainingAmmo - PendingForWeapon, 0);
	AcceptAmmoBaseline(Result.RemainingAmmo, Result.AmmoRevision);
	NotifyDisplayAmmoChanged();
	if (CVarNetTrace.GetValueOnGameThread() > 0)
	{
		UE_LOG(LogFPSCombat, Log,
			TEXT("[FPSNet][Ack] phase=%d %s seq=%d burst=%u accepted=%d reason=%s predictedShot=%d serverShot=%d next=%d ackMs=%.2f offsetsMatch=%d hitMatch=%d corrected=%d displayBefore=%d displayAfter=%d serverAmmo=%d pending=%d"),
			TracePhase, *NetTraceContext(this), Result.Sequence, Result.BurstId, bAccepted ? 1 : 0,
			*UEnum::GetValueAsString(Result.RejectReason), PredictedShotIndex, Result.ServerShotIndex, Result.NextShotIndex, AckMs,
			bOffsetsAgree ? 1 : 0, bHitAgrees ? 1 : 0, (!bAccepted || !bOffsetsAgree || !bHitAgrees) ? 1 : 0,
			DisplayBefore, GetDisplayAmmo(), Result.RemainingAmmo, PendingShots.Num());
	}
	if (Result.BurstId != ActiveFireBurstId || !bWantsFire)
	{
		if (bAccepted) PresentHitConfirmation(Result);
		return;
	}

	// Rebase the next prediction on accepted + still-in-flight bullets. Preserve
	// each pending shot's ORIGINAL offsets so its reply can detect actual divergence.
	const int32 NextIndex = int32(FMath::Min<int64>(int64(FMath::Max(Result.NextShotIndex, 0)) + PendingForBurst, MAX_int32));
	FWeaponCombatSettings Settings;
	if (!ResolveCombatSettings(CurrentWeapon, Settings)) return;
	NextLocalBurstShotIndex = NextIndex;
	FVector2D PatternOffset, SpreadOffset;
	CameraRecoilTarget = FVector2D::ZeroVector;
	if (NextIndex > 0 && FPSRecoil::Evaluate(RecoilPattern, ShotSeed, ActiveFireBurstId, NextIndex - 1,
		Settings.Spread, MaxSpreadRadiusDegrees, PatternOffset, SpreadOffset,
		Settings.SpreadRampShots, Settings.SpreadRampExponent))
	{
		CameraRecoilTarget = PatternOffset * FMath::Clamp(CameraRecoilRatio, 0.f, 1.f);
	}
	ScheduleNextShot();
	if (bAccepted) PresentHitConfirmation(Result);
}

void UFPSCombatComponent::PresentHitConfirmation(const FFPSShotResult& Result)
{
	if (!IsLocallyControlledOwner()) return;
	const bool bDamage = (Result.Outcome == EFPSShotOutcome::Damaged || Result.Outcome == EFPSShotOutcome::Killed) && Result.ActualDamage > 0.f;
	if (!bDamage && Result.Outcome != EFPSShotOutcome::Protected) return;
	if (bDamage)
	{
		USoundBase* Sound = Result.Outcome == EFPSShotOutcome::Killed ? KillHitSound.Get()
			: (Result.bHeadshot ? HeadHitSound.Get() : BodyHitSound.Get());
		const double Now = FPlatformTime::Seconds();
		// Preserve head/kill rewards even when reliable body confirmations bunch up.
		if (Sound && (Result.bHeadshot || Result.Outcome == EFPSShotOutcome::Killed || Now - LastBodyHitSoundTime >= .045))
		{
			UGameplayStatics::PlaySound2D(this, Sound, FMath::Clamp(HitSoundVolume, 0.f, 1.f));
			if (!Result.bHeadshot && Result.Outcome != EFPSShotOutcome::Killed) LastBodyHitSoundTime = Now;
		}
	}
	// Last action: UI listeners can leave the room or destroy their owner during this broadcast.
	OnHitConfirmed.Broadcast(Result.Outcome, Result.bHeadshot, Result.ActualDamage);
}

void UFPSCombatComponent::Multicast_ShotFX_Implementation(const FFPSShotFX& FX)
{
	// The shooter already predicted this shot.
	if (IsLocallyControlledOwner())
	{
		return;
	}
	PlayFireMontages(FX.Weapon);

	const FVector Direction = (FX.ImpactLocation - FX.MuzzleLocation).GetSafeNormal();
	const FRotator Rotation = Direction.IsNearlyZero() ? FRotator::ZeroRotator : Direction.Rotation();

	SpawnMuzzleFX(FX.MuzzleLocation, Rotation);
	if (FX.bHit)
	{
		SpawnImpactFX(FX.ImpactLocation, FX.ImpactNormal);
	}
}

//////////////////////////////////////////////////////////////////////////
// Reload

void UFPSCombatComponent::StartReloadTimer(const FWeaponCombatSettings& Settings)
{
	UWorld* World = GetWorld();
	const AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	if (!World || bReloading || !Character || !Character->HasAuthority() || Character->IsDead() ||
		!IsValid(CurrentWeapon) || CurrentWeapon->GetAmmoInMag() >= Settings.MagSize)
	{
		return;
	}

	StopFireAndAimForReload();
	const uint32 Generation = ++ReloadGeneration;
	ReloadWeapon = CurrentWeapon;
	ReloadMagSize = Settings.MagSize;
	const TWeakObjectPtr<AFPSWeapon> Weapon = ReloadWeapon;
	FTimerDelegate Callback = FTimerDelegate::CreateWeakLambda(this, [this, Generation, Weapon]()
	{
		FinishReload(Generation, Weapon);
	});
	World->GetTimerManager().SetTimer(ReloadTimerHandle, Callback, Settings.ReloadTime, false);
	SetReloadingState(true, CurrentWeapon, World->GetTimeSeconds() + Settings.ReloadTime);
}

void UFPSCombatComponent::FinishReload(uint32 ExpectedGeneration, TWeakObjectPtr<AFPSWeapon> ExpectedWeapon)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || ExpectedGeneration != ReloadGeneration || !bReloading)
	{
		return;
	}

	const AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	if (Character && !Character->IsDead() && ExpectedWeapon.IsValid() &&
		ReloadWeapon == ExpectedWeapon && CurrentWeapon == ExpectedWeapon.Get())
	{
		ExpectedWeapon->SetAmmoInMag(ReloadMagSize);
		UE_LOG(LogFPSCombat, Verbose, TEXT("[FPSShot] '%s' reloaded to %d."),
			*GetNameSafe(ExpectedWeapon.Get()), ReloadMagSize);
	}
	CancelReloadInternal();
	PublishCombatState();
}
