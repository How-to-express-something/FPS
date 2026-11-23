// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSLagCompensationSubsystem.h"
#include "FPSMatchRules.h"

#include "FPS.h"
#include "FPSCharacter.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSLagCompensation, Log, All);

static TAutoConsoleVariable<int32> CVarLagCompDebug(
	TEXT("FPS.LagComp.Debug"), 0,
	TEXT("1 = log every resolved shot (frame, rewind); 2 = also draw history."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarLagCompDrawHistory(
	TEXT("FPS.LagComp.DrawHistory"), 0,
	TEXT("Debug-draw every character's hitboxes N frames in the past. 0 = off."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarLagCompInterpDelay(
	TEXT("FPS.LagComp.InterpDelay"), 0.05f,
	TEXT("Extra seconds to subtract from the estimated client render time."),
	ECVF_Cheat);

UFPSLagCompensationSubsystem* UFPSLagCompensationSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UFPSLagCompensationSubsystem>() : nullptr;
}

void UFPSLagCompensationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UWorld* World = GetWorld();
	bIsServer = World && World->GetNetMode() != NM_Client;

	if (bIsServer)
	{
		UE_LOG(LogFPSLagCompensation, Log,
			TEXT("[FPSLagComp] Server history enabled (%d frames at %d Hz)."),
			FPSLagComp::HistoryFrameCount, FPSLagComp::ServerFrameRate);
	}
}

TStatId UFPSLagCompensationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFPSLagCompensationSubsystem, STATGROUP_Tickables);
}

void UFPSLagCompensationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsServer)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 TargetFrame = FPSLagComp::TimeToFrame(World->GetTimeSeconds());

	// A server tick can cover more than one 60 Hz frame (default net tick rate
	// is 30). Fill the skipped frame numbers with the same sample so that every
	// frame a client can ask for has an entry.
	if (LastRecordedFrame == INDEX_NONE)
	{
		LastRecordedFrame = TargetFrame - 1;
	}

	if (TargetFrame > LastRecordedFrame)
	{
		const int32 FillFrom = FMath::Max(LastRecordedFrame + 1,
			TargetFrame - FPSLagComp::MaxFillFrames);
		for (int32 Frame = FillFrom; Frame <= TargetFrame; ++Frame)
		{
			RecordFrame(Frame);
		}
	}

	if (CVarLagCompDrawHistory.GetValueOnGameThread() > 0)
	{
		DrawHistory(CVarLagCompDrawHistory.GetValueOnGameThread());
	}
	else if (CVarLagCompDebug.GetValueOnGameThread() >= 2)
	{
		DrawHistory(0);
	}
}

void UFPSLagCompensationSubsystem::RegisterCharacter(AFPSCharacter* Character)
{
	if (!bIsServer || !Character)
	{
		return;
	}

	FCharacterHistory& History = Histories.FindOrAdd(Character);
	History.BirthFrame = FMath::CeilToInt(Character->GetBirthServerTime() * FPSLagComp::ServerFrameRate);
}

void UFPSLagCompensationSubsystem::UnregisterCharacter(AFPSCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	Histories.Remove(TWeakObjectPtr<AFPSCharacter>(Character));
}

int32 UFPSLagCompensationSubsystem::GetServerFrame() const
{
	const UWorld* World = GetWorld();
	return World ? FPSLagComp::TimeToFrame(World->GetTimeSeconds()) : 0;
}

int32 UFPSLagCompensationSubsystem::EstimateClientRenderFrame(const APlayerController* PC) const
{
	// A listen host renders the server's own state: no rewind.
	if (!PC || PC->HasAuthority())
	{
		return GetServerFrame();
	}

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return GetServerFrame();
	}

	const float PingSeconds = PC->PlayerState
		? PC->PlayerState->GetPingInMilliseconds() / 1000.f
		: 0.f;
	const float InterpSeconds = CVarLagCompInterpDelay.GetValueOnGameThread();

	// The client renders roughly half an RTT behind the server, plus the time
	// spent interpolating remote characters.
	const double RenderServerTime =
		GameState->GetServerWorldTimeSeconds() - 0.5 * PingSeconds - InterpSeconds;

	return FPSLagComp::TimeToFrame(RenderServerTime);
}

