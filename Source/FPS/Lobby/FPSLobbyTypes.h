#pragma once
#include "CoreMinimal.h"
#include "FPSLobbyTypes.generated.h"

UENUM(BlueprintType)
enum class EFPSLobbyPhase : uint8 { Waiting, Countdown, Traveling, Closing };

USTRUCT(BlueprintType)
struct FPS_API FFPSLobbySnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EFPSLobbyPhase Phase = EFPSLobbyPhase::Waiting;
	UPROPERTY(BlueprintReadOnly) int32 Capacity = 4;
	UPROPERTY(BlueprintReadOnly) int32 MinimumPlayers = 2;
	UPROPERTY(BlueprintReadOnly) int32 HostPlayerId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) double CountdownEndServerTime = 0.;
	UPROPERTY(BlueprintReadOnly) int32 Revision = 0;
};

USTRUCT(BlueprintType)
struct FPS_API FFPSLobbyPlayerInfo
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 PlayerId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) FString PlayerName;
	UPROPERTY(BlueprintReadOnly) float PingMs = 0.f;
	UPROPERTY(BlueprintReadOnly) bool bReady = false;
	UPROPERTY(BlueprintReadOnly) bool bHost = false;
};

/** Pure rules shared by final Login admission, the start request and timer revalidation. */
namespace FPSLobby
{
	inline bool CanJoin(EFPSLobbyPhase Phase, int32 Count, int32 Capacity)
	{
		return (Phase == EFPSLobbyPhase::Waiting || Phase == EFPSLobbyPhase::Countdown) && Count < Capacity;
	}
	inline bool CanStart(EFPSLobbyPhase Phase, int32 Count, int32 ReadyCount, int32 Minimum, int32 Capacity)
	{
		return (Phase == EFPSLobbyPhase::Waiting || Phase == EFPSLobbyPhase::Countdown)
			&& Count >= Minimum && Count <= Capacity && ReadyCount == Count;
	}
}
