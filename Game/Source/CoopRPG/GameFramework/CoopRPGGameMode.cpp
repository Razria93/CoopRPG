#include "CoopRPGGameMode.h"

#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Combat/CoopRPGAttackTarget.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"
#include "CoopRPGExtractionZone.h"
#include "CoopRPGGameState.h"
#include "CoopRPGLootHUD.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPGRoundTravelSeats.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundTravel, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundAdmission, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGValidationRoster, Log, All);

namespace
{
    constexpr TCHAR FirstMapPackage[] = TEXT("/Game/Maps/L_Movement");
    constexpr TCHAR SecondMapPackage[] = TEXT("/Game/Maps/L_Movement_Round2");
    constexpr TCHAR ForestMapName[] = TEXT("L_ForestExpedition");
    constexpr TCHAR RoundOneMapName[] = TEXT("L_Round_01");
    constexpr double ResultDisplaySeconds = 3.0;
    constexpr int32 RoundCapacity = 2;

    bool UsesPlacedRoundActors(const FString& MapName)
    {
        return MapName == ForestMapName || MapName == RoundOneMapName;
    }

    void LogAdmissionRejection(const TCHAR* Stage, const FString& Reason)
    {
        UE_LOG(LogCoopRPGRoundAdmission, Log, TEXT("RoundJoinRejected Stage=%s Reason=%s"), Stage, *Reason);
    }

    FString GetNextRoundMap(const FString& CurrentMap)
    {
        return CurrentMap.EndsWith(TEXT("L_Movement_Round2")) ? FirstMapPackage : SecondMapPackage;
    }
}

ACoopRPGGameMode::ACoopRPGGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    // Evaluate after combat timers and movement have updated the authoritative facts.
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    DefaultPawnClass = ACoopRPGCharacter::StaticClass();
    PlayerControllerClass = ACoopRPGPlayerController::StaticClass();
    GameStateClass = ACoopRPGGameState::StaticClass();
    HUDClass = ACoopRPGLootHUD::StaticClass();
}

ACoopRPGAttackTarget* ACoopRPGGameMode::GetAttackTarget() const
{
    return AttackTarget;
}

AActor* ACoopRPGGameMode::GetMeleeTarget() const
{
    if (!bEnemyCombat)
    {
        return AttackTarget.Get();
    }
    const auto* State = GetGameState<ACoopRPGGameState>();
    return State && State->IsRoundPlaying() && IsValid(Enemy.Get()) && !Enemy->IsDead() ? Enemy.Get() : nullptr;
}

void ACoopRPGGameMode::UpdateLootRoundPhase(const AController* Leaving)
{
    auto* State = GetGameState<ACoopRPGGameState>();
    if (!State)
    {
        return;
    }

    PublishReadySlots(*State);
    if (State->HasRoundResult() || (bEnemyCombat && State->IsRoundPlaying()))
    {
        return;
    }

    int32 Players = 0;
    TArray<TWeakObjectPtr<ACoopRPGPlayerController>> ReadyPlayers;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (auto* Player = Cast<ACoopRPGPlayerController>(It->Get()); Player && Player != Leaving && Player->GetPawn())
        {
            ++Players;
            if (Player->IsRoundReady())
            {
                ReadyPlayers.Add(Player);
            }
        }
    }
    const bool bAllReady = Players == RoundCapacity && ReadyPlayers.Num() == RoundCapacity;
    if (bEnemyCombat && bAllReady)
    {
        RoundPlayers = MoveTemp(ReadyPlayers);
        bParticipantDisconnected = false;
    }
    State->SetRoundPlaying(bAllReady);
}

void ACoopRPGGameMode::AcknowledgeRoundResult(ACoopRPGPlayerController* Player, FGuid RoundId)
{
    const auto* State = GetGameState<ACoopRPGGameState>();
    if (!bEnemyCombat || !State || !State->HasRoundResult() || State->GetRoundId() != RoundId
        || bParticipantDisconnected || bTravelAttempted || !IsRoundParticipant(Player))
    {
        return;
    }

    const int32 PreviousCount = ResultAcknowledgements.Num();
    ResultAcknowledgements.AddUnique(Player);
    const int32 Count = ResultAcknowledgements.Num();
    if (Count == PreviousCount)
    {
        return;
    }

    const APlayerState* PlayerState = Player->GetPlayerState<APlayerState>();
    UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("RoundResultAcknowledged Round=%s PlayerId=%d Count=%d"),
        *RoundId.ToString(), PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE, Count);
    if (Count == RoundCapacity)
    {
        NextTravelTime = State->GetServerWorldTimeSeconds() + ResultDisplaySeconds;
    }
}

void ACoopRPGGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    bEnemyCombat = UGameplayStatics::HasOption(Options, TEXT("EnemyCombat"))
        || UsesPlacedRoundActors(UGameplayStatics::GetCurrentLevelName(this));
}

void ACoopRPGGameMode::BeginPlay()
{
    Super::BeginPlay();
    const FString MapName = UGameplayStatics::GetCurrentLevelName(this);
    if (UsesPlacedRoundActors(MapName))
    {
        auto* Chest = Cast<ACoopRPGSharedChest>(UGameplayStatics::GetActorOfClass(this, ACoopRPGSharedChest::StaticClass()));
        auto* Zone = Cast<ACoopRPGExtractionZone>(UGameplayStatics::GetActorOfClass(this, ACoopRPGExtractionZone::StaticClass()));
        Enemy = Cast<ACoopRPGEnemy>(UGameplayStatics::GetActorOfClass(this, ACoopRPGEnemy::StaticClass()));
        auto* State = GetGameState<ACoopRPGGameState>();
        if (ensureMsgf(Chest && Zone && IsValid(Enemy.Get()) && State,
            TEXT("Round map %s requires a shared chest, enemy, and extraction zone."), *MapName))
        {
            State->InitializeLootRound(Chest, Zone);
            PublishReadySlots(*State);
            const TCHAR* Event = MapName == ForestMapName ? TEXT("ForestStageReady") : TEXT("RoundStageReady");
            UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("%s Map=%s Chest=%s Enemy=%s Extraction=%s"),
                Event, *MapName, *Chest->GetActorLocation().ToString(), *Enemy->GetActorLocation().ToString(),
                *Zone->GetActorLocation().ToString());
        }
        return;
    }

    if (MapName == TEXT("L_Movement") || MapName == TEXT("L_Movement_Round2"))
    {
        ACoopRPGExtractionZone* Zone = nullptr;
        if (bEnemyCombat)
        {
            Zone = GetWorld()->SpawnActor<ACoopRPGExtractionZone>(FVector(620.0f, 350.0f, 6.0f), FRotator::ZeroRotator);
            Enemy = GetWorld()->SpawnActor<ACoopRPGEnemy>(FVector(750.0f, -95.0f, 90.0f), FRotator::ZeroRotator);
        }
        else
        {
            AttackTarget = GetWorld()->SpawnActor<ACoopRPGAttackTarget>(FVector(350.0f, 35.0f, 60.0f), FRotator::ZeroRotator);
        }
        if (auto* State = GetGameState<ACoopRPGGameState>())
        {
            auto* Chest = GetWorld()->SpawnActor<ACoopRPGSharedChest>(FVector(300.0f, -95.0f, 28.0f), FRotator::ZeroRotator);
            State->InitializeLootRound(Chest, Zone);
            PublishReadySlots(*State);
        }
    }
}

void ACoopRPGGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    auto* State = GetGameState<ACoopRPGGameState>();
    if (!bEnemyCombat || !State)
    {
        return;
    }
    if (State->HasRoundResult())
    {
        AdvanceRoundIfReady(*State);
        return;
    }
    if (!State->IsRoundPlaying())
    {
        return;
    }

    const ACoopRPGExtractionZone* Zone = State->GetExtractionZone();
    const bool bCanUseZone = IsValid(Zone) && State->GetTeamLootCount() >= 1;
    bool bDeath = false;
    int32 PlayersInside = 0;
    for (const TWeakObjectPtr<ACoopRPGPlayerController>& Participant : RoundPlayers)
    {
        const ACoopRPGPlayerController* Player = Participant.Get();
        if (!Player)
        {
            bParticipantDisconnected = true;
            continue;
        }

        const auto* Pawn = Cast<ACoopRPGCharacter>(Player->GetPawn());
        bDeath |= !Pawn || Pawn->GetHealth() <= 0.0f;
        if (bCanUseZone && Pawn && Zone->ContainsPawn(*Pawn))
        {
            ++PlayersInside;
        }
    }
    State->EvaluateRound(bParticipantDisconnected, bDeath, PlayersInside == RoundCapacity);
}

void ACoopRPGGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
    FString& ErrorMessage)
{
    ErrorMessage = GetRoundJoinRejection(Options, UniqueId);
    if (ErrorMessage.IsEmpty())
    {
        Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
        return;
    }

    FGameModeEvents::GameModePreLoginEvent.Broadcast(this, UniqueId, ErrorMessage);
    if (!ErrorMessage.IsEmpty())
    {
        LogAdmissionRejection(TEXT("PreLogin"), ErrorMessage);
    }
}

