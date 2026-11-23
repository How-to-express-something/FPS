#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSHealthPickup.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class AFPSGameMode;
class AFPSCharacter;

/** Server overlap is the request. GameMode validates its private position registry. */
UCLASS()
class FPS_API AFPSHealthPickup : public AActor
{
	GENERATED_BODY()
public:
	AFPSHealthPickup();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pickup") TObjectPtr<UBoxComponent> PickupBox;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pickup") TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(EditDefaultsOnly, Category="Pickup", meta=(ClampMin="1",ClampMax="300")) float LifetimeSeconds = 30.f;
	bool IsConsumed() const { return bConsumed; }
private:
	UFUNCTION() void HandleOverlap(UPrimitiveComponent* Overlapped, AActor* Other,
		UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);
	bool bConsumed = false;
	TWeakObjectPtr<AFPSGameMode> RegisteredGameMode;
	friend class AFPSGameMode;
};