void UFPSLagCompensationSubsystem::RecordFrame(int32 Frame)
{
	for (auto It = Histories.CreateIterator(); It; ++It)
	{
		AFPSCharacter* Character = It.Key().Get();
		if (!Character)
		{
			It.RemoveCurrent();
			continue;
		}

		// A dead body is no longer a valid target and its pose may be frozen.
		if (Character->IsDead() || !FPSMatch::FrameExistsForLife(Frame, It.Value().BirthFrame))
		{
			continue;
		}

		FPSLagComp::FSampleFrame Sample;
		FPSLagComp::SampleCharacter(Character, Sample);
		if (!Sample.bValid)
		{
			continue;
		}

		Sample.Frame = Frame;
		const int32 Slot = Frame % FPSLagComp::HistoryFrameCount;
		It.Value().Frames[Slot] = Sample;
		It.Value().NewestFrame = Frame;
	}

	LastRecordedFrame = Frame;
	NewestRecordedFrame = Frame;
}

bool UFPSLagCompensationSubsystem::FindFrame(const FCharacterHistory& History, int32 Frame,
	FPSLagComp::FSampleFrame& OutSample) const
{
	// Timestamps in the ring let us fall back to the closest earlier sample
	// when a frame number was skipped entirely.
	for (int32 Offset = 0; Offset < FPSLagComp::HistoryFrameCount; ++Offset)
	{
		const int32 Candidate = Frame - Offset;
		if (Candidate < 0)
		{
			break;
		}

		const FPSLagComp::FSampleFrame& Sample =
			History.Frames[Candidate % FPSLagComp::HistoryFrameCount];
		if (Sample.Frame == Candidate)
		{
			OutSample = Sample;
			return true;
		}
	}

	return false;
}

bool UFPSLagCompensationSubsystem::ResolveShot(const AFPSCharacter* Shooter,
	const FFPSShotRequest& Request, float MaxRange, FPSLagComp::FResolvedShot& Out) const
{
	Out = FPSLagComp::FResolvedShot();

	UWorld* World = GetWorld();
	const FVector Direction = Request.ViewDirection.GetSafeNormal();
	if (!World || Direction.IsNearlyZero() || MaxRange <= 0.f)
	{
		return false;
	}

	const FVector Start = Request.ViewLocation;
	const FVector End = Start + Direction * MaxRange;

	Out.RequestedFrame = Request.ClientFrame;
	Out.ResolvedFrame = Request.ClientFrame;
	Out.bHaveHistory = NewestRecordedFrame != INDEX_NONE;

	// Favor the shooter: clamp an old request to the oldest sample we still
	// have instead of dropping the shot. A request from the future is rejected
	// by the caller before it gets here.
	int32 ResolvedFrame = Request.ClientFrame;
	if (Out.bHaveHistory)
	{
		const int32 OldestFrame = FMath::Max(
			NewestRecordedFrame - FPSLagComp::HistoryFrameCount + 1, 0);
		ResolvedFrame = FMath::Clamp(Request.ClientFrame, OldestFrame, NewestRecordedFrame);
		Out.bFrameClamped = ResolvedFrame != Request.ClientFrame;
	}
	Out.ResolvedFrame = ResolvedFrame;

	if (Out.bFrameClamped)
	{
		UE_LOG(LogFPSLagCompensation, Verbose,
			TEXT("[FPSLagComp] Shot frame %d clamped to %d (newest %d)."),
			Request.ClientFrame, ResolvedFrame, NewestRecordedFrame);
	}

	// Static world occlusion. Characters are ignored here because they are
	// tested against their rewound hitboxes below.
	FHitResult WorldHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FPSLagCompShot), false);
	if (Shooter)
	{
		Params.AddIgnoredActor(Shooter);
	}
	for (TActorIterator<AFPSCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	const bool bWorldHit = World->LineTraceSingleByChannel(
		WorldHit, Start, End, FPSTraceChannels::ECC_Weapon, Params);

	float BestDistance = bWorldHit ? WorldHit.Distance : MaxRange;
	if (bWorldHit)
	{
		Out.bBlockedByWorld = true;
		Out.Distance = WorldHit.Distance;
		Out.Location = WorldHit.Location;
		Out.Normal = WorldHit.ImpactNormal;
	}
	else
	{
		Out.Location = End;
		Out.Normal = -Direction;
	}

	bool bHitCharacter = false;
	AFPSCharacter* BestCharacter = nullptr;
	FPSLagComp::FHitboxHit BestHit;

	for (TActorIterator<AFPSCharacter> It(World); It; ++It)
	{
		AFPSCharacter* Other = *It;
		if (!Other || Other == Shooter || Other->IsDead())
		{
			continue;
		}

		// Never give a new life a pose before it existed. Missing historical data
		// means no historical target, rather than substituting today's position.
		FPSLagComp::FSampleFrame Sample;
		bool bHaveSample = false;
		const FCharacterHistory* History = Histories.Find(TWeakObjectPtr<AFPSCharacter>(Other));
		const int32 BirthFrame = History ? History->BirthFrame :
			FMath::CeilToInt(Other->GetBirthServerTime() * FPSLagComp::ServerFrameRate);
		if (!FPSMatch::FrameExistsForLife(Request.ClientFrame, BirthFrame)) continue;
		if (Out.bHaveHistory)
		{
			if (History)
			{
				bHaveSample = FindFrame(*History, ResolvedFrame, Sample);
			}
			if (!bHaveSample) continue;
		}

		if (!bHaveSample)
		{
			FPSLagComp::SampleCharacter(Other, Sample);
		}

		if (!Sample.bValid)
		{
			continue;
		}

		// Broadphase against the rewinded body capsule.
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

		bHitCharacter = true;
		BestDistance = Hit.Distance;
		BestCharacter = Other;
		BestHit = Hit;
	}

	// Only a character closer than the wall counts; otherwise the shot is
	// blocked by geometry.
	if (bHitCharacter && (!bWorldHit || BestHit.Distance <= WorldHit.Distance))
	{
		Out.bHit = true;
		Out.bBlockedByWorld = false;
		Out.Character = BestCharacter;
		Out.Bone = BestHit.Bone;
		Out.bHeadshot = BestHit.bHeadshot;
		Out.Distance = BestHit.Distance;
		Out.Location = BestHit.Location;
		Out.Normal = -Direction;
	}

	return true;
}

