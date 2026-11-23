#pragma once

#include "CoreMinimal.h"
#include "FPSSessionTypes.generated.h"

UENUM(BlueprintType)
enum class EFPSSessionFlow : uint8
{
	Idle, Creating, Searching, Joining, Connecting, InLobby, InMatch, Destroying, Recovering, Error
};

/** Presentation snapshot. Generation + index resolves the ORIGINAL OSS search result. */
USTRUCT(BlueprintType)
struct FPS_API FFPSRoomSearchResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int64 Generation = 0;
	UPROPERTY(BlueprintReadOnly) int32 ResultIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) FString RoomName;
	UPROPERTY(BlueprintReadOnly) int32 PlayerCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 Capacity = 0;
	UPROPERTY(BlueprintReadOnly) int32 PingMs = 0;
	UPROPERTY(BlueprintReadOnly) bool bCompatible = false;
};

namespace FPSSession
{
	inline constexpr int32 ProtocolVersion = 1;
	inline constexpr int32 MaxCapacity = 16;
	inline const FName RoomNameKey(TEXT("FPS_ROOM"));
	inline const FName ProtocolKey(TEXT("FPS_PROTOCOL"));
	inline const FName ModeKey(TEXT("FPS_MODE"));
	inline const FString Mode(TEXT("FPS_LAN"));
}
