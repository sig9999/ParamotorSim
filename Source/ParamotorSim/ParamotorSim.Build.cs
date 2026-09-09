// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ParamotorSim : ModuleRules
{
	public ParamotorSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Landscape", "ProceduralMeshComponent", "CableComponent", "AnimGraph", "AnimGraphRuntime", "AnimationCore", "Water" });

		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "HairStrandsCore" });

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.Add("UnrealEd");
		}
	}
}
