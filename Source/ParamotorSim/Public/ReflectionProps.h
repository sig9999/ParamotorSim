// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Generic UObject reflection setters — the project talks to plugin classes
// (CesiumRuntime) without linking against their headers, so properties get
// poked by name. Used by CesiumWorldHelpers and WorldMapGameMode.
namespace Refl
{
	/** Sets a named floating-point property (float or double). */
	inline void SetNumeric(UObject* O, const TCHAR* Name, double Val)
	{
		if (!O) return;
		FProperty* Prop = O->GetClass()->FindPropertyByName(FName(Name));
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		{
			DP->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), Val);
		}
		else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			FP->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), (float)Val);
		}
	}

	/** Sets a named numeric property — floats/doubles/ints/bools all accepted. */
	inline void SetNumberOrBool(UObject* O, const TCHAR* Name, double Val)
	{
		if (!O) return;
		FProperty* Prop = O->GetClass()->FindPropertyByName(FName(Name));
		if (!Prop) return;

		if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			if (NumProp->IsFloatingPoint())
			{
				NumProp->SetFloatingPointPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), Val);
			}
			else if (NumProp->IsInteger())
			{
				NumProp->SetIntPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), (int64)Val);
			}
		}
		else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			BoolProp->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), Val != 0.0);
		}
	}

	/** Sets a named integer property (any int width). */
	inline void SetInt(UObject* O, const TCHAR* Name, int64 Val)
	{
		if (!O) return;
		FProperty* Prop = O->GetClass()->FindPropertyByName(FName(Name));
		if (FInt64Property* I64 = CastField<FInt64Property>(Prop))
		{
			I64->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), Val);
		}
		else if (FIntProperty* I32 = CastField<FIntProperty>(Prop))
		{
			I32->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), (int32)Val);
		}
		else if (FUInt32Property* U32 = CastField<FUInt32Property>(Prop))
		{
			U32->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), (uint32)Val);
		}
		else if (FUInt64Property* U64 = CastField<FUInt64Property>(Prop))
		{
			U64->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), (uint64)Val);
		}
	}

	/** Sets a named bool property. */
	inline void SetBool(UObject* O, const TCHAR* Name, bool Val)
	{
		if (!O) return;
		FProperty* Prop = O->GetClass()->FindPropertyByName(FName(Name));
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			BP->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), Val);
		}
	}

	/** Sets a named FString property. */
	inline void SetString(UObject* O, const TCHAR* Name, const FString& Val)
	{
		if (!O) return;
		FProperty* Prop = O->GetClass()->FindPropertyByName(FName(Name));
		if (FStrProperty* SP = CastField<FStrProperty>(Prop))
		{
			SP->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), Val);
		}
	}

	/** Sets a named enum property by its numeric value. */
	inline void SetEnum(UObject* O, const TCHAR* Name, int64 Val)
	{
		if (!O) return;
		FProperty* Prop = O->GetClass()->FindPropertyByName(FName(Name));
		if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			EP->GetUnderlyingProperty()->SetIntPropertyValue(
				Prop->ContainerPtrToValuePtr<void>(O), Val);
		}
		else if (FByteProperty* BP = CastField<FByteProperty>(Prop))
		{
			BP->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(O), (uint8)Val);
		}
	}
}
