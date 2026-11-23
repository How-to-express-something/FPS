// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSWeapon.h"
#include "FPSCharacter.h"
#include "FPSCombatComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AFPSWeapon::AFPSWeapon()
{
	// The meshes are re-aligned to their sockets from Tick (see the comment there).
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	// Follow the owning character's relevancy so the weapon reaches whoever can see them.
	bNetUseOwnerRelevancy = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// First person: drawn only for the holder, so it must not cast shadows, take
	// decals or collide. Starts hidden until the weapon is equipped.
	WeaponMesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh1P"));
	WeaponMesh1P->SetupAttachment(SceneRoot);
	WeaponMesh1P->SetOnlyOwnerSee(true);
	WeaponMesh1P->bCastDynamicShadow = false;
	WeaponMesh1P->CastShadow = false;
	WeaponMesh1P->bReceivesDecals = false;
	WeaponMesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh1P->SetGenerateOverlapEvents(false);
	WeaponMesh1P->SetHiddenInGame(true);

	// Third person: the gun other players see in your hands.
	WeaponMesh3P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh3P"));
	WeaponMesh3P->SetupAttachment(SceneRoot);
	WeaponMesh3P->SetOwnerNoSee(true);
	WeaponMesh3P->bReceivesDecals = false;
	WeaponMesh3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh3P->SetGenerateOverlapEvents(false);
	WeaponMesh3P->SetHiddenInGame(true);
}

void AFPSWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSWeapon, bEquipped);
	DOREPLIFETIME(AFPSWeapon, AmmoState);
}

void AFPSWeapon::SetAmmoInMag(int32 NewAmmo)
{
	if (HasAuthority())
	{
		const int32 PreviousAmmo = AmmoInMag;
		AmmoInMag = FMath::Max(NewAmmo, 0);
		if (PreviousAmmo == AmmoInMag) return;
		AmmoState.Ammo = AmmoInMag;
		++AmmoState.Revision;
		OnAmmoInMagChanged.Broadcast(this, AmmoInMag, PreviousAmmo);
	}
}

void AFPSWeapon::OnRep_AmmoState()
{
	const int32 PreviousAmmo = AmmoInMag;
	AmmoInMag = AmmoState.Ammo;
	// A newer revision can carry the same raw value after shot+reload coalescing.
	// Combat still needs to see it to release an older prediction baseline.
	OnAmmoInMagChanged.Broadcast(this, AmmoInMag, PreviousAmmo);
}

void AFPSWeapon::BeginPlay()
{
	Super::BeginPlay();

	// Clients only learn about this actor through replication, and that can happen
	// before or after the combat component's CurrentWeapon notification. Attaching
	// here as well means the weapon is already in the hands on the first frame the
	// remote client sees it, whatever order the two arrived in.
	if (!HasAuthority() && bEquipped)
	{
		TryAttachToOwner();
	}
}

void AFPSWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// A socket child is only re-evaluated when its parent's transform changes. A
	// skeletal mesh animates its bones without moving the component, so a character
	// standing still would leave the weapon at whatever transform it was attached
	// with. Recomputing the world transform here makes the weapon track the hand
	// frame by frame.
	if (WeaponMesh3P && WeaponMesh3P->GetAttachParent())
	{
		WeaponMesh3P->UpdateComponentToWorld();
	}
	if (WeaponMesh1P && WeaponMesh1P->GetAttachParent())
	{
		WeaponMesh1P->UpdateComponentToWorld();
	}
}

void AFPSWeapon::EndPlay(const EEndPlayReason::Type Reason)
{
	DetachFromCharacter();
	Super::EndPlay(Reason);
}

void AFPSWeapon::SetEquipped(bool bNewEquipped)
{
	const AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	if (bNewEquipped && Character && (Character->IsDead() || Character->IsActorBeingDestroyed()))
	{
		HideForOwnerDeath();
		return;
	}
	const bool bChanged = bEquipped != bNewEquipped;
	bEquipped = bNewEquipped;
	if (bChanged && HasAuthority()) ForceNetUpdate();
}

void AFPSWeapon::OnRep_Equipped()
{
	if (bEquipped)
	{
		TryAttachToOwner();
	}
	else
	{
		DetachFromCharacter();
	}
}

void AFPSWeapon::OnRep_Owner()
{
	Super::OnRep_Owner();

	// Owner and bEquipped replicate in the same bunch; whichever arrives second
	// re-runs the attach. Attaching twice is harmless.
	if (bEquipped)
	{
		TryAttachToOwner();
	}
}

