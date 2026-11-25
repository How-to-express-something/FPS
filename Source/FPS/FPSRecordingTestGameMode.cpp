#include "FPSRecordingTestGameMode.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

bool AFPSRecordingTestGameMode::IsListenHostObserver(const APawn* Pawn)
{
	const UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World || World->GetNetMode() != NM_ListenServer) return false;
	const auto* Mode = World->GetAuthGameMode<AFPSRecordingTestGameMode>();
	const auto* PC = Cast<APlayerController>(Pawn->GetController());
	return Mode && Mode->bHideListenHostFirstPerson && PC && PC->IsLocalPlayerController();
}
