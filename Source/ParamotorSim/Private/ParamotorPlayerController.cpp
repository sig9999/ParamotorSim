// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorPlayerController.h"
#include "UW_PauseMenu.h"
#include "UW_FlightHUD.h"
#include "ParamotorActor.h"
#include "WingActor.h"
#include "ParamotorCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "AudioDevice.h"

AParamotorPlayerController::AParamotorPlayerController()
{
	bIsPaused = false;
	PauseMenuInstance = nullptr;
	FlightHUDInstance = nullptr;
	SpawnedParamotorActor = nullptr;

	// Available wing models (Sig brand)
	WingModelNames = { TEXT("Sig 16"), TEXT("Sig 20"), TEXT("Sig 22"), TEXT("Sig 25"), TEXT("Sig 30"), TEXT("Sig 35"), TEXT("MacPara Charger 25M") };
}

void AParamotorPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Create and display the flight HUD
	if (!FlightHUDInstance)
	{
		FlightHUDInstance = CreateWidget<UUserWidget>(this, UUW_FlightHUD::StaticClass());
	}
	if (FlightHUDInstance)
	{
		FlightHUDInstance->AddToViewport(0);
	}
}

void AParamotorPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AParamotorPlayerController::TogglePauseMenu);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AParamotorPlayerController::OnInteract);
	// Engine controls
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AParamotorPlayerController::OnStartEnginePressed);
	InputComponent->BindKey(EKeys::P, IE_Released, this, &AParamotorPlayerController::OnStartEngineReleased);
	InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AParamotorPlayerController::OnThrottleUp);
	InputComponent->BindKey(EKeys::Up, IE_Released, this, &AParamotorPlayerController::OnThrottleUpReleased);
	InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AParamotorPlayerController::OnThrottleDown);
	InputComponent->BindKey(EKeys::Down, IE_Released, this, &AParamotorPlayerController::OnThrottleDownReleased);
	// Wing brake controls: Q = left brake, E = right brake, both = flare
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AParamotorPlayerController::OnLeftBrakePressed);
	InputComponent->BindKey(EKeys::Q, IE_Released, this, &AParamotorPlayerController::OnLeftBrakeReleased);
	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AParamotorPlayerController::OnRightBrakePressed);
	InputComponent->BindKey(EKeys::E, IE_Released, this, &AParamotorPlayerController::OnRightBrakeReleased);
	// A-line lift: R = lift A-lines (start inflation), release to stop lifting
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AParamotorPlayerController::OnALinesPressed);
	InputComponent->BindKey(EKeys::R, IE_Released, this, &AParamotorPlayerController::OnALinesReleased);
	// Trimmer controls: = trims fast, - trims slow (hold to adjust)
	InputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &AParamotorPlayerController::OnTrimmerFastPressed);
	InputComponent->BindKey(EKeys::Equals, IE_Released, this, &AParamotorPlayerController::OnTrimmerReleased);
	InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &AParamotorPlayerController::OnTrimmerSlowPressed);
	InputComponent->BindKey(EKeys::Hyphen, IE_Released, this, &AParamotorPlayerController::OnTrimmerReleased);
	// Temporary debug keys: T = spawn wing, Y = spawn paramotor, U = connect risers
	InputComponent->BindKey(EKeys::T, IE_Pressed, this, &AParamotorPlayerController::SpawnWing);
	InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AParamotorPlayerController::SpawnParamotor);
	InputComponent->BindKey(EKeys::L, IE_Pressed, this, &AParamotorPlayerController::OnFuelCutoff);
	// F5 = force takeoff (debug)
	InputComponent->BindKey(EKeys::F5, IE_Pressed, this, &AParamotorPlayerController::OnForceTakeoff);
	// G = toggle seated/standing position (reduces drag when seated in flight)
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AParamotorPlayerController::OnToggleSeat);
	// [ = hang point back (lean motor back, more upright), ] = hang point forward (lean motor forward, more reclined)
	InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &AParamotorPlayerController::OnHangPointBack);
	InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &AParamotorPlayerController::OnHangPointForward);

	// Left mouse button = grab/release wing (when looking at it)
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AParamotorPlayerController::OnClickGrab);
}

