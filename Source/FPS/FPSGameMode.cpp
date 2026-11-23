// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSGameMode.h"
#include "FPSCharacter.h"
#include "FPSGameState.h"
#include "FPSPlayerController.h"
#include "FPSPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "FPSCombatComponent.h"
#include "FPSHealthPickup.h"
#include "FPSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/PlayerStartPIE.h"
#include "Misc/PackageName.h"

AFPSGameMode::AFPSGameMode()
{
	bUseSeamlessTravel = true;
	HealthPickupClass = AFPSHealthPickup::StaticClass();
	PlayerControllerClass = AFPSPlayerController::StaticClass();
	GameStateClass = AFPSGameState::StaticClass();
	PlayerStateClass = AFPSPlayerState::StaticClass();

	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
	else
	{
		DefaultPawnClass = AFPSCharacter::StaticClass();
	}
}

void AFPSGameMode::InitGame(const FString& MapName, const FString& Options, FString& Error)
{
	Super::InitGame(MapName, Options, Error);
	bSessionMatch = UGameplayStatics::GetIntOption(Options, TEXT("SessionMatch"), 0) != 0;
	SessionCapacity = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("MaxPlayers"), 4), 2, 16);
}

void AFPSGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id, FString& Error)
{
	Super::PreLogin(Options, Address, Id, Error);
	if (Error.IsEmpty() && bSessionMatch) Error = TEXT("MatchAlreadyStarted");
}

APlayerController* AFPSGameMode::Login(UPlayer* Player, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& Id, FString& Error)
{
	// Hard travel/bootstrap needs to spawn the listen host's own LocalPlayer.
	// Rejecting it makes UEngine::LoadMap raise "Couldn't spawn player" (Fatal).
	if (ShouldRejectSessionLogin(Player)) { Error = TEXT("MatchAlreadyStarted"); return nullptr; }
	return Super::Login(Player, InRemoteRole, Portal, Options, Id, Error);
}

bool AFPSGameMode::ShouldRejectSessionLogin(const UPlayer* Player) const
{
	// This is a server-side UObject type check, not a client URL flag that could
	// bypass admission. Remote connections are UPlayers, never ULocalPlayers here.
	return bSessionMatch && !Cast<ULocalPlayer>(Player);
}


void AFPSGameMode::InitGameState()
{
    Super::InitGameState();
    if (AFPSGameState* GS = GetGameState<AFPSGameState>()) GS->KillTarget = FMath::Max(1, KillsToWin);
}

void AFPSGameMode::RestartPlayer(AController* Controller)
{
    const AFPSGameState* GS = GetGameState<AFPSGameState>();
    if (bClosing || (GS && GS->HasFinished())) return;
    Super::RestartPlayer(Controller);
    if (AFPSCharacter* C = Controller ? Cast<AFPSCharacter>(Controller->GetPawn()) : nullptr)
        C->StartSpawnProtection(SpawnProtectionSeconds);
}

