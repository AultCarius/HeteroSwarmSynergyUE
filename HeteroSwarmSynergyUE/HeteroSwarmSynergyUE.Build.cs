// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class HeteroSwarmSynergyUE : ModuleRules
{
    public HeteroSwarmSynergyUE(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        string projectDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
        string[] mavlinkIncludeCandidates =
        {
            Path.Combine(projectDir, "Plugins", "MAVLink", "Source", "ThirdParty", "MAVLinkLibrary", "include"),
            Path.GetFullPath(Path.Combine(projectDir, "..", "HeteroSwarmSynergyUE", "Plugins", "MAVLink", "Source", "ThirdParty", "MAVLinkLibrary", "include"))
        };

        foreach (string candidate in mavlinkIncludeCandidates)
        {
            if (Directory.Exists(candidate))
            {
                PublicIncludePaths.Add(candidate);
                break;
            }
        }

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "RenderCore",
            "RHI",
            "Sockets",
            "Networking",
            "GPUPointCloudRenderer",
            "GPUPointCloudRendererEditor",
            "CustomMeshComponent"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Json",
            "JsonUtilities"
        });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
