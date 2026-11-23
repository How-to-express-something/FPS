// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSCharacter.h"
#include "FPSMatchRules.h"
#include "FPSGameState.h"
#include "FPSPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "FPSAimAlignment.h"
#include "FPSCombatComponent.h"
#include "FPSGameMode.h"
#include "FPSHealthComponent.h"
#include "FPSHitReactionComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/DamageEvents.h"
#include "FPSLagCompensationSubsystem.h"
#include "FPSWeapon.h"
#include "FPSWeaponDataAsset.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

namespace
{
TAutoConsoleVariable<int32> CVarFPSAimDebug(
	TEXT("fps.Aim.Debug"), 0,
	TEXT("Draw camera/sight axes and projected rear/front errors while aiming. 0=off, 1=on."),
	ECVF_Cheat);

void DrawSightDebug(AFPSCharacter* Character, UCameraComponent* Camera,
	const FVector& Rear, const FVector& Front)
{
	UWorld* World = Character->GetWorld();
	const FVector CameraPosition = Camera->GetComponentLocation();
	const FVector SightDirection = (Front - Rear).GetSafeNormal();
	DrawDebugLine(World, CameraPosition, CameraPosition + Camera->GetForwardVector() * 200.f,
		FColor::Red, false, -1.f, 0, 0.5f);
	DrawDebugLine(World, Rear - SightDirection * 30.f, Front + SightDirection * 100.f,
		FColor::Cyan, false, -1.f, 0, 0.5f);
	DrawDebugPoint(World, Rear, 8.f, FColor::Yellow, false, -1.f);
	DrawDebugPoint(World, Front, 8.f, FColor::Green, false, -1.f);

	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (!PC || !GEngine)
	{
		return;
	}
	int32 Width = 0, Height = 0;
	PC->GetViewportSize(Width, Height);
	FVector2D ViewSize(Width, Height);
	if (const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
	{
		ViewSize *= LocalPlayer->Size;
	}
	FVector2D RearScreen, FrontScreen;
	FString Projection = TEXT("Sight points outside camera projection");
	if (ViewSize.X > 0.f && ViewSize.Y > 0.f &&
		PC->ProjectWorldLocationToScreen(Rear, RearScreen, true) &&
		PC->ProjectWorldLocationToScreen(Front, FrontScreen, true))
	{
		const FVector2D Centre = ViewSize * 0.5f;
		Projection = FString::Printf(TEXT("Rear error %.2f px | Front error %.2f px"),
			FVector2D::Distance(RearScreen, Centre), FVector2D::Distance(FrontScreen, Centre));
	}
	GEngine->AddOnScreenDebugMessage((uint64(1) << 32) | Character->GetUniqueID(), 0.f,
		FColor::Cyan, FString::Printf(TEXT("ADS: %s | Red=camera Cyan=sight Yellow=rear Green=front"), *Projection));
}
}


//////////////////////////////////////////////////////////////////////////
// AFPSCharacter

AFPSCharacter::AFPSCharacter()
{
	HitReactionComponent = CreateDefaultSubobject<UFPSHitReactionComponent>(TEXT("HitReactionComponent"));
	HitReactionMontages.SetNum(FPSHitReaction::MontageCount);
	HitReactionMontages1P.SetNum(FPSHitReaction::MontageCount);
	// Advance ADS before camera evaluation; align the arms after skeletal animation.
	PrimaryActorTick.bCanEverTick = true;
	AimAlignmentTick.bCanEverTick = true;
	AimAlignmentTick.bStartWithTickEnabled = true;
	AimAlignmentTick.bAllowTickOnDedicatedServer = false;
	AimAlignmentTick.TickGroup = TG_PostUpdateWork;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Only yaw follows the controller: the body faces where you aim (so remote
	// clients see the correct orientation) while pitch/roll stay on the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = false; // The body is driven by the aim, not the movement input
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Crouch is opt-in on the movement component: without this, CanCrouch() is false
	// and Crouch() returns without doing anything.
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;

	// // Create a camera boom (pulls in towards the player if there is a collision)
	// CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	// CameraBoom->SetupAttachment(GetMesh(),FName(TEXT("head")));
	// CameraBoom->TargetArmLength = 400.0f; // The camera follows at this distance behind the character	
	// CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera. It has to follow the control rotation, otherwise the
	// first-person mesh hanging off it would never move.
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(RootComponent);
	// Combat adds cosmetic recoil without changing the player's base control aim.
	FollowCamera->bUsePawnControlRotation = false;

	// Third-person body: hung off the capsule like any normal character mesh, hidden
	// from the owning player but still casting a hidden shadow for them.
	Mesh3P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh3P"));
	Mesh3P->SetupAttachment(RootComponent);
	Mesh3P->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	Mesh3P->SetOwnerNoSee(true);
	Mesh3P->bCastHiddenShadow = true;

	// First-person arms: re-parented onto the camera, drawn only for the owning
	// player. The relative transform has to be zeroed here because the inherited
	// Mesh carries the body's default offset from its own constructor/Blueprint.
	GetMesh()->SetupAttachment(FollowCamera);
	GetMesh()->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	GetMesh()->SetOnlyOwnerSee(true);
	GetMesh()->bOwnerNoSee = false;
	// The camera sits inside these arms, so they must not cast shadows or take decals.
	GetMesh()->bCastDynamicShadow = false;
	GetMesh()->CastShadow = false;
	GetMesh()->bReceivesDecals = false;
	// Purely cosmetic: the capsule owns all collision.
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetGenerateOverlapEvents(false);

	// The inherited mesh is the third-person body: hidden from its own player so the
	// camera does not sit inside it, but bCastHiddenShadow keeps the player's shadow.
	

	CombatComponent = CreateDefaultSubobject<UFPSCombatComponent>(TEXT("CombatComponent"));
	HealthComponent = CreateDefaultSubobject<UFPSHealthComponent>(TEXT("HealthComponent"));

	// Note: The skeletal mesh and anim blueprint references on Mesh1P and on the
	// inherited Mesh are set in the derived blueprint asset (to avoid direct content
	// references in C++).
	DefaultFoV = 90.0f;
	FollowCamera->SetFieldOfView(DefaultFoV);
}

void AFPSCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Seamless host RestartPlayer can run before World BeginPlay. The life must
	// already have a timestamp when GameMode establishes protection after Spawn.
	if (HasAuthority()) BirthServerTime = GetWorld()->GetTimeSeconds();
}

void AFPSCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer && GetMesh())
	{
		PoseReadyHandle = GetMesh()->RegisterOnBoneTransformsFinalizedDelegate(
			FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateWeakLambda(this,
				[this]() { bPoseEvaluated = true; }));
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}

	// Seed the previous yaw so the first frame does not report a huge turn.
	PreviousYaw = GetActorRotation().Yaw;

	// Remember the rest transforms: crouching offsets both meshes and has to restore
	// exactly these values (the inherited Mesh is the arms, not the body).
	if (GetMesh())
	{
		Mesh1PDefaultRelativeLocation = GetMesh()->GetRelativeLocation();
		Mesh1PDefaultRelativeRotation = GetMesh()->GetRelativeRotation();
	}
	if (Mesh3P)
	{
		Mesh3PDefaultRelativeLocation = Mesh3P->GetRelativeLocation();
	}

	if (FollowCamera)
	{
		DefaultFoV = FollowCamera->FieldOfView;
	}
	AimAlignmentTick.AddPrerequisite(this, PrimaryActorTick);
	if (GetMesh())
	{
		AimAlignmentTick.AddPrerequisite(GetMesh(), GetMesh()->PrimaryComponentTick);
	}

	// Death can arrive from the server path or from replication; both end up
	// in the same handler.
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AFPSCharacter::HandleDeath);
		// Initial replicated death may predate this subscription. Delegates do not
		// replay old broadcasts, so seed presentation from the persistent state too.
		if (HealthComponent->IsDead()) HandleDeath(nullptr);
	}

	// The lag compensation history samples Mesh3P's bones. Without this the
	// dedicated server may skip pose evaluation and every hitbox collapses onto
	// the body root.
	if (Mesh3P)
	{
		Mesh3P->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh3P->bNoSkeletonUpdate = false;
	}

	if (HasAuthority())
	{
		if (UFPSLagCompensationSubsystem* LagComp = UFPSLagCompensationSubsystem::Get(this))
		{
			LagComp->RegisterCharacter(this);
		}
	}
}

void AFPSCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bCharacterEndingPlay = true;
	FPSAnimation::Stop(ActiveHitReactionMontages, 0.f);
	if (GetMesh() && PoseReadyHandle.IsValid())
		GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(PoseReadyHandle);
	PoseReadyHandle.Reset();
	if (HealthComponent) HealthComponent->OnDeath.RemoveDynamic(this, &AFPSCharacter::HandleDeath);
	if (UFPSLagCompensationSubsystem* LagComp = UFPSLagCompensationSubsystem::Get(this))
	{
		LagComp->UnregisterCharacter(this);
	}

	Super::EndPlay(EndPlayReason);
}

float AFPSCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	// Single authoritative path: only the server applies damage. Clients get
	// the result through the replicated health value.
	if (bCharacterEndingPlay || !HasAuthority() || !HealthComponent)
	{
		return 0.f;
	}

	// Copy the accepted point hit before health delegates may destroy/travel the pawn.
	const bool bPointDamage = DamageEvent.IsOfType(FPointDamageEvent::ClassID);
	const FPointDamageEvent Point = bPointDamage ? static_cast<const FPointDamageEvent&>(DamageEvent) : FPointDamageEvent();
	const float Applied = HealthComponent->ApplyDamage(DamageAmount, EventInstigator, DamageCauser);
	if (bPointDamage && Applied > 0.f && !bCharacterEndingPlay && !IsActorBeingDestroyed() && !IsDead() && HitReactionComponent)
		HitReactionComponent->HandleAcceptedPointDamage(Applied, Point.ShotDirection, Point.HitInfo.ImpactPoint, Point.HitInfo.BoneName);
	return Applied;
}

