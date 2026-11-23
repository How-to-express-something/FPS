// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSLagCompensationTypes.h"

#include "FPSCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	const TArray<FPSLagComp::FHitboxDef>& GetHitboxesInternal()
	{
		static const TArray<FPSLagComp::FHitboxDef> Defs = []()
		{
			TArray<FPSLagComp::FHitboxDef> Out;
			auto Add = [&Out](const TCHAR* Name, const TCHAR* Bone, const TCHAR* BoneEnd,
				float Radius, bool bHeadshot)
			{
				FPSLagComp::FHitboxDef Def;
				Def.Name = FName(Name);
				Def.Bone = FName(Bone);
				Def.BoneEnd = BoneEnd ? FName(BoneEnd) : NAME_None;
				Def.Radius = Radius;
				Def.bHeadshot = bHeadshot;
				Out.Add(Def);
			};

			// Mannequin skeleton bone names.
			Add(TEXT("head"), TEXT("head"), nullptr, 11.f, true);
			Add(TEXT("neck"), TEXT("neck_01"), nullptr, 7.f, false);
			Add(TEXT("chest"), TEXT("spine_03"), nullptr, 18.f, false);
			Add(TEXT("spine"), TEXT("spine_01"), nullptr, 16.f, false);
			Add(TEXT("pelvis"), TEXT("pelvis"), nullptr, 17.f, false);
			Add(TEXT("upperarm_l"), TEXT("upperarm_l"), nullptr, 8.f, false);
			Add(TEXT("upperarm_r"), TEXT("upperarm_r"), nullptr, 8.f, false);
			Add(TEXT("lowerarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), 5.5f, false);
			Add(TEXT("lowerarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"), 5.5f, false);
			Add(TEXT("hand_l"), TEXT("hand_l"), nullptr, 6.f, false);
			Add(TEXT("hand_r"), TEXT("hand_r"), nullptr, 6.f, false);
			Add(TEXT("thigh_l"), TEXT("thigh_l"), TEXT("calf_l"), 10.f, false);
			Add(TEXT("thigh_r"), TEXT("thigh_r"), TEXT("calf_r"), 10.f, false);
			Add(TEXT("calf_l"), TEXT("calf_l"), TEXT("foot_l"), 8.f, false);
			Add(TEXT("calf_r"), TEXT("calf_r"), TEXT("foot_r"), 8.f, false);
			return Out;
		}();
		return Defs;
	}

	const TArray<FName>& GetBonesInternal()
	{
		static const TArray<FName> Bones = []()
		{
			TArray<FName> Out;
			for (const FPSLagComp::FHitboxDef& Def : GetHitboxesInternal())
			{
				if (!Out.Contains(Def.Bone))
				{
					Out.Add(Def.Bone);
				}
				if (!Def.BoneEnd.IsNone() && !Out.Contains(Def.BoneEnd))
				{
					Out.Add(Def.BoneEnd);
				}
			}
			return Out;
		}();
		return Bones;
	}
}

int32 FPSLagComp::TimeToFrame(double ServerTime)
{
	return FMath::FloorToInt32(ServerTime * static_cast<double>(ServerFrameRate));
}

double FPSLagComp::FrameToTime(int32 Frame)
{
	return static_cast<double>(Frame) / static_cast<double>(ServerFrameRate);
}

const TArray<FPSLagComp::FHitboxDef>& FPSLagComp::GetHitboxDefs()
{
	return GetHitboxesInternal();
}

const TArray<FName>& FPSLagComp::GetRecordedBones()
{
	return GetBonesInternal();
}

void FPSLagComp::SampleCharacter(const AFPSCharacter* Character, FSampleFrame& OutSample)
{
	OutSample = FSampleFrame();
	if (!Character)
	{
		return;
	}

	// The body capsule is always available, even on a dedicated server that is
	// not evaluating animation. It doubles as the broadphase and as the fallback
	// hit shape when no bones could be sampled.
	if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const FVector Center = Capsule->GetComponentLocation();
		const FVector Up = Capsule->GetUpVector();
		const float SegmentHalf = FMath::Max(HalfHeight - Radius, 0.f);

		OutSample.CapsuleA = Center - Up * SegmentHalf;
		OutSample.CapsuleB = Center + Up * SegmentHalf;
		OutSample.CapsuleRadius = Radius;
		OutSample.bValid = true;
	}

	// Mesh3P is the body everyone else sees; the first-person arms are not a
	// valid hit target.
	const USkeletalMeshComponent* Mesh = Character->GetMesh3P();
	if (!Mesh)
	{
		return;
	}

	const TArray<FName>& Bones = GetRecordedBones();
	OutSample.BoneCount = FMath::Min(Bones.Num(), MaxRecordedBones);
	for (int32 Index = 0; Index < OutSample.BoneCount; ++Index)
	{
		if (Mesh->GetBoneIndex(Bones[Index]) != INDEX_NONE)
		{
			OutSample.BoneTransforms[Index] = Mesh->GetBoneTransform(Bones[Index], RTS_World);
			OutSample.BoneValid[Index] = true;
			OutSample.bValid = true;
		}
	}
}

bool FPSLagComp::RaySphere(const FVector& Origin, const FVector& Dir, const FVector& Center,
	float Radius, float MaxDist, float& OutDistance)
{
	const FVector Direction = Dir.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	const FVector Offset = Origin - Center;
	const float B = FVector::DotProduct(Offset, Direction);
	const float C = FVector::DotProduct(Offset, Offset) - Radius * Radius;

	// Outside the sphere and pointing away from it.
	if (C > 0.f && B > 0.f)
	{
		return false;
	}

	const float Discriminant = B * B - C;
	if (Discriminant < 0.f)
	{
		return false;
	}

	const float SqrtDisc = FMath::Sqrt(Discriminant);
	float T = -B - SqrtDisc;
	if (T < 0.f)
	{
		T = -B + SqrtDisc;
	}

	if (T < 0.f || T > MaxDist)
	{
		return false;
	}

	OutDistance = T;
	return true;
}