void AParamotorPlayerController::TogglePauseMenu()
{
	bIsPaused = !bIsPaused;

	if (bIsPaused)
	{
		SetPause(true);

		// Mute all game audio when paused
		if (FAudioDevice* AudioDevice = GetWorld()->GetAudioDeviceRaw())
		{
			AudioDevice->SetDeviceMuted(true);
		}

		if (!PauseMenuInstance)
		{
			PauseMenuInstance = CreateWidget<UUserWidget>(this, UUW_PauseMenu::StaticClass());
		}

		if (PauseMenuInstance)
		{
			PauseMenuInstance->AddToViewport(100);
			PauseMenuInstance->SetVisibility(ESlateVisibility::Visible);
		}

		// Use UIOnly so the widget receives all keyboard input (including ESC to resume)
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(PauseMenuInstance->TakeWidget());
		SetInputMode(InputMode);
		bShowMouseCursor = true;
	}
	else
	{
		SetPause(false);

		// Restore game audio when unpaused
		if (FAudioDevice* AudioDevice = GetWorld()->GetAudioDeviceRaw())
		{
			AudioDevice->SetDeviceMuted(false);
		}

		if (PauseMenuInstance)
		{
			PauseMenuInstance->RemoveFromParent();
			PauseMenuInstance = nullptr;
		}

		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void AParamotorPlayerController::SpawnParamotor()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;

	FVector Forward = MyPawn->GetActorForwardVector();
	FVector Start = MyPawn->GetActorLocation() + Forward * 200.0f;
	// Trace down to ground to find proper Z
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(MyPawn);
	FVector TraceEnd = Start - FVector(0, 0, 500.0f);
	float SpawnZ = 0.0f;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, TraceEnd, ECC_Visibility, QueryParams))
	{
		SpawnZ = Hit.Location.Z;
	}
	FVector SpawnLoc = FVector(Start.X, Start.Y, SpawnZ);
	FRotator SpawnRot = MyPawn->GetActorRotation();

	// Spawn the ParamotorActor instead of individual static meshes
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedParamotorActor = GetWorld()->SpawnActor<AParamotorActor>(SpawnLoc, SpawnRot, SpawnParams);
	if (SpawnedParamotorActor)
	{
		SpawnedActors.Add(SpawnedParamotorActor);
		UE_LOG(LogTemp, Warning, TEXT("Spawned ParamotorActor at %s"), *SpawnLoc.ToString());
	}
}

void AParamotorPlayerController::OnClickGrab()
{
	if (!SpawnedWingActor) return;

	// If already grabbing, release
	if (SpawnedWingActor->IsGrabbing())
	{
		SpawnedWingActor->ReleaseEdge();
		return;
	}

	// Only grab if wing is crinkled or spread (not in bag, not connected)
	if (!SpawnedWingActor->IsCrinkled() && !SpawnedWingActor->IsSpread()) return;
	if (SpawnedWingActor->IsConnected()) return;

	// Trace from camera center (crosshair) into the world
	FVector CamLoc;
	FRotator CamRot;
	GetPlayerViewPoint(CamLoc, CamRot);
	FVector TraceStart = CamLoc;
	FVector TraceEnd = CamLoc + CamRot.Vector() * 1000.0f;  // 10m trace

	// Simple distance check — if player is close enough to the wing
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;
	FVector PlayerPos = MyPawn->GetActorLocation();
	float DistToWing = FVector::Dist2D(PlayerPos, SpawnedWingActor->GetActorLocation());
	if (DistToWing > 800.0f)
	{
		UE_LOG(LogTemp, Log, TEXT("Too far from wing to grab (%.0f cm)"), DistToWing);
		return;
	}

	// Use the trace end point as the grab position (where the crosshair points)
	// But clamp it to be near the player's reach
	FVector GrabPos = TraceEnd;

	// If the trace hits something, use that point
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(MyPawn);
	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		GrabPos = Hit.Location;
	}

	// Grab the wing at the crosshair point
	bool bGrabbed = SpawnedWingActor->GrabNearestEdge(GrabPos);
	if (bGrabbed)
	{
		UE_LOG(LogTemp, Warning, TEXT("Grabbed wing via crosshair at %s"), *GrabPos.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Crosshair not pointing at grabbable wing"));
	}
}