void AFPSCharacter::PlayHitReactionMontages(int32 Index)
{
	if (bCharacterEndingPlay || bDeathHandled || IsDead() || Index < 0 || Index >= FPSHitReaction::MontageCount) return;
	FPSAnimation::Stop(ActiveHitReactionMontages, .05f);
	auto Play = [this, Index](USkeletalMeshComponent* Body, const TArray<TObjectPtr<UAnimMontage>>& Montages)
	{
		if (bCharacterEndingPlay || bDeathHandled || IsDead() || !Body || !Body->GetAnimInstance() ||
			!Montages.IsValidIndex(Index) || !Montages[Index]) return;
		ActiveHitReactionMontages.Add({Body->GetAnimInstance(), Montages[Index]});
		FPSAnimation::Play(Body, Montages[Index]);
	};
	// The authoritative 3P pose feeds lag-compensation history even on a Dedicated Server.
	Play(Mesh3P, HitReactionMontages);
	if (GetNetMode() != NM_DedicatedServer && IsLocallyControlled()) Play(GetMesh1P(), HitReactionMontages1P);
}

bool AFPSCharacter::IsDead() const
{
	return HealthComponent && HealthComponent->IsDead();
}

void AFPSCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFPSCharacter, BirthServerTime);
	DOREPLIFETIME(AFPSCharacter, ProtectionUntil);
	DOREPLIFETIME(AFPSCharacter, ProtectionCancelledAt);
}

void AFPSCharacter::StartSpawnProtection(float Seconds)
{
	if (!HasAuthority() || IsDead()) return;
	ProtectionOrigin = GetActorLocation();
	ProtectionUntil = BirthServerTime + FMath::Max(0.f, Seconds);
	ProtectionCancelledAt = -1.;
	ForceNetUpdate();
	UE_LOG(LogTemplateCharacter, Log, TEXT("[FPSLife] Born pawn=%s birth=%.3f shieldUntil=%.3f"),
		*GetName(), BirthServerTime, ProtectionUntil);
}

