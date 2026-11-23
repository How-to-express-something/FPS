#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "../Lobby/FPSLobbyGameMode.h"
#include "../Lobby/FPSLobbyGameState.h"
#include "../Lobby/FPSLobbyPlayerController.h"
#include "../FPSCharacter.h"
#include "../FPSGameMode.h"
#include "../FPSGameState.h"
#include "../FPSPlayerState.h"
#include "../FPSHealthComponent.h"
#include "../FPSCombatComponent.h"
#include "../FPSWeapon.h"
#include "../FPSHealthPickup.h"
#include "../FPSHitReactionComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

struct FFPSPIETravelTestAccess
{
	static void SetCapacity(AFPSLobbyGameMode* GM, int32 Capacity)
	{
		GM->Capacity = Capacity;
		if (GM->GameSession) GM->GameSession->MaxPlayers = Capacity;
		AFPSLobbyGameState* GS = GM->GetGameState<AFPSLobbyGameState>();
		FFPSLobbySnapshot S = GS->GetLobbySnapshot();
		S.Capacity = Capacity;
		GS->SetSnapshot(S);
	}
};

namespace
{
/** Starts ACTUAL three-world PIE, preserving the user's default play settings. */
class FVerifyPIESessionTravel : public IAutomationLatentCommand
{
public:
	explicit FVerifyPIESessionTravel(FAutomationTestBase* InTest, bool bInCycle = false) : Test(InTest), bFullCycle(bInCycle) {}
	virtual bool Update() override
	{
		if (!bRequested)
		{
			IConsoleVariable* Allow = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
			if (!Allow || Allow->GetInt() != 1) { Test->AddError(TEXT("Project config must enable net.AllowPIESeamlessTravel before running PIE regression.")); return true; }
			Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
			Settings->SetPlayNetMode(EPlayNetMode::PIE_ListenServer);
			Settings->SetPlayNumberOfClients(3);
			Settings->SetRunUnderOneProcess(true);
			Settings->bLaunchSeparateServer = false;
			Settings->NetworkEmulationSettings.bIsNetworkEmulationEnabled = false;
			FRequestPlaySessionParams Params;
			Params.EditorPlaySettings = Settings.Get();
			Params.StartLocation = FVector::ZeroVector;
			GEditor->RequestPlaySession(Params);
			bRequested = true;
			StartedAt = FPlatformTime::Seconds();
			return false;
		}
		if (FPlatformTime::Seconds() - StartedAt > 75.)
		{
			Test->AddError(FString::Printf(TEXT("Three-player PIE flow exceeded 75 seconds (cycle phase %d)."), CyclePhase));
			return true;
		}
		if (CyclePhase > 0) return VerifyMatchCycle();
		int32 PIEWorlds = 0, MatchWorlds = 0;
		AFPSLobbyGameMode* Server = nullptr;
		AFPSLobbyPlayerController* Host = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || World->WorldType != EWorldType::PIE || !World->HasBegunPlay()) continue;
			++PIEWorlds;
			APlayerController* LocalPC = World->GetGameInstance() ? World->GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
			if (!LocalPC || !LocalPC->PlayerState) continue;
			if (AFPSLobbyPlayerController* LobbyPC = Cast<AFPSLobbyPlayerController>(LocalPC))
			{
				if (World->GetGameState<AFPSLobbyGameState>() && !ReadyRequested.Contains(LobbyPC))
				{
					ReadyRequested.Add(LobbyPC);
					LobbyPC->Server_SetReady(true);
				}
				if (World->GetNetMode() == NM_ListenServer)
				{
					Server = World->GetAuthGameMode<AFPSLobbyGameMode>();
					Host = LobbyPC;
				}
			}
			if (World->GetMapName().EndsWith(TEXT("Demo")) && Cast<AFPSCharacter>(LocalPC->GetPawn())) ++MatchWorlds;
		}
		if (!bStarted && PIEWorlds == 3 && Server && Server->AllPlayersReady())
		{
			// UE's AdditionalServerGameOptions is for new-process PIE, not this
			// single-process path. Use a non-default capacity in this test only.
			if (bFullCycle) FFPSPIETravelTestAccess::SetCapacity(Server, 3);
			Server->RequestStart(Host);
			bStarted = true;
			Test->AddInfo(TEXT("[FPSPIE] Three single-process PIE worlds ready; host requested server travel."));
		}
		if (bStarted && PIEWorlds == 3 && MatchWorlds == 3)
		{
			if (bFullCycle) { CyclePhase = 1; return false; }
			Test->AddInfo(TEXT("[FPSPIE] PASS: Listen host + two clients all entered Demo and received FPS pawns."));
			return true;
		}
		return false;
	}
