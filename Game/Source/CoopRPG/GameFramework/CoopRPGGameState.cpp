#include "CoopRPGGameState.h"

#include "CoopRPGExtractionZone.h"
#include "CoopRPGExpeditionMaps.h"
#include "CoopRPGGameMode.h"
#include "CoopRPGImpactCameraShake.h"
#include "CoopRPGLootHUD.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "CoreGlobals.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Color.h"
#include "Math/Rotator.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRound, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGMeleeEffects, Log, All);

namespace
{
    constexpr double RoundDurationSeconds = 300.0;
    constexpr double ExtractionHoldSeconds = 3.0;
    constexpr TCHAR FirstValidationMap[] = TEXT("/Game/Maps/L_Movement");
    constexpr TCHAR SecondValidationMap[] = TEXT("/Game/Maps/L_Movement_Round2");

    enum class EBossImpactTrial
    {
        Default,
        Effects,
        HitStop,
        Shake
    };

    EBossImpactTrial GetBossImpactTrial()
    {
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
        static const EBossImpactTrial Mode = []
        {
            FString Value;
            if (FParse::Param(FCommandLine::Get(), TEXT("CoopCombatTest"))
                && FParse::Value(FCommandLine::Get(), TEXT("CoopBossImpact="), Value))
            {
                if (Value.Equals(TEXT("Effects"), ESearchCase::IgnoreCase)) { return EBossImpactTrial::Effects; }
                if (Value.Equals(TEXT("HitStop"), ESearchCase::IgnoreCase)) { return EBossImpactTrial::HitStop; }
                if (Value.Equals(TEXT("Shake"), ESearchCase::IgnoreCase)) { return EBossImpactTrial::Shake; }
            }
            return EBossImpactTrial::Default;
        }();
        return Mode;
#else
        return EBossImpactTrial::Default;
#endif
    }

    struct FMeleeEffectAssets
    {
        TSoftObjectPtr<UNiagaraSystem> System;
        TSoftObjectPtr<USoundBase> Sound;
        float Scale = 1.0f;
        float Volume = 1.0f;
    };

    FMeleeEffectAssets& GetMeleeEffectAssets(ECoopRPGMeleeEffect Effect)
    {
        static FMeleeEffectAssets Assets[] =
        {
            {},
            {
                TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/ThirdParty/Effects/Combat/MedievalRPGVFX/Niagara/Combat/Melee/NS_MRPG_Sword_Slash_Light.NS_MRPG_Sword_Slash_Light"))),
                TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/ThirdParty/Effects/Combat/WeaponMaster/cue/1_WM_Mono/Sword_Whoosh_02_Cue.Sword_Whoosh_02_Cue"))),
                0.57f, 0.37f
            },
            {
                TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/ThirdParty/Effects/Combat/MedievalRPGVFX/Niagara/Combat/Hits/NS_MRPG_Hit_Blood.NS_MRPG_Hit_Blood"))),
                TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/ThirdParty/Effects/Combat/WeaponMaster/cue/1_WM_Mono/Sword_Damage_02_Cue.Sword_Damage_02_Cue"))),
                0.78f, 0.68f
            },
            {
                TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/ThirdParty/Effects/Combat/MedievalRPGVFX/Niagara/Combat/Hits/NS_MRPG_Hit_Metal.NS_MRPG_Hit_Metal"))),
                TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/ThirdParty/Effects/Combat/WeaponMaster/cue/1_WM_Mono/Sword_Damage_02_Cue.Sword_Damage_02_Cue"))),
                0.90f, 0.68f
            },
            {
                TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/ThirdParty/Effects/Combat/MedievalRPGVFX/Niagara/Combat/Melee/NS_MRPG_Shield_Block.NS_MRPG_Shield_Block"))),
                TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/ThirdParty/Effects/Combat/WeaponMaster/cue/1_WM_Mono/Sword_Defense_Impact_02_Cue.Sword_Defense_Impact_02_Cue"))),
                0.52f, 0.76f
            }
        };
        return Assets[static_cast<uint8>(Effect)];
    }

    template <typename AssetType>
    AssetType* LoadLocalEffectAsset(TSoftObjectPtr<AssetType>& Asset)
    {
        if (Asset.IsValid())
        {
            return Asset.Get();
        }
        const FString Package = Asset.ToSoftObjectPath().GetLongPackageName();
        return FPackageName::DoesPackageExist(Package) ? Asset.LoadSynchronous() : nullptr;
    }

    bool PlayFallbackShieldImpact(UWorld& World, const FVector& Position)
    {
        constexpr uint32 SampleRate = 24000;
        constexpr float Duration = 0.10f;
        TArray<int16> Samples;
        Samples.SetNumUninitialized(static_cast<int32>(SampleRate * Duration));
        for (int32 Index = 0; Index < Samples.Num(); ++Index)
        {
            const double Time = static_cast<double>(Index) / SampleRate;
            const double Envelope = FMath::Min(Time * 600.0, 1.0) * FMath::Exp(-50.0 * Time);
            Samples[Index] = static_cast<int16>(7000.0 * Envelope
                * (0.7 * FMath::Sin(2.0 * UE_DOUBLE_PI * 890.0 * Time)
                    + 0.3 * FMath::Sin(2.0 * UE_DOUBLE_PI * 1430.0 * Time)));
        }

        auto* Sound = NewObject<USoundWaveProcedural>(&World);
        Sound->SetSampleRate(SampleRate);
        Sound->NumChannels = 1;
        Sound->Duration = Duration;
        Sound->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
        if (UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(&World, Sound, Position, FRotator::ZeroRotator, 0.65f))
        {
            Audio->FadeOut(Duration, 0.0f);
            return true;
        }
        return false;
    }

