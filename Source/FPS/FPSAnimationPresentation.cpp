#include "FPSAnimationPresentation.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

DEFINE_LOG_CATEGORY(LogFPSAnimation);

bool FPSAnimation::Play(USkeletalMeshComponent* Mesh, UAnimMontage* Montage, float Rate, float Position)
{
	if (!IsValid(Mesh) || !IsValid(Montage) || !Mesh->GetAnimInstance() || !Mesh->GetSkeletalMeshAsset() ||
		!FMath::IsFinite(Rate) || Rate <= 0.f || !FMath::IsFinite(Position) || Position < 0.f ||
		Montage->GetPlayLength() <= Position || Montage->HasRootMotion())
	{
		UE_LOG(LogFPSAnimation, Verbose, TEXT("[FPSAnim] Skip mesh=%s montage=%s: missing/unready asset, invalid playback or root motion"),
			*GetNameSafe(Mesh), *GetNameSafe(Montage));
		return false;
	}
	// Require explicit retargeting for different skeleton assets. Runtime Montage_Play
	// does not enforce the editor's skeleton-compatibility picker restriction in 5.5.
	if (Mesh->GetSkeletalMeshAsset()->GetSkeleton() != Montage->GetSkeleton())
	{
		UE_LOG(LogFPSAnimation, Verbose, TEXT("[FPSAnim] Skip mesh=%s montage=%s: different Skeleton assets"),
			*GetNameSafe(Mesh), *GetNameSafe(Montage));
		return false;
	}
	const bool bPlayed = Mesh->GetAnimInstance()->Montage_Play(Montage, Rate,
		EMontagePlayReturnType::MontageLength, Position, false) > 0.f;
	UE_LOG(LogFPSAnimation, Verbose, TEXT("[FPSAnim] Play mesh=%s montage=%s rate=%.3f position=%.3f success=%d"),
		*GetNameSafe(Mesh), *GetNameSafe(Montage), Rate, Position, bPlayed ? 1 : 0);
	return bPlayed;
}

void FPSAnimation::Stop(TArray<FFPSCosmeticMontage>& Started, float BlendOut)
{
	// Montage delegates can re-enter combat/death/travel. Detach our list first.
	TArray<FFPSCosmeticMontage> Stopping = MoveTemp(Started);
	Started.Reset();
	for (const auto& Entry : Stopping)
		if (Entry.AnimInstance.IsValid() && Entry.Montage.IsValid())
			Entry.AnimInstance->Montage_Stop(BlendOut, Entry.Montage.Get());
}
