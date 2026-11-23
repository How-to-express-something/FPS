#include "FPSHealthPickup.h"
#include "FPSCharacter.h"
#include "FPSGameMode.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AFPSHealthPickup::AFPSHealthPickup()
{
	bReplicates = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = false;
	PickupBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PickupBox"));
	RootComponent = PickupBox;
	PickupBox->InitBoxExtent(FVector(55.f, 55.f, 45.f));
	PickupBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupBox->SetGenerateOverlapEvents(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(PickupBox);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(.5f, .5f, .22f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);
	UTextRenderComponent* Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(PickupBox);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	Label->SetText(FText::FromString(TEXT("+ HEALTH")));
	Label->SetTextRenderColor(FColor::Green);
	Label->SetWorldSize(18.f);
	Label->SetHorizontalAlignment(EHTA_Center);
}

void AFPSHealthPickup::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) { PickupBox->SetCollisionEnabled(ECollisionEnabled::NoCollision); return; }
	AFPSGameMode* GM = GetWorld()->GetAuthGameMode<AFPSGameMode>();
	if (!GM || !GM->RegisterHealthPickup(this)) { Destroy(); return; }
	RegisteredGameMode = GM;
	SetLifeSpan(FMath::Clamp(LifetimeSeconds, 1.f, 300.f));
	PickupBox->OnComponentBeginOverlap.AddDynamic(this, &AFPSHealthPickup::HandleOverlap);
	PickupBox->UpdateOverlaps();
	TArray<AActor*> Overlapping;
	PickupBox->GetOverlappingActors(Overlapping, AFPSCharacter::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		if (bConsumed || IsActorBeingDestroyed()) break;
		GM->TryCollectHealthPickup(this, Cast<AFPSCharacter>(Actor));
	}
}

void AFPSHealthPickup::HandleOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (!HasAuthority() || bConsumed) return;
	if (AFPSGameMode* GM = RegisteredGameMode.Get()) GM->TryCollectHealthPickup(this, Cast<AFPSCharacter>(Other));
}

void AFPSHealthPickup::EndPlay(const EEndPlayReason::Type Reason)
{
	PickupBox->OnComponentBeginOverlap.RemoveDynamic(this, &AFPSHealthPickup::HandleOverlap);
	if (AFPSGameMode* GM = RegisteredGameMode.Get()) GM->UnregisterHealthPickup(this);
	RegisteredGameMode.Reset();
	Super::EndPlay(Reason);
}