void AFPSGameMode::NotifyPlayerKilled(AController* Victim, AController* Killer)
{
    AFPSGameState* GS = GetGameState<AFPSGameState>();
    APawn* DeadPawn = Victim ? Victim->GetPawn() : nullptr;
    if (bClosing || (GS && GS->HasFinished()) || !DeadPawn || ScoredDeaths.Contains(DeadPawn)) return;
    if (const AFPSCharacter* C = Cast<AFPSCharacter>(DeadPawn); C && !C->IsDead()) return;
    for (auto It = ScoredDeaths.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
    ScoredDeaths.Add(DeadPawn);
    DropHealthPickup(DeadPawn);
    AFPSPlayerState* VS = Victim->GetPlayerState<AFPSPlayerState>();
    AFPSPlayerState* KS = Killer ? Killer->GetPlayerState<AFPSPlayerState>() : nullptr;
    if (VS) VS->AddDeath();
    if (KS && KS != VS)
    {
        KS->AddKill();
        if (GS) GS->AddTeamScore(1);
        if (KS->GetKills() >= FMath::Max(1, KillsToWin)) { FinishDeathMatch(KS); return; }
    }
    FRespawnTask& Task = RespawnTasks.FindOrAdd(Victim);
    GetWorldTimerManager().ClearTimer(Task.Timer);
    Task.DeadPawn = DeadPawn;
    const uint64 Generation = Task.Generation = ++RespawnGeneration;
    const TWeakObjectPtr<AController> WeakVictim = Victim;
    const TWeakObjectPtr<APawn> ExpectedPawn = DeadPawn;
    GetWorldTimerManager().SetTimer(Task.Timer, FTimerDelegate::CreateWeakLambda(this, [this, WeakVictim, ExpectedPawn, Generation]()
    {
        FRespawnTask* Active = RespawnTasks.Find(WeakVictim);
        AController* PC = WeakVictim.Get();
        const AFPSGameState* State = GetGameState<AFPSGameState>();
        if (!Active || Active->Generation != Generation) return;
        RespawnTasks.Remove(WeakVictim);
        if (bClosing || !PC || PC->GetWorld() != GetWorld() || (State && State->HasFinished()) ||
            !ExpectedPawn.IsValid() || PC->GetPawn() != ExpectedPawn.Get()) return;
        RespawnPlayer(PC);
    }), FMath::Max(.001f, RespawnDelay), false);
}

void AFPSGameMode::RespawnPlayer(AController* Controller)
{
    if (!IsValid(Controller) || bClosing) return;
    APawn* OldPawn = Controller->GetPawn();
    Controller->UnPossess();
    if (OldPawn) OldPawn->SetLifeSpan(1.f);
    RestartPlayer(Controller);
    UE_LOG(LogTemp, Log, TEXT("[FPSMatch] Respawn controller=%s pawn=%s"), *GetNameSafe(Controller), *GetNameSafe(Controller->GetPawn()));
}

void AFPSGameMode::FinishDeathMatch(APlayerState* Winner)
{
    AFPSGameState* GS = GetGameState<AFPSGameState>();
    if (!GS || GS->HasFinished() || bClosing) return;
    GS->Finish(Winner);
    GS->ReturnToLobbyTime = GS->GetServerWorldTimeSeconds() + FMath::Max(.1f, ResultsSeconds);
    for (auto& Pair : RespawnTasks) GetWorldTimerManager().ClearTimer(Pair.Value.Timer);
    RespawnTasks.Reset();
    ClearHealthPickups();
    for (TActorIterator<AFPSCharacter> It(GetWorld()); It; ++It)
    {
        It->CancelSpawnProtection();
        if (UFPSCombatComponent* Combat = It->GetCombatComponent()) Combat->StopForMatchEnd();
    }
    UE_LOG(LogTemp, Log, TEXT("[FPSMatch] Finished winner=%s target=%d"), *GetNameSafe(Winner), KillsToWin);
    GetWorldTimerManager().SetTimer(ReturnTimer, this, &AFPSGameMode::ReturnToLobby, FMath::Max(.1f, ResultsSeconds), false);
}

void AFPSGameMode::ReturnToLobby()
{
    if (bClosing) return;
    const FString URL = FString::Printf(TEXT("%s?MaxPlayers=%d"), *LobbyMap, SessionCapacity);
    if (!FPackageName::DoesPackageExist(LobbyMap) || !GetWorld()->ServerTravel(URL, true))
        UE_LOG(LogTemp, Error, TEXT("[FPSMatch] ReturnToLobby failed url=%s; results remain visible, use FPSLeaveRoom to exit."), *URL);
}

void AFPSGameMode::Logout(AController* Exiting)
{
    if (FRespawnTask* Task = RespawnTasks.Find(Exiting)) GetWorldTimerManager().ClearTimer(Task->Timer);
    RespawnTasks.Remove(Exiting);
    Super::Logout(Exiting);
}

void AFPSGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    bClosing = true;
    HealthPickupPositions.Reset(); // World teardown owns actor destruction.
    for (auto& Pair : RespawnTasks) GetWorldTimerManager().ClearTimer(Pair.Value.Timer);
    RespawnTasks.Reset();
    GetWorldTimerManager().ClearTimer(ReturnTimer);
    Super::EndPlay(Reason);
}

AActor* AFPSGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TArray<APlayerStart*> Candidates;
	UClass* PawnClass = GetDefaultPawnClassForController(Player);
	APawn* Template = PawnClass ? PawnClass->GetDefaultObject<APawn>() : nullptr;
	if (!Template) return nullptr;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->IsA<APlayerStartPIE>()) continue;
		if (!GetWorld()->EncroachingBlockingGeometry(Template, It->GetActorLocation(), It->GetActorRotation())) Candidates.Add(*It);
	}
	if (Candidates.Num() > 1 && Player)
		Candidates.Remove(Cast<APlayerStart>(Player->StartSpot.Get()));
	return Candidates.IsEmpty() ? nullptr : Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
}

