#include "FPSHitReactionComponent.h"
#include "FPSCharacter.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

EFPSHitDirection FPSHitReaction::ClassifyDirection(const FVector2D& LocalSource)
{
	// 45-degree boundaries choose front/back, not right/left. Vertical/no source -> front.
	if (FMath::Abs(LocalSource.X) >= FMath::Abs(LocalSource.Y))
		return LocalSource.X >= 0.f ? EFPSHitDirection::Front : EFPSHitDirection::Back;
	return LocalSource.Y >= 0.f ? EFPSHitDirection::Right : EFPSHitDirection::Left;
}

int32 FPSHitReaction::MontageIndex(EFPSHitRegion Region, EFPSHitDirection Direction)
{
	return uint8(Region) < 6 && uint8(Direction) < 4 ? int32(Region) * DirectionCount + int32(Direction) : INDEX_NONE;
}

UFPSHitReactionComponent::UFPSHitReactionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	BoneRegions.Add(TEXT("head"), EFPSHitRegion::Head);
	BoneRegions.Add(TEXT("neck_01"), EFPSHitRegion::Head);
	for (const TCHAR* Name : {TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l")})
		BoneRegions.Add(FName(Name), EFPSHitRegion::LeftArm);
	for (const TCHAR* Name : {TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")})
		BoneRegions.Add(FName(Name), EFPSHitRegion::RightArm);
	for (const TCHAR* Name : {TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l")})
		BoneRegions.Add(FName(Name), EFPSHitRegion::LeftLeg);
	for (const TCHAR* Name : {TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r")})
		BoneRegions.Add(FName(Name), EFPSHitRegion::RightLeg);
}

EFPSHitRegion UFPSHitReactionComponent::ResolveRegion(FName Bone) const
{
	const EFPSHitRegion* Region = BoneRegions.Find(Bone);
	return Region && uint8(*Region) < 6 ? *Region : EFPSHitRegion::Torso;
}

void UFPSHitReactionComponent::HandleAcceptedPointDamage(float ActualDamage, const FVector& ShotDirection,
	const FVector& ImpactPoint, FName Bone)
{
	AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	if (bEndingPlay || !Character || !Character->HasAuthority() || Character->IsDead() || Character->IsActorBeingDestroyed() ||
		!FMath::IsFinite(ActualDamage) || ActualDamage <= 0.f || ShotDirection.ContainsNaN() || ImpactPoint.ContainsNaN()) return;
	// ShotDirection travels TO victim; source direction points back toward the shooter.
	FVector Source = -ShotDirection;
	if (Source.IsNearlyZero()) Source = ImpactPoint - Character->GetActorLocation();
	const FVector Local = FRotator(0.f, Character->GetActorRotation().Yaw, 0.f).UnrotateVector(Source);
	FFPSHitReactionCue Cue;
	Cue.EventId = int32(++NextEventId);
	Cue.LocalSourceDirection = FVector2D(Local.X, Local.Y).GetSafeNormal();
	Cue.Direction = FPSHitReaction::ClassifyDirection(Cue.LocalSourceDirection);
	Cue.Region = ResolveRegion(Bone);
	Cue.MontageIndex = FPSHitReaction::MontageIndex(Cue.Region, Cue.Direction);
	Cue.Bone = Bone;
	Cue.ImpactPoint = ImpactPoint;
	Cue.ActualDamage = ActualDamage;
	Cue.ServerTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogFPSAnimation, Verbose, TEXT("[FPSHitReact][Select] pawn=%s id=%d bone=%s xy=(%.3f,%.3f) index=%d damage=%.1f"),
		*GetNameSafe(Character), Cue.EventId, *Bone.ToString(), Cue.LocalSourceDirection.X, Cue.LocalSourceDirection.Y,
		Cue.MontageIndex, ActualDamage);
	Multicast_HitReaction(Cue);
}

void UFPSHitReactionComponent::Multicast_HitReaction_Implementation(const FFPSHitReactionCue& Cue)
{
	AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	if (bEndingPlay || !Character || Character->IsDead() || Character->IsActorBeingDestroyed() ||
		!FMath::IsFinite(Cue.ActualDamage) || Cue.ActualDamage <= 0.f || !FMath::IsFinite(Cue.ServerTime) ||
		Cue.LocalSourceDirection.ContainsNaN() || Cue.ImpactPoint.ContainsNaN() ||
		Cue.MontageIndex != FPSHitReaction::MontageIndex(Cue.Region, Cue.Direction) || Cue.MontageIndex == INDEX_NONE) return;
	const uint32 Id = uint32(Cue.EventId);
	const uint32 Delta = Id - LastReceivedEventId;
	if (bHasReceivedCue && (Delta == 0 || Delta >= 0x80000000u)) return;
	// Commit event identity before any montage/delegate can re-enter.
	bHasReceivedCue = true;
	LastReceivedEventId = Id;
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float AgeLimit = FMath::IsFinite(MaxCueAge) ? FMath::Clamp(MaxCueAge, .1f, 2.f) : .5f;
	if (Now - Cue.ServerTime > AgeLimit) return;
	LastCue = Cue;
	UE_LOG(LogFPSAnimation, Verbose, TEXT("[FPSHitReact][Receive] pawn=%s mode=%d id=%d index=%d age=%.3f"),
		*GetNameSafe(Character), int32(GetNetMode()), Cue.EventId, Cue.MontageIndex, Now-Cue.ServerTime);
	const float Interval = FMath::IsFinite(MinMontageInterval) ? FMath::Clamp(MinMontageInterval, 0.f, 1.f) : .1f;
	if (Now - LastMontageTime >= Interval)
	{
		LastMontageTime = Now;
		Character->PlayHitReactionMontages(Cue.MontageIndex);
	}
	if (bEndingPlay || Character->IsDead() || Character->IsActorBeingDestroyed()) return;
	// Last operation: listeners may destroy the pawn, end the match or leave the room.
	OnHitReaction.Broadcast(Cue);
}

void UFPSHitReactionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	OnHitReaction.Clear();
	Super::EndPlay(Reason);
}
