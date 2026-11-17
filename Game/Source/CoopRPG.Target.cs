using UnrealBuildTool;

public class CoopRPGTarget : TargetRules
{
    public CoopRPGTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("CoopRPG");
    }
}