APlayerController* ACoopRPGGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
    const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    ErrorMessage = GetRoundJoinRejection(Options, UniqueId);
    if (!ErrorMessage.IsEmpty())
    {
        LogAdmissionRejection(TEXT("Login"), ErrorMessage);
        return nullptr;
    }

    APlayerController* Player = Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
    if (!Player)
    {
        return nullptr;
    }

    int32 ReservedSlot = INDEX_NONE;
    if (GetNetMode() == NM_DedicatedServer)
    {
        const FName Map(*UGameplayStatics::GetCurrentLevelName(this));
        ReservedSlot = GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>()->Admit(Map, UniqueId);
        UE_LOG(LogCoopRPGRoundAdmission, Log, TEXT("RoundPlayerAdmitted Map=%s Players=%d"),
            *Map.ToString(), GetNumPlayers());
    }
    int32 SlotIndex = ReservedSlot;
    if (SlotIndex == INDEX_NONE)
    {
        for (int32 Index = 0; Index < RoundCapacity; ++Index)
        {
            if (!PlayerSlots[Index].IsValid())
            {
                SlotIndex = Index;
                break;
            }
        }
    }
    if (SlotIndex != INDEX_NONE)
    {
        PlayerSlots[SlotIndex] = Player;
        const FString Label = UGameplayStatics::ParseOption(Options, TEXT("CoopValidationLabel"));
        ValidationClientLabels[SlotIndex] = Label.Equals(TEXT("A"), ESearchCase::CaseSensitive)
            || Label.Equals(TEXT("B"), ESearchCase::CaseSensitive) ? Label : FString();
        if (auto* State = GetGameState<ACoopRPGGameState>())
        {
            PublishReadySlots(*State);
        }
    }
    return Player;
}

void ACoopRPGGameMode::Logout(AController* Exiting)
{
    const auto* State = GetGameState<ACoopRPGGameState>();
    if (bEnemyCombat && State && (State->IsRoundPlaying() || State->HasRoundResult())
        && IsRoundParticipant(Exiting))
    {
        bParticipantDisconnected = true;
    }
    for (int32 Index = 0; Index < RoundCapacity; ++Index)
    {
        if (PlayerSlots[Index].Get() == Exiting)
        {
            PlayerSlots[Index].Reset();
            ValidationClientLabels[Index].Reset();
        }
    }
    Super::Logout(Exiting);
    UpdateLootRoundPhase(Exiting);
}

void ACoopRPGGameMode::RestartPlayer(AController* NewPlayer)
{
    Super::RestartPlayer(NewPlayer);
    if (auto* Character = NewPlayer ? Cast<ACoopRPGCharacter>(NewPlayer->GetPawn()) : nullptr)
    {
        for (int32 Index = 0; Index < RoundCapacity; ++Index)
        {
            if (PlayerSlots[Index].Get() == NewPlayer)
            {
                Character->SetDisplaySlot(Index + 1);
                if (auto* State = GetGameState<ACoopRPGGameState>())
                {
                    PublishReadySlots(*State);
                }
                break;
            }
        }
    }
}

void ACoopRPGGameMode::PublishReadySlots(ACoopRPGGameState& State)
{
    const auto GetStatus = [](const APlayerController* Controller)
    {
        const auto* Player = Cast<ACoopRPGPlayerController>(Controller);
        if (!Player || !Player->GetPawn())
        {
            return ECoopRPGReadyStatus::Disconnected;
        }
        return Player->IsRoundReady() ? ECoopRPGReadyStatus::Ready : ECoopRPGReadyStatus::Waiting;
    };
    const FCoopRPGReadySlots ReadySlots{GetStatus(PlayerSlots[0].Get()), GetStatus(PlayerSlots[1].Get())};
    State.SetReadySlots(ReadySlots);
    if (GetNetMode() != NM_DedicatedServer || !State.GetRoundId().IsValid())
    {
        return;
    }

    const auto DescribeSlot = [this](int32 Index, ECoopRPGReadyStatus ReadyStatus)
    {
        const APlayerController* Controller = PlayerSlots[Index].Get();
        const APlayerState* PlayerState = Controller ? Controller->GetPlayerState<APlayerState>() : nullptr;
        const FString& Label = ValidationClientLabels[Index];
        const TCHAR* Status = !Controller ? TEXT("Disconnected") : !Controller->GetPawn() ? TEXT("Joining")
            : ReadyStatus == ECoopRPGReadyStatus::Ready ? TEXT("Ready") : TEXT("Waiting");
        return FString::Printf(TEXT("P%dLabel=%s P%dId=%d P%dState=%s"), Index + 1,
            Controller && !Label.IsEmpty() ? *Label : TEXT("None"), Index + 1,
            PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE, Index + 1, Status);
    };

    const FString Snapshot = FString::Printf(TEXT("Round=%s %s %s"), *State.GetRoundId().ToString(),
        *DescribeSlot(0, ReadySlots.PlayerOne), *DescribeSlot(1, ReadySlots.PlayerTwo));
    if (Snapshot != LastValidationParticipants)
    {
        LastValidationParticipants = Snapshot;
        UE_LOG(LogCoopRPGValidationRoster, Log, TEXT("ValidationParticipants %s"), *Snapshot);
    }
}

