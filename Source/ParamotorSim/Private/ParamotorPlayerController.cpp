// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorPlayerController.h"
#include "ParamotorLogging.h"
#include "UW_PauseMenu.h"
#include "UW_FlightHUD.h"
#include "ParamotorActor.h"
#include "WingActor.h"
#include "ChargerWingActor.h"
#include "ParamotorCharacter.h"
#include "CesiumWorldHelpers.h"
#include "ParamotorGameMode.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "AudioDevice.h"

AParamotorPlayerController::AParamotorPlayerController()
{
	bIsPaused = false;
	PauseMenuInstance = nullptr;
	FlightHUDInstance = nullptr;
	SpawnedParamotorActor = nullptr;
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
	// T = spawn MacPara Charger 25M test wing (the only wing), Y = spawn paramotor
	InputComponent->BindKey(EKeys::T, IE_Pressed, this, &AParamotorPlayerController::SpawnTestWing);
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

	// Middle mouse button = free cam (look around without moving character)
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &AParamotorPlayerController::OnFreeCamPressed);
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Released, this, &AParamotorPlayerController::OnFreeCamReleased);
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
		if (PauseMenuInstance)
		{
			TSharedPtr<SWidget> FocusWidget = PauseMenuInstance->TakeWidget();
			if (FocusWidget.IsValid())
			{
				InputMode.SetWidgetToFocus(FocusWidget);
			}
		}
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
		UE_LOG(LogParamotor, Log, TEXT("Too far from wing to grab (%.0f cm)"), DistToWing);
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
		UE_LOG(LogParamotor, Log, TEXT("Grabbed wing via crosshair at %s"), *GrabPos.ToString());
	}
	else
	{
		UE_LOG(LogParamotor, Log, TEXT("Crosshair not pointing at grabbable wing"));
	}
}

bool AParamotorPlayerController::GetCarabinerPositions(FVector& OutLeft, FVector& OutRight)
{
	if (SpawnedParamotorActor && SpawnedParamotorActor->GetCarabinerWorldPositions(OutLeft, OutRight))
	{
		return true;  // Real ring positions from the paramotor frame
	}
	// Fallback: estimate from character position (hip/harness height)
	APawn* MyPawn = GetPawn();
	AParamotorCharacter* Char = Cast<AParamotorCharacter>(MyPawn);
	if (!Char) return false;
	FVector CharPos = Char->GetActorLocation();
	FVector CharRight = Char->GetActorRightVector();
	float CarabHalfSpacing = CARABINER_SPACING_CM * 0.5f;
	float HarnessHeightCm = 95.0f;  // Hip/harness height (real carabiner position)
	OutLeft = CharPos + CharRight * -CarabHalfSpacing + FVector(0, 0, HarnessHeightCm);
	OutRight = CharPos + CharRight * CarabHalfSpacing + FVector(0, 0, HarnessHeightCm);
	return true;  // Estimated positions are valid for use
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
			UE_LOG(LogParamotor, Log, TEXT("Walk to the wing bag to unpack it (%.0f cm away)"), DistToWing);
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
			// Don't return — fall through to connect risers if wing is nearby
		}
		else
		{
			UE_LOG(LogParamotor, Log, TEXT("Get closer to the paramotor to equip it (%.0f cm away)"), DistToParamotor);
			return;
		}
	}

	// 4. Connect risers to spread wing (when paramotor is equipped)
	if (SpawnedParamotorActor && SpawnedParamotorActor->IsEquipped()
		&& SpawnedWingActor && SpawnedWingActor->IsSpread() && !SpawnedWingActor->IsConnected())
	{
		// The pilot clips the risers at the pile on the pilot side of the
		// layout — measure range to the riser pile, not the canopy (the
		// actor origin sits at the canopy ~7.5m out).
		const FVector PilePos = SpawnedWingActor->GetActorLocation()
			+ SpawnedWingActor->GetActorForwardVector() * 450.0f;
		float DistToWing = FVector::Dist2D(PlayerPos, PilePos);
		if (DistToWing < 600.0f)
		{
			// Get carabiner positions from the paramotor's wing clip rings
			FVector LeftCarabiner, RightCarabiner;
			GetCarabinerPositions(LeftCarabiner, RightCarabiner);

			SpawnedWingActor->ConnectRisers(SpawnedParamotorActor, LeftCarabiner, RightCarabiner);

			// Pilot weight: character mass + paramotor mass + fuel mass
			float CharMass = 80.0f;
			if (auto* MC = Char->GetCharacterMovement()) CharMass = FMath::Max(50.0f, MC->Mass);
			float ParamotorMass = (SpawnedParamotorActor) ? SpawnedParamotorActor->GetParamotorMassKg() : 0.0f;
			float FuelMass = 5.0f;
			SpawnedWingActor->SetPilotWeight(CharMass + ParamotorMass + FuelMass);

			UE_LOG(LogParamotor, Log, TEXT("Risers connected to paramotor (pilot weight: %.0f kg, max: %.0f kg)"),
				SpawnedWingActor->GetPilotWeight(), SpawnedWingActor->GetMaxTakeoffWeight());
		}
		else
		{
			UE_LOG(LogParamotor, Log, TEXT("Walk to the wing to connect risers (%.0f cm away)"), DistToWing);
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
	if (SpawnedWingActor)
	{
		// Move trimmer target toward full fast (+1.0)
		SpawnedWingActor->SetTrimmerTarget(SpawnedWingActor->GetTrimmerTargetPos() + 0.5f);
		UE_LOG(LogParamotor, Log, TEXT("Trimmer: FAST → %.2f (%s)"),
			SpawnedWingActor->GetTrimmerTargetPos(), *SpawnedWingActor->GetTrimmerLabel());
	}
}

void AParamotorPlayerController::OnTrimmerSlowPressed()
{
	if (SpawnedWingActor)
	{
		// Move trimmer target toward full slow (-1.0)
		SpawnedWingActor->SetTrimmerTarget(SpawnedWingActor->GetTrimmerTargetPos() - 0.5f);
		UE_LOG(LogParamotor, Log, TEXT("Trimmer: SLOW → %.2f (%s)"),
			SpawnedWingActor->GetTrimmerTargetPos(), *SpawnedWingActor->GetTrimmerLabel());
	}
}

void AParamotorPlayerController::OnTrimmerReleased()
{
}

void AParamotorPlayerController::OnLeftBrakePressed()
{
	bLeftBrakeHeld = true;
}

void AParamotorPlayerController::OnLeftBrakeReleased()
{
	bLeftBrakeHeld = false;
}

void AParamotorPlayerController::OnRightBrakePressed()
{
	bRightBrakeHeld = true;
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
		UE_LOG(LogParamotor, Log, TEXT("Seat toggled — seated: %s"), SpawnedWingActor->IsSeated() ? TEXT("YES") : TEXT("NO"));
	}
}

