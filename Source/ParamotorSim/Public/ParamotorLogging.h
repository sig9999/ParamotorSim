// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Module-wide log category — replaces the LogTemp spam. In a Shipping build
// this still writes to the log file but stays out of the console.
DECLARE_LOG_CATEGORY_EXTERN(LogParamotor, Log, All);
