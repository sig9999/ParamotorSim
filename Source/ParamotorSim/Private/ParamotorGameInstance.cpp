// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorGameInstance.h"
#include "HAL/IConsoleManager.h"

void UParamotorGameInstance::Init()
{
	Super::Init();

	// Global high-def baseline — every screen gets supersampling and maxed
	// scalability. Maps can override (hangar pushes 150%, world map 120%).
	IConsoleManager& CM = IConsoleManager::Get();
	auto SetCVar = [&CM](const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* CVar = CM.FindConsoleVariable(Name))
		{
			CVar->Set(Value);
		}
	};
	SetCVar(TEXT("r.ScreenPercentage"), 125.0f);
	SetCVar(TEXT("sg.AntiAliasingQuality"), 4.0f);
	SetCVar(TEXT("sg.ShadowQuality"), 4.0f);
	SetCVar(TEXT("sg.TextureQuality"), 4.0f);
	SetCVar(TEXT("sg.ViewDistanceQuality"), 4.0f);
	SetCVar(TEXT("sg.EffectsQuality"), 4.0f);
	SetCVar(TEXT("sg.PostProcessQuality"), 4.0f);
	SetCVar(TEXT("sg.FoliageQuality"), 4.0f);
	SetCVar(TEXT("r.Shadow.MaxResolution"), 4096.0f);
	SetCVar(TEXT("r.ContactShadows"), 1.0f);
	SetCVar(TEXT("r.DetailMode"), 3.0f);
}

void UParamotorGameInstance::EnsureSpecData()
{
	if (EngineSpecs.Num() == 0)
	{
		FParamotorEngineSpec E;
		E.Brand = TEXT("EOS");
		E.Model = TEXT("EOS 100");
		E.Displacement_cc = 100.0f;
		E.Power_hp = 18.0f;
		E.Torque_Nm = 26.0f;
		E.Weight_kg = 14.2f;
		E.IdleRPM = 2600.0f;
		E.RedlineRPM = 9500.0f;
		E.HubSize = TEXT("6-hole 75mm");
		E.StaticThrustByProp = TEXT("130cm ~72kg");
		E.Notes = TEXT("Compact, light 2-stroke. Great for light pilots and the 130cm EOS prop.");
		EngineSpecs.Add(E);
	}

	if (FrameSpecs.Num() == 0)
	{
		FParamotorFrameSpec F;
		F.Brand = TEXT("Kangook");
		F.Model = TEXT("Amaruk 140");
		F.Weight_kg = 4.31f;
		F.EngineMount = TEXT("EOS 100");
		F.Notes = TEXT("Black powder-coated aluminum cage frame — 1400mm ring, orange safety netting, stainless hardware. Real mesh by Codex.");
		FrameSpecs.Add(F);
	}

	if (PropSpecs.Num() == 0)
	{
		FParamotorPropSpec P;
		P.Brand = TEXT("EOS");
		P.Model = TEXT("PT130 Carbon");
		P.Diameter = TEXT("130 cm");
		P.Pitch = TEXT("12.5 deg");
		P.Weight_kg = 0.95f;
		P.HubSize = TEXT("6-hole 75mm");
		P.StaticThrust = TEXT("~72 kg");
		P.Notes = TEXT("EOS P&T carbon 2-blade — real mesh by Codex, mounts to the EOS 100 prop socket.");
		PropSpecs.Add(P);
	}

	if (HarnessSpecs.Num() == 0)
	{
		FParamotorHarnessSpec H;
		H.Brand = TEXT("Dudek");
		H.Model = TEXT("PowerSeat DP");
		H.Weight_kg = 3.09f;
		H.Sizes = TEXT("S / M");
		H.Notes = TEXT("PowerSeat Comfort DP — low-hang seat harness, 390mm seat, real mesh by Codex.");
		HarnessSpecs.Add(H);
	}

	if (WingSpecs.Num() == 0)
	{
		FParamotorWingSpec W;
		W.Brand = TEXT("Macpara");
		W.Model = TEXT("Charger 25");
		W.Size = TEXT("25 m²");
		W.MaxWeightKg = 115;
		W.MaxWeightLb = 253;
		W.TrimSpeeds = TEXT("39 / 43 / 47 km/h");
		W.TopSpeed = TEXT("~53 km/h");
		W.Cells = 44;
		W.FlatArea_m2 = 25.26f;
		W.ProjectedArea_m2 = 21.4f;
		W.FlatSpan_m = 11.46f;
		W.ProjectedSpan_m = 9.02f;
		W.AspectRatioFlat = 5.2f;
		W.AspectRatioProjected = 3.8f;
		W.WingWeight_kg = 4.9f;
		W.TowRange = TEXT("85 – 115 kg");
		W.Certification = TEXT("EN-B / LTF-B");
		W.Risers = TEXT("4-line, split A/B/C/D — speed-bar compatible");
		W.Colors = TEXT("Lime / Blue / Orange (stock liveries)");
		W.ImagePath = TEXT("/Game/Textures/Wings/T_Wing_Charger25.T_Wing_Charger25");
		W.Notes = TEXT("Macpara's classic all-round powered-paraglider wing. Forgiving pitch behaviour, easy inflation, and a wide weight range make it the go-to school and club wing. Pairs well with light 100-140cc rigs.");
		// Aero coefficients — tuned values the ChargerWingActor flight model
		// was built around.
		W.CL0 = 0.50f; W.CLMax = 1.45f; W.CD0 = 0.035f;
		W.StallAoA = 16.0f; W.TrimAoA = 1.5f;
		WingSpecs.Add(W);
	}
}