// [ = hang point back (motor leans back, pilot more upright)
void AParamotorPlayerController::OnHangPointBack()
{
	if (SpawnedWingActor && SpawnedWingActor->IsFlying())
	{
		SpawnedWingActor->AdjustHangPoint(-0.1f);
		UE_LOG(LogParamotor, Log, TEXT("Hang point back: %.2f"), SpawnedWingActor->GetHangPointOffset());
	}
}

// ] = hang point forward (motor leans forward, pilot more reclined)
void AParamotorPlayerController::OnHangPointForward()
{
	if (SpawnedWingActor && SpawnedWingActor->IsFlying())
	{
		SpawnedWingActor->AdjustHangPoint(0.1f);
		UE_LOG(LogParamotor, Log, TEXT("Hang point forward: %.2f"), SpawnedWingActor->GetHangPointOffset());
	}
}

void AParamotorPlayerController::OnFreeCamPressed()
{
	bFreeCamActive = true;
	// Disable character movement input while in free cam
	if (APawn* MyPawn = GetPawn())
	{
		MyPawn->DisableInput(this);
	}
	SetInputMode(FInputModeGameOnly());
	UE_LOG(LogParamotor, Log, TEXT("Free cam activated"));
}

void AParamotorPlayerController::OnFreeCamReleased()
{
	bFreeCamActive = false;
	// Re-enable character movement input
	if (APawn* MyPawn = GetPawn())
	{
		MyPawn->EnableInput(this);
	}
	SetInputMode(FInputModeGameOnly());
	UE_LOG(LogParamotor, Log, TEXT("Free cam deactivated"));
}