bool AFPSGameMode::RegisterHealthPickup(AFPSHealthPickup* Pickup)
{
	if (bClosing || !HasAuthority() || !IsValid(Pickup) || !Pickup->HasAuthority() || Pickup->GetWorld() != GetWorld()) return false;
	if (const AFPSGameState* GS = GetGameState<AFPSGameState>(); GS && GS->HasFinished()) return false;
	for (auto It = HealthPickupPositions.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	if (HealthPickupPositions.Contains(Pickup)) return true;
	if (HealthPickupPositions.Num() >= FMath::Clamp(MaxHealthPickups, 0, 128)) return false;
	HealthPickupPositions.Add(Pickup, Pickup->GetActorLocation());
	Pickup->RegisteredGameMode = this;
	return true;
}

void AFPSGameMode::UnregisterHealthPickup(AFPSHealthPickup* Pickup)
{
	HealthPickupPositions.Remove(Pickup);
}

bool AFPSGameMode::TryCollectHealthPickup(AFPSHealthPickup* Pickup, AFPSCharacter* Character)
{
	if (bClosing || !HasAuthority() || !IsValid(Pickup) || !IsValid(Character) || Pickup->bConsumed ||
		Pickup->IsActorBeingDestroyed() || Character->IsActorBeingDestroyed() || Character->IsDead() ||
		Pickup->GetWorld() != GetWorld() || Character->GetWorld() != GetWorld() || !Character->HasAuthority()) return false;
	if (const AFPSGameState* GS = GetGameState<AFPSGameState>(); GS && GS->HasFinished()) return false;
	const FVector* Recorded = HealthPickupPositions.Find(Pickup);
	if (!Recorded || !Pickup->GetActorLocation().Equals(*Recorded, .1f)) return false;
	// Both engine overlap and explicit server distance must agree. No client-supplied position.
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!Capsule || Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision ||
		!Capsule->OverlapComponent(Pickup->PickupBox->GetComponentLocation(), Pickup->PickupBox->GetComponentQuat(),
			FCollisionShape::MakeBox(Pickup->PickupBox->GetScaledBoxExtent()))) return false;
	FHitResult Occlusion;
	FCollisionQueryParams Visibility(SCENE_QUERY_STAT(FPSPickupVisibility), false, Character);
	Visibility.AddIgnoredActor(Pickup);
	if (GetWorld()->LineTraceSingleByChannel(Occlusion, Character->GetPawnViewLocation(), *Recorded, ECC_Visibility, Visibility)) return false;
	const FVector Delta = Character->GetActorLocation() - *Recorded;
	const FVector Extent = Pickup->PickupBox->GetScaledBoxExtent();
	if (FMath::Abs(Delta.Z) > Extent.Z + Capsule->GetScaledCapsuleHalfHeight() ||
		Delta.SizeSquared2D() > FMath::Square(Extent.Size2D() + Capsule->GetScaledCapsuleRadius())) return false;
	UFPSHealthComponent* Health = Character->GetHealthComponent();
	if (!Health || Health->GetHealth() >= Health->GetMaxHealth()) return false;
	Pickup->bConsumed = true; // Claim BEFORE broadcasts: reentrant overlap cannot consume twice.
	const float SignedApplied = Health->ApplyDamage(-Health->GetMaxHealth(), nullptr, Pickup);
	if (SignedApplied >= 0.f) { Pickup->bConsumed = false; return false; }
	if (bClosing || !IsValid(Pickup) || Pickup->IsActorBeingDestroyed()) return true;
	UnregisterHealthPickup(Pickup);
	Pickup->PickupBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogTemp, Log, TEXT("[FPSPickup] Collected pickup=%s pawn=%s restored=%.1f"), *GetNameSafe(Pickup), *GetNameSafe(Character), -SignedApplied);
	Pickup->Destroy();
	return true;
}

void AFPSGameMode::DropHealthPickup(APawn* DeadPawn)
{
	if (!DeadPawn || !HealthPickupClass || bClosing) return;
	FVector Location = DeadPawn->GetActorLocation();
	FHitResult Ground;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(FPSHealthDrop), false, DeadPawn);
	if (GetWorld()->LineTraceSingleByChannel(Ground, Location, Location - FVector(0,0,2000), ECC_Visibility, Query))
		Location = Ground.ImpactPoint + FVector(0,0,25.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<AFPSHealthPickup>(HealthPickupClass, Location, FRotator::ZeroRotator, Params);
}

void AFPSGameMode::ClearHealthPickups()
{
	TArray<TWeakObjectPtr<AFPSHealthPickup>> Pickups;
	HealthPickupPositions.GetKeys(Pickups);
	HealthPickupPositions.Reset();
	for (auto Pickup : Pickups) if (Pickup.IsValid()) Pickup->Destroy();
}