void AFPSWeapon::TryAttachToOwner()
{
	AFPSCharacter* Character = Cast<AFPSCharacter>(GetOwner());
	if (!IsValid(Character))
	{
		// Owner may be an unresolved NetGUID. Preserve bEquipped so OnRep_Owner
		// can attach when the living character arrives later.
		DetachFromCharacter();
		return;
	}
	if (Character->IsDead() || Character->IsActorBeingDestroyed())
	{
		HideForOwnerDeath();
		return;
	}
	UFPSCombatComponent* Combat = Character ? Character->GetCombatComponent() : nullptr;
	if (!Combat)
	{
		return;
	}

	// Only the equipped weapon goes into the hands. A null CurrentWeapon is exactly
	// the case this path exists for, so treat it as "this one is equipped".
	const AFPSWeapon* Equipped = Combat->GetCurrentWeapon();
	if (Equipped && Equipped != this)
	{
		return;
	}

	Combat->AttachWeaponToOwner(this);
}

void AFPSWeapon::AttachToCharacter(ACharacter* Character, FName Socket1P, FName Socket3P)
{
	if (!Character)
	{
		return;
	}
	if (const AFPSCharacter* FPSCharacter = Cast<AFPSCharacter>(Character);
		FPSCharacter && (FPSCharacter->IsDead() || FPSCharacter->IsActorBeingDestroyed()))
	{
		HideForOwnerDeath();
		return;
	}

	// Both meshes are always attached: the own-see flags above decide which one each
	// client actually draws. Gating the first-person attach on the local controller
	// instead would drop it, because a player pawn runs BeginPlay before it is
	// possessed (and therefore before it has a controller).
	//
	// Always go through the character's accessors: which component is which is a
	// character-class decision, and GetMesh() is not necessarily the third-person mesh.
	USkeletalMeshComponent* Mesh1P = nullptr;
	USkeletalMeshComponent* Mesh3P = nullptr;
	if (const AFPSCharacter* FPSCharacter = Cast<AFPSCharacter>(Character))
	{
		Mesh1P = FPSCharacter->GetMesh1P();
		Mesh3P = FPSCharacter->GetMesh3P();

		// A null Mesh3P means the weapon would hang off the first-person arms below.
		if (!Mesh3P)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("GetMesh3P() returned null on '%s'; third-person weapon will use the inherited mesh."),
				*Character->GetName());
		}
	}
	// Character classes without a dedicated first-person mesh show the weapon on the
	// body mesh for their owner as well.
	if (!Mesh3P)
	{
		Mesh3P = Character->GetMesh();
	}
	if (!Mesh1P)
	{
		Mesh1P = Mesh3P;
	}

	// AttachToComponent does not validate socket names: a missing socket silently
	// snaps the component to the parent's origin. Validate here and fall back to a
	// socket both skeletons have, so a typo in the data asset cannot make the weapon
	// disappear inside the body.
	auto ResolveSocket = [](const USkeletalMeshComponent* Mesh, FName Requested) -> FName
	{
		if (!Mesh)
		{
			return NAME_None;
		}

		if (!Requested.IsNone() && Mesh->DoesSocketExist(Requested))
		{
			return Requested;
		}

		const FName FallbackSocket(TEXT("hand_r"));
		if (Mesh->DoesSocketExist(FallbackSocket))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Weapon attach socket '%s' does not exist on '%s'; falling back to '%s'."),
				*Requested.ToString(), *Mesh->GetName(), *FallbackSocket.ToString());
			return FallbackSocket;
		}

		UE_LOG(LogTemp, Error,
			TEXT("Weapon attach socket '%s' does not exist on '%s' and fallback '%s' is missing too; "
				 "the weapon will sit at the mesh origin."),
			*Requested.ToString(), *Mesh->GetName(), *FallbackSocket.ToString());
		return NAME_None;
	};

	const FName EffectiveSocket3P = ResolveSocket(Mesh3P, Socket3P);
	const FName EffectiveSocket1P = ResolveSocket(Mesh1P, Socket1P);

	if (WeaponMesh3P && Mesh3P)
	{
		if (!IsSocketTransformPlausible(Mesh3P, EffectiveSocket3P))
		{
			// The character's mesh has not evaluated a pose yet, so the socket reports a
			// position far outside the body. Attaching now would bake that position in;
			// keep the mesh hidden and retry once the pose exists.
			if (SocketRetriesLeft <= 0)
			{
				SocketRetriesLeft = 20;
			}
			--SocketRetriesLeft;

			if (SocketRetriesLeft > 0)
			{
				GetWorldTimerManager().SetTimer(SocketRetryHandle, this, &AFPSWeapon::TryAttachToOwner, 0.1f, false);
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("Giving up attaching WeaponMesh3P of '%s' to '%s': socket '%s' never resolved to a plausible position."),
					*GetName(), *Mesh3P->GetName(), *EffectiveSocket3P.ToString());
			}
		}
		// Only show the mesh once it is attached: otherwise a failed attach leaves it
		// visible at the actor's spawn transform (the world origin).
		else if (WeaponMesh3P->AttachToComponent(Mesh3P, FAttachmentTransformRules::SnapToTargetNotIncludingScale, EffectiveSocket3P))
		{
			SocketRetriesLeft = 0;
			GetWorldTimerManager().ClearTimer(SocketRetryHandle);
			WeaponMesh3P->SetHiddenInGame(false);

			// Says which machine attached what and where the mesh ended up relative to
			// the body. "offset" is the decisive value: a fist-sized offset means the
			// socket resolved, a zero-ish offset means it silently snapped to the mesh
			// origin. The visibility fields catch a component that is attached but not
			// drawn for a reason other than bHiddenInGame.
			const USceneComponent* AttachParent = WeaponMesh3P->GetAttachParent();
			UE_LOG(LogTemp, Log,
				TEXT("[WeaponAttach] '%s' role=%d netmode=%d parent='%s' parentLoc=%s socket='%s' loc=%s offset=%s "
					 "scale=%s vis=%d bVis=%d hiddenGame=%d actorHidden=%d asset='%s'"),
				*GetName(), static_cast<int32>(GetLocalRole()), static_cast<int32>(GetNetMode()),
				*AttachParent->GetName(),
				*AttachParent->GetComponentLocation().ToCompactString(),
				*WeaponMesh3P->GetAttachSocketName().ToString(),
				*WeaponMesh3P->GetComponentLocation().ToCompactString(),
				*(WeaponMesh3P->GetComponentLocation() - AttachParent->GetComponentLocation()).ToCompactString(),
				*WeaponMesh3P->GetComponentScale().ToCompactString(),
				WeaponMesh3P->IsVisible() ? 1 : 0,
				WeaponMesh3P->GetVisibleFlag() ? 1 : 0,
				WeaponMesh3P->bHiddenInGame ? 1 : 0,
				(GetOwner() && GetOwner()->IsHidden()) ? 1 : 0,
				WeaponMesh3P->GetSkinnedAsset() ? *WeaponMesh3P->GetSkinnedAsset()->GetName() : TEXT("None"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to attach WeaponMesh3P to '%s' at socket '%s'."),
				*Mesh3P->GetName(), *EffectiveSocket3P.ToString());
		}
	}

	if (WeaponMesh1P && Mesh1P)
	{
		if (WeaponMesh1P->AttachToComponent(Mesh1P, FAttachmentTransformRules::SnapToTargetNotIncludingScale, EffectiveSocket1P))
		{
			WeaponMesh1P->SetHiddenInGame(false);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to attach WeaponMesh1P to '%s' at socket '%s'."),
				*Mesh1P->GetName(), *EffectiveSocket1P.ToString());
		}
	}
}