void AParamotorPlayerController::OnInteract()
{
	APawn* MyPawn = GetPawn();
	AParamotorCharacter* Char = Cast<AParamotorCharacter>(MyPawn);
	if (!Char) return;

	FVector PlayerPos = Char->GetActorLocation();

	// Context-sensitive F:
	// 1. If wing is in bag and near -> unpack from bag
	// 2. If paramotor not equipped AND near paramotor -> equip paramotor

	// 1. Unpack wing from bag
	if (SpawnedWingActor && SpawnedWingActor->IsInBag())
	{
		float DistToWing = FVector::Dist2D(PlayerPos, SpawnedWingActor->GetActorLocation());
		if (DistToWing < 300.0f)
		{
			SpawnedWingActor->UnpackFromBag();
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Walk to the wing bag to unpack it (%.0f cm away)"), DistToWing);
		}
		return;
	}

	// 2. (Grab/release is now handled by left mouse click — see OnClickGrab)

	// 3. Equip paramotor
	if (SpawnedParamotorActor && !SpawnedParamotorActor->IsEquipped())
	{
		float DistToParamotor = FVector::Dist2D(PlayerPos, SpawnedParamotorActor->GetActorLocation());
		if (DistToParamotor < 500.0f)
		{
			SpawnedParamotorActor->Interact(Char);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Get closer to the paramotor to equip it (%.0f cm away)"), DistToParamotor);
		}
		return;
	}

	// 4. Connect risers to spread wing (when paramotor is equipped)
	if (SpawnedParamotorActor && SpawnedParamotorActor->IsEquipped()
		&& SpawnedWingActor && SpawnedWingActor->IsSpread() && !SpawnedWingActor->IsConnected())
	{
		float DistToWing = FVector::Dist2D(PlayerPos, SpawnedWingActor->GetActorLocation());
		if (DistToWing < 600.0f)
		{
			// Get carabiner positions from the paramotor's wing clip rings
			FVector LeftCarabiner, RightCarabiner;
			if (SpawnedParamotorActor->GetCarabinerWorldPositions(LeftCarabiner, RightCarabiner))
			{
				// Use real ring positions from the paramotor frame
			}
			else
			{
				// Fallback: estimate from character position (hip/harness height)
				FVector CharRight = Char->GetActorRightVector();
				float CarabHalfSpacing = CARABINER_SPACING_CM * 0.5f;
				float HarnessHeightCm = 95.0f;  // Hip/harness height (real carabiner position)
				LeftCarabiner = PlayerPos + CharRight * -CarabHalfSpacing + FVector(0, 0, HarnessHeightCm);
				RightCarabiner = PlayerPos + CharRight * CarabHalfSpacing + FVector(0, 0, HarnessHeightCm);
			}

			SpawnedWingActor->ConnectRisers(SpawnedParamotorActor, LeftCarabiner, RightCarabiner);

			// Pilot weight: character mass + paramotor mass + fuel mass
			float CharMass = 80.0f;
			if (auto* MC = Char->GetCharacterMovement()) CharMass = FMath::Max(50.0f, MC->Mass);
			float ParamotorMass = (SpawnedParamotorActor) ? 25.0f : 0.0f;
			float FuelMass = 5.0f;
			SpawnedWingActor->SetPilotWeight(CharMass + ParamotorMass + FuelMass);

			UE_LOG(LogTemp, Warning, TEXT("Risers connected to paramotor (pilot weight: %.0f kg, max: %.0f kg)"),
				SpawnedWingActor->GetPilotWeight(), SpawnedWingActor->GetMaxTakeoffWeight());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Walk to the wing to connect risers (%.0f cm away)"), DistToWing);
		}
		return;
	}
}

void AParamotorPlayerController::OnStartEnginePressed()
{
	if (SpawnedParamotorActor && SpawnedParamotorActor->IsEquipped())
	{
		EEngineState State = SpawnedParamotorActor->GetEngineState();
		if (State == EEngineState::Off)
		{
			// Start cranking
			SpawnedParamotorActor->StartCranking();
		}
		else if (State == EEngineState::Running)
		{
			// Press P while running = stop engine
			SpawnedParamotorActor->StopEngine();
		}
	}
}

void AParamotorPlayerController::OnStartEngineReleased()
{
	// If still cranking and P is released, stop cranking
	if (SpawnedParamotorActor && SpawnedParamotorActor->GetEngineState() == EEngineState::Cranking)
	{
		SpawnedParamotorActor->StopCranking();
	}
}

void AParamotorPlayerController::OnThrottleUp()
{
	bThrottleUpHeld = true;
}

void AParamotorPlayerController::OnThrottleUpReleased()
{
	bThrottleUpHeld = false;
}

void AParamotorPlayerController::OnThrottleDown()
{
	bThrottleDownHeld = true;
}

void AParamotorPlayerController::OnThrottleDownReleased()
{
	bThrottleDownHeld = false;
}

// ============================================================
// TRIMMER CONTROLS
// = trims fast (hold to increase), - trims slow (hold to decrease)
// Trimmer position: -1.0 = full slow, 0.0 = neutral, +1.0 = full fast
// ============================================================
void AParamotorPlayerController::OnTrimmerFastPressed()
{
	bTrimmerFastHeld = true;
	if (SpawnedWingActor)
	{
		// Move trimmer target toward full fast (+1.0)
		SpawnedWingActor->SetTrimmerTarget(SpawnedWingActor->GetTrimmerTargetPos() + 0.5f);
		UE_LOG(LogTemp, Warning, TEXT("Trimmer: FAST → %.2f (%s)"),
			SpawnedWingActor->GetTrimmerTargetPos(), *SpawnedWingActor->GetTrimmerLabel());
	}
}

