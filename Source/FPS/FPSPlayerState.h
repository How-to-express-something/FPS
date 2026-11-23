// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "FPSPlayerState.generated.h"

/** Per-player replicated state, e.g. scoreboard counters. */
UCLASS()
class FPS_API AFPSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AFPSPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Score")
	int32 GetKills() const { return Kills; }

	UFUNCTION(BlueprintPure, Category = "Score")
	int32 GetDeaths() const { return Deaths; }

	void AddKill() { ++Kills; }
	void AddDeath() { ++Deaths; }

	UFUNCTION(BlueprintPure, Category = "Lobby")
	bool IsLobbyReady() const { return bLobbyReady; }

	/** Only a validated LobbyGameMode request writes this authoritative state. */
	void SetLobbyReady(bool bReady);

protected:
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Lobby")
	bool bLobbyReady = false;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Score")
	int32 Kills = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Score")
	int32 Deaths = 0;
};