void UParamotorGameInstance::EnsureSiteData() const
{
	if (LaunchSites.Num() > 0) return;

	// L_Westerly flat-map origin = UTM19N (269580, 4580015); a site's LocalSpawn
	// is its UTM easting/northing minus that origin, in cm.
	auto Add = [this](const TCHAR* Name, const TCHAR* ICAO, double Lat, double Lon,
	                  float ElevM, double UtmE, double UtmN)
	{
		FLaunchSite S;
		S.Name = Name; S.ICAO = ICAO; S.Lat = Lat; S.Lon = Lon; S.Elev_m = ElevM;
		S.MapName = TEXT("L_Westerly");
		S.LocalSpawn = FVector((UtmE - 269580.0) * 100.0, (UtmN - 4580015.0) * 100.0, 0.0);
		LaunchSites.Add(S);
	};

	Add(TEXT("Westerly State Airport"), TEXT("KWST"), 41.3483, -71.8000, 19.0f,
	    265748.0, 4581206.0);   // apron south of the runway intersection
	Add(TEXT("Ninigret Airstrip"),     TEXT("NINI"), 41.3650, -71.6600, 6.0f,
	    279150.0, 4583800.0);   // park aux-strip parking area — dressed field
	Add(TEXT("Misquamicut Beach"),     TEXT("MISQ"), 41.3255, -71.7970, 1.0f,
	    266013.0, 4578660.0);   // firm sand above the waterline (0.9m)
	// TEMP ocean verification site — ~350m offshore south of Misquamicut.
	Add(TEXT("Ocean Test"),            TEXT("OTST"), 41.3225, -71.7970, 0.0f,
	    266013.0, 4578310.0);
}

const FLaunchSite* UParamotorGameInstance::GetActiveSite() const
{
	EnsureSiteData();
	return LaunchSites.IsValidIndex(ActiveSiteIndex) ? &LaunchSites[ActiveSiteIndex] : nullptr;
}

const FLaunchSite* UParamotorGameInstance::FindSiteByICAO(const FString& ICAO) const
{
	EnsureSiteData();
	for (const FLaunchSite& S : LaunchSites)
	{
		if (S.ICAO.IsEmpty()) continue;
		// Match the airport catalog's local code too (WST == KWST).
		if (S.ICAO.Equals(ICAO, ESearchCase::IgnoreCase) ||
		    S.ICAO.Equals(FString(TEXT("K")) + ICAO, ESearchCase::IgnoreCase))
		{
			return &S;
		}
	}
	return nullptr;
}

bool UParamotorGameInstance::SelectSiteByICAO(const FString& ICAO)
{
	EnsureSiteData();
	for (int32 i = 0; i < LaunchSites.Num(); ++i)
	{
		if (&LaunchSites[i] == FindSiteByICAO(ICAO))
		{
			ActiveSiteIndex = i;
			return true;
		}
	}
	return false;
}