void AFPSCharacter::CancelSpawnProtection()
{
	if (!HasAuthority() || ProtectionCancelledAt >= 0.) return;
	ProtectionCancelledAt = GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
	UE_LOG(LogTemplateCharacter, Log, TEXT("[FPSLife] ShieldCancelled pawn=%s time=%.3f"), *GetName(), ProtectionCancelledAt);
}

void AFPSCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	UE_LOG(LogTemplateCharacter, Log, TEXT("[FPSLife] Jump pawn=%s role=%d cancelled=%.3f"), *GetName(), int32(GetLocalRole()), ProtectionCancelledAt);
	if (HasAuthority()) CancelSpawnProtection();
}

bool AFPSCharacter::WasSpawnProtectedAt(double Time) const
{
	return FPSMatch::ProtectedAt(Time, BirthServerTime, ProtectionUntil, ProtectionCancelledAt);
}

bool AFPSCharacter::IsSpawnProtected() const
{
	if (!GetWorld() || IsDead()) return false;
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return WasSpawnProtectedAt(GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds());
}

bool AFPSCharacter::IsPresentationReady() const
{
	return HasActorBegunPlay() && !IsDead() && GetMesh() && GetMesh()->IsRegistered() &&
		GetMesh()->GetSkeletalMeshAsset() && bPoseEvaluated;
}



//////////////////////////////////////////////////////////////////////////
// Aim down sights

void AFPSCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && IsSpawnProtected())
	{
		// Server-observed movement only. Spawn falling/vertical settling does not cancel.
		if (GetVelocity().SizeSquared2D() > 1.f ||
			GetCharacterMovement()->GetCurrentAcceleration().SizeSquared2D() > 1.f ||
			FVector::DistSquared2D(GetActorLocation(), ProtectionOrigin) > 4.f)
			CancelSpawnProtection();
	}

	// Every machine needs this for turn/lean animation, so sample it before the
	// locally-controlled early out: remote proxies read it too.
	const float CurrentYaw = GetActorRotation().Yaw;
	MovementOffsetYaw = FRotator::NormalizeAxis(CurrentYaw - PreviousYaw);
	PreviousYaw = CurrentYaw;

	if (!IsLocallyControlled())
	{
		return;
	}

	if (Controller && FollowCamera)
	{
		const FRotator Offset = CombatComponent ? CombatComponent->GetCameraRecoilOffset() : FRotator::ZeroRotator;
		FollowCamera->SetWorldRotation(FPSRecoil::ApplyOffset(GetViewRotation(), FVector2D(Offset.Pitch, Offset.Yaw)));
	}

	if (bAimActive)
	{
		UpdateAim(DeltaSeconds);
	}
}

void AFPSCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (FollowCamera)
	{
		// Set this here too: camera evaluation may occur before the character Tick.
		const FRotator Offset = CombatComponent && IsLocallyControlled()
			? CombatComponent->GetCameraRecoilOffset() : FRotator::ZeroRotator;
		FollowCamera->SetWorldRotation(FPSRecoil::ApplyOffset(GetViewRotation(), FVector2D(Offset.Pitch, Offset.Yaw)));
		// Existing Blueprint defaults may still have this flag saved as true.
		FollowCamera->bUsePawnControlRotation = false;
		FollowCamera->GetCameraView(DeltaTime, OutResult);
		return;
	}
	Super::CalcCamera(DeltaTime, OutResult);
}

void AFPSCharacter::SetAiming(bool bNewAiming)
{
	if (!IsLocallyControlled() || bWantsAim == bNewAiming)
	{
		return;
	}

	bWantsAim = bNewAiming;
	if (bWantsAim)
	{
		// May clear bWantsAim again when there is no valid weapon/socket yet.
		BeginAim();
	}
	// UpdateAim reverses the same timeline when aim is released.
}

bool AFPSCharacter::ResolveAimSettings(const AFPSWeapon* Weapon, FWeaponAimSettings& OutSettings) const
{
	return CombatComponent && CombatComponent->ResolveAimSettings(Weapon, OutSettings);
}

