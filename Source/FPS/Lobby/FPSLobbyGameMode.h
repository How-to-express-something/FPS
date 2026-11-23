#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FPSLobbyTypes.h"
#include "FPSLobbyGameMode.generated.h"

class AFPSLobbyPlayerController;

UCLASS(Config=Game)
class FPS_API AFPSLobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AFPSLobbyGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& Error) override;
	virtual void InitGameState() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id, FString& Error) override;
	virtual APlayerController* Login(UPlayer* Player, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& Id, FString& Error) override;
	virtual void PostLogin(APlayerController* PC) override;
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;
	virtual void Logout(AController* Exiting) override;
	void SetPlayerReady(AFPSLobbyPlayerController* PC, bool bReady);
	void RequestStart(AFPSLobbyPlayerController* PC);
	void CloseRoom();
	bool IsHost(const APlayerController* PC) const;
	bool AllPlayersReady() const;

	UPROPERTY(Config, EditDefaultsOnly, Category="Lobby") FString MatchMap = TEXT("/Game/LP_FPSLite_JC/Maps/Demo");
	UPROPERTY(Config, EditDefaultsOnly, Category="Lobby") int32 MinimumPlayers = 2;
	UPROPERTY(Config, EditDefaultsOnly, Category="Lobby") float CountdownSeconds = 3.f;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	int32 Capacity = 4;
	void InitializeLobbyPlayer(APlayerController* PC);
	FTimerHandle CountdownTimer;
	uint32 CountdownGeneration = 0;
	TWeakObjectPtr<APlayerController> Host;
	int32 CountConnections() const;
	FString AdmissionError(const FString& Options) const;
	FString GetTravelConfigurationError() const;
	void CancelCountdown();
	void CompleteCountdown(uint32 Generation);
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSLobbyTestAccess;
	friend struct FFPSPIETravelTestAccess;
#endif
};