private:
	bool VerifyMatchCycle()
	{
		UWorld* ServerWorld = nullptr;
		APlayerController* ServerHost = nullptr;
		APlayerController* ServerVictim = nullptr;
		APlayerController* ClientVictim = nullptr;
		int32 LobbyCount = 0;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* W = Context.World();
			if (!W || W->WorldType != EWorldType::PIE || !W->HasBegunPlay()) continue;
			APlayerController* Local = W->GetGameInstance()->GetFirstLocalPlayerController();
			if (!Local || !Local->PlayerState) continue;
			if (W->GetNetMode() == NM_ListenServer)
			{
				ServerWorld = W; ServerHost = Local;
				for (auto It = W->GetPlayerControllerIterator(); It; ++It)
					if (It->Get() && !It->Get()->IsLocalController() && It->Get()->GetPawn()) { ServerVictim = It->Get(); break; }
			}
			else if (Local->PlayerState->GetPlayerId() == VictimId) ClientVictim = Local;
			if (AFPSGameState* GS = W->GetGameState<AFPSGameState>(); GS && GS->HasFinished()) FinishedWorlds.Add(W);
			if (AFPSLobbyGameState* GS = W->GetGameState<AFPSLobbyGameState>())
			{
				bool bReset = GS->GetLobbySnapshot().Capacity == 3 && GS->GetLobbyPlayers().Num() == 3;
				for (const FFPSLobbyPlayerInfo& P : GS->GetLobbyPlayers()) bReset &= !P.bReady;
				if (bReset) ++LobbyCount;
			}
		}
		if (CyclePhase == 1)
		{
			if (!ServerWorld || !ServerHost || !ServerVictim) return false;
			AFPSCharacter* C = Cast<AFPSCharacter>(ServerVictim->GetPawn());
			if (!C) return false;
			Test->TestTrue(TEXT("Listen host shield exists even when RestartPlayer preceded World BeginPlay"),
				CastChecked<AFPSCharacter>(ServerHost->GetPawn())->IsSpawnProtected());
			VictimId = ServerVictim->PlayerState->GetPlayerId();
			OldServerPawn = C;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* W = Context.World();
				if (!W || W->WorldType != EWorldType::PIE || W->GetNetMode() != NM_Client) continue;
				APlayerController* PC = W->GetGameInstance()->GetFirstLocalPlayerController();
				if (PC && PC->PlayerState && PC->PlayerState->GetPlayerId() == VictimId) OldClientPawn = PC->GetPawn();
			}
			C->CancelSpawnProtection();
			FHitResult Hit;
			Hit.BoneName=TEXT("upperarm_l"); Hit.ImpactPoint=C->GetActorLocation();
			const float Applied=UGameplayStatics::ApplyPointDamage(C,5.f,-C->GetActorRightVector(),Hit,ServerHost,nullptr,UDamageType::StaticClass());
			Test->TestEqual(TEXT("Real server point hit produces positive damage"),Applied,5.f);
			ReactionEventId=C->GetHitReactionComponent()->LastCue.EventId;
			Test->TestEqual(TEXT("Server maps right-source left-arm hit to index 9"),C->GetHitReactionComponent()->LastCue.MontageIndex,9);
			CyclePhase=10;
			return false;
		}
		if (CyclePhase == 10)
		{
			int32 Recipients=0;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* W=Context.World();
				if (!W || W->WorldType!=EWorldType::PIE || W->GetNetMode()!=NM_Client) continue;
				for (TActorIterator<AFPSCharacter> It(W); It; ++It)
				{
					if (!It->GetPlayerState() || It->GetPlayerState()->GetPlayerId()!=VictimId) continue;
					const auto& Cue=It->GetHitReactionComponent()->LastCue;
					if (Cue.EventId!=ReactionEventId || ReactionEventId==0) continue;
					Test->TestEqual(TEXT("Owner and observer receive server-selected reaction index"),Cue.MontageIndex,9);
					Test->TestTrue(TEXT("Right source XY arrives at both clients"),Cue.LocalSourceDirection.Equals(FVector2D(0,1),.01));
					++Recipients;
				}
			}
			if (Recipients!=2) return false;
			Test->AddInfo(TEXT("[FPSPIE] Authoritative hit reaction index 9 reached victim owner and observer via multicast."));
			AFPSCharacter* C=OldServerPawn.IsValid() ? Cast<AFPSCharacter>(OldServerPawn.Get()) : nullptr;
			if (!C || !ServerHost) return false;
			C->GetHealthComponent()->ApplyDamage(10000.f, ServerHost, nullptr);
			Test->TestTrue(TEXT("Real server death enters respawn path"), C->IsDead());
			CyclePhase = 2;
			return false;
		}
		if (CyclePhase == 2)
		{
			// Both the victim's own death camera and the observer must lose the old
			// body/weapons, while retaining the dead Pawn long enough for respawn.
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* W=Context.World();
				if (!W || W->WorldType!=EWorldType::PIE || W->GetNetMode()!=NM_Client || DeathVisualWorlds.Contains(W)) continue;
				for (TActorIterator<AFPSCharacter> It(W); It; ++It)
				{
					if (!It->GetPlayerState() || It->GetPlayerState()->GetPlayerId()!=VictimId || !It->IsDead()) continue;
					AFPSWeapon* Weapon=It->GetCombatComponent()->GetCurrentWeapon();
					if (!Weapon) continue;
					Test->TestTrue(TEXT("Dead body hidden on owner and observer"),It->GetMesh3P()->bHiddenInGame);
					Test->TestTrue(TEXT("Dead arms hidden on owner and observer"),It->GetMesh1P()->bHiddenInGame);
					Test->TestTrue(TEXT("Dead 1P weapon hidden on both clients"),Weapon->GetWeaponMesh1P()->bHiddenInGame);
					Test->TestTrue(TEXT("Dead 3P weapon hidden on both clients"),Weapon->GetWeaponMesh3P()->bHiddenInGame);
					Test->TestFalse(TEXT("Dead body has no hidden shadow"),It->GetMesh3P()->bCastHiddenShadow);
					DeathVisualWorlds.Add(W);
					break;
				}
			}
			// Verify a death drop is replicated before collecting it via a real server overlap.
			if (!bPickupChecked && ServerWorld && ServerHost && ClientVictim)
			{
				AFPSHealthPickup* Pack = nullptr;
				for (TActorIterator<AFPSHealthPickup> It(ServerWorld); It; ++It) if (!It->IsActorBeingDestroyed()) {Pack=*It;break;}
				bool bReplica = false;
				for (TActorIterator<AFPSHealthPickup> It(ClientVictim->GetWorld()); It; ++It) if (!It->IsActorBeingDestroyed()) bReplica=true;
				if (Pack && bReplica)
				{
					AFPSCharacter* Collector = CastChecked<AFPSCharacter>(ServerHost->GetPawn());
					Collector->CancelSpawnProtection();
					Collector->GetHealthComponent()->ApplyDamage(25.f,nullptr,nullptr);
					Collector->SetActorLocation(Pack->GetActorLocation()+FVector(0,0,80.f),false,nullptr,ETeleportType::TeleportPhysics);
					Pack->PickupBox->UpdateOverlaps();
					ServerWorld->GetAuthGameMode<AFPSGameMode>()->TryCollectHealthPickup(Pack,Collector);
					Test->TestEqual(TEXT("Death pickup replicated and server overlap restored health"),Collector->GetHealthComponent()->GetHealth(),Collector->GetHealthComponent()->GetMaxHealth());
					Test->TestTrue(TEXT("Collected death pack is consumed once"),Pack->IsConsumed());
					bPickupChecked=true;
					Test->AddInfo(TEXT("[FPSPIE] Death pack replicated to client; authoritative overlap restored collector health."));
				}
			}
			if (ClientVictim && Cast<ACameraActor>(ClientVictim->GetViewTarget())) bSawDeathCamera = true;
			AFPSCharacter* NewPawn = ServerVictim ? Cast<AFPSCharacter>(ServerVictim->GetPawn()) : nullptr;
			AFPSCharacter* ClientPawn = ClientVictim ? Cast<AFPSCharacter>(ClientVictim->GetPawn()) : nullptr;
			if (!NewPawn || NewPawn == OldServerPawn.Get() || !ClientPawn || ClientPawn == OldClientPawn.Get() ||
				ClientVictim->GetViewTarget() != ClientPawn || !ClientPawn->IsPresentationReady()) return false;
			Test->TestTrue(TEXT("Owning client held an independent death camera"), bSawDeathCamera);
			Test->TestTrue(TEXT("Death pickup path was exercised before respawn"),bPickupChecked);
			Test->TestEqual(TEXT("Both clients applied death mesh hiding before respawn"),DeathVisualWorlds.Num(),2);
			Test->TestFalse(TEXT("New life arms visible again"),ClientPawn->GetMesh1P()->bHiddenInGame);
			Test->TestFalse(TEXT("New life body has fresh visibility state"),ClientPawn->GetMesh3P()->bHiddenInGame);
			Test->AddInfo(TEXT("[FPSPIE] Death hid body/arms/both weapons on owner and observer; new-life meshes restored."));
			Test->TestFalse(TEXT("Remote owner received a living new pawn"), ClientPawn->IsDead());
			Test->AddInfo(TEXT("[FPSPIE] New pawn possessed, first skeletal pose evaluated, camera transitioned."));
			ShotAttemptAt = FPlatformTime::Seconds() + .25;
			CyclePhase = 3;
			return false;
		}
		if (CyclePhase == 3)
		{
			if (FPlatformTime::Seconds() < ShotAttemptAt || !ServerVictim || !ClientVictim) return false;
			AFPSCharacter* NewPawn = Cast<AFPSCharacter>(ServerVictim->GetPawn());
			AFPSCharacter* ClientPawn = Cast<AFPSCharacter>(ClientVictim->GetPawn());
			AFPSWeapon* ServerWeapon = NewPawn ? NewPawn->GetCombatComponent()->GetCurrentWeapon() : nullptr;
			AFPSWeapon* ClientWeapon = ClientPawn ? ClientPawn->GetCombatComponent()->GetCurrentWeapon() : nullptr;
			if (!ServerWeapon || !ClientWeapon) return false;
			ExpectedServerAmmo = ServerWeapon->GetAmmoInMag() - 1;
			ClientVictim->SetControlRotation(FRotator(80.f, 0.f, 0.f));
			ClientPawn->GetCombatComponent()->StartFire();
			ClientPawn->GetCombatComponent()->StopFire();
			CyclePhase = 4;
			return false;
		}
		if (CyclePhase == 4)
		{
			AFPSCharacter* NewPawn = ServerVictim ? Cast<AFPSCharacter>(ServerVictim->GetPawn()) : nullptr;
			AFPSWeapon* Weapon = NewPawn ? NewPawn->GetCombatComponent()->GetCurrentWeapon() : nullptr;
			if (!Weapon || Weapon->GetAmmoInMag() != ExpectedServerAmmo) return false;
			Test->TestFalse(TEXT("Remote client's real StartFire RPC removes authoritative spawn protection"), NewPawn->IsSpawnProtected());
			Test->AddInfo(TEXT("[FPSPIE] Remote client predicted a real shot; server accepted and consumed one round."));
			// Seed 38 prior kills to exercise the real default 40-kill threshold in a short test.
			AFPSPlayerState* PS = ServerHost->GetPlayerState<AFPSPlayerState>();
			for (int32 I = 0; I < 38; ++I) PS->AddKill();
			NewPawn->CancelSpawnProtection();
			NewPawn->GetHealthComponent()->ApplyDamage(10000.f, ServerHost, nullptr);
			Test->TestEqual(TEXT("Winning kill reaches forty"), PS->GetKills(), 40);
			CyclePhase = 5;
			return false;
		}
		if (CyclePhase == 5 && LobbyCount == 3 && ServerWorld)
		{
			AFPSLobbyGameMode* GM = ServerWorld->GetAuthGameMode<AFPSLobbyGameMode>();
			if (!GM || !GM->IsHost(ServerHost)) return false;
			Test->TestEqual(TEXT("Results replicated to all three worlds before return"), FinishedWorlds.Num(), 3);
			Test->AddInfo(TEXT("[FPSPIE] PASS: respawn camera + 40-kill finish + three-world lobby return, capacity/ready/host reset."));
			return true;
		}
		return false;
	}
	FAutomationTestBase* Test;
	TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	TSet<TWeakObjectPtr<APlayerController>> ReadyRequested;
	double StartedAt = 0.;
	bool bRequested = false, bStarted = false;
	bool bFullCycle = false, bSawDeathCamera = false;
	bool bPickupChecked = false;
	int32 CyclePhase = 0, VictimId = INDEX_NONE;
	int32 ReactionEventId = 0;
	TWeakObjectPtr<APawn> OldServerPawn, OldClientPawn;
	TSet<TWeakObjectPtr<UWorld>> FinishedWorlds;
	TSet<TWeakObjectPtr<UWorld>> DeathVisualWorlds;
	double ShotAttemptAt = 0.;
	int32 ExpectedServerAmmo = 0;
};

