// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSPlayerController.h"
#include "FPSCombatComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "FPSCharacter.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "UI/HUDWidget.h"
#include "Online/MultiplayerSessionsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/PlatformTime.h"

AFPSPlayerController::AFPSPlayerController()
{
	HUDWidgetClass = UHUDWidget::StaticClass();
}

void AFPSPlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);
	UpdatePawnHUD();
}

void AFPSPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	
	UpdatePawnHUD();
}

void AFPSPlayerController::AcknowledgePossession(class APawn* P)
{
	Super::AcknowledgePossession(P);
	UpdatePawnHUD();
}

void AFPSPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;

	if (CombatMappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(CombatMappingContext, 0);
		}
	}
	UpdatePawnHUD();
}

void AFPSPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearDeathCamera();
	if (HUDWidget)
	{
		HUDWidget->SetObservedCharacter(nullptr);
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AFPSPlayerController::Client_BeginDeathCamera_Implementation(AFPSCharacter* DeadPawn)
{
	BeginDeathCamera(DeadPawn);
}

void AFPSPlayerController::BeginDeathCamera(AFPSCharacter* DeadPawn)
{
	// A late death RPC for the previous life cannot freeze the new life's view.
	if (!IsLocalController() || !IsValid(DeadPawn) || GetPawn() != DeadPawn ||
		(CameraDeadPawn == DeadPawn && DeathCamera)) return;
	ClearDeathCamera();
	if (!PlayerCameraManager) return;
	const FVector Location = PlayerCameraManager->GetCameraLocation();
	const FRotator Rotation = PlayerCameraManager->GetCameraRotation();
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	DeathCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Location, Rotation, Params);
	if (!DeathCamera) return;
	DeathCamera->GetCameraComponent()->SetFieldOfView(PlayerCameraManager->GetFOVAngle());
	CameraDeadPawn = DeadPawn;
	bHoldingDeathCamera = true;
	DeathCameraStart = FPlatformTime::Seconds();
	SetViewTarget(DeathCamera);
	UE_LOG(LogTemp, Log, TEXT("[FPSCamera] Hold pawn=%s"), *GetNameSafe(DeadPawn));
}

void AFPSPlayerController::AutoManageActiveCameraTarget(AActor* SuggestedTarget)
{
	if (!bHoldingDeathCamera) Super::AutoManageActiveCameraTarget(SuggestedTarget);
}

void AFPSPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocalController() || !DeathCamera) return;
	const double Now = FPlatformTime::Seconds();
	if (!bHoldingDeathCamera)
	{
		if (Now >= CameraBlendEnd) ClearDeathCamera();
		return;
	}
	AFPSCharacter* NewPawn = Cast<AFPSCharacter>(GetPawn());
	if (!NewPawn || NewPawn == CameraDeadPawn.Get() || NewPawn->IsDead()) return;
	if (CameraNextPawn.Get() != NewPawn)
	{
		CameraNextPawn = NewPawn;
		NextPawnWaitStart = Now;
	}
	const bool bReady = NewPawn->IsPresentationReady();
	if (Now - DeathCameraStart < FMath::Max(0.f, DeathCameraHoldSeconds) ||
		(!bReady && Now - NextPawnWaitStart < FMath::Max(.1f, PoseWaitTimeoutSeconds))) return;
	bHoldingDeathCamera = false;
	const float BlendTime = FMath::Max(0.f, RespawnCameraBlendSeconds);
	CameraBlendEnd = Now + BlendTime + .05;
	SetViewTargetWithBlend(NewPawn, BlendTime);
	UE_LOG(LogTemp, Log, TEXT("[FPSCamera] Blend pawn=%s poseReady=%d timeout=%d"),
		*GetNameSafe(NewPawn), bReady, !bReady);
}

void AFPSPlayerController::ClearDeathCamera()
{
	bHoldingDeathCamera = false;
	CameraDeadPawn.Reset();
	CameraNextPawn.Reset();
	if (DeathCamera) { DeathCamera->Destroy(); DeathCamera = nullptr; }
}

void AFPSPlayerController::FPSLeaveRoom()
{
	if (IsLocalController() && GetGameInstance())
		if (UMultiplayerSessionsSubsystem* SS = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>()) SS->LeaveRoom();
}

void AFPSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	if (AimAction)
	{
		EnhancedInput->BindAction(AimAction, ETriggerEvent::Started, this, &AFPSPlayerController::OnAimStarted);
		EnhancedInput->BindAction(AimAction, ETriggerEvent::Completed, this, &AFPSPlayerController::OnAimStopped);
		EnhancedInput->BindAction(AimAction, ETriggerEvent::Canceled, this, &AFPSPlayerController::OnAimStopped);
	}

	if (FireAction)
	{
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this, &AFPSPlayerController::OnFireStarted);
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Completed, this, &AFPSPlayerController::OnFireStopped);
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Canceled, this, &AFPSPlayerController::OnFireStopped);
	}
	if (ReloadAction)
	{
		EnhancedInput->BindAction(ReloadAction, ETriggerEvent::Started, this, &AFPSPlayerController::OnReloadStarted);
	}
}

UFPSCombatComponent* AFPSPlayerController::GetCombatComponent() const
{
	if (GetPawn())
	{
		return GetPawn()->FindComponentByClass<UFPSCombatComponent>();
	}
	return nullptr;
}

void AFPSPlayerController::UpdatePawnHUD()
{
	if (!IsLocalController() || !HasActorBegunPlay() || !GetLocalPlayer()) return;
	if (!HUDWidget && HUDWidgetClass)
	{
		HUDWidget = CreateWidget<UHUDWidget>(this, HUDWidgetClass);
		if (HUDWidget) HUDWidget->AddToPlayerScreen();
	}
	if (HUDWidget) HUDWidget->SetObservedCharacter(Cast<AFPSCharacter>(GetPawn()));
}

void AFPSPlayerController::OnAimStarted(const FInputActionValue& Value)
{
	if (UFPSCombatComponent* Combat = GetCombatComponent())
	{
		Combat->StartAim();
	}
}

void AFPSPlayerController::OnAimStopped(const FInputActionValue& Value)
{
	if (UFPSCombatComponent* Combat = GetCombatComponent())
	{
		Combat->StopAim();
	}
}

void AFPSPlayerController::OnFireStarted(const FInputActionValue& Value)
{
	if (UFPSCombatComponent* Combat = GetCombatComponent())
	{
		Combat->StartFire();
	}
}

void AFPSPlayerController::OnFireStopped(const FInputActionValue& Value)
{
	if (UFPSCombatComponent* Combat = GetCombatComponent())
	{
		Combat->StopFire();
	}
}

void AFPSPlayerController::OnReloadStarted(const FInputActionValue& Value)
{
	if (UFPSCombatComponent* Combat = GetCombatComponent())
	{
		Combat->Reload();
	}
}
