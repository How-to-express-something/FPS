// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FPSHealthComponent.generated.h"

class AController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFPSHealthChanged, float, NewHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFPSDeath, AController*, Killer);

/**
 * Replicated health for a character. Damage is authoritative: only the server
 * applies it. Death is broadcast both from the server path and from the
 * replicated flag, so clients run their local death visuals too.
 */
UCLASS(ClassGroup = (FPS), meta = (BlueprintSpawnableComponent))
class FPS_API UFPSHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSHealthComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bDead; }
	
	
	/** Server only: positive damages, negative heals. Returns signed applied damage. Never revives. */
	UFUNCTION(BlueprintCallable,Category = "Health")
	float ApplyDamage(float Amount, AController* Instigator, AActor* Causer);
	/** Prefer explicit intent at new call sites; internally uses the same signed ApplyDamage path. */
	float Heal(float Amount, AActor* Causer = nullptr);

	/** Server and clients. Killer may be null on the replicated death path. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnFPSHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnFPSDeath OnDeath;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "Health")
	float CurrentHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Dead, BlueprintReadOnly, Category = "Health")
	bool bDead = false;

	UFUNCTION()
	void OnRep_Health();

	UFUNCTION()
	void OnRep_Dead();
	
	UFUNCTION(BlueprintCallable, Category = "Health")
	void YourDutyIsNotOver();
	

private:
	void BroadcastDeath(AController* Killer);

	/** Guards the delegate so the server broadcast and the replicated flag
	 *  cannot report the same death twice. */
	bool bDeathBroadcast = false;
	bool bEndingPlay = false;
};
