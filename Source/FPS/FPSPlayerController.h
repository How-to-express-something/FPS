// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FPSPlayerController.generated.h"

class UFPSCombatComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UHUDWidget;
class UNetworkDiagnosticsWidget;
class ACameraActor;
class AFPSCharacter;
/**
 * Owns the combat input bindings and forwards them to the possessed pawn's
 * UFPSCombatComponent. Movement/look stay on the character.
 */
UCLASS()
class FPS_API AFPSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFPSPlayerController();
	/** Available in the console after entering the match: clean up and return to menu. */
	UFUNCTION(Exec) void FPSLeaveRoom();
	
	virtual void OnRep_Pawn() override;
	virtual void AcknowledgePossession(class APawn* P) override;
	virtual void SetPawn(APawn* InPawn) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void AutoManageActiveCameraTarget(AActor* SuggestedTarget) override;
	void BeginDeathCamera(AFPSCharacter* DeadPawn);
	UFUNCTION(Client, Reliable) void Client_BeginDeathCamera(AFPSCharacter* DeadPawn);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;

	/** Optional context dedicated to combat actions. Leave unset if the aim/fire
	 *  actions already live in the character's mapping context. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> CombatMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	/** Assign IA_Reload here and map it to R in an active input context. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ReloadAction;

	void OnAimStarted(const FInputActionValue& Value);
	void OnAimStopped(const FInputActionValue& Value);
	void OnFireStarted(const FInputActionValue& Value);
	void OnFireStopped(const FInputActionValue& Value);
	void OnReloadStarted(const FInputActionValue& Value);

	UFPSCombatComponent* GetCombatComponent() const;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UHUDWidget> HUDWidgetClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD")
	TObjectPtr<UHUDWidget> HUDWidget;
	
private:
	void UpdateNetworkDiagnostics();
	UPROPERTY(EditDefaultsOnly, Category="HUD|Diagnostics") TSubclassOf<UNetworkDiagnosticsWidget> NetworkDiagnosticsWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UNetworkDiagnosticsWidget> NetworkDiagnosticsWidget;
	TWeakObjectPtr<UWorld> NetworkDiagnosticsWorld;
	bool bEndingPlay = false;
	void UpdatePawnHUD();
	void ClearDeathCamera();
	UPROPERTY(Transient) TObjectPtr<ACameraActor> DeathCamera;
	TWeakObjectPtr<AFPSCharacter> CameraDeadPawn;
	TWeakObjectPtr<AFPSCharacter> CameraNextPawn;
	double DeathCameraStart = 0.;
	double NextPawnWaitStart = 0.;
	double CameraBlendEnd = 0.;
	bool bHoldingDeathCamera = false;
	UPROPERTY(EditDefaultsOnly, Category="RespawnCamera") float DeathCameraHoldSeconds = 1.f;
	UPROPERTY(EditDefaultsOnly, Category="RespawnCamera") float PoseWaitTimeoutSeconds = 2.f;
	UPROPERTY(EditDefaultsOnly, Category="RespawnCamera") float RespawnCameraBlendSeconds = .35f;
	
};