void AParamotorPlayerController::OnTrimmerSlowPressed()
{
	bTrimmerSlowHeld = true;
	if (SpawnedWingActor)
	{
		// Move trimmer target toward full slow (-1.0)
		SpawnedWingActor->SetTrimmerTarget(SpawnedWingActor->GetTrimmerTargetPos() - 0.5f);
		UE_LOG(LogTemp, Warning, TEXT("Trimmer: SLOW → %.2f (%s)"),
			SpawnedWingActor->GetTrimmerTargetPos(), *SpawnedWingActor->GetTrimmerLabel());
	}
}

void AParamotorPlayerController::OnTrimmerReleased()
{
	bTrimmerFastHeld = false;
	bTrimmerSlowHeld = false;
}

void AParamotorPlayerController::OnLeftBrakePressed()
{
	bLeftBrakeHeld = true;
	if (SpawnedWingActor) SpawnedWingActor->PumpBrakes();
}

void AParamotorPlayerController::OnLeftBrakeReleased()
{
	bLeftBrakeHeld = false;
}

void AParamotorPlayerController::OnRightBrakePressed()
{
	bRightBrakeHeld = true;
	if (SpawnedWingActor) SpawnedWingActor->PumpBrakes();
}

void AParamotorPlayerController::OnRightBrakeReleased()
{
	bRightBrakeHeld = false;
}

void AParamotorPlayerController::OnALinesPressed()
{
	if (SpawnedWingActor)
	{
		SpawnedWingActor->LiftALines();
	}
}

void AParamotorPlayerController::OnALinesReleased()
{
	if (SpawnedWingActor)
	{
		SpawnedWingActor->ReleaseALines();
	}
}

void AParamotorPlayerController::OnFuelCutoff()
{
	if (SpawnedParamotorActor && SpawnedParamotorActor->IsEquipped())
	{
		SpawnedParamotorActor->ToggleFuelCutoff();
	}
}

void AParamotorPlayerController::OnForceTakeoff()
{
	if (SpawnedWingActor)
	{
		SpawnedWingActor->ForceTakeoff();
	}
}

// G = toggle seated/standing position
// In flight: transitions pilot from standing to seated (or back)
// Seated position reduces drag (better aerodynamics) and changes weight balance
void AParamotorPlayerController::OnToggleSeat()
{
	if (SpawnedWingActor && SpawnedWingActor->IsFlying())
	{
		SpawnedWingActor->ToggleSeat();
		UE_LOG(LogTemp, Warning, TEXT("Seat toggled — seated: %s"), SpawnedWingActor->IsSeated() ? TEXT("YES") : TEXT("NO"));
	}
}

// [ = hang point back (motor leans back, pilot more upright)
void AParamotorPlayerController::OnHangPointBack()
{
	if (SpawnedWingActor && SpawnedWingActor->IsFlying())
	{
		SpawnedWingActor->AdjustHangPoint(-0.1f);
		UE_LOG(LogTemp, Log, TEXT("Hang point back: %.2f"), SpawnedWingActor->GetHangPointOffset());
	}
}

// ] = hang point forward (motor leans forward, pilot more reclined)
void AParamotorPlayerController::OnHangPointForward()
{
	if (SpawnedWingActor && SpawnedWingActor->IsFlying())
	{
		SpawnedWingActor->AdjustHangPoint(0.1f);
		UE_LOG(LogTemp, Log, TEXT("Hang point forward: %.2f"), SpawnedWingActor->GetHangPointOffset());
	}
}

void AParamotorPlayerController::UpdateRotation(float DeltaTime)
{
	// During flight, the WingActor controls the pawn's full rotation (bank, pitch, yaw).
	// The default APlayerController::UpdateRotation calls Pawn->FaceRotation() which
	// forces the pawn to the control rotation (zero roll), locking the character level.
	// We override it to: save the WingActor's rotation, let the base class update
	// control rotation from mouse, then restore the pawn's rotation.

	bool bWingFlying = (SpawnedWingActor && SpawnedWingActor->IsFlying());

	if (bWingFlying && GetPawn())
	{
		// Save the pawn's current rotation (set by WingActor)
		FRotator SavedRot = GetPawn()->GetActorRotation();

		// Let the base class update control rotation from mouse input
		// It will also call FaceRotation, but with bUseControllerRotationYaw=false
		// (set during takeoff), FaceRotation won't override the pawn's rotation.
		Super::UpdateRotation(DeltaTime);

		// Force-restore the pawn's rotation in case anything touched it
		GetPawn()->SetActorRotation(SavedRot);
	}
	else
	{
		// Ground mode: normal controller rotation behavior
		Super::UpdateRotation(DeltaTime);
	}
}

void AParamotorPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Process throttle input when engine is running or cranking
	if (SpawnedParamotorActor)
	{
		EEngineState State = SpawnedParamotorActor->GetEngineState();
		if (State == EEngineState::Running || State == EEngineState::Cranking)
		{
			float CurrentThrottle = SpawnedParamotorActor->GetThrottle();

			// Throttle trigger speed: real Moster 185 trigger moves full travel in ~0.3s
		// Rate = 1/0.3 ≈ 3.3/s (spring-return trigger with finger force)
		const float ThrottleRate = 3.3f;
		// Spring return rate: real throttle spring returns to idle in ~2s → 0.5/s
		const float ThrottleReturnRate = 0.5f;
			if (bThrottleUpHeld)
			{
				CurrentThrottle += ThrottleRate * DeltaTime;
			}
			if (bThrottleDownHeld)
			{
				CurrentThrottle -= ThrottleRate * DeltaTime;
			}
			// Auto-return to idle: when neither key is held, throttle slowly
			// drifts back to 0 (like releasing the throttle trigger on a real paramotor)
			if (!bThrottleUpHeld && !bThrottleDownHeld && CurrentThrottle > 0.0f)
			{
				CurrentThrottle -= ThrottleReturnRate * DeltaTime;  // Spring return to idle
			}

			SpawnedParamotorActor->SetThrottle(CurrentThrottle);
		}

		// Pass thrust info to character for trip/fall logic
		APawn* MyPawn = GetPawn();
		AParamotorCharacter* Char = Cast<AParamotorCharacter>(MyPawn);
		if (Char && SpawnedParamotorActor->IsEquipped())
		{
			float Thrust = 0.0f;
			if (State == EEngineState::Running)
			{
				Thrust = SpawnedParamotorActor->GetThrottle();
			}
			Char->SetThrust(Thrust);
		}
	}

	// Update wing brake state (only when inflated)
	// Brake controls are handled below via Q/E keys and progressive ramping.

	// Update grab position while grabbing — follows the crosshair aim point
	if (SpawnedWingActor && SpawnedWingActor->IsGrabbing())
	{
		FVector CamLoc;
		FRotator CamRot;
		GetPlayerViewPoint(CamLoc, CamRot);
		// Grab point = where the crosshair points, but limited to player's reach
		FVector TraceEnd = CamLoc + CamRot.Vector() * 300.0f;  // 3m reach

		// If trace hits something, use that point
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		APawn* MyPawn = GetPawn();
		if (MyPawn) QueryParams.AddIgnoredActor(MyPawn);
		if (GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, TraceEnd, ECC_Visibility, QueryParams))
		{
			SpawnedWingActor->UpdateGrabPosition(Hit.Location);
		}
		else
		{
			SpawnedWingActor->UpdateGrabPosition(TraceEnd);
		}
	}

	// Update carabiner positions while risers are connected
	if (SpawnedWingActor && SpawnedWingActor->IsConnected() && SpawnedParamotorActor)
	{
		APawn* MyPawn3 = GetPawn();
		AParamotorCharacter* Char3 = Cast<AParamotorCharacter>(MyPawn3);
		if (Char3)
		{
			// Get actual carabiner positions from the paramotor's wing clip rings
			FVector LeftCarabiner, RightCarabiner;
			if (SpawnedParamotorActor->GetCarabinerWorldPositions(LeftCarabiner, RightCarabiner))
			{
				// Use real ring positions from the paramotor frame
				SpawnedWingActor->UpdateCarabinerPositions(LeftCarabiner, RightCarabiner);
			}
			else
			{
				// Fallback: estimate from character position (hip height, not chest)
				FVector CharPos = Char3->GetActorLocation();
				FVector CharRight = Char3->GetActorRightVector();
				float CarabHalfSpacing = CARABINER_SPACING_CM * 0.5f;
				float HarnessHeightCm = 95.0f;  // Hip/harness height (real carabiner position)
				LeftCarabiner = CharPos + CharRight * -CarabHalfSpacing + FVector(0, 0, HarnessHeightCm);
				RightCarabiner = CharPos + CharRight * CarabHalfSpacing + FVector(0, 0, HarnessHeightCm);
				SpawnedWingActor->UpdateCarabinerPositions(LeftCarabiner, RightCarabiner);
			}

			// Send brake input to wing — PROGRESSIVE (ramp up/down like real brake handles)
			// Q = pilot's left hand = left brake, E = pilot's right hand = right brake
			// Real brakes: pull takes ~0.5s to reach full, release takes ~0.3s
			float BrakePullSpeed = 2.0f;   // Hand-limited pull rate (0.5s full travel)
			float BrakeReleaseSpeed = 3.3f; // Spring-return release (0.3s)
			if (bLeftBrakeHeld)
				LeftBrakeAmount = FMath::Min(1.0f, LeftBrakeAmount + DeltaTime * BrakePullSpeed);
			else
				LeftBrakeAmount = FMath::Max(0.0f, LeftBrakeAmount - DeltaTime * BrakeReleaseSpeed);
			if (bRightBrakeHeld)
				RightBrakeAmount = FMath::Min(1.0f, RightBrakeAmount + DeltaTime * BrakePullSpeed);
			else
				RightBrakeAmount = FMath::Max(0.0f, RightBrakeAmount - DeltaTime * BrakeReleaseSpeed);

			SpawnedWingActor->SetLeftBrake(LeftBrakeAmount);
			SpawnedWingActor->SetRightBrake(RightBrakeAmount);

			// Send throttle and velocity for flight physics
			SpawnedWingActor->SetThrottle(SpawnedParamotorActor ? SpawnedParamotorActor->GetThrottle() : 0.0f);
			SpawnedWingActor->SetPilotVelocity(Char3->GetVelocity());

			// Set weight shift active when flying (mouse look = weight shift, not yaw)
			Char3->bWeightShiftActive = SpawnedWingActor->IsFlying();

			// Pass weight shift to wing for riser lowering + turn initiation
			SpawnedWingActor->SetWeightShift(Char3->WeightShift);

			// Set character arm IK targets to brake handle positions
			// The wing's internal "left/right" labels are in wing-local space and may not
			// match the pilot's left/right due to the wing's yaw rotation.
			// Determine which handle is actually on the pilot's left vs right using world-space.
			FVector CharPos = Char3->GetActorLocation();
			FVector WingLeftPos = SpawnedWingActor->GetLeftBrakeHandleWorldPos();
			FVector WingRightPos = SpawnedWingActor->GetRightBrakeHandleWorldPos();

			// Project both handle positions onto the pilot's right vector
			FVector PilotRight = Char3->GetActorRightVector();
			FVector ToLeftHandle = WingLeftPos - CharPos;
			FVector ToRightHandle = WingRightPos - CharPos;
			float LeftDot = FVector::DotProduct(ToLeftHandle, PilotRight);
			float RightDot = FVector::DotProduct(ToRightHandle, PilotRight);

			// The handle with negative dot product is on the pilot's left,
			// the one with positive dot product is on the pilot's right.
			FVector PilotLeftTarget, PilotRightTarget;
			if (LeftDot <= RightDot)
			{
				PilotLeftTarget = WingLeftPos;   // Wing-left is pilot-left
				PilotRightTarget = WingRightPos; // Wing-right is pilot-right
			}
			else
			{
				PilotLeftTarget = WingRightPos;  // Wing-right is pilot-left (wing flipped)
				PilotRightTarget = WingLeftPos;  // Wing-left is pilot-right
			}

			Char3->SetBrakeArmTargets(true, true, PilotLeftTarget, PilotRightTarget, bLeftBrakeHeld, bRightBrakeHeld);

			// Debug log for IK target verification
			IKLogTimer += DeltaTime;
			if (IKLogTimer > 1.0f)
			{
				IKLogTimer = 0.0f;
				UE_LOG(LogTemp, Warning, TEXT("IK: CharPos=%s L_Target=%s R_Target=%s LeftDot=%.0f RightDot=%.0f"),
					*CharPos.ToString(), *PilotLeftTarget.ToString(), *PilotRightTarget.ToString(),
					LeftDot, RightDot);
			}
		}
	}
	else
	{
		// Clear arm IK when not connected
		APawn* MyPawn4 = GetPawn();
		AParamotorCharacter* Char4 = Cast<AParamotorCharacter>(MyPawn4);
		if (Char4) Char4->ClearBrakeArmTargets();
	}
}

