#pragma once

#include "CoreMinimal.h"
#include "FPSCombatTypes.h"

/** Value-only, game-thread telemetry. Never owns a weapon, pawn, World or callback. */
struct FPS_API FFPSNetworkDiagnostics
{
	int32 PredictedCount = 0;
	int32 AcceptedCount = 0;
	int32 RejectedCount = 0;
	int32 CorrectedCount = 0;
	int32 IgnoredCount = 0;
	int32 LastPredictedSequence = INDEX_NONE;
	int32 LastPredictedIndex = INDEX_NONE;
	int32 LastRequestFrame = INDEX_NONE;
	int32 LastAckSequence = INDEX_NONE;
	int32 LastServerIndex = INDEX_NONE;
	int32 LastServerAmmo = INDEX_NONE;
	int32 RequestedFrame = INDEX_NONE;
	int32 ResolvedFrame = INDEX_NONE;
	int32 SampleFrame = INDEX_NONE;
	int32 NewestFrame = INDEX_NONE;
	uint32 LastAckBurst = 0;
	double AckMilliseconds = -1.;
	float ActualDamage = 0.f;
	bool bHasReceipt = false;
	bool bHasHistory = false;
	bool bQueryPerformed = false;
	bool bFrameClamped = false;
	bool bOffsetsAgree = false;
	bool bHitAgrees = false;
	EFPSShotReject RejectReason = EFPSShotReject::None;
	EFPSShotOutcome Outcome = EFPSShotOutcome::Miss;
	TArray<FString> Events;

	void AddEvent(const FString& Event)
	{
		constexpr int32 Limit = 8;
		if (Events.Num() >= Limit) Events.RemoveAt(0, Events.Num() - Limit + 1, EAllowShrinking::No);
		Events.Add(FString::Printf(TEXT("%.3f %s"), FPlatformTime::Seconds(), *Event));
	}
};
