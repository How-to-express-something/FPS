// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/** Native gameplay tags declared by the FPS module. */
namespace FPSGameplayTags
{
	/** Weapon type tags (Weapon.Type.*), used to look up per-weapon data. */
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Type_Rifle);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Type_Pistol);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Type_Shotgun);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Type_Sniper);
}