bool FPSLagComp::RayCapsule(const FVector& Origin, const FVector& Dir, const FVector& A,
	const FVector& B, float Radius, float MaxDist, float& OutDistance)
{
	const FVector Direction = Dir.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	const FVector AxisVector = B - A;
	const float Length = AxisVector.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return RaySphere(Origin, Direction, A, Radius, MaxDist, OutDistance);
	}

	const FVector Axis = AxisVector / Length;
	const FVector Offset = Origin - A;
	const float OriginAlongAxis = FVector::DotProduct(Offset, Axis);
	const float DirAlongAxis = FVector::DotProduct(Direction, Axis);

	const FVector OriginPerp = Offset - Axis * OriginAlongAxis;
	const FVector DirPerp = Direction - Axis * DirAlongAxis;

	const float QA = FVector::DotProduct(DirPerp, DirPerp);
	const float QB = 2.f * FVector::DotProduct(OriginPerp, DirPerp);
	const float QC = FVector::DotProduct(OriginPerp, OriginPerp) - Radius * Radius;

	float Best = TNumericLimits<float>::Max();

	// Infinite cylinder, clipped to the segment's axial range.
	if (QA > KINDA_SMALL_NUMBER)
	{
		const float Discriminant = QB * QB - 4.f * QA * QC;
		if (Discriminant >= 0.f)
		{
			const float SqrtDisc = FMath::Sqrt(Discriminant);
			const float Roots[2] =
			{
				(-QB - SqrtDisc) / (2.f * QA),
				(-QB + SqrtDisc) / (2.f * QA)
			};

			for (const float T : Roots)
			{
				if (T < 0.f || T > MaxDist)
				{
					continue;
				}

				const float AlongAxis = OriginAlongAxis + T * DirAlongAxis;
				if (AlongAxis >= 0.f && AlongAxis <= Length)
				{
					Best = FMath::Min(Best, T);
				}
			}
		}
	}
	else if (QC <= 0.f && OriginAlongAxis >= 0.f && OriginAlongAxis <= Length)
	{
		// Parallel to the axis and already inside the cylinder.
		Best = 0.f;
	}

	// End caps. The union of the finite cylinder and the two spheres is exactly
	// a capsule.
	float SphereT = 0.f;
	if (RaySphere(Origin, Direction, A, Radius, MaxDist, SphereT))
	{
		Best = FMath::Min(Best, SphereT);
	}
	if (RaySphere(Origin, Direction, B, Radius, MaxDist, SphereT))
	{
		Best = FMath::Min(Best, SphereT);
	}

	if (Best == TNumericLimits<float>::Max())
	{
		return false;
	}

	OutDistance = Best;
	return true;
}

bool FPSLagComp::RaycastHitboxes(const FSampleFrame& Sample, const FVector& Origin,
	const FVector& Dir, float MaxDist, FHitboxHit& OutHit)
{
	const FVector Direction = Dir.GetSafeNormal();
	if (Direction.IsNearlyZero() || !Sample.bValid)
	{
		return false;
	}

	const TArray<FHitboxDef>& Defs = GetHitboxDefs();
	const TArray<FName>& Bones = GetRecordedBones();

	bool bAnyBone = false;
	for (int32 Index = 0; Index < Sample.BoneCount; ++Index)
	{
		if (Sample.BoneValid[Index])
		{
			bAnyBone = true;
			break;
		}
	}

	// Dedicated servers may not have evaluated a pose. In that case the body
	// capsule is still a usable (if coarse) hit target.
	if (!bAnyBone)
	{
		float Distance = 0.f;
		if (!RayCapsule(Origin, Direction, Sample.CapsuleA, Sample.CapsuleB,
			Sample.CapsuleRadius, MaxDist, Distance))
		{
			return false;
		}

		OutHit = FHitboxHit();
		OutHit.Distance = Distance;
		OutHit.Location = Origin + Direction * Distance;
		return true;
	}

	float Best = MaxDist;
	bool bHit = false;

	for (const FHitboxDef& Def : Defs)
	{
		const int32 BoneIndex = Bones.IndexOfByKey(Def.Bone);
		if (BoneIndex == INDEX_NONE || BoneIndex >= Sample.BoneCount || !Sample.BoneValid[BoneIndex])
		{
			continue;
		}

		const FVector Start = Sample.BoneTransforms[BoneIndex].GetLocation();
		float Distance = 0.f;

		if (Def.BoneEnd.IsNone())
		{
			if (!RaySphere(Origin, Direction, Start, Def.Radius, Best, Distance))
			{
				continue;
			}
		}
		else
		{
			const int32 EndIndex = Bones.IndexOfByKey(Def.BoneEnd);
			if (EndIndex == INDEX_NONE || EndIndex >= Sample.BoneCount || !Sample.BoneValid[EndIndex])
			{
				continue;
			}

			const FVector End = Sample.BoneTransforms[EndIndex].GetLocation();
			if (!RayCapsule(Origin, Direction, Start, End, Def.Radius, Best, Distance))
			{
				continue;
			}
		}

		bHit = true;
		Best = Distance;
		OutHit.Bone = Def.Name;
		OutHit.bHeadshot = Def.bHeadshot;
		OutHit.Distance = Distance;
		OutHit.Location = Origin + Direction * Distance;
	}

	return bHit;
}