    bool IsSuccessful(ECoopRPGRoundEndReason Reason)
    {
        return Reason == ECoopRPGRoundEndReason::Extracted || Reason == ECoopRPGRoundEndReason::PuzzleSolved
            || Reason == ECoopRPGRoundEndReason::BossDefeated;
    }

    const TCHAR* GetResultEvent(ECoopRPGRoundEndReason Reason, bool bReplicated)
    {
        switch (Reason)
        {
        case ECoopRPGRoundEndReason::Extracted:
            return bReplicated ? TEXT("ExtractionResultReplicated") : TEXT("ExtractionResult");
        case ECoopRPGRoundEndReason::PuzzleSolved:
            return bReplicated ? TEXT("PuzzleResultReplicated") : TEXT("PuzzleResult");
        case ECoopRPGRoundEndReason::BossDefeated:
            return bReplicated ? TEXT("ExpeditionResultReplicated") : TEXT("ExpeditionResult");
        default:
            return bReplicated ? TEXT("RoundResultReplicated") : TEXT("RoundResult");
        }
    }

    const TCHAR* GetRoundOutcome(ECoopRPGRoundEndReason Reason)
    {
        if (IsSuccessful(Reason))
        {
            return TEXT("Success");
        }
        return Reason == ECoopRPGRoundEndReason::Disconnected ? TEXT("Aborted") : TEXT("Failed");
    }
}

void ACoopRPGGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGGameState, SharedChest);
    DOREPLIFETIME(ACoopRPGGameState, RoundId);
    DOREPLIFETIME(ACoopRPGGameState, RoundStatus);
    DOREPLIFETIME(ACoopRPGGameState, ReadySlots);
    DOREPLIFETIME(ACoopRPGGameState, LobbyDepartureDeadline);
    DOREPLIFETIME(ACoopRPGGameState, RoundStartDeadline);
    DOREPLIFETIME(ACoopRPGGameState, PuzzleRewardDeadline);
    DOREPLIFETIME(ACoopRPGGameState, SharedStorage);
    DOREPLIFETIME(ACoopRPGGameState, PuzzleStatus);
    DOREPLIFETIME(ACoopRPGGameState, ExtractionZone);
    DOREPLIFETIME(ACoopRPGGameState, EnemyPotion);
    DOREPLIFETIME(ACoopRPGGameState, EnemyLoot);
    DOREPLIFETIME(ACoopRPGGameState, TeamLootCount);
    DOREPLIFETIME(ACoopRPGGameState, SkirmishDefeats);
}

void ACoopRPGGameState::InitializeLobby()
{
    if (HasAuthority() && !RoundId.IsValid())
    {
        RoundId = FGuid::NewGuid();
        if (const auto* Storage = GetWorld()->GetGameInstance()->GetSubsystem<UCoopRPGLobbyStorage>())
        {
            SharedStorage = Storage->GetShared();
        }
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExpeditionLobbyCreated Round=%s"), *RoundId.ToString());
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::UpdateSharedStorage(const FCoopRPGStorageStock& Stock)
{
    if (HasAuthority())
    {
        SharedStorage = Stock;
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::InitializePuzzle(const FVector& PlateA, const FVector& PlateB)
{
    if (HasAuthority() && !RoundId.IsValid())
    {
        PuzzleStatus.PlateA = PlateA;
        PuzzleStatus.PlateB = PlateB;
        RoundId = FGuid::NewGuid();
        UE_LOG(LogCoopRPGRound, Log,
            TEXT("ExpeditionPuzzleCreated Round=%s PlateA=%s PlateB=%s Radius=%.0f TeamLoot=%d Score=%d"),
            *RoundId.ToString(), *PlateA.ToCompactString(), *PlateB.ToCompactString(),
            CoopRPG::Expedition::PuzzlePlateRadius, GetTeamLootCount(), GetTeamScore());
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::InitializeLootRound(ACoopRPGSharedChest* Chest, ACoopRPGExtractionZone* Zone)
{
    if (HasAuthority() && IsValid(Chest) && !RoundId.IsValid())
    {
        SharedChest = Chest;
        ExtractionZone = Zone;
        RoundId = FGuid::NewGuid();
        UE_LOG(LogCoopRPGRound, Log, TEXT("LootRoundCreated Round=%s Chest=%s Stock=%d Map=%s"),
            *RoundId.ToString(), *GetNameSafe(SharedChest), Chest->GetRecoveryStock(),
            *UGameplayStatics::GetCurrentLevelName(this));
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::SetRoundPlaying(bool bPlaying)
{
    if (HasAuthority() && RoundId.IsValid() && !HasRoundResult() && IsRoundPlaying() != bPlaying)
    {
        const double Now = GetServerWorldTimeSeconds();
        if (!bPlaying)
        {
            RoundStatus.ExtractionPlayersInside = 0;
            UpdateExtraction(false, Now);
        }
        const bool bTimedRound = IsValid(ExtractionZone) || CoopRPG::Expedition::IsBoss(GetWorld());
        RoundStatus.RoundDeadline = bPlaying && bTimedRound ? Now + RoundDurationSeconds : 0.0;
        if (RoundStatus.RoundDeadline > 0.0)
        {
            UE_LOG(LogCoopRPGRound, Log, TEXT("RoundClockStarted Round=%s Time=%.3f Deadline=%.3f Duration=%.0f"),
                *RoundId.ToString(), Now, RoundStatus.RoundDeadline, RoundDurationSeconds);
        }
        RoundStatus.Phase = bPlaying ? ECoopRPGRoundPhase::Playing : ECoopRPGRoundPhase::Waiting;
        UE_LOG(LogCoopRPGRound, Log, TEXT("LootRoundPhase Round=%s Phase=%s"),
            *RoundId.ToString(), bPlaying ? TEXT("Playing") : TEXT("Waiting"));
        ForceNetUpdate();
    }
}

bool ACoopRPGGameState::CanStartBossRewards() const
{
    return HasAuthority() && IsRoundPlaying() && !AreBossRewardsAvailable()
        && GetServerWorldTimeSeconds() < RoundStatus.RoundDeadline;
}

void ACoopRPGGameState::StartBossRewards(ACoopRPGSharedChest& Chest, ACoopRPGExtractionZone& Zone)
{
    check(CanStartBossRewards());
    SharedChest = &Chest;
    ExtractionZone = &Zone;
    RoundStatus.bBossRewardsAvailable = true;
    RoundStatus.RoundDeadline = 0.0;
    UE_LOG(LogCoopRPGRound, Log, TEXT("BossRewardsOpened Round=%s Chest=%s ChestLocation=%s Portal=%s PortalLocation=%s"),
        *RoundId.ToString(), *Chest.GetName(), *Chest.GetActorLocation().ToCompactString(),
        *Zone.GetName(), *Zone.GetActorLocation().ToCompactString());
    OnRep_ExtractionZone();
    ForceNetUpdate();
}

void ACoopRPGGameState::MulticastGameplayFeedback_Implementation(const FCoopRPGGameplayFeedback& Feedback)
{
    PresentGameplayFeedback(Feedback);
}

void ACoopRPGGameState::MulticastSlashFeedback_Implementation(const TArray<FCoopRPGGameplayFeedback>& Events)
{
    for (const FCoopRPGGameplayFeedback& Feedback : Events)
    {
        PresentGameplayFeedback(Feedback);
    }
}

void ACoopRPGGameState::PlayMeleeEffect(ECoopRPGMeleeEffect Effect, const FVector& Location,
    float Yaw, int32 Slot, uint32 Swing, float ScaleMultiplier)
{
    if (Effect == ECoopRPGMeleeEffect::None || GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    FMeleeEffectAssets& Assets = GetMeleeEffectAssets(Effect);
    const FRotator Rotation(0.0, Yaw, 0.0);
    const bool bArmorHit = Effect == ECoopRPGMeleeEffect::ArmorHit;
    FVector VisualLocation = Location;
    if (bArmorHit)
    {
        // Keep the spark just outside the body at the sweep contact.
        VisualLocation -= Rotation.Vector() * 20.0;
        VisualLocation.Z += 30.0;
    }

    UNiagaraComponent* Visual = nullptr;
    if (auto* System = LoadLocalEffectAsset(Assets.System))
    {
        Visual = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, VisualLocation, Rotation,
            FVector::OneVector, true, false);
    }
    if (Visual)
    {
        Visual->SetVariableFloat(TEXT("User.Scale"), Assets.Scale * ScaleMultiplier);
        if (Effect == ECoopRPGMeleeEffect::MushroomHit)
        {
            Visual->SetVariableLinearColor(TEXT("User.PrimaryColor"), FLinearColor(1.65f, 0.86f, 0.30f));
            Visual->SetVariableInt(TEXT("User.DropCount"), 9);
            Visual->SetVariableInt(TEXT("User.SplatCount"), 1);
        }
        else if (bArmorHit)
        {
            Visual->SetVariableFloat(TEXT("User.Intensity"), 1.5f);
        }
        else
        {
            Visual->SetVariableFloat(TEXT("User.Intensity"), 0.65f);
        }
    }

    UNiagaraComponent* ImpactFlash = nullptr;
    if (bArmorHit || Effect == ECoopRPGMeleeEffect::MushroomHit)
    {
        // Give each damaging hit a short flash distinct from the sword trail and shield ring.
        auto& FlashAssets = GetMeleeEffectAssets(ECoopRPGMeleeEffect::ArmorHit);
        if (auto* System = LoadLocalEffectAsset(FlashAssets.System))
        {
            const FVector FlashLocation = bArmorHit ? VisualLocation + FVector(0.0, 0.0, 28.0)
                : Location - Rotation.Vector() * 10.0 + FVector(0.0, 0.0, 34.0);
            ImpactFlash = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, FlashLocation, Rotation,
                FVector::OneVector, true, false);
            if (ImpactFlash)
            {
                for (const FNiagaraEmitterHandle& Emitter : System->GetEmitterHandles())
                {
                    const FName EmitterName = Emitter.GetName();
                    ImpactFlash->SetEmitterEnable(EmitterName, EmitterName == TEXT("Flash") || EmitterName == TEXT("Glow"));
                }
                ImpactFlash->SetVariableFloat(TEXT("User.Scale"), bArmorHit ? 1.65f : 1.38f);
                ImpactFlash->SetVariableFloat(TEXT("User.Intensity"), bArmorHit ? 2.25f : 2.15f);
                ImpactFlash->SetVariableLinearColor(TEXT("User.PrimaryColor"),
                    bArmorHit ? FLinearColor(1.85f, 1.36f, 0.60f) : FLinearColor(1.85f, 0.30f, 0.045f));
                ImpactFlash->SetCustomTimeDilation(0.68f);
                ImpactFlash->Activate(true);
            }
        }
    }

    if (Visual)
    {
        if (bArmorHit && ImpactFlash)
        {
            Visual->SetEmitterEnable(TEXT("Flash"), false);
            Visual->SetEmitterEnable(TEXT("Glow"), false);
        }
        Visual->Activate(true);
    }

    auto* Sound = LoadLocalEffectAsset(Assets.Sound);
    bool bFallbackSoundPlayed = false;
    if (Sound)
    {
        if (!MeleeSoundAttenuation)
        {
            MeleeSoundAttenuation = NewObject<USoundAttenuation>(this);
            MeleeSoundAttenuation->Attenuation.bSpatialize = true;
            MeleeSoundAttenuation->Attenuation.AttenuationShapeExtents = FVector(900.0, 0.0, 0.0);
            MeleeSoundAttenuation->Attenuation.FalloffDistance = 1500.0f;
        }
        UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, Rotation, Assets.Volume, 1.0f, 0.0f,
            MeleeSoundAttenuation);
    }
    else if (Effect == ECoopRPGMeleeEffect::ShieldBlock)
    {
        bFallbackSoundPlayed = PlayFallbackShieldImpact(*GetWorld(), Location);
    }

    UE_LOG(LogCoopRPGMeleeEffects, Log,
        TEXT("MeleeEffect Kind=%s Slot=%d Swing=%u VFX=%d SFX=%d Location=%s VisualLocation=%s Flash=%d FallbackSFX=%d"),
        *UEnum::GetValueAsString(Effect), Slot, Swing, Visual != nullptr, Sound != nullptr,
        *Location.ToCompactString(), *VisualLocation.ToCompactString(), ImpactFlash != nullptr, bFallbackSoundPlayed);
}

void ACoopRPGGameState::MulticastMeleeSweepTrace_Implementation(const FCoopRPGMeleeSweepTrace& Trace)
{
#if ENABLE_DRAW_DEBUG && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (GetNetMode() == NM_DedicatedServer || !FParse::Param(FCommandLine::Get(), TEXT("CoopMeleeSweepDebug")))
    {
        return;
    }

    const FVector Delta = Trace.End - Trace.Start;
    const double HalfLength = Delta.Size() * 0.5;
    const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Delta.GetSafeNormal());
    const bool bHit = Trace.Result == ECoopRPGMeleeSweepResult::Hit;
    const bool bObstructed = Trace.Result == ECoopRPGMeleeSweepResult::Obstructed;
    const FColor Color = bHit ? FColor::Cyan : bObstructed ? FColor::Orange : FColor::Red;
    constexpr float Lifetime = 1.25f;
    DrawDebugCapsule(GetWorld(), Trace.Start + Delta * 0.5, HalfLength + Trace.Radius, Trace.Radius,
        Rotation, Color, false, Lifetime, SDPG_Foreground, 1.4f);
    DrawDebugDirectionalArrow(GetWorld(), Trace.Start, Trace.End, 18.0f, Color, false, Lifetime, SDPG_Foreground, 2.0f);
    if (bHit || bObstructed)
    {
        // Time is the sweep fraction at the winning contact. The marker is the sphere center at that fraction.
        DrawDebugPoint(GetWorld(), FMath::Lerp(Trace.Start, Trace.End, Trace.ContactTime), 10.0f,
            Color, false, Lifetime, SDPG_Foreground);
    }
    UE_LOG(LogCoopRPGRound, Log, TEXT("MeleeSweepDrawn Slot=%d Swing=%u Start=%s End=%s Radius=%.2f Result=%s Contact=%.4f"),
        Trace.Slot, Trace.Swing, *Trace.Start.ToCompactString(), *Trace.End.ToCompactString(), Trace.Radius,
        *StaticEnum<ECoopRPGMeleeSweepResult>()->GetNameStringByValue(static_cast<int64>(Trace.Result)), Trace.ContactTime);
#endif
}

void ACoopRPGGameState::SetReadySlots(const FCoopRPGReadySlots& Slots)
{
    if (HasAuthority() && (Slots.PlayerOne != ReadySlots.PlayerOne || Slots.PlayerTwo != ReadySlots.PlayerTwo))
    {
        ReadySlots = Slots;
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::SetLobbyDepartureDeadline(double Deadline)
{
    if (HasAuthority() && LobbyDepartureDeadline != Deadline)
    {
        LobbyDepartureDeadline = Deadline;
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::SetRoundStartDeadline(double Deadline)
{
    if (HasAuthority() && RoundStartDeadline != Deadline)
    {
        RoundStartDeadline = Deadline;
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::SetPuzzleRewardDeadline(double Deadline)
{
    if (HasAuthority())
    {
        PuzzleRewardDeadline = Deadline;
        ForceNetUpdate();
    }
}

double ACoopRPGGameState::GetLobbyDepartureRemainingSeconds() const
{
    return FMath::Max(0.0, LobbyDepartureDeadline - GetServerWorldTimeSeconds());
}

double ACoopRPGGameState::GetRoundStartRemainingSeconds() const
{
    return FMath::Max(0.0, RoundStartDeadline - GetServerWorldTimeSeconds());
}

int32 ACoopRPGGameState::RegisterSkirmishDefeat(ACoopRPGEnemy* Defeated)
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ACoopRPGGameMode>();
    if (!HasAuthority() || !IsRoundPlaying() || !IsValid(Defeated) || !Defeated->IsDead()
        || !Mode || !Mode->IsSkirmishEnemy(Defeated))
    {
        return 0;
    }

    bool bAlreadyCounted = false;
    DefeatedSkirmishEnemies.Add(TWeakObjectPtr<ACoopRPGEnemy>(Defeated), &bAlreadyCounted);
    if (bAlreadyCounted)
    {
        return 0;
    }

    SkirmishDefeats = DefeatedSkirmishEnemies.Num();
    UE_LOG(LogCoopRPGRound, Log, TEXT("SkirmishDefeat Round=%s Enemy=%s Defeated=%d Total=%d"),
        *RoundId.ToString(), *Defeated->GetName(), SkirmishDefeats, CoopRPG::Expedition::SkirmishEnemyCount);
    ForceNetUpdate();
    return SkirmishDefeats;
}

bool ACoopRPGGameState::RegisterEnemyLoot(ACoopRPGEnemyLoot* Drop)
{
    if (!HasAuthority() || !RoundId.IsValid() || !IsRoundPlaying() || !IsValid(Drop) || !Drop->GetDropId().IsValid())
    {
        return false;
    }

    auto& Slot = Drop->IsPotion() ? EnemyPotion : EnemyLoot;
    if (Slot)
    {
        return false;
    }
    Slot = Drop;
    ForceNetUpdate();
    return true;
}

bool ACoopRPGGameState::TryGrantEnemyLoot(ACoopRPGEnemyLoot* Drop)
{
    if (!HasAuthority() || !IsRoundPlaying() || !IsValid(Drop) || EnemyLoot != Drop || !Drop->TryClaim())
    {
        return false;
    }

    ++TeamLootCount;
    OnRep_ExtractionZone();
    ForceNetUpdate();
    return true;
}

void ACoopRPGGameState::EvaluateRound(bool bDisconnected, bool bDeath, int32 PlayersInside)
{
    if (!HasAuthority() || !IsRoundPlaying())
    {
        return;
    }

    const double Now = GetServerWorldTimeSeconds();
    const bool bTimeout = RoundStatus.RoundDeadline > 0.0 && Now >= RoundStatus.RoundDeadline;
    const bool bBothInside = PlayersInside == RoundCapacity;
    const bool bExtractionReady = bBothInside && IsExtractionInProgress() && Now >= RoundStatus.ExtractionDeadline;
    ECoopRPGRoundEndReason Reason = ECoopRPGRoundEndReason::None;
    if (bDisconnected)
    {
        Reason = ECoopRPGRoundEndReason::Disconnected;
    }
    else if (bDeath)
    {
        Reason = ECoopRPGRoundEndReason::Death;
    }
    else if (bTimeout)
    {
        Reason = ECoopRPGRoundEndReason::Timeout;
    }
    else if (bExtractionReady)
    {
        Reason = AreBossRewardsAvailable() ? ECoopRPGRoundEndReason::BossDefeated : ECoopRPGRoundEndReason::Extracted;
    }
    else
    {
        const int32 PreviousPlayersInside = RoundStatus.ExtractionPlayersInside;
        RoundStatus.ExtractionPlayersInside = PlayersInside;
        if (PreviousPlayersInside != PlayersInside)
        {
            UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionOccupancy Round=%s Before=%d Inside=%d"),
                *RoundId.ToString(), PreviousPlayersInside, PlayersInside);
            ForceNetUpdate();
        }
        UpdateExtraction(bBothInside, Now);
        return;
    }

    FinishRound(Reason, Now, bDisconnected, bDeath, bTimeout, bExtractionReady);
}

void ACoopRPGGameState::EvaluatePuzzle(bool bDisconnected, bool bDeath, uint8 OccupantsA, uint8 OccupantsB)
{
    if (!HasAuthority() || !IsRoundPlaying())
    {
        return;
    }

    if (PuzzleStatus.OccupantsA != OccupantsA || PuzzleStatus.OccupantsB != OccupantsB)
    {
        PuzzleStatus.OccupantsA = OccupantsA;
        PuzzleStatus.OccupantsB = OccupantsB;
        UpdatePuzzlePlatePresentation();
        UE_LOG(LogCoopRPGRound, Log, TEXT("PuzzleOccupancy Round=%s A=%u B=%u"),
            *RoundId.ToString(), OccupantsA, OccupantsB);
        ForceNetUpdate();
    }

    const bool bDifferentPlayers = (OccupantsA == 1 && OccupantsB == 2)
        || (OccupantsA == 2 && OccupantsB == 1);
    if (bDisconnected || bDeath)
    {
        EvaluateRound(bDisconnected, bDeath, 0);
    }
    else if (bDifferentPlayers)
    {
        FinishRound(ECoopRPGRoundEndReason::PuzzleSolved, GetServerWorldTimeSeconds(), false, false, false, false);
    }
}

double ACoopRPGGameState::GetRoundRemainingSeconds() const
{
    return FMath::Max(0.0, RoundStatus.RoundDeadline - GetServerWorldTimeSeconds());
}

double ACoopRPGGameState::GetExtractionRemainingSeconds() const
{
    return FMath::Max(0.0, RoundStatus.ExtractionDeadline - GetServerWorldTimeSeconds());
}

FString ACoopRPGGameState::GetRoundTravelDestination() const
{
    const FString CurrentMap = UWorld::RemovePIEPrefix(GetWorld()->GetPackage()->GetName());
    switch (GetRoundEndReason())
    {
    case ECoopRPGRoundEndReason::Death:
    case ECoopRPGRoundEndReason::Timeout:
        return CurrentMap;
    case ECoopRPGRoundEndReason::PuzzleSolved:
        return CurrentMap == CoopRPG::Expedition::PuzzlePackage ? FString(CoopRPG::Expedition::BossPackage) : FString();
    case ECoopRPGRoundEndReason::BossDefeated:
        return CurrentMap == CoopRPG::Expedition::BossPackage ? FString(CoopRPG::Expedition::LobbyPackage) : FString();
    case ECoopRPGRoundEndReason::Extracted:
        if (CurrentMap == CoopRPG::Expedition::SkirmishPackage)
        {
            return CoopRPG::Expedition::PuzzlePackage;
        }
        if (CurrentMap == FirstValidationMap)
        {
            return SecondValidationMap;
        }
        if (CurrentMap == SecondValidationMap)
        {
            return FirstValidationMap;
        }
        return {};
    default:
        return {};
    }
}

void ACoopRPGGameState::PresentGameplayFeedback(const FCoopRPGGameplayFeedback& Feedback)
{
    if (GetNetMode() != NM_DedicatedServer)
    {
        const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
        if (auto* HUD = Controller ? Cast<ACoopRPGLootHUD>(Controller->GetHUD()) : nullptr)
        {
            HUD->ShowGameplayFeedback(Feedback);
        }
        FVector EffectLocation = Feedback.MeleeEffectLocation;
        if (Feedback.MeleeEffect == ECoopRPGMeleeEffect::ShieldBlock)
        {
            if (const auto* Defender = Cast<ACoopRPGCharacter>(Feedback.Subject.Get()))
            {
                EffectLocation = Defender->GetShieldImpactLocation();
            }
        }
        PlayMeleeEffect(Feedback.MeleeEffect, EffectLocation, Feedback.MeleeEffectYaw,
            Feedback.AttackerSlot, Feedback.MeleeSwing);
        if (Feedback.Kind == ECoopRPGFeedbackKind::Damage
            && (Feedback.MeleeEffect == ECoopRPGMeleeEffect::MushroomHit
                || Feedback.MeleeEffect == ECoopRPGMeleeEffect::ArmorHit))
        {
            if (Feedback.bPreserveEnemyAttackPose)
            {
                UE_LOG(LogCoopRPGMeleeEffects, Log,
                    TEXT("EnemyMeleeReactionSuppressed Slot=%d Swing=%u Target=%s"),
                    Feedback.AttackerSlot, Feedback.MeleeSwing, *GetNameSafe(Feedback.Subject.Get()));
            }
            auto* Hero = Controller ? Cast<ACoopRPGCharacter>(Controller->GetPawn()) : nullptr;
            if (Feedback.bTriggerLocalMeleeImpact && Hero && Controller->IsLocalController()
                && Hero->GetDisplaySlot() == Feedback.AttackerSlot)
            {
                auto* Enemy = Cast<ACoopRPGEnemy>(Feedback.Subject.Get());
                const EBossImpactTrial Mode = Enemy && Enemy->IsBlackKnight() ? GetBossImpactTrial() : EBossImpactTrial::Default;
                if (Mode == EBossImpactTrial::Default || Mode == EBossImpactTrial::Shake)
                {
                    auto* Camera = Controller->PlayerCameraManager.Get();
                    if (Camera && Camera->StartCameraShake(UCoopRPGImpactCameraShake::StaticClass()))
                    {
                        UE_LOG(LogCoopRPGMeleeEffects, Log, TEXT("MeleeCameraShakeStarted Slot=%d Swing=%u"),
                            Feedback.AttackerSlot, Feedback.MeleeSwing);
                    }
                }
                if (Mode == EBossImpactTrial::Default || Mode == EBossImpactTrial::HitStop)
                {
                    Hero->RequestMeleeHitStop(Feedback.MeleeSwing);
                }
            }
        }
    }
}

void ACoopRPGGameState::FinishRound(ECoopRPGRoundEndReason Reason, double Now, bool bDisconnected,
    bool bDeath, bool bTimeout, bool bExtractionReady)
{
    const double HeldSeconds = IsExtractionInProgress()
        ? Now - (RoundStatus.ExtractionDeadline - ExtractionHoldSeconds) : 0.0;
    RoundStatus.Phase = ECoopRPGRoundPhase::Result;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Player = It->Get();
        if (auto* Hero = Player ? Cast<ACoopRPGCharacter>(Player->GetPawn()) : nullptr)
        {
            Hero->ClearGuard();
        }
    }
    RoundStatus.EndReason = Reason;
    RoundStatus.TeamScore = IsSuccessful(Reason) ? 1 : 0;
    RoundStatus.ExtractionDeadline = 0.0;
    RoundStatus.ExtractionPlayersInside = 0;
    RoundStatus.RoundDeadline = 0.0;
    const FString ReasonName = StaticEnum<ECoopRPGRoundEndReason>()->GetNameStringByValue(static_cast<int64>(Reason));
    UE_LOG(LogCoopRPGRound, Log,
        TEXT("%s Round=%s Outcome=%s Score=%d Time=%.3f Held=%.3f Reason=%s Frame=%llu Disconnect=%d Death=%d Timeout=%d Extraction=%d"),
        GetResultEvent(Reason, false), *RoundId.ToString(), GetRoundOutcome(Reason),
        RoundStatus.TeamScore, Now, HeldSeconds, *ReasonName, static_cast<unsigned long long>(GFrameCounter),
        bDisconnected, bDeath, bTimeout, bExtractionReady);
    ForceNetUpdate();
}

void ACoopRPGGameState::UpdateExtraction(bool bEligible, double Now)
{
    double& Deadline = RoundStatus.ExtractionDeadline;
    if (!bEligible)
    {
        if (IsExtractionInProgress())
        {
            UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionReset Round=%s Time=%.3f Elapsed=%.3f"),
                *RoundId.ToString(), Now, Now - (Deadline - ExtractionHoldSeconds));
            Deadline = 0.0;
            ForceNetUpdate();
        }
        return;
    }

    if (!IsExtractionInProgress())
    {
        Deadline = Now + ExtractionHoldSeconds;
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionStarted Round=%s Time=%.3f Deadline=%.3f"),
            *RoundId.ToString(), Now, Deadline);
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::OnRep_ExtractionZone()
{
    if (IsValid(ExtractionZone))
    {
        const bool bActive = IsExtractionUnlocked();
        if (bActive)
        {
            ExtractionZone->OpenPortal();
        }
        ExtractionZone->PresentExpeditionPortal(bActive, bActive && IsRoundPlaying() && IsExtractionInProgress());
    }
}

void ACoopRPGGameState::OnRep_TeamLootCount(int32 PreviousCount)
{
    OnRep_ExtractionZone();
    UE_LOG(LogCoopRPGRound, Log, TEXT("TeamLootCountReplicated Before=%d After=%d %s"),
        PreviousCount, TeamLootCount, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGGameState::OnRep_SkirmishDefeats()
{
    UE_LOG(LogCoopRPGRound, Log, TEXT("SkirmishDefeatsReplicated Round=%s Defeated=%d Total=%d"),
        *RoundId.ToString(), SkirmishDefeats, CoopRPG::Expedition::SkirmishEnemyCount);
}

void ACoopRPGGameState::OnRep_RoundStatus(const FCoopRPGRoundStatus& PreviousStatus)
{
    OnRep_ExtractionZone();
    if (AreBossRewardsAvailable() && !PreviousStatus.bBossRewardsAvailable)
    {
        UE_LOG(LogCoopRPGRound, Log, TEXT("BossRewardsReplicated Round=%s"), *RoundId.ToString());
    }
    const FString Context = CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__));
    if (HasRoundResult() && PreviousStatus.Phase != ECoopRPGRoundPhase::Result)
    {
        const ECoopRPGRoundEndReason Reason = GetRoundEndReason();
        const FString ReasonName = StaticEnum<ECoopRPGRoundEndReason>()->GetNameStringByValue(static_cast<int64>(Reason));
        UE_LOG(LogCoopRPGRound, Log, TEXT("%s Round=%s Outcome=%s Score=%d Reason=%s %s"),
            GetResultEvent(Reason, true), *RoundId.ToString(), GetRoundOutcome(Reason), GetTeamScore(), *ReasonName, *Context);
    }
    else if (PreviousStatus.ExtractionPlayersInside != RoundStatus.ExtractionPlayersInside)
    {
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionOccupancyReplicated Round=%s Before=%d Inside=%d %s"),
            *RoundId.ToString(), PreviousStatus.ExtractionPlayersInside, RoundStatus.ExtractionPlayersInside, *Context);
    }
    if (!HasRoundResult() && PreviousStatus.ExtractionDeadline != RoundStatus.ExtractionDeadline)
    {
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionProgressReplicated Round=%s Phase=%s Deadline=%.3f %s"),
            *RoundId.ToString(), IsExtractionInProgress() ? TEXT("Started") : TEXT("Reset"),
            RoundStatus.ExtractionDeadline, *Context);
    }
}

void ACoopRPGGameState::OnRep_PuzzleStatus()
{
    UE_LOG(LogCoopRPGRound, Log, TEXT("PuzzleOccupancyReplicated Round=%s A=%u B=%u"),
        *RoundId.ToString(), PuzzleStatus.OccupantsA, PuzzleStatus.OccupantsB);
    UpdatePuzzlePlatePresentation();
}

void ACoopRPGGameState::UpdatePuzzlePlatePresentation()
{
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    if (!PuzzlePlateA && PuzzleStatus.OccupantsA)
    {
        PuzzlePlateA = CreatePuzzlePlate(TEXT("PuzzlePlateA"));
    }
    if (!PuzzlePlateB && PuzzleStatus.OccupantsB)
    {
        PuzzlePlateB = CreatePuzzlePlate(TEXT("PuzzlePlateB"));
    }
    UpdatePuzzlePlate(PuzzlePlateA, PuzzleStatus.OccupantsA);
    UpdatePuzzlePlate(PuzzlePlateB, PuzzleStatus.OccupantsB);
}

void ACoopRPGGameState::UpdatePuzzlePlate(UDynamicMeshComponent* Plate, uint8 Occupants) const
{
    if (!Plate)
    {
        return;
    }

    Plate->SetVisibility(Occupants != 0);
    if (Occupants)
    {
        const bool bP1 = (Occupants & 1) != 0;
        const bool bP2 = (Occupants & 2) != 0;
        CastChecked<UMaterialInstanceDynamic>(Plate->GetMaterial(0))->SetVectorParameterValue(
            TEXT("Color"), CoopRPG::Presentation::PlayerColor(bP1 ? 1 : 2));
        CastChecked<UMaterialInstanceDynamic>(Plate->GetMaterial(1))->SetVectorParameterValue(
            TEXT("Color"), CoopRPG::Presentation::PlayerColor(bP2 ? 2 : 1));
    }
}

UDynamicMeshComponent* ACoopRPGGameState::CreatePuzzlePlate(FName PlateTag)
{
    TArray<AActor*> Plates;
    UGameplayStatics::GetAllActorsWithTag(this, PlateTag, Plates);
    if (Plates.Num() != 1 || !Plates[0]->GetRootComponent())
    {
        return nullptr;
    }
    AActor* PlateActor = Plates[0];
    FVector PlateOrigin, Extent;
    PlateActor->GetActorBounds(false, PlateOrigin, Extent);

    // Reuse the unlit, tintable floor marker material on top of the existing gray plate.
    auto* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Presentation/Materials/M_EnemyGroundRing.M_EnemyGroundRing"));
    if (!Material)
    {
        UE_LOG(LogCoopRPGRound, Warning, TEXT("Puzzle plate could not load the tint material"));
        return nullptr;
    }

    using namespace UE::Geometry;
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    Mesh.Attributes()->EnableMaterialID();
    Mesh.EnableVertexNormals(FVector3f(0.0f, 0.0f, 1.0f));

    constexpr int32 SegmentsPerHalf = 24;
    constexpr double Radius = CoopRPG::Expedition::PuzzlePlateRadius - 3.0;
    const int32 Origin = Mesh.AppendVertex(FVector::ZeroVector);
    int32 Previous = Mesh.AppendVertex(FVector(Radius, 0.0, 0.0));
    const int32 Start = Previous;
    for (int32 Index = 1; Index <= 2 * SegmentsPerHalf; ++Index)
    {
        const double Angle = UE_DOUBLE_PI * Index / SegmentsPerHalf;
        const int32 Next = Index == 2 * SegmentsPerHalf ? Start
            : Mesh.AppendVertex(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0));
        const int32 Triangle = Mesh.AppendTriangle(Origin, Next, Previous);
        Mesh.Attributes()->GetMaterialID()->SetValue(Triangle, Index <= SegmentsPerHalf ? 0 : 1);
        Previous = Next;
    }
    FMeshNormals::InitializeOverlayToPerVertexNormals(Mesh.Attributes()->PrimaryNormals());

    auto* Plate = NewObject<UDynamicMeshComponent>(PlateActor);
    Plate->SetupAttachment(PlateActor->GetRootComponent());
    Plate->SetAbsolute(true, true, true);
    Plate->SetWorldLocation(PlateOrigin + FVector(0.0, 0.0, Extent.Z + 0.75));
    Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Plate->SetGenerateOverlapEvents(false);
    Plate->SetCanEverAffectNavigation(false);
    Plate->SetCastShadow(false);
    Plate->SetReceivesDecals(false);
    Plate->SetVisibility(false);
    Plate->ConfigureMaterialSet({
        UMaterialInstanceDynamic::Create(Material, Plate),
        UMaterialInstanceDynamic::Create(Material, Plate)});
    Plate->SetMesh(MoveTemp(Mesh));
    Plate->RegisterComponent();
    return Plate;
}

void ACoopRPGGameState::OnRep_ReadySlots()
{
    const auto* StatusEnum = StaticEnum<ECoopRPGReadyStatus>();
    UE_LOG(LogCoopRPGRound, Log, TEXT("RoundReadySlotsReplicated Round=%s P1=%s P2=%s %s"),
        *RoundId.ToString(), *StatusEnum->GetNameStringByValue(static_cast<int64>(ReadySlots.PlayerOne)),
        *StatusEnum->GetNameStringByValue(static_cast<int64>(ReadySlots.PlayerTwo)),
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}
