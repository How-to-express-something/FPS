// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FPSGameMode.generated.h"

class AController;
class APlayerState;
class APawn;
class AFPSHealthPickup;
class AFPSCharacter;

UCLASS(minimalapi)
class AFPSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFPSGameMode();
	virtual void InitGameState() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	bool RegisterHealthPickup(AFPSHealthPickup* Pickup);
	void UnregisterHealthPickup(AFPSHealthPickup* Pickup);
	bool TryCollectHealthPickup(AFPSHealthPickup* Pickup, AFPSCharacter* Character);
	virtual void Logout(AController* Exiting) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& Error) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id, FString& Error) override;
	virtual APlayerController* Login(UPlayer* Player, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& Id, FString& Error) override;

	/** Server-side kill bookkeeping: K/D, team score and a delayed respawn. */
	UFUNCTION(BlueprintCallable, Category = "Match")
	void NotifyPlayerKilled(AController* VictimController, AController* KillerController);

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditDefaultsOnly, Category="DeathMatch") int32 KillsToWin = 40;
	UPROPERTY(EditDefaultsOnly, Category="DeathMatch") float SpawnProtectionSeconds = 3.f;
	UPROPERTY(EditDefaultsOnly, Category="DeathMatch") float ResultsSeconds = 6.f;
	UPROPERTY(EditDefaultsOnly, Category="DeathMatch") FString LobbyMap = TEXT("/Game/FPS/Maps/Lobby_Map");
	struct FRespawnTask { FTimerHandle Timer; TWeakObjectPtr<APawn> DeadPawn; uint64 Generation = 0; };
	TMap<TWeakObjectPtr<AController>, FRespawnTask> RespawnTasks;
	TSet<TWeakObjectPtr<APawn>> ScoredDeaths;
	FTimerHandle ReturnTimer;
	uint64 RespawnGeneration = 0;
	bool bClosing = false;
	TMap<TWeakObjectPtr<AFPSHealthPickup>, FVector> HealthPickupPositions;
	UPROPERTY(EditDefaultsOnly, Category="DeathMatch|Pickup") TSubclassOf<AFPSHealthPickup> HealthPickupClass;
	UPROPERTY(EditDefaultsOnly, Category="DeathMatch|Pickup", meta=(ClampMin="0",ClampMax="128")) int32 MaxHealthPickups = 32;
	void DropHealthPickup(APawn* DeadPawn);
	void ClearHealthPickups();
	void FinishDeathMatch(APlayerState* Winner);
	void ReturnToLobby();
	bool ShouldRejectSessionLogin(const UPlayer* Player) const;
	/** Set only by the authoritative lobby travel URL; direct sandbox play stays unchanged. */
	bool bSessionMatch = false;
	int32 SessionCapacity = 4;
	/** Seconds between death and respawn. Zero respawns immediately. */
	UPROPERTY(EditDefaultsOnly, Category = "Match", meta = (ClampMin = "0.0"))
	float RespawnDelay = 3.f;

	/** Unpossesses the corpse, destroys it shortly after, and restarts the player. */
	void RespawnPlayer(AController* Controller);
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSLobbyTestAccess;
	friend struct FFPSMatchTestAccess;
	friend struct FFPSFeedbackTestAccess;
#endif
};