void AParamotorPlayerController::ApplyWingModel(AWingActor* Wing, int32 ModelIndex)
{
	if (!Wing) return;
	switch (ModelIndex)
	{
	case 0:  // Sig 16 — 16m², super small practice wing (kiting only)
		// Tiny wing for ground handling practice. Extremely high wing loading.
		// Will only fly in rare circumstances: strong wind, downhill run, full throttle.
		// Very fast, very twitchy, stalls easily — NOT for normal flight.
		Wing->WingModel.Name = TEXT("Sig 16");
		Wing->WingModel.WingArea = 16.0f;
		Wing->WingModel.MaxTakeoffWeight = 85.0f;
		Wing->WingModel.WingWeight = 4.5f;
		Wing->WingModel.CL0 = 0.45f;     // Realistic cambered airfoil CL0
		Wing->WingModel.CLMax = 1.4f;    // Realistic max lift for small wing
		Wing->WingModel.CD0 = 0.040f;    // Higher drag (small chord, low Re)
		Wing->WingModel.AR = 5.8f;       // Higher aspect ratio (long thin wing)
		Wing->WingModel.StallAoA = 13.0f; // Stalls earlier — twitchy

		Wing->WingModel.TrimAoA = 2.0f;  // Default trim AoA		break;
	case 1:  // Sig 20 — 20m², max 110kg (light pilot)
		Wing->WingModel.Name = TEXT("Sig 20");
		Wing->WingModel.WingArea = 20.0f;
		Wing->WingModel.MaxTakeoffWeight = 110.0f;
		Wing->WingModel.WingWeight = 5.5f;
		Wing->WingModel.CL0 = 0.48f;
		Wing->WingModel.CLMax = 1.5f;
		Wing->WingModel.CD0 = 0.036f;
		Wing->WingModel.AR = 5.2f;
		Wing->WingModel.StallAoA = 15.0f;

		Wing->WingModel.TrimAoA = 2.0f;  // Default trim AoA		break;
	case 2:  // Sig 22 — 22m², max 120kg (light-to-medium pilot)
		Wing->WingModel.Name = TEXT("Sig 22");
		Wing->WingModel.WingArea = 22.0f;
		Wing->WingModel.MaxTakeoffWeight = 120.0f;
		Wing->WingModel.WingWeight = 5.8f;
		Wing->WingModel.CL0 = 0.49f;
		Wing->WingModel.CLMax = 1.5f;
		Wing->WingModel.CD0 = 0.036f;
		Wing->WingModel.AR = 5.1f;
		Wing->WingModel.StallAoA = 15.0f;

		Wing->WingModel.TrimAoA = 2.0f;  // Default trim AoA		break;
	case 3:  // Sig 25 — 25m², max 130kg (standard)
		// Realistic EN-A beginner wing values:
		// CL0=0.50, CLMax=1.5, CD0=0.037, AR=5.0
		// At trim (10.5 m/s, 116kg): CL≈0.67, L/D≈8.2 with motor+lines+pilot drag
		Wing->WingModel.Name = TEXT("Sig 25");
		Wing->WingModel.WingArea = 25.0f;
		Wing->WingModel.MaxTakeoffWeight = 130.0f;
		Wing->WingModel.WingWeight = 6.0f;
		Wing->WingModel.CL0 = 0.50f;
		Wing->WingModel.CLMax = 1.5f;
		Wing->WingModel.CD0 = 0.037f;
		Wing->WingModel.AR = 5.0f;
		Wing->WingModel.StallAoA = 15.0f;

		Wing->WingModel.TrimAoA = 2.0f;  // Default trim AoA		break;
	case 4:  // Sig 30 — 30m², max 160kg (heavy pilot / tandem)
		Wing->WingModel.Name = TEXT("Sig 30");
		Wing->WingModel.WingArea = 30.0f;
		Wing->WingModel.MaxTakeoffWeight = 160.0f;
		Wing->WingModel.WingWeight = 7.0f;
		Wing->WingModel.CL0 = 0.52f;
		Wing->WingModel.CLMax = 1.55f;
		Wing->WingModel.CD0 = 0.038f;
		Wing->WingModel.AR = 4.5f;
		Wing->WingModel.StallAoA = 16.0f;

		Wing->WingModel.TrimAoA = 2.0f;  // Default trim AoA		break;
	case 5:  // Sig 35 — 35m², max 180kg (tandem / very heavy)
		Wing->WingModel.Name = TEXT("Sig 35");
		Wing->WingModel.WingArea = 35.0f;
		Wing->WingModel.MaxTakeoffWeight = 180.0f;
		Wing->WingModel.WingWeight = 8.0f;
		Wing->WingModel.CL0 = 0.55f;
		Wing->WingModel.CLMax = 1.6f;
		Wing->WingModel.CD0 = 0.040f;
		Wing->WingModel.AR = 4.2f;
		Wing->WingModel.StallAoA = 16.0f;

		Wing->WingModel.TrimAoA = 2.0f;  // Default trim AoA		break;
	case 6:  // MacPara Charger 25M — real reflex paramotor wing (DGAC)
		// Real specs from MacPara Charger 1 user manual (page 39):
		//   Zoom: 95.3% (of size 28 reference)
		//   Area flat: 25.43 m², Area projected: 22.83 m²
		//   Span flat: 11.44 m, Root chord: 2.71 m
		//   Aspect ratio flat: 5.14, Cells: 46
		//   Weight: 5.6 kg
		//   Weight range free flight: 75-95 kg
		//   Weight range PPG/MPG: 100-137 kg
		//   Min speed: 24-26 km/h, Max speed: 47-50 km/h
		//   Top speed (accelerator): 55-60 km/h
		//   Glide ratio: 8.9, Min sink rate: 1.1 m/s
		//   Reflex airfoil — resistant to collapse, stable at speed
		//   DGAC certified paramotor wing
		//   5 risers (A, A1, B, C, D) with trimmers + speed system
		//   Wing tip steering handles (WTST)
		Wing->WingModel.Name = TEXT("MacPara Charger 25M");
		Wing->WingModel.WingArea = 25.43f;
		Wing->WingModel.MaxTakeoffWeight = 137.0f;  // Real PPG max
		Wing->WingModel.WingWeight = 5.6f;
		Wing->WingModel.CL0 = 0.50f;     // Reflex airfoil — slightly lower CL0
		Wing->WingModel.CLMax = 1.45f;   // Reflex wings have lower CLMax (stable)
		Wing->WingModel.CD0 = 0.035f;    // Clean reflex profile
		Wing->WingModel.AR = 5.14f;      // Real Charger aspect ratio
		Wing->WingModel.StallAoA = 16.0f; // Reflex stalls later

		Wing->WingModel.TrimAoA = 1.5f;  // Reflex wing trims at lower AoA
		break;
	default:
		Wing->WingModel.Name = TEXT("Sig 25");
		Wing->WingModel.WingArea = 25.0f;
		Wing->WingModel.MaxTakeoffWeight = 130.0f;
		Wing->WingModel.WingWeight = 6.0f;
		Wing->WingModel.CL0 = 0.50f;
		Wing->WingModel.CLMax = 1.5f;
		Wing->WingModel.CD0 = 0.037f;
		Wing->WingModel.AR = 5.0f;
		Wing->WingModel.StallAoA = 15.0f;

		Wing->WingModel.TrimAoA = 2.0f;  // Default trim AoA		break;
	}
}

