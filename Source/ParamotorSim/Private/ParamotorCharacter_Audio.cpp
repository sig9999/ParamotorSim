// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorCharacter.h"
#include "ParamotorLogging.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "ParamotorGameInstance.h"
#include "ParamotorPlayerController.h"
#include "WingActor.h"
#include "AtmosphereModel.h"
#include "SurfaceService.h"

// ==== FOOTSTEP + WIND AUDIO ====

void AParamotorCharacter::LoadFootstepSounds()
{
	// Grass (normal walk)
	for (int32 i = 1; i <= 6; i++)
	{
		FString Path = FString::Printf(TEXT("/Game/Sounds/footstep_grass_%d.footstep_grass_%d"), i, i);
		USoundBase* Snd = LoadObject<USoundBase>(nullptr, *Path);
		if (Snd) GrassFootstepSounds.Add(Snd);
	}
	// Grass (heavy - jogging/running)
	for (int32 i = 1; i <= 3; i++)
	{
		FString Path = FString::Printf(TEXT("/Game/Sounds/footstep_grass_heavy_%d.footstep_grass_heavy_%d"), i, i);
		USoundBase* Snd = LoadObject<USoundBase>(nullptr, *Path);
		if (Snd) GrassFootstepHeavySounds.Add(Snd);
	}
	// Dirt (normal walk)
	for (int32 i = 1; i <= 6; i++)
	{
		FString Path = FString::Printf(TEXT("/Game/Sounds/footstep_dirt_%d.footstep_dirt_%d"), i, i);
		USoundBase* Snd = LoadObject<USoundBase>(nullptr, *Path);
		if (Snd) DirtFootstepSounds.Add(Snd);
	}
	// Dirt (heavy - jogging/running)
	for (int32 i = 1; i <= 3; i++)
	{
		FString Path = FString::Printf(TEXT("/Game/Sounds/footstep_dirt_heavy_%d.footstep_dirt_heavy_%d"), i, i);
		USoundBase* Snd = LoadObject<USoundBase>(nullptr, *Path);
		if (Snd) DirtFootstepHeavySounds.Add(Snd);
	}
	// Fallback: load old single sounds if new ones weren't found
	if (GrassFootstepSounds.Num() == 0)
	{
		USoundBase* OldGrass = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/footstep_grass.footstep_grass"));
		if (OldGrass) GrassFootstepSounds.Add(OldGrass);
	}
	if (DirtFootstepSounds.Num() == 0)
	{
		USoundBase* OldDirt = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/footstep_dirt.footstep_dirt"));
		if (OldDirt) DirtFootstepSounds.Add(OldDirt);
	}
	UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Loaded %d grass + %d grass-heavy + %d dirt + %d dirt-heavy footstep sounds"),
		GrassFootstepSounds.Num(), GrassFootstepHeavySounds.Num(),
		DirtFootstepSounds.Num(), DirtFootstepHeavySounds.Num());
}
void AParamotorCharacter::LoadAmbientAudio()
{
	USoundBase* WindSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/wind_ambient.wind_ambient"));
	if (WindSound && WindAmbientAudio)
	{
		WindAmbientAudio->SetSound(WindSound);
		WindAmbientAudio->SetVolumeMultiplier(0.3f);
		WindAmbientAudio->Play();
		UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Wind ambient started"));
	}

	USoundBase* FallSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/fall_sound.fall_sound"));
	if (FallSound && FallAudio)
	{
		FallAudio->SetSound(FallSound);
	}
	UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: BeginPlay at %s possessed=%d"),
		*GetActorLocation().ToString(), IsPlayerControlled());
}
void AParamotorCharacter::UpdateFootstepAudio(float DeltaTime, UCharacterMovementComponent* CMC)
{
	if (!CMC) return;

	bool bIsMoving = SmoothedAnimSpeed > 20.0f && CMC->IsMovingOnGround();

	if (bIsMoving)
	{
		float SpeedRatio = FMath::Clamp(SmoothedAnimSpeed / 270.0f, 0.0f, 1.0f);
		float StepInterval = FMath::Lerp(0.55f, 0.32f, SpeedRatio);

		FootstepTimer += DeltaTime;

		if (FootstepTimer >= StepInterval)
		{
			PlayFootstep();
			FootstepTimer = 0.0f;
		}
	}
	else
	{
		FootstepTimer = 0.0f;
	}
}
void AParamotorCharacter::UpdateWindAudio()
{
	if (!WindAmbientAudio) return;

	// Real wind noise: SPL ∝ U^5 (50 dB/decade), so amplitude ∝ U^2.5
	// The sound source is apparent wind at the pilot/ear:
	//   U_apparent = U_true_wind - U_vehicle (vector difference)
	// We use the wing's live dynamic wind (includes gusts, turbulence, altitude gradient)
	// for the true wind, and the pilot's velocity for the vehicle motion.

	float WindMs = 0.0f;
	FVector WindVec = FVector::ZeroVector;

	// Try to get live wind from the wing actor (includes gusts, turbulence, gradient)
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetController());
	if (PC && PC->SpawnedWingActor)
	{
		WindVec = PC->SpawnedWingActor->GetWindVector() / 100.0f;  // cm/s to m/s
		WindMs = WindVec.Size();
	}
	else if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
	{
		WindMs = GI->WindSpeed;
	}

	// Apparent wind = true wind - vehicle velocity (vector)
	FVector PilotVel = GetVelocity() / 100.0f;  // cm/s to m/s
	FVector ApparentWindVec = WindVec - PilotVel;
	float ApparentWindMs = ApparentWindVec.Size();

	// Use the AtmosphereModel wind noise amplitude (U^2.5 scaling)
	float NoiseAmplitude = ComputeWindNoiseAmplitude(ApparentWindMs);

	// Map to volume: 0.05 (silence) to 0.7 (strong wind)
	// The U^2.5 curve gives a realistic progression:
	//   2 m/s → 0.06, 5 m/s → 0.15, 10 m/s → 0.35, 20 m/s → 0.65, 30 m/s → 0.70
	float WindVol = 0.05f + NoiseAmplitude * 0.65f;
	WindAmbientAudio->SetVolumeMultiplier(WindVol);

	// Also modulate pitch slightly with wind speed (higher wind = slightly higher pitch)
	// Real wind noise shifts toward higher frequencies at higher speeds
	float PitchRatio = 0.9f + FMath::Clamp(ApparentWindMs / 30.0f, 0.0f, 1.0f) * 0.3f;
	WindAmbientAudio->SetPitchMultiplier(PitchRatio);
}
void AParamotorCharacter::PlayFootstep()
{
	if (!FootstepAudio) return;

	// Trace down to detect surface type
	FVector Start = GetActorLocation();
	FVector End = Start - FVector(0.0f, 0.0f, 120.0f);
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	// Determine if jogging (heavy footsteps)
	float Speed = GetVelocity().Size();
	bool bIsJogging = Speed > 280.0f;  // Jog threshold

	TArray<USoundBase*>* SurfaceSounds = nullptr;
	TArray<USoundBase*>* HeavySounds = nullptr;

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, QueryParams))
	{
		// Default to grass - the map is a coastal grass environment
		SurfaceSounds = &GrassFootstepSounds;
		HeavySounds = &GrassFootstepHeavySounds;

		// Semantic surface routing: geography → ESurfaceSemantic → phys
		// profile → sound set. Bound profile sounds win; otherwise the
		// profile's built-in fallback set (grass/dirt) is used.
		bool bSemHandled = false;
		if (ASurfaceService* SS = ASurfaceService::Get(GetWorld()))
		{
			FResolvedSurface Surf;
			if (SS->ResolveAtWorldPos(Hit.ImpactPoint, Surf, Hit.ImpactPoint.Z) && Surf.PhysProfile)
			{
				const FSurfacePhysProfile* PP = Surf.PhysProfile;
				// Bound profile sounds — loaded on first use and cached.
				static TMap<const FSurfacePhysProfile*, TArray<TObjectPtr<USoundBase>>> StepCache;
				static TMap<const FSurfacePhysProfile*, TArray<TObjectPtr<USoundBase>>> HeavyCache;
				if (!StepCache.Contains(PP))
				{
					TArray<TObjectPtr<USoundBase>>& N = StepCache.Add(PP);
					for (const TSoftObjectPtr<USoundBase>& S : PP->FootstepSounds)
						if (USoundBase* L = S.LoadSynchronous()) N.Add(L);
					TArray<TObjectPtr<USoundBase>>& H = HeavyCache.Add(PP);
					for (const TSoftObjectPtr<USoundBase>& S : PP->FootstepHeavySounds)
						if (USoundBase* L = S.LoadSynchronous()) H.Add(L);
				}
				TArray<TObjectPtr<USoundBase>>& N = StepCache[PP];
				TArray<TObjectPtr<USoundBase>>& H = HeavyCache[PP];
				if (N.Num())
				{
					// TObjectPtr view — PlayFootstep below uses TArray<USoundBase*>*,
					// so copy into a plain view when bound sounds exist.
					static TArray<USoundBase*> NView, HView;
					NView.Reset(); HView.Reset();
					for (USoundBase* S : N) NView.Add(S);
					for (USoundBase* S : H) HView.Add(S);
					SurfaceSounds = &NView;
					HeavySounds = HView.Num() ? &HView : &NView;
				}
				else
				{
					SurfaceSounds = PP->FootstepFallback == EFootstepFallback::Grass
						? &GrassFootstepSounds : &DirtFootstepSounds;
					HeavySounds = PP->FootstepFallback == EFootstepFallback::Grass
						? &GrassFootstepHeavySounds : &DirtFootstepHeavySounds;
				}
				bSemHandled = true;
			}
		}

		if (!bSemHandled)
		{
			// Off-globe fallback (landscape maps): legacy physmat-name hint.
			UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get();
			if (PhysMat)
			{
				FString MatName = PhysMat->GetName().ToLower();
				if (MatName.Contains(TEXT("dirt")) || MatName.Contains(TEXT("rock")) ||
					MatName.Contains(TEXT("sand")) || MatName.Contains(TEXT("stone")) ||
					MatName.Contains(TEXT("gravel")))
				{
					SurfaceSounds = &DirtFootstepSounds;
					HeavySounds = &DirtFootstepHeavySounds;
				}
			}
		}
	}
	else
	{
		SurfaceSounds = &GrassFootstepSounds;
		HeavySounds = &GrassFootstepHeavySounds;
	}

	// Use heavy sounds when jogging, if available
	TArray<USoundBase*>* ActiveSounds = bIsJogging ? HeavySounds : SurfaceSounds;

	// Fallback: if heavy sounds empty, use normal sounds
	if (!ActiveSounds || ActiveSounds->Num() == 0)
	{
		ActiveSounds = SurfaceSounds;
	}
	if (!ActiveSounds || ActiveSounds->Num() == 0)
	{
		ActiveSounds = &GrassFootstepSounds;
	}

	if (ActiveSounds->Num() == 0) return;

	int32 Index = FMath::RandRange(0, ActiveSounds->Num() - 1);
	USoundBase* Sound = (*ActiveSounds)[Index];
	if (Sound)
	{
		// Natural variation - wider pitch range for variety, volume scales with speed
		float Pitch = FMath::FRandRange(0.92f, 1.08f);
		// Volume: walk = 0.55-0.70, jog = 0.70-0.90
		float BaseVol = bIsJogging ? 0.75f : 0.55f;
		float Vol = BaseVol + FMath::FRandRange(0.0f, 0.15f);
		FootstepAudio->SetSound(Sound);
		FootstepAudio->SetPitchMultiplier(Pitch);
		FootstepAudio->SetVolumeMultiplier(Vol);
		FootstepAudio->Play();
	}
}