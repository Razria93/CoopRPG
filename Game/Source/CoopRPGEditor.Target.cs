using UnrealBuildTool;

public class CoopRPGEditorTarget : TargetRules
{
    public CoopRPGEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("CoopRPG");
    }
}