FString ACoopRPGGameMode::GetRoundJoinRejection(const FString& Options, const FUniqueNetIdRepl& UniqueId)
{
    if (GetNetMode() != NM_DedicatedServer)
    {
        return {};
    }

    const ACoopRPGGameState* State = GetGameState<ACoopRPGGameState>();
    if (State && State->IsRoundPlaying())
    {
        return TEXT("RoundInProgress");
    }
    if (bEnemyCombat && State && State->HasRoundResult())
    {
        return TEXT("RoundFinished");
    }
    if (GetNumPlayers() >= RoundCapacity)
    {
        return TEXT("RoundFull");
    }
    if (UGameplayStatics::GetIntOption(Options, TEXT("SpectatorOnly"), 0) != 0
        || UGameplayStatics::GetIntOption(Options, TEXT("SplitscreenCount"), 1) > 1)
    {
        return TEXT("UnsupportedJoin");
    }
    const FName Map(*UGameplayStatics::GetCurrentLevelName(this));
    const auto* Seats = GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>();
    return Seats->Allows(Map, UniqueId) ? FString() : FString(TEXT("RoundTravelReserved"));
}

bool ACoopRPGGameMode::ReserveRoundTravelSeats(const FString& Destination) const
{
    if (GetNetMode() != NM_DedicatedServer)
    {
        return true;
    }

    TArray<FUniqueNetIdRepl> ConnectionIds;
    for (const auto& Slot : PlayerSlots)
    {
        if (const auto* Player = Slot.Get(); Player && IsRoundParticipant(Player))
        {
            const APlayerState* PlayerState = Player->GetPlayerState<APlayerState>();
            if (PlayerState && PlayerState->GetUniqueId().IsValid())
            {
                ConnectionIds.AddUnique(PlayerState->GetUniqueId());
            }
        }
    }
    if (ConnectionIds.Num() != RoundCapacity)
    {
        return false;
    }

    const FName Map(*FPackageName::GetShortName(Destination));
    GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>()->Reserve(Map, MoveTemp(ConnectionIds));
    return true;
}

bool ACoopRPGGameMode::IsRoundParticipant(const AController* Controller) const
{
    return Controller && RoundPlayers.ContainsByPredicate(
        [Controller](const auto& Player) { return Player.Get() == Controller; });
}

void ACoopRPGGameMode::AdvanceRoundIfReady(const ACoopRPGGameState& State)
{
    if (bTravelAttempted || bParticipantDisconnected || NextTravelTime <= 0.0
        || State.GetRoundEndReason() == ECoopRPGRoundEndReason::Disconnected
        || State.GetServerWorldTimeSeconds() < NextTravelTime
        || RoundPlayers.ContainsByPredicate([](const auto& Player) { return !Player.IsValid(); }))
    {
        return;
    }

    bTravelAttempted = true;
    const FString Destination = GetNextRoundMap(UGameplayStatics::GetCurrentLevelName(this));
    if (!ReserveRoundTravelSeats(Destination))
    {
        UE_LOG(LogCoopRPGRoundTravel, Error, TEXT("RoundTravelFailed Round=%s TargetMap=%s Reason=ParticipantIdentityUnavailable"),
            *State.GetRoundId().ToString(), *Destination);
        return;
    }
    const FString URL = Destination + TEXT("?EnemyCombat?NoSeamlessTravel");
    UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("RoundTravelStarted Round=%s TargetMap=%s"),
        *State.GetRoundId().ToString(), *Destination);
    if (!GetWorld()->ServerTravel(URL, true))
    {
        if (GetNetMode() == NM_DedicatedServer)
        {
            GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>()->Clear();
        }
        UE_LOG(LogCoopRPGRoundTravel, Error, TEXT("RoundTravelFailed Round=%s TargetMap=%s"),
            *State.GetRoundId().ToString(), *Destination);
    }
}