bool AFPSWeapon::IsSocketTransformPlausible(const USkeletalMeshComponent* Body, FName Socket) const
{
	if (!Body || Socket.IsNone())
	{
		return true;
	}

	// No hand is ever a kilometre from the body. A larger offset means the socket is
	// being resolved against bone transforms that have not been evaluated yet.
	const FVector Offset =
		Body->GetSocketTransform(Socket, RTS_World).GetTranslation() - Body->GetComponentLocation();
	const double Distance = Offset.Size();
	if (Distance <= MaxSocketOffset)
	{
		return true;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[WeaponAttach] '%s': socket '%s' resolves %.0f units away from '%s'; the pose is not ready yet, retrying."),
		*GetName(), *Socket.ToString(), Distance, *Body->GetName());
	return false;
}

void AFPSWeapon::DetachFromCharacter()
{
	// An unequipped weapon must not be re-attached by a pending retry.
	GetWorldTimerManager().ClearTimer(SocketRetryHandle);
	SocketRetriesLeft = 0;

	if (WeaponMesh1P)
	{
		WeaponMesh1P->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		WeaponMesh1P->SetHiddenInGame(true, true);
	}

	if (WeaponMesh3P)
	{
		WeaponMesh3P->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		WeaponMesh3P->SetHiddenInGame(true, true);
	}
}

void AFPSWeapon::HideForOwnerDeath()
{
	SetEquipped(false);
	DetachFromCharacter();
	for (USkeletalMeshComponent* Mesh : {WeaponMesh1P.Get(), WeaponMesh3P.Get()})
	{
		if (!Mesh) continue;
		Mesh->SetCastHiddenShadow(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}
