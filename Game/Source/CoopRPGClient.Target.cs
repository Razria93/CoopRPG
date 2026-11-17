using UnrealBuildTool;

public class CoopRPGClientTarget : TargetRules
{
    public CoopRPGClientTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Client;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("CoopRPG");
    }
}
