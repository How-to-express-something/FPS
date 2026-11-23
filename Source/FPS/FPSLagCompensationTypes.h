// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AFPSCharacter;

/**
 * Gameplay-level lag compensation primitives.
 *
 * The server samples every character's bone transforms at a fixed 60 Hz frame
 * clock. A shot carries the frame the shooter was rendering; the server rewinds
 * to that frame and runs the same hitbox math the client used for prediction.
 */
namespace FPSLagComp
{
	/** Server history clock. */
	constexpr int32 ServerFrameRate = 60;

	/** Ring buffer length: 64 frames at 60 Hz is about 1.07 seconds. */
	constexpr int32 HistoryFrameCount = 64;

	/** Maximum number of distinct bones sampled per character. */
	constexpr int32 MaxRecordedBones = 20;

	/** Frames the server may backfill after a hitch. */
	constexpr int32 MaxFillFrames = 8;

	/** How far into the future a client frame may be before the request is refused. */
	constexpr int32 FutureFrameTolerance = 2;

	/** One character pose at one server frame. */
	struct FSampleFrame
	{
		int32 Frame = INDEX_NONE;
		bool bValid = false;

		/** Body capsule at sample time, used as a broadphase. */
		FVector CapsuleA = FVector::ZeroVector;
		FVector CapsuleB = FVector::ZeroVector;
		float CapsuleRadius = 0.f;

		int32 BoneCount = 0;
		bool BoneValid[MaxRecordedBones] = {};
		FTransform BoneTransforms[MaxRecordedBones];
	};

	/** One sphere or capsule hitbox defined in bone space. */
	struct FHitboxDef
	{
		FName Name;
		FName Bone;
		/** NAME_None for a sphere; otherwise the capsule's end bone. */
		FName BoneEnd;
		float Radius = 0.f;
		bool bHeadshot = false;
	};

	struct FHitboxHit
	{
		FName Bone;
		bool bHeadshot = false;
		float Distance = 0.f;
		FVector Location = FVector::ZeroVector;
	};

	/** Result of a server-side rewound shot. */
	struct FResolvedShot
	{
		bool bHit = false;
		/** True when the ray hit the world; Location/Normal describe that impact. */
		bool bBlockedByWorld = false;

		AFPSCharacter* Character = nullptr;
		FName Bone;
		bool bHeadshot = false;
		float Distance = 0.f;
		FVector Location = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;

		int32 RequestedFrame = 0;
		int32 ResolvedFrame = 0;
		bool bFrameClamped = false;
		bool bHaveHistory = false;
	};

	/** Server time (seconds) to the fixed-rate frame number. */
	FPS_API int32 TimeToFrame(double ServerTime);

	/** Inverse of TimeToFrame, for debug output. */
	FPS_API double FrameToTime(int32 Frame);

	/** Static hitbox table in a stable order. */
	FPS_API const TArray<FHitboxDef>& GetHitboxDefs();

	/** Unique bone names referenced by the hitbox table. */
	FPS_API const TArray<FName>& GetRecordedBones();

	/** Samples Mesh3P bones and the body capsule of a character. */
	FPS_API void SampleCharacter(const AFPSCharacter* Character, FSampleFrame& OutSample);

	FPS_API bool RaySphere(const FVector& Origin, const FVector& Dir, const FVector& Center,
		float Radius, float MaxDist, float& OutDistance);

	FPS_API bool RayCapsule(const FVector& Origin, const FVector& Dir, const FVector& A,
		const FVector& B, float Radius, float MaxDist, float& OutDistance);

	/** Nearest hitbox along the ray, or false. Falls back to the body capsule
	 *  when the recorded pose has no valid bones. */
	FPS_API bool RaycastHitboxes(const FSampleFrame& Sample, const FVector& Origin,
		const FVector& Dir, float MaxDist, FHitboxHit& OutHit);
}