void AParamotorPlayerController::UpdateRotation(float DeltaTime)
{
	// During flight, the WingActor controls the pawn's full rotation (bank, pitch, yaw).
	// The default APlayerController::UpdateRotation calls Pawn->FaceRotation() which
	// forces the pawn to the control rotation (zero roll), locking the character level.
	// We override it to: save the WingActor's rotation, let the base class update
	// control rotation from mouse, then restore the pawn's rotation.

	bool bWingFlying = (SpawnedWingActor && SpawnedWingActor->IsFlying());

	if (bWingFlying)
	{
		if (APawn* FlightPawn = GetPawn())
		{
			// Save the pawn's current rotation (set by WingActor)
			FRotator SavedRot = FlightPawn->GetActorRotation();

			// Let the base class update control rotation from mouse input
			// It will also call FaceRotation, but with bUseControllerRotationYaw=false
			// (set during takeoff), FaceRotation won't override the pawn's rotation.
			Super::UpdateRotation(DeltaTime);

			// Force-restore the pawn's rotation in case anything touched it
			FlightPawn->SetActorRotation(SavedRot);
			return;
		}
	}

	// Free cam mode: update control rotation (camera look) but don't rotate the pawn
	if (bFreeCamActive)
	{
		if (APawn* FreeCamPawn = GetPawn())
		{
			FRotator SavedPawnRot = FreeCamPawn->GetActorRotation();
			Super::UpdateRotation(DeltaTime);  // Updates control rotation + camera
			FreeCamPawn->SetActorRotation(SavedPawnRot);  // But keep pawn facing same direction
		}
		return;
	}

	// Ground mode: normal controller rotation behavior
	Super::UpdateRotation(DeltaTime);
}

void AParamotorPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APawn* CachedPawn = GetPawn();

#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
	// Dev file-pipe — automation writes a console command to ps_cmd.txt and
	// we exec it here. Window focus isn't required (synthetic input can't
	// always reach the game window after repeated kills/relaunches).
	CmdPollT += DeltaTime;
	if (CmdPollT >= 0.5f)
	{
		CmdPollT = 0.0f;
		const TCHAR* CmdFile = TEXT("C:\\DevinTools\\ps_cmd.txt");
		FString Cmd;
		if (FFileHelper::LoadFileToString(Cmd, CmdFile))
		{
			Cmd.TrimStartAndEndInline();
			IFileManager::Get().Delete(CmdFile);
			if (!Cmd.IsEmpty())
			{
				UE_LOG(LogParamotor, Warning, TEXT("PsFileCmd: %s"), *Cmd);
				ConsoleCommand(Cmd);
			}
		}
	}
#endif

	// Geodetic teleport — one frame after the georeference rebase, move the
	// pawn to the destination and let the GameMode's settle loop land it.
	if (bPendingLLTeleport && CachedPawn)
	{
		LLTeleportTimer -= DeltaTime;
		if (LLTeleportTimer <= 0.0f)
		{
			bPendingLLTeleport = false;
			FVector Dest = CesiumWorld::GeodeticToUnreal(GetWorld(), PendingLon, PendingLat, PendingAltM);
			if (ACharacter* Ch = Cast<ACharacter>(CachedPawn))
			{
				if (UCharacterMovementComponent* Move = Ch->GetCharacterMovement())
				{
					Move->SetMovementMode(MOVE_Flying);
					Move->StopMovementImmediately();
					Move->GravityScale = 0.0f;
				}
			}
			CachedPawn->TeleportTo(Dest, FRotator(PendingPitch, PendingYaw, 0.0f));
			SetControlRotation(FRotator(PendingPitch, PendingYaw, 0.0f));
			if (AParamotorGameMode* GM = Cast<AParamotorGameMode>(GetWorld()->GetAuthGameMode()))
				GM->RequestGlobeSettle(Dest.Z);
			UE_LOG(LogParamotor, Log, TEXT("PsFlyToLL: teleported to %.0f %.0f %.0f — settling"), Dest.X, Dest.Y, Dest.Z);
		}
	}

	UpdateThrottle(DeltaTime);
	UpdateGrabPosition(CachedPawn);
	UpdateWingRig(DeltaTime, CachedPawn);
}

