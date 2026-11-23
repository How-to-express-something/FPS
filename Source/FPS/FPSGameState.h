// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FPSGameState.generated.h"

/** Replicated, match-wide state shared by every client. */
UCLASS()
class FPS_API AFPSGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AFPSGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Match")
	int32 GetTeamScore() const { return TeamScore; }

	void AddTeamScore(int32 Amount) { TeamScore += Amount; }
	UFUNCTION(BlueprintPure, Category="Match") bool HasFinished() const { return bFinished; }
	UFUNCTION(BlueprintPure, Category="Match") APlayerState* GetWinner() const { return Winner; }
	void Finish(APlayerState* InWinner);
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match") int32 KillTarget = 40;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match") double ReturnToLobbyTime = 0.;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match") FString WinnerName;

protected:
	UPROPERTY(Replicated) bool bFinished = false;
	UPROPERTY(Replicated) TObjectPtr<APlayerState> Winner;
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Match")
	int32 TeamScore = 0;
};