void UFPSLagCompensationSubsystem::DrawHistory(int32 FramesBack) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 Newest = NewestRecordedFrame != INDEX_NONE
		? NewestRecordedFrame
		: GetServerFrame();
	const int32 TargetFrame = Newest - FMath::Max(FramesBack, 0);

	const double RewindMilliseconds =
		(Newest - TargetFrame) * (1000.0 / FPSLagComp::ServerFrameRate);

	for (const auto& Pair : Histories)
	{
		const AFPSCharacter* Character = Pair.Key.Get();
		if (!Character)
		{
			continue;
		}

		FPSLagComp::FSampleFrame Sample;
		if (!FindFrame(Pair.Value, TargetFrame, Sample))
		{
			continue;
		}

		const FColor Color = Sample.BoneValid[0] ? FColor::Green : FColor::Orange;
		for (const FPSLagComp::FHitboxDef& Def : FPSLagComp::GetHitboxDefs())
		{
			const TArray<FName>& Bones = FPSLagComp::GetRecordedBones();
			const int32 BoneIndex = Bones.IndexOfByKey(Def.Bone);
			if (BoneIndex == INDEX_NONE || BoneIndex >= Sample.BoneCount ||
				!Sample.BoneValid[BoneIndex])
			{
				continue;
			}

			const FVector Start = Sample.BoneTransforms[BoneIndex].GetLocation();
			if (Def.BoneEnd.IsNone())
			{
				DrawDebugSphere(World, Start, Def.Radius, 8, Color, false, 0.f, 0, 0.75f);
			}
			else
			{
				const int32 EndIndex = Bones.IndexOfByKey(Def.BoneEnd);
				if (EndIndex == INDEX_NONE || EndIndex >= Sample.BoneCount ||
					!Sample.BoneValid[EndIndex])
				{
					continue;
				}

				const FVector End = Sample.BoneTransforms[EndIndex].GetLocation();
				const FVector Center = (Start + End) * 0.5f;
				const FVector Axis = End - Start;
				const float HalfHeight = Axis.Size() * 0.5f + Def.Radius;
				const FQuat Rotation = Axis.IsNearlyZero()
					? FQuat::Identity
					: FRotationMatrix::MakeFromZ(Axis.GetSafeNormal()).ToQuat();
				DrawDebugCapsule(World, Center, HalfHeight, Def.Radius, Rotation,
					Color, false, 0.f, 0, 0.75f);
			}
		}

		DrawDebugCapsule(World,
			(Sample.CapsuleA + Sample.CapsuleB) * 0.5f,
			(Sample.CapsuleB - Sample.CapsuleA).Size() * 0.5f + Sample.CapsuleRadius,
			Sample.CapsuleRadius,
			FQuat::Identity, FColor::Silver, false, 0.f, 0, 0.5f);

		DrawDebugString(World, Sample.CapsuleB + FVector(0.f, 0.f, 30.f),
			FString::Printf(TEXT("%s frame %d (-%.0f ms)"),
				*GetNameSafe(Character), TargetFrame, RewindMilliseconds),
			nullptr, Color, 0.f, true, 1.f);
	}
#endif
}