void AFPSCharacter::BeginAim()
{
	AFPSWeapon* Weapon = CombatComponent ? CombatComponent->GetCurrentWeapon() : nullptr;

	FWeaponAimSettings Settings;
	if (!Weapon || !ResolveAimSettings(Weapon, Settings))
	{
		ResetAimState();
		return;
	}

	USkeletalMeshComponent* WeaponMesh = Weapon->GetWeaponMesh1P();
	if (!FollowCamera || !GetMesh() || !WeaponMesh ||
		!WeaponMesh->IsAttachedTo(GetMesh()) ||
		!WeaponMesh->DoesSocketExist(Settings.RearSightSocket) ||
		!WeaponMesh->DoesSocketExist(Settings.FrontSightSocket))
	{
		UE_LOG(LogTemplateCharacter, Warning,
			TEXT("ADS aborted: weapon must be attached to the arms and have sight sockets rear='%s', front='%s'."),
			*Settings.RearSightSocket.ToString(), *Settings.FrontSightSocket.ToString());
		ResetAimState();
		return;
	}

	if (AimWeapon.IsValid())
	{
		if (USkeletalMeshComponent* PreviousMesh = AimWeapon->GetWeaponMesh1P())
		{
			AimAlignmentTick.RemovePrerequisite(PreviousMesh, PreviousMesh->PrimaryComponentTick);
		}
	}
	AimAlignmentTick.AddPrerequisite(WeaponMesh, WeaponMesh->PrimaryComponentTick);

	AimSettings = Settings;
	AimTime = Settings.AimTime > 0.f ? Settings.AimTime : 0.2f;
	AimTargetFOV = Settings.AimFOV > 0.f
		? Settings.AimFOV
		: CombatComponent->GetFovByWeaponZoomRatio(Weapon);
	AimWeapon = Weapon;

	bAimActive = true;
}

void AFPSCharacter::NotifyWeaponEquipped(AFPSWeapon* NewWeapon)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	// Also covers the case where the player pressed aim before the weapon replicated.
	const bool bShouldAim = CombatComponent && CombatComponent->IsAiming();
	if (!bShouldAim)
	{
		ResetAimState();
		return;
	}

	if (!NewWeapon)
	{
		ResetAimState();
		return;
	}

	// Retain timeline progress while resolving the newly equipped weapon.
	bWantsAim = true;
	BeginAim();
}

void AFPSCharacter::UpdateAim(float DeltaSeconds)
{
	// Advance the blend. It can reverse mid-transition, so rapid taps do not snap.
	// Reach exact endpoints so ADS fully aligns and the return restores the hip transform.
	const float Target = bWantsAim ? 1.f : 0.f;
	const float Step = DeltaSeconds / FMath::Max(AimTime, KINDA_SMALL_NUMBER);
	if (AimAlpha < Target)
	{
		AimAlpha = FMath::Min(AimAlpha + Step, Target);
	}
	else if (AimAlpha > Target)
	{
		AimAlpha = FMath::Max(AimAlpha - Step, Target);
	}

	// Optional per-weapon easing curve. Its X axis is seconds, exactly like the
	// Timeline that used to play it, so sample it over the same AimTime span the
	// blend advances through. (Sampling a 0..0.2s curve at the normalized alpha
	// would jump straight to the curve's end and make the transition snap.)
	float Eased = AimAlpha;
	if (AimSettings.AimCurve)
	{
		Eased = FMath::Clamp(
			AimSettings.AimCurve->GetFloatValue(AimAlpha * AimTime), 0.f, 1.f);
	}
	// Force complete alignment/restoration even if the curve endpoints are imprecise.
	if (AimAlpha >= 1.f)
	{
		Eased = 1.f;
	}
	else if (AimAlpha <= 0.f)
	{
		Eased = 0.f;
	}

	AimBlendAlpha = Eased;
	if (FollowCamera)
	{
		FollowCamera->SetFieldOfView(FMath::Lerp(DefaultFoV, AimTargetFOV, Eased));
	}
	if (!bWantsAim && AimAlpha <= 0.f)
	{
		ResetAimState();
	}
}

void FFPSAimAlignmentTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (IsValid(Target) && TickType != LEVELTICK_ViewportsOnly)
	{
		Target->AlignFirstPersonMesh();
	}
}