void AParamotorPlayerController::SpawnWing()
{
	SpawnWingByModel(CurrentWingModelIndex);
}

void AParamotorPlayerController::GetWingModelSpecs(int32 Index, FString& OutName, float& OutArea, float& OutMaxWeight) const
{
	switch (Index)
	{
	case 0:  OutName = TEXT("Sig 16"); OutArea = 16.0f; OutMaxWeight = 85.0f;  break;
	case 1:  OutName = TEXT("Sig 20"); OutArea = 20.0f; OutMaxWeight = 110.0f; break;
	case 2:  OutName = TEXT("Sig 22"); OutArea = 22.0f; OutMaxWeight = 120.0f; break;
	case 3:  OutName = TEXT("Sig 25"); OutArea = 25.0f; OutMaxWeight = 130.0f; break;
	case 4:  OutName = TEXT("Sig 30"); OutArea = 30.0f; OutMaxWeight = 160.0f; break;
	case 5:  OutName = TEXT("Sig 35"); OutArea = 35.0f; OutMaxWeight = 180.0f; break;
	default: OutName = TEXT("Sig 25"); OutArea = 25.0f; OutMaxWeight = 130.0f; break;
	}
}

void AParamotorPlayerController::SpawnWingByModel(int32 ModelIndex)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;

	// If wing already exists, destroy it first
	if (SpawnedWingActor)
	{
		SpawnedActors.Remove(SpawnedWingActor);
		SpawnedWingActor->Destroy();
		SpawnedWingActor = nullptr;
	}

	FVector Forward = MyPawn->GetActorForwardVector();
	FVector Backward = -Forward;

	float ChordCenter = 145.0f;
	// Lay out wing ~5m behind pilot (realistic layout distance)
	// Line length is 7m, so wing must be within line length when connected
	FVector Start = MyPawn->GetActorLocation() + Backward * (ChordCenter + 350.0f);

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(MyPawn);
	FVector TraceEnd = Start - FVector(0, 0, 500.0f);
	float SpawnZ = 0.0f;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, TraceEnd, ECC_Visibility, QueryParams))
	{
		SpawnZ = Hit.Location.Z + 5.0f;
	}
	FVector SpawnLoc = FVector(Start.X, Start.Y, SpawnZ);

	FRotator SpawnRot = FRotator(0.0f, MyPawn->GetActorRotation().Yaw + 180.0f, 0.0f);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedWingActor = GetWorld()->SpawnActor<AWingActor>(SpawnLoc, SpawnRot, SpawnParams);
	if (SpawnedWingActor)
	{
		// Apply selected wing model
		CurrentWingModelIndex = FMath::Clamp(ModelIndex, 0, FMath::Max(0, WingModelNames.Num() - 1));
		ApplyWingModel(SpawnedWingActor, CurrentWingModelIndex);

		SpawnedActors.Add(SpawnedWingActor);
		UE_LOG(LogTemp, Warning, TEXT("Spawned %s wing (%.1f m², max %.0f kg) at %s"),
			*SpawnedWingActor->WingModel.Name,
			SpawnedWingActor->WingModel.WingArea,
			SpawnedWingActor->WingModel.MaxTakeoffWeight,
			*SpawnLoc.ToString());
	}
}

void AParamotorPlayerController::ClearSpawnedActors()
{
	for (AActor* Actor : SpawnedActors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	SpawnedActors.Empty();
	SpawnedParamotorActor = nullptr;
	SpawnedWingActor = nullptr;
	UE_LOG(LogTemp, Warning, TEXT("Cleared all spawned actors"));
}
