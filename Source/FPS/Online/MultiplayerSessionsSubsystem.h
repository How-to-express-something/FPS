#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "FPSSessionTypes.h"
#include "MultiplayerSessionsSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSSessionChanged);
DECLARE_LOG_CATEGORY_EXTERN(LogFPSSession, Log, All);

/** One local user's LAN session workflow. Widgets request work and subscribe; never block. */
UCLASS(Config = Game, DefaultConfig)
class FPS_API UMultiplayerSessionsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintCallable, Category="Session") bool CreateRoom(const FString& RoomName, int32 Capacity = 4);
	UFUNCTION(BlueprintCallable, Category="Session") bool FindRooms();
	UFUNCTION(BlueprintCallable, Category="Session") bool JoinRoom(int64 Generation, int32 ResultIndex);
	UFUNCTION(BlueprintCallable, Category="Session") void LeaveRoom();
	UFUNCTION(BlueprintCallable, Category="Session") void CancelPendingOperation();
	UFUNCTION(BlueprintPure, Category="Session") EFPSSessionFlow GetFlow() const { return Flow; }
	UFUNCTION(BlueprintPure, Category="Session") FString GetLastError() const { return LastError; }
	UFUNCTION(BlueprintPure, Category="Session") bool IsBusy() const;
	UFUNCTION(BlueprintPure, Category="Session") TArray<FFPSRoomSearchResult> GetRooms() const { return Rooms; }
	UPROPERTY(BlueprintAssignable, Category="Session") FFPSSessionChanged OnStateChanged;
	UPROPERTY(BlueprintAssignable, Category="Session") FFPSSessionChanged OnRoomsChanged;
	/** Invoked by the local lobby controller only after its replicated state is usable. */
	void NotifyLobbyReady();
	static FString NormalizeRoomName(const FString& Name);
	static FOnlineSessionSettings MakeLANSettings(const FString& RoomName, int32 Capacity);
	static bool IsMenuOperationAllowed(EFPSSessionFlow State);

	UPROPERTY(Config, EditAnywhere, Category="Session") FString LobbyMap = TEXT("/Game/FPS/Maps/Lobby_Map");
	UPROPERTY(Config, EditAnywhere, Category="Session") FString MenuMap = TEXT("/Game/FPS/Maps/MainMenu_Map");
	UPROPERTY(Config, EditAnywhere, Category="Session") float OperationTimeoutSeconds = 30.f;

private:
	enum class EBackendOp : uint8 { None, Create, Find, Join, Destroy };
	enum class EAfterDestroy : uint8 { Menu, Recreate, Recover };
	IOnlineSessionPtr Sessions;
	TSharedPtr<FOnlineSessionSearch> Search;
	UPROPERTY(Transient) TArray<FFPSRoomSearchResult> Rooms;
	EFPSSessionFlow Flow = EFPSSessionFlow::Idle;
	EBackendOp BackendOp = EBackendOp::None;
	EAfterDestroy AfterDestroy = EAfterDestroy::Menu;
	FString LastError, PendingRoomName;
	int32 PendingCapacity = 4;
	int64 SearchGeneration = 0;
	uint64 OperationId = 0;
	double Deadline = 0.;
	bool bAbandoned = false;
	bool bDeinitializing = false;
	FDelegateHandle CreateHandle, FindHandle, JoinHandle, DestroyHandle, NetworkFailureHandle, TravelFailureHandle;
	FTSTicker::FDelegateHandle TickHandle;

	bool EnsureInterface();
	bool PrepareMenuOperation();
	int32 GetLocalUserNum() const;
	void SetFlow(EFPSSessionFlow NewFlow);
	void BeginBackend(EBackendOp Op, EFPSSessionFlow State);
	void ClearBackend();
	void ClearDelegates();
	void IssueCreate();
	void BeginSessionDestroy(EAfterDestroy Action);
	void FinishSessionDestroy(bool bSuccess);
	void Recover(const FString& Error);
	void ReturnToMenu();
	bool Tick(float DeltaSeconds);
	bool OwnsWorld(const UWorld* World) const;
	void OnCreateComplete(FName Name, bool bSuccess);
	void OnFindComplete(bool bSuccess);
	void OnJoinComplete(FName Name, EOnJoinSessionCompleteResult::Type Result);
	void OnDestroyComplete(FName Name, bool bSuccess);
	void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFPSSessionTestAccess;
#endif
};