FString FFPSAimAlignmentTickFunction::DiagnosticMessage()
{
	return TEXT("FFPSAimAlignmentTickFunction");
}

void AFPSCharacter::RegisterActorTickFunctions(bool bRegister)
{
	Super::RegisterActorTickFunctions(bRegister);
	if (bRegister)
	{
		AimAlignmentTick.Target = this;
		AimAlignmentTick.RegisterTickFunction(GetLevel());
	}
	else
	{
		AimAlignmentTick.UnRegisterTickFunction();
	}
}

void AFPSCharacter::AlignFirstPersonMesh()
{
	if (!IsLocallyControlled() || !bAimActive)
	{
		return;
	}
	USkeletalMeshComponent* Arms = GetMesh();
	USkeletalMeshComponent* WeaponMesh = AimWeapon.IsValid() ? AimWeapon->GetWeaponMesh1P() : nullptr;
	if (!FollowCamera || !Arms || Arms->GetAttachParent() != FollowCamera ||
		!WeaponMesh || !WeaponMesh->IsAttachedTo(Arms) ||
		!WeaponMesh->DoesSocketExist(AimSettings.RearSightSocket) ||
		!WeaponMesh->DoesSocketExist(AimSettings.FrontSightSocket))
	{
		ResetAimState();
		return;
	}

	// Remove the CURRENT arms transform before solving. Otherwise our previous
	// translation/rotation feeds back into the next target and causes drift.
	const FTransform ArmsWorld = Arms->GetComponentTransform();
	const FVector Rear = ArmsWorld.InverseTransformPosition(
		WeaponMesh->GetSocketLocation(AimSettings.RearSightSocket));
	const FVector Front = ArmsWorld.InverseTransformPosition(
		WeaponMesh->GetSocketLocation(AimSettings.FrontSightSocket));
	const FVector Up = ArmsWorld.InverseTransformVector(
		WeaponMesh->GetComponentTransform().TransformVector(AimSettings.SightUpDirection));

	FTransform Target;
	if (!FPSAim::SolveSightAlignment(Rear, Front, Up, Arms->GetRelativeScale3D(),
		AimSettings.EyeRelief, Target))
	{
		UE_LOG(LogTemplateCharacter, Warning,
			TEXT("ADS aborted for '%s': sight points must be distinct, up must not be parallel to the sight axis, "
				"scale must be nonzero and EyeRelief must be finite and nonnegative."), *AimWeapon->GetName());
		ResetAimState();
		return;
	}

	const FVector Location = FMath::Lerp(
		Mesh1PDefaultRelativeLocation, Target.GetLocation(), AimBlendAlpha);
	const FQuat Rotation = FQuat::Slerp(Mesh1PDefaultRelativeRotation.Quaternion(),
		Target.GetRotation(), AimBlendAlpha).GetNormalized();
	Arms->SetRelativeLocationAndRotation(Location, Rotation);

	if (CVarFPSAimDebug.GetValueOnGameThread() != 0)
	{
		DrawSightDebug(this, FollowCamera,
			WeaponMesh->GetSocketLocation(AimSettings.RearSightSocket),
			WeaponMesh->GetSocketLocation(AimSettings.FrontSightSocket));
	}
}

void AFPSCharacter::ResetAimState()
{
	if (AimWeapon.IsValid())
	{
		if (USkeletalMeshComponent* WeaponMesh = AimWeapon->GetWeaponMesh1P())
		{
			AimAlignmentTick.RemovePrerequisite(WeaponMesh, WeaponMesh->PrimaryComponentTick);
		}
	}
	bWantsAim = false;
	bAimActive = false;
	AimAlpha = 0.f;
	AimBlendAlpha = 0.f;
	AimWeapon = nullptr;
	if (GetMesh())
	{
		GetMesh()->SetRelativeLocationAndRotation(
			Mesh1PDefaultRelativeLocation, Mesh1PDefaultRelativeRotation);
	}
	if (FollowCamera)
	{
		FollowCamera->SetFieldOfView(DefaultFoV);
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void AFPSCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// Unpossessed: restore the cosmetic ADS offset and FOV.
	if (!Controller)
	{
		ResetAimState();
	}

	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void AFPSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFPSCharacter::Move);

		// Crouching: held down to stay crouched, released to stand up.
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this, &AFPSCharacter::StartCrouch);
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AFPSCharacter::StopCrouch);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFPSCharacter::Look);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