// ============================================================
// Tick helpers
// ============================================================
void AParamotorPlayerController::UpdateThrottle(float DeltaTime)
{
	if (!SpawnedParamotorActor) return;

	EEngineState State = SpawnedParamotorActor->GetEngineState();
	if (State == EEngineState::Running || State == EEngineState::Cranking)
	{
		float CurrentThrottle = SpawnedParamotorActor->GetThrottle();

		// Dev latch: PsEngine holds a fixed throttle for scripted testing.
		if (DevThrottleHold >= 0.0f)
		{
			SpawnedParamotorActor->SetThrottle(DevThrottleHold);
			return;
		}

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
	AParamotorCharacter* Char = Cast<AParamotorCharacter>(GetPawn());
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

void AParamotorPlayerController::UpdateGrabPosition(APawn* CachedPawn)
{
	if (!SpawnedWingActor || !SpawnedWingActor->IsGrabbing()) return;

	FVector CamLoc;
	FRotator CamRot;
	GetPlayerViewPoint(CamLoc, CamRot);
	// Grab point = where the crosshair points, but limited to player's reach
	FVector TraceEnd = CamLoc + CamRot.Vector() * 300.0f;  // 3m reach

	// If trace hits something, use that point
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	if (CachedPawn) QueryParams.AddIgnoredActor(CachedPawn);
	if (GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, TraceEnd, ECC_Visibility, QueryParams))
	{
		SpawnedWingActor->UpdateGrabPosition(Hit.Location);
	}
	else
	{
		SpawnedWingActor->UpdateGrabPosition(TraceEnd);
	}
}

void AParamotorPlayerController::ComputeBrakeArmTargets(AParamotorCharacter* Char, AWingActor* Wing,
	FVector& OutPilotLeftTarget, FVector& OutPilotRightTarget)
{
	// The wing's internal "left/right" labels are in wing-local space and may not
	// match the pilot's left/right due to the wing's yaw rotation.
	// Determine which handle is actually on the pilot's left vs right using world-space.
	FVector CharPos = Char->GetActorLocation();
	FVector WingLeftPos = Wing->GetLeftBrakeHandleWorldPos();
	FVector WingRightPos = Wing->GetRightBrakeHandleWorldPos();

	// Project both handle positions onto the pilot's right vector
	FVector PilotRight = Char->GetActorRightVector();
	FVector ToLeftHandle = WingLeftPos - CharPos;
	FVector ToRightHandle = WingRightPos - CharPos;
	float LeftDot = FVector::DotProduct(ToLeftHandle, PilotRight);
	float RightDot = FVector::DotProduct(ToRightHandle, PilotRight);

	// The handle with negative dot product is on the pilot's left,
	// the one with positive dot product is on the pilot's right.
	if (LeftDot <= RightDot)
	{
		OutPilotLeftTarget = WingLeftPos;   // Wing-left is pilot-left
		OutPilotRightTarget = WingRightPos; // Wing-right is pilot-right
	}
	else
	{
		OutPilotLeftTarget = WingRightPos;  // Wing-right is pilot-left (wing flipped)
		OutPilotRightTarget = WingLeftPos;  // Wing-left is pilot-right
	}
}

void AParamotorPlayerController::UpdateWingRig(float DeltaTime, APawn* CachedPawn)
{
	if (!SpawnedWingActor || !SpawnedWingActor->IsConnected() || !SpawnedParamotorActor)
	{
		// Clear arm IK when not connected
		AParamotorCharacter* Char = CachedPawn ? Cast<AParamotorCharacter>(CachedPawn) : nullptr;
		if (Char) Char->ClearBrakeArmTargets();
		return;
	}

	AParamotorCharacter* Char = Cast<AParamotorCharacter>(CachedPawn);
	if (!Char) return;

	// Update carabiner positions from paramotor hardware
	FVector LeftCarabiner, RightCarabiner;
	GetCarabinerPositions(LeftCarabiner, RightCarabiner);
	SpawnedWingActor->UpdateCarabinerPositions(LeftCarabiner, RightCarabiner);

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

	// Set weight shift active when flying (mouse look = weight shift, not yaw)
	Char->bWeightShiftActive = SpawnedWingActor->IsFlying();

	// Pass weight shift to wing for riser lowering + turn initiation
	SpawnedWingActor->SetWeightShift(Char->WeightShift);

	// Set character arm IK targets to brake handle positions
	FVector PilotLeftTarget, PilotRightTarget;
	ComputeBrakeArmTargets(Char, SpawnedWingActor, PilotLeftTarget, PilotRightTarget);
	Char->SetBrakeArmTargets(true, true, PilotLeftTarget, PilotRightTarget, bLeftBrakeHeld, bRightBrakeHeld);

	// Debug log for IK target verification
	IKLogTimer += DeltaTime;
	if (IKLogTimer > 1.0f)
	{
		IKLogTimer = 0.0f;
		FVector CharPos = Char->GetActorLocation();
		UE_LOG(LogParamotor, Log, TEXT("IK: CharPos=%s L_Target=%s R_Target=%s"),
			*CharPos.ToString(), *PilotLeftTarget.ToString(), *PilotRightTarget.ToString());
	}
}
