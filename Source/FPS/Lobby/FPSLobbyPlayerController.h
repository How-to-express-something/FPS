#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FPSLobbyPlayerController.generated.h"

class ULobbyWidget;

UCLASS()
class FPS_API AFPSLobbyPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AFPSLobbyPlayerController();
	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Lobby") void Server_SetReady(bool bReady);
	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Lobby") void Server_StartMatch();
	UFUNCTION(Client, Reliable) void Client_LobbyError(const FString& Error);
	UFUNCTION(Client, Reliable) void Client_RoomClosing();
	UFUNCTION(BlueprintCallable, Category="Lobby") void LeaveLobby();
	FString GetLobbyError() const { return LobbyError; }
protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditDefaultsOnly, Category="Lobby") TSubclassOf<ULobbyWidget> LobbyWidgetClass;
	UPROPERTY(Transient) TObjectPtr<ULobbyWidget> LobbyWidget;
private:
	FString LobbyError;
	double LastReadyRequestTime = -1.;
	double LastStartRequestTime = -1.;
};
