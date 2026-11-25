#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "FPSRecordingTestGameMode.generated.h"

/** Parent for the existing sandbox Blueprint; deliberately not DeathMatch. */
UCLASS()
class FPS_API AFPSRecordingTestGameMode : public AGameMode
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, Category="Recording")
	bool bHideListenHostFirstPerson = true;
	/** Local presentation policy only; never excludes a target from authoritative collision. */
	static bool IsListenHostObserver(const APawn* Pawn);
};
