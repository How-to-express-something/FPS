// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSHealthComponent.h"
#include "FPSCharacter.h"
#include "FPSGameState.h"
#include "Engine/World.h"

#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"

UFPSHealthComponent::UFPSHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UFPSHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	// Clients start at full health until replication corrects them; the server
	// must also seed the replicated value from the configured maximum.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		MaxHealth = FMath::IsFinite(MaxHealth) ? FMath::Max(1.f, MaxHealth) : 100.f;
		CurrentHealth = MaxHealth;
	}
}

void UFPSHealthComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	Super::EndPlay(Reason);
}

float UFPSHealthComponent::Heal(float Amount, AActor* Causer)
{
	return FMath::IsFinite(Amount) && Amount > 0.f ? -ApplyDamage(-Amount, nullptr, Causer) : 0.f;
}

void UFPSHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFPSHealthComponent, CurrentHealth);
	DOREPLIFETIME(UFPSHealthComponent, bDead);
}

float UFPSHealthComponent::ApplyDamage(float Amount, AController* Instigator, AActor* Causer)
{
	if (bEndingPlay || !GetOwner() || !GetOwner()->HasAuthority() || bDead ||
		!FMath::IsFinite(Amount) || Amount == 0.f)
	{
		return 0.f;
	}
	const AFPSGameState* GS = GetWorld()->GetGameState<AFPSGameState>();
	const AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	if ((GS && GS->HasFinished()) || (Amount > 0.f && Character && Character->IsSpawnProtected())) return 0.f;

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth - Amount, 0.f, MaxHealth);
	const float Applied = PreviousHealth - CurrentHealth;
	if (Applied == 0.f) return 0.f;
	// Commit death BEFORE broadcasting: a health listener cannot heal/re-enter a fatal hit.
	bDead = CurrentHealth <= 0.f;
	GetOwner()->ForceNetUpdate();

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);

	if (bDead && !bEndingPlay && !GetOwner()->IsActorBeingDestroyed())
	{
		BroadcastDeath(Instigator);
	}

	return Applied;
}

void UFPSHealthComponent::OnRep_Health()
{
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void UFPSHealthComponent::OnRep_Dead()
{
	if (bDead)
	{
		// The killer controller is server-only information and does not
		// replicate; clients only need to know that the character died.
		BroadcastDeath(nullptr);
	}
}

void UFPSHealthComponent::YourDutyIsNotOver()
{
	bDead = false;
}

void UFPSHealthComponent::BroadcastDeath(AController* Killer)
{
	if (bDeathBroadcast)
	{
		return;
	}

	bDeathBroadcast = true;
	OnDeath.Broadcast(Killer);
}
