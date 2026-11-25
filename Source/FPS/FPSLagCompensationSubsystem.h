// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FPSCombatTypes.h"
#include "FPSLagCompensationTypes.h"
#include "FPSLagCompensationSubsystem.generated.h"

class AFPSCharacter;
class APlayerController;

/**
 * Records a server-side history of every character's pose at a fixed 60 Hz and
 * resolves shots against the frame the shooter was rendering.
 *
 * History lives only on the server: it is never replicated. Clients use the
 * same FPSLagComp sampling helpers for local prediction.
 */
UCLASS()
class FPS_API UFPSLagCompensationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Returns the subsystem for the given world context, or null. */
	static UFPSLagCompensationSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterCharacter(AFPSCharacter* Character);
	void UnregisterCharacter(AFPSCharacter* Character);

	/** Current server frame (client worlds: local estimate). */
	int32 GetServerFrame() const;

	/** Newest frame written to history, or INDEX_NONE before the first tick. */
	int32 GetNewestRecordedFrame() const { return NewestRecordedFrame; }

	/** Server frame the given client was most likely rendering. */
	int32 EstimateClientRenderFrame(const APlayerController* PC) const;

	/**
	 * Rewinds to the request's frame and resolves the hitscan against bone
	 * hitboxes, comparing with world occlusion. Returns false only when the
	 * query could not be performed; check Out.bHit for the verdict.
	 */
	bool ResolveShot(const AFPSCharacter* Shooter, const FFPSShotRequest& Request,
		float MaxRange, FPSLagComp::FResolvedShot& Out) const;

	/** Debug-draws hitboxes from N frames ago (FPS.LagComp.DrawHistory). */
	void DrawHistory(int32 FramesBack) const;

	bool IsServer() const { return bIsServer; }
	const TArray<FPSLagComp::FShotDebugSnapshot>& GetShotDebugSnapshots() const { return ShotDebugSnapshots; }

private:
	void DrawShotSnapshots();
	mutable TArray<FPSLagComp::FShotDebugSnapshot> ShotDebugSnapshots;
	struct FCharacterHistory
	{
		TStaticArray<FPSLagComp::FSampleFrame, FPSLagComp::HistoryFrameCount> Frames;
		int32 NewestFrame = INDEX_NONE;
		int32 BirthFrame = 0;
	};

	void RecordFrame(int32 Frame);
	bool FindFrame(const FCharacterHistory& History, int32 Frame,
		FPSLagComp::FSampleFrame& OutSample) const;

	TMap<TWeakObjectPtr<AFPSCharacter>, FCharacterHistory> Histories;

	int32 LastRecordedFrame = INDEX_NONE;
	int32 NewestRecordedFrame = INDEX_NONE;
	bool bIsServer = false;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSLifeHistoryTestAccess;
	friend struct FFPSNetworkDiagnosticsTestAccess;
#endif
};
