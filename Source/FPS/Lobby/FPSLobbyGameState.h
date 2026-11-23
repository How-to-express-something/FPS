#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FPSLobbyTypes.h"
#include "FPSLobbyGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSLobbyStateChanged);

/** The roster is a view of PlayerArray; no second replicated player list. */
UCLASS()
class FPS_API AFPSLobbyGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION(BlueprintPure, Category="Lobby") FFPSLobbySnapshot GetLobbySnapshot() const { return Snapshot; }
	UFUNCTION(BlueprintPure, Category="Lobby") TArray<FFPSLobbyPlayerInfo> GetLobbyPlayers() const;
	UFUNCTION(BlueprintPure, Category="Lobby") float GetCountdownRemaining() const;
	void SetSnapshot(FFPSLobbySnapshot NewSnapshot);
	UPROPERTY(BlueprintAssignable, Category="Lobby") FFPSLobbyStateChanged OnLobbyStateChanged;
protected:
	UPROPERTY(ReplicatedUsing=OnRep_Snapshot) FFPSLobbySnapshot Snapshot;
	UFUNCTION() void OnRep_Snapshot();
};
