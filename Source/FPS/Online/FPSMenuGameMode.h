#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "FPSMenuGameMode.generated.h"

class UMainMenuWidget;

UCLASS()
class FPS_API AFPSMenuPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AFPSMenuPlayerController();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditDefaultsOnly, Category="Session") TSubclassOf<UMainMenuWidget> MainMenuWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UMainMenuWidget> MainMenuWidget;
};

UCLASS()
class FPS_API AFPSMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AFPSMenuGameMode();
};