class FWaitForPIEToStop : public IAutomationLatentCommand
{
public:
	explicit FWaitForPIEToStop(FAutomationTestBase* InTest) : Test(InTest), StartedAt(FPlatformTime::Seconds()) {}
	virtual bool Update() override
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType != EWorldType::PIE) continue;
			if (FPlatformTime::Seconds() - StartedAt > 90.) { Test->AddError(TEXT("PIE worlds did not finish cleanup.")); return true; }
			return false;
		}
		return true;
	}
private:
	FAutomationTestBase* Test;
	double StartedAt;
};
}

// Separate prefix: this opens a map and runs PIE; it is intentionally opt-in.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPIETravelRegression, "FPSPIE.Lobby.ThreePlayerTravel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSPIETravelRegression::RunTest(const FString&)
{
	if (!GEditor || GEditor->PlayWorld) { AddError(TEXT("Run this test in an idle editor / dedicated automation process.")); return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/FPS/Maps/Lobby_Map")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FVerifyPIESessionTravel>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWaitForPIEToStop>(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPIEMatchCycle, "FPSPIE.Match.RespawnAndReturnLoop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFPSPIEMatchCycle::RunTest(const FString&)
{
	if (!GEditor || GEditor->PlayWorld) { AddError(TEXT("Run this test in an idle editor / dedicated automation process.")); return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/FPS/Maps/Lobby_Map")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FVerifyPIESessionTravel>(this, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWaitForPIEToStop>(this));
	return true;
}
#endif
