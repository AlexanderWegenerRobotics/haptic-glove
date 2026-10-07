using UnrealBuildTool;

public class HapticGlove : ModuleRules
{
	public HapticGlove(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "DeveloperSettings",
			"UMG", "Slate", "SlateCore", "HeadMountedDisplay", "EyeTracker",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Sockets", "Networking", "Json", "JsonUtilities", "RHI", "RenderCore",
			"AudioMixer", "AudioMixerCore", "AudioCaptureCore", "AudioCapture",
		});
	}
}