FRotator AFPSCharacter::GetFixedAimRotation() const
{
	
	FRotator Rotation = GetBaseAimRotation();
	
	if (Rotation.Pitch > 90.f && !IsLocallyControlled())
	{
		const FVector2D InRange(270.f,360.f);
		const FVector2D OutRange(-90.0f,0.f);
		
		Rotation.Pitch = FMath::GetMappedRangeValueClamped(InRange,OutRange,Rotation.Pitch);
	}
	
	return Rotation;
}

void AFPSCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	
		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AFPSCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

//////////////////////////////////////////////////////////////////////////
// Crouch

void AFPSCharacter::StartCrouch()
{
	// Routes to UCharacterMovementComponent::Crouch, which sets bWantsToCrouch. That
	// flag travels in the move packets, so a client can crouch without its own RPC.
	Crouch();
}

void AFPSCharacter::StopCrouch()
{
	UnCrouch();
}

void AFPSCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	const FVector ArmsLocation = GetMesh() ? GetMesh()->GetRelativeLocation() : Mesh1PDefaultRelativeLocation;
	const FRotator ArmsRotation = GetMesh() ? GetMesh()->GetRelativeRotation() : Mesh1PDefaultRelativeRotation;
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Super() moved the inherited Mesh by the height change to keep its feet on the
	// ground. Here that mesh is the first-person arms hanging off the camera, so put
	// them back where they belong.
	if (GetMesh())
	{
		GetMesh()->SetRelativeLocationAndRotation(ArmsLocation, ArmsRotation);
	}

	// The capsule centre dropped by HalfHeightAdjust, and Mesh3P is parented to it, so
	// lift the body by the same amount to keep its feet on the ground.
	if (Mesh3P)
	{
		Mesh3P->SetRelativeLocation(Mesh3PDefaultRelativeLocation + FVector(0.f, 0.f, HalfHeightAdjust));
	}
}

void AFPSCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	const FVector ArmsLocation = GetMesh() ? GetMesh()->GetRelativeLocation() : Mesh1PDefaultRelativeLocation;
	const FRotator ArmsRotation = GetMesh() ? GetMesh()->GetRelativeRotation() : Mesh1PDefaultRelativeRotation;
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	if (GetMesh())
	{
		GetMesh()->SetRelativeLocationAndRotation(ArmsLocation, ArmsRotation);
	}

	if (Mesh3P)
	{
		Mesh3P->SetRelativeLocation(Mesh3PDefaultRelativeLocation);
	}
}

void AFPSCharacter::HandleDeath_Implementation(AController* Killer)
{
	if (bDeathHandled)
	{
		return;
	}
	bDeathHandled = true;
	FPSAnimation::Stop(ActiveHitReactionMontages);
	if (AFPSPlayerController* PC = Cast<AFPSPlayerController>(GetController()))
	{
		if (PC->IsLocalController()) PC->BeginDeathCamera(this);
		else if (HasAuthority()) PC->Client_BeginDeathCamera(this);
	}

	// Restore the arms and FOV before applying death visuals.
	ResetAimState();

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Death means immediate visual removal in this prototype. Keep the Pawn
	// alive for its death camera/respawn task; hide components, not the Actor,
	// so bHidden + disabled root collision cannot cut off death replication.
	if (GetMesh())
	{
		GetMesh()->SetHiddenInGame(true, true);
		GetMesh()->SetCastHiddenShadow(false);
		GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (Mesh3P)
	{
		Mesh3P->SetHiddenInGame(true, true);
		Mesh3P->SetCastHiddenShadow(false);
		Mesh3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (CombatComponent)
	{
		CombatComponent->OnOwnerDied();
	}

	if (HasAuthority())
	{
		if (AFPSGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSGameMode>() : nullptr)
		{
			GameMode->NotifyPlayerKilled(GetController(), Killer);
		}
	}
	
}
