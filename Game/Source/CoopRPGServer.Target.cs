using UnrealBuildTool;

public class CoopRPGServerTarget : TargetRules
{
    public CoopRPGServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("CoopRPG");
    }
}
