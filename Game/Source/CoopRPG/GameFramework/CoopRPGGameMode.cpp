#include "CoopRPGGameMode.h"

#include "CoopRPGLobbyNpc.h"

#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Combat/CoopRPGAttackTarget.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"
#include "CoopRPGExtractionZone.h"
#include "CoopRPGExpeditionMaps.h"
#include "CoopRPGGameState.h"
#include "CoopRPGLootHUD.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPGRoundTravelSeats.h"
#include "CoopRPGPlayerState.h"

#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundTravel, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundAdmission, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGValidationRoster, Log, All);

namespace
{
    constexpr TCHAR ForestMapName[] = TEXT("L_ForestExpedition");
    constexpr TCHAR RoundOneMapName[] = TEXT("L_Round_01");
    constexpr double ResultDisplaySeconds = 3.0;
    constexpr double LobbyDepartureSeconds = 3.0;
    constexpr double RoundStartSeconds = 3.0;
    constexpr double PuzzleRewardSeconds = 30.0;
    constexpr int32 RoundCapacity = ACoopRPGGameState::RoundCapacity;

    bool UsesPlacedRoundActors(const FString& MapName)
    {
        return MapName == ForestMapName || MapName == RoundOneMapName
            || MapName == CoopRPG::Expedition::SkirmishMap || MapName == CoopRPG::Expedition::BossMap;
    }

    bool IsOnPuzzlePlate(const ACoopRPGCharacter& Character, const FVector& Center)
    {
        const FVector Location = Character.GetActorLocation();
        const double FeetZ = Location.Z - Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        return FMath::Abs(FeetZ - Center.Z) <= 28.0
            && FVector::DistSquared2D(Location, Center) <= FMath::Square(CoopRPG::Expedition::PuzzlePlateRadius);
    }

    void LogAdmissionRejection(const TCHAR* Stage, const FString& Reason)
    {
        UE_LOG(LogCoopRPGRoundAdmission, Log, TEXT("RoundJoinRejected Stage=%s Reason=%s"), Stage, *Reason);
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
    PlayerStateClass = ACoopRPGPlayerState::StaticClass();
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
    if (!State || !State->IsRoundPlaying())
    {
        return nullptr;
    }
    if (bExpeditionSkirmish)
    {
        const auto* FirstAlive = SkirmishEnemies.FindByPredicate([](const ACoopRPGEnemy* Candidate)
        {
            return IsValid(Candidate) && !Candidate->IsDead();
        });
        return FirstAlive ? FirstAlive->Get() : nullptr;
    }
    return IsValid(Enemy.Get()) && !Enemy->IsDead() ? Enemy.Get() : nullptr;
}

bool ACoopRPGGameMode::IsSkirmishEnemy(const ACoopRPGEnemy* Candidate) const
{
    return bExpeditionSkirmish && Candidate && SkirmishEnemies.ContainsByPredicate(
        [Candidate](const ACoopRPGEnemy* Registered) { return Registered == Candidate; });
}

ACoopRPGEnemy* ACoopRPGGameMode::FindClosestSkirmishEnemy(const FVector& Origin) const
{
    const auto* State = GetGameState<ACoopRPGGameState>();
    if (!State || !State->IsRoundPlaying())
    {
        return nullptr;
    }

    ACoopRPGEnemy* Closest = nullptr;
    double BestDistance = TNumericLimits<double>::Max();
    for (ACoopRPGEnemy* Candidate : SkirmishEnemies)
    {
        if (!IsValid(Candidate) || Candidate->IsDead())
        {
            continue;
        }
        const double Distance = FVector::DistSquared2D(Origin, Candidate->GetActorLocation());
        if (Distance < BestDistance)
        {
            Closest = Candidate;
            BestDistance = Distance;
        }
    }
    return Closest;
}

void ACoopRPGGameMode::UpdateLootRoundPhase(const AController* Leaving)
{
    auto* State = GetGameState<ACoopRPGGameState>();
    if (!State)
    {
        return;
    }

    PublishReadySlots(*State);
    if (State->HasRoundResult() || ((bEnemyCombat || bExpeditionPuzzle) && State->IsRoundPlaying()))
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
    if (bExpeditionLobby)
    {
        if (!bAllReady && !bTravelAttempted)
        {
            NextTravelTime = 0.0;
            State->SetLobbyDepartureDeadline(0.0);
            RoundPlayers.Reset();
        }
        else if (bAllReady && !bTravelAttempted && NextTravelTime <= 0.0 && State->GetRoundId().IsValid())
        {
            RoundPlayers = MoveTemp(ReadyPlayers);
            NextTravelTime = State->GetServerWorldTimeSeconds() + LobbyDepartureSeconds;
            State->SetLobbyDepartureDeadline(NextTravelTime);
            UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("ExpeditionLobbyReady Round=%s ReadyPlayers=%d"),
                *State->GetRoundId().ToString(), RoundPlayers.Num());
        }
        return;
    }

    if (bExpeditionSkirmish || bExpeditionPuzzle || bExpeditionBoss)
    {
        if (!bAllReady || !State->GetRoundId().IsValid())
        {
            State->SetRoundStartDeadline(0.0);
            return;
        }
        if (!State->IsRoundStartScheduled())
        {
            const double Now = State->GetServerWorldTimeSeconds();
            State->SetRoundStartDeadline(Now + RoundStartSeconds);
            UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("ExpeditionRoundStartScheduled Round=%s Time=%.3f Deadline=%.3f"),
                *State->GetRoundId().ToString(), Now, Now + RoundStartSeconds);
            return;
        }
        if (State->GetRoundStartRemainingSeconds() > 0.0)
        {
            return;
        }
        State->SetRoundStartDeadline(0.0);
    }

    if ((bEnemyCombat || bExpeditionPuzzle) && bAllReady)
    {
        RoundPlayers = MoveTemp(ReadyPlayers);
        bParticipantDisconnected = false;
    }
    State->SetRoundPlaying(bAllReady);
}

void ACoopRPGGameMode::AcknowledgeRoundResult(ACoopRPGPlayerController* Player, FGuid RoundId)
{
    const auto* State = GetGameState<ACoopRPGGameState>();
    if ((!bEnemyCombat && !bExpeditionPuzzle) || !State || !State->HasRoundResult() || State->GetRoundId() != RoundId
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
    if (Count == RoundCapacity && !State->GetRoundTravelDestination().IsEmpty()
        && State->GetRoundEndReason() != ECoopRPGRoundEndReason::PuzzleSolved)
    {
        NextTravelTime = State->GetServerWorldTimeSeconds() + ResultDisplaySeconds;
    }
}

void ACoopRPGGameMode::SelectPuzzleReward(ACoopRPGPlayerController& Player, FGuid RoundId, ECoopRPGPuzzleReward Reward)
{
    auto* State = GetGameState<ACoopRPGGameState>();
    if (!State || State->GetRoundId() != RoundId || State->GetRoundEndReason() != ECoopRPGRoundEndReason::PuzzleSolved
        || bParticipantDisconnected || bTravelAttempted || !IsRoundParticipant(&Player)
        || State->GetPuzzleRewardDeadline() <= 0.0
        || (Reward != ECoopRPGPuzzleReward::ExpeditionPower && Reward != ECoopRPGPuzzleReward::HealthPotionCapacity
            && Reward != ECoopRPGPuzzleReward::StaminaPotionCapacity))
    {
        return;
    }

    if (State->GetServerWorldTimeSeconds() >= State->GetPuzzleRewardDeadline())
    {
        TickPuzzleRewards(*State);
        return;
    }
    FinalizePuzzleReward(*State, Player, Reward, false);
}

FIntPoint ACoopRPGGameMode::GetPotionCapacityBonus(const FUniqueNetIdRepl& ConnectionId) const
{
    return ConnectionId.IsValid() ? GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>()->GetPotionCapacityBonus(ConnectionId)
        : FIntPoint::ZeroValue;
}

void ACoopRPGGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    const FString CurrentMap = UGameplayStatics::GetCurrentLevelName(this);
    bExpeditionLobby = CurrentMap == CoopRPG::Expedition::LobbyMap;
    bExpeditionPuzzle = CurrentMap == CoopRPG::Expedition::PuzzleMap;
    bExpeditionBoss = CurrentMap == CoopRPG::Expedition::BossMap;
    bExpeditionSkirmish = CurrentMap == CoopRPG::Expedition::SkirmishMap;
    bEnemyCombat = !bExpeditionLobby && !bExpeditionPuzzle
        && (UGameplayStatics::HasOption(Options, TEXT("EnemyCombat")) || UsesPlacedRoundActors(CurrentMap));
}

void ACoopRPGGameMode::BeginPlay()
{
    Super::BeginPlay();
    const FString MapName = UGameplayStatics::GetCurrentLevelName(this);
    if (bExpeditionLobby)
    {
        if (auto* State = GetGameState<ACoopRPGGameState>())
        {
            State->InitializeLobby();
            InitializeTravelRound(*State);
        }
        const auto SpawnNpc = [this](ECoopRPGLobbyService Service, double Y)
        {
            const FVector Location(385.0, Y, 2.0);
            if (auto* Npc = GetWorld()->SpawnActor<ACoopRPGLobbyNpc>(Location, FRotator(0.0, 180.0, 0.0)))
            {
                Npc->InitializeService(Service);
                UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("LobbyNpcSpawned Service=%s X=%.0f Y=%.0f"),
                    *UEnum::GetValueAsString(Service), Location.X, Location.Y);
            }
        };
        SpawnNpc(ECoopRPGLobbyService::Storage, -370.0);
        SpawnNpc(ECoopRPGLobbyService::Job, 0.0);
        SpawnNpc(ECoopRPGLobbyService::Shop, 370.0);
        return;
    }

    if (bExpeditionPuzzle)
    {
        TArray<AActor*> PlatesA;
        TArray<AActor*> PlatesB;
        UGameplayStatics::GetAllActorsWithTag(this, FName(TEXT("PuzzlePlateA")), PlatesA);
        UGameplayStatics::GetAllActorsWithTag(this, FName(TEXT("PuzzlePlateB")), PlatesB);
        auto* State = GetGameState<ACoopRPGGameState>();
        if (ensureMsgf(State && PlatesA.Num() == 1 && PlatesB.Num() == 1
            && FVector::Dist2D(PlatesA[0]->GetActorLocation(), PlatesB[0]->GetActorLocation())
                > 2.0 * CoopRPG::Expedition::PuzzlePlateRadius,
            TEXT("The puzzle requires two separate, non-overlapping plates.")))
        {
            State->InitializePuzzle(PlatesA[0]->GetActorLocation(), PlatesB[0]->GetActorLocation());
            InitializeTravelRound(*State);
        }
        return;
    }

    if (UsesPlacedRoundActors(MapName))
    {
        auto* Chest = Cast<ACoopRPGSharedChest>(UGameplayStatics::GetActorOfClass(this, ACoopRPGSharedChest::StaticClass()));
        auto* Zone = Cast<ACoopRPGExtractionZone>(UGameplayStatics::GetActorOfClass(this, ACoopRPGExtractionZone::StaticClass()));
        if (bExpeditionSkirmish)
        {
            for (TActorIterator<ACoopRPGEnemy> It(GetWorld()); It; ++It)
            {
                if (It->IsMushroom())
                {
                    SkirmishEnemies.Add(*It);
                }
            }
            ensureMsgf(SkirmishEnemies.Num() == CoopRPG::Expedition::SkirmishEnemyCount,
                TEXT("Skirmish requires %d placed mushrooms, found %d."),
                CoopRPG::Expedition::SkirmishEnemyCount, SkirmishEnemies.Num());
            for (const ACoopRPGEnemy* Mushroom : SkirmishEnemies)
            {
                UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("SkirmishEnemyPlaced Actor=%s Position=%s HP=%.0f"),
                    *Mushroom->GetName(), *Mushroom->GetActorLocation().ToCompactString(), Mushroom->GetHealth());
            }
        }
        else
        {
            Enemy = Cast<ACoopRPGEnemy>(UGameplayStatics::GetActorOfClass(this, ACoopRPGEnemy::StaticClass()));
        }
        auto* State = GetGameState<ACoopRPGGameState>();
        const bool bHasEnemies = bExpeditionSkirmish
            ? SkirmishEnemies.Num() == CoopRPG::Expedition::SkirmishEnemyCount : IsValid(Enemy.Get());
        if (ensureMsgf(Chest && bHasEnemies && State
            && (bExpeditionBoss ? !Zone && Enemy->IsBlackKnight() : IsValid(Zone)),
            TEXT("Round map %s has missing or incompatible round actors."), *MapName))
        {
            State->InitializeLootRound(Chest, Zone);
            InitializeTravelRound(*State);
            const TCHAR* Event = MapName == ForestMapName ? TEXT("ForestStageReady") : TEXT("RoundStageReady");
            const FString Extraction = Zone ? Zone->GetActorLocation().ToString() : FString(TEXT("None"));
            UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("%s Map=%s Chest=%s Enemy=%s Extraction=%s"),
                Event, *MapName, *Chest->GetActorLocation().ToString(),
                bExpeditionSkirmish ? TEXT("SkirmishGroup") : *Enemy->GetActorLocation().ToString(), *Extraction);
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
            InitializeTravelRound(*State);
        }
    }
}

void ACoopRPGGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    auto* State = GetGameState<ACoopRPGGameState>();
    if (!State)
    {
        return;
    }
    if (bExpeditionLobby)
    {
        AdvanceRoundIfReady(*State, CoopRPG::Expedition::SkirmishPackage);
        return;
    }
    if (!bEnemyCombat && !bExpeditionPuzzle)
    {
        return;
    }
    if (State->HasRoundResult())
    {
        if (State->GetRoundEndReason() == ECoopRPGRoundEndReason::PuzzleSolved)
        {
            TickPuzzleRewards(*State);
        }
        AdvanceRoundIfReady(*State, State->GetRoundTravelDestination());
        return;
    }
    if (!State->IsRoundPlaying())
    {
        if (State->IsRoundStartScheduled() && State->GetRoundStartRemainingSeconds() <= 0.0)
        {
            UpdateLootRoundPhase();
        }
        return;
    }

    const ACoopRPGExtractionZone* Zone = State->GetExtractionZone();
    const bool bCanUseZone = IsValid(Zone) && State->IsExtractionUnlocked();
    bool bDeath = false;
    int32 PlayersInside = 0;
    uint8 OccupantsA = 0;
    uint8 OccupantsB = 0;
    for (const TWeakObjectPtr<ACoopRPGPlayerController>& Participant : RoundPlayers)
    {
        const ACoopRPGPlayerController* Player = Participant.Get();
        if (!Player)
        {
            bParticipantDisconnected = true;
            continue;
        }

        const auto* Pawn = Cast<ACoopRPGCharacter>(Player->GetPawn());
        const bool bAlive = Pawn && Pawn->GetHealth() > 0.0f;
        bDeath |= !bAlive;
        if (bCanUseZone && bAlive && Zone->ContainsPawn(*Pawn))
        {
            ++PlayersInside;
        }
        if (bExpeditionPuzzle && bAlive)
        {
            const uint8 PlayerBit = PlayerSlots[0].Get() == Player ? 1 : 2;
            const FCoopRPGPuzzleStatus& Puzzle = State->GetPuzzleStatus();
            if (IsOnPuzzlePlate(*Pawn, Puzzle.PlateA))
            {
                OccupantsA |= PlayerBit;
            }
            if (IsOnPuzzlePlate(*Pawn, Puzzle.PlateB))
            {
                OccupantsB |= PlayerBit;
            }
        }
    }
    if (bExpeditionPuzzle)
    {
        State->EvaluatePuzzle(bParticipantDisconnected, bDeath, OccupantsA, OccupantsB);
    }
    else
    {
        if (!bParticipantDisconnected && !bDeath && bExpeditionBoss && IsValid(Enemy.Get())
            && Enemy->IsDead() && State->CanStartBossRewards())
        {
            OpenBossRewards(*State);
        }
        State->EvaluateRound(bParticipantDisconnected, bDeath, PlayersInside);
        if (State->GetRoundEndReason() == ECoopRPGRoundEndReason::BossDefeated)
        {
            for (const auto& Participant : RoundPlayers)
            {
                if (auto* Player = Participant.Get())
                {
                    Player->AwardBossGold(State->GetRoundId());
                }
            }
        }
    }
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
    auto* State = GetGameState<ACoopRPGGameState>();
    UCoopRPGRoundTravelSeats* Seats = nullptr;
    if (GetNetMode() == NM_DedicatedServer)
    {
        const FName Map(*UGameplayStatics::GetCurrentLevelName(this));
        Seats = GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>();
        ReservedSlot = Seats->Admit(Map, UniqueId);
        UE_LOG(LogCoopRPGRoundAdmission, Log, TEXT("RoundPlayerAdmitted Map=%s Players=%d"),
            *Map.ToString(), GetNumPlayers());
    }
    int32 SlotIndex = ReservedSlot != INDEX_NONE && !PlayerSlots[ReservedSlot].IsValid() ? ReservedSlot : INDEX_NONE;
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
        auto* CoopPlayer = Cast<ACoopRPGPlayerController>(Player);
        auto* Identity = Player->GetPlayerState<ACoopRPGPlayerState>();
        FCoopRPGRoundTravelParticipant Arrival;
        const bool bRestored = Seats && State && CoopPlayer && Identity && CoopRPG::Expedition::IsExpedition(GetWorld())
            && Seats->TakeParticipant(State->GetRoundId(), UniqueId, Arrival);
        if (Identity)
        {
            const ECoopRPGHero DefaultHero = SlotIndex == 0 ? ECoopRPGHero::Adventurer : ECoopRPGHero::AnimalHero;
            Identity->SetSelectedHero(bRestored && Arrival.Hero != ECoopRPGHero::Unassigned ? Arrival.Hero : DefaultHero);
        }
        if (bRestored)
        {
            CoopPlayer->RestoreTravelInventory(Arrival.Inventory, Arrival.StageEntry);
            const TCHAR* Stage = bExpeditionLobby ? TEXT("Lobby")
                : bExpeditionBoss ? TEXT("Boss") : bExpeditionPuzzle ? TEXT("Puzzle") : TEXT("Skirmish");
            UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("%sRecoveryRestored Slot=%d Count=%d"),
                Stage, SlotIndex + 1, CoopPlayer->GetRecoveryCount());
            UE_LOG(LogCoopRPGRoundTravel, Log,
                TEXT("ExpeditionParticipantRestored Round=%s PlayerId=%d Slot=%d Reserved=%d Entry=%d HP=%d SP=%d RegisteredHP=%d RegisteredSP=%d Sword=%d Shield=%d ShieldEquipped=%d MartialPower=%d Protection=%d SlashCapsule=%d TauntCapsule=%d SlashLearned=%d TauntLearned=%d SlashSlot=%d TauntSlot=%d"),
                *State->GetRoundId().ToString(), Identity->GetPlayerId(), SlotIndex + 1, ReservedSlot != INDEX_NONE,
                Arrival.StageEntry.IsSet(), Arrival.Inventory.Recovery.Personal.X, Arrival.Inventory.Recovery.Personal.Y,
                Arrival.Inventory.Recovery.Registration.bHealth, Arrival.Inventory.Recovery.Registration.bStamina,
                static_cast<int32>(Arrival.Inventory.Equipment.Sword.Ownership), static_cast<int32>(Arrival.Inventory.Equipment.Shield.Ownership),
                CoopPlayer->IsPersonalShieldEquipped(), CoopPlayer->GetMartialPower(), CoopPlayer->GetProtection(),
                Arrival.Inventory.SkillProgress.Slash.bHasCapsule, Arrival.Inventory.SkillProgress.Taunt.bHasCapsule,
                Arrival.Inventory.SkillProgress.Slash.bRegistered, Arrival.Inventory.SkillProgress.Taunt.bRegistered,
                Arrival.Inventory.SkillSlots.Slash, Arrival.Inventory.SkillSlots.Taunt);
        }
        const FString Label = UGameplayStatics::ParseOption(Options, TEXT("CoopValidationLabel"));
        ValidationClientLabels[SlotIndex] = Label.Equals(TEXT("A"), ESearchCase::CaseSensitive)
            || Label.Equals(TEXT("B"), ESearchCase::CaseSensitive) ? Label : FString();
        if (State)
        {
            PublishReadySlots(*State);
        }
    }
    return Player;
}

void ACoopRPGGameMode::Logout(AController* Exiting)
{
    const auto* State = GetGameState<ACoopRPGGameState>();
    if ((bEnemyCombat || bExpeditionPuzzle) && State && (State->IsRoundPlaying() || State->HasRoundResult())
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
    const auto* Player = Cast<ACoopRPGPlayerController>(Exiting);
    const auto* Identity = Player ? Player->GetPlayerState<ACoopRPGPlayerState>() : nullptr;
    if (GetNetMode() == NM_DedicatedServer && State && !State->IsRoundPlaying() && !State->HasRoundResult()
        && Identity && Identity->GetUniqueId().IsValid() && CoopRPG::Expedition::IsExpedition(GetWorld()))
    {
        const FGuid RoundId = State->GetRoundId();
        GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>()->SaveParticipant(RoundId, Identity->GetUniqueId(),
            {Player->GetTravelInventory(false), Identity->GetSelectedHero(), Player->GetStageEntryInventory(RoundId)});
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
                    auto* Controller = CastChecked<ACoopRPGPlayerController>(NewPlayer);
                    if (CoopRPG::Expedition::IsExpedition(GetWorld()))
                    {
                        Controller->LoadSessionGold(State->GetRoundId());
                        Controller->ApplyExpeditionEquipment();
                    }
                    if (bExpeditionLobby)
                    {
                        Controller->LoadPersonalStorage();
                    }
                    else if (CoopRPG::Expedition::IsExpedition(GetWorld()))
                    {
                        Controller->InitializeStageInventory(State->GetRoundId());
                    }
                    if (bExpeditionPuzzle || bExpeditionBoss)
                    {
                        UE_LOG(LogCoopRPGRoundTravel, Log,
                            TEXT("%sPlayerInitialized Round=%s Slot=%d HP=%.0f SP=%.0f Armed=%d Recovery=%d Ready=%d Playing=%d TeamLoot=%d Score=%d"),
                            bExpeditionBoss ? TEXT("Boss") : TEXT("Puzzle"), *State->GetRoundId().ToString(), Index + 1, Character->GetHealth(),
                            Character->GetStamina(), Character->HasMeleeWeapon(), Controller->GetRecoveryCount(),
                            Controller->IsRoundReady(), State->IsRoundPlaying(), State->GetTeamLootCount(),
                            State->GetTeamScore());
                    }
                }
                break;
            }
        }
    }
}

void ACoopRPGGameMode::InitializeTravelRound(ACoopRPGGameState& State)
{
    if (GetNetMode() == NM_DedicatedServer)
    {
        GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>()->EnterRound(State.GetRoundId(),
            FName(*UGameplayStatics::GetCurrentLevelName(this)), bExpeditionLobby);
    }
    PublishReadySlots(State);
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
    if ((bExpeditionLobby && bTravelAttempted) || (State && State->IsRoundPlaying()))
    {
        return TEXT("RoundInProgress");
    }
    if ((bEnemyCombat || bExpeditionPuzzle) && State && State->HasRoundResult())
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

bool ACoopRPGGameMode::ReserveRoundTravelSeats(const ACoopRPGGameState& State, const FString& Destination) const
{
    if (GetNetMode() != NM_DedicatedServer)
    {
        return true;
    }

    const bool bToPuzzle = Destination == CoopRPG::Expedition::PuzzlePackage
        && CoopRPG::Expedition::IsSkirmish(GetWorld());
    const bool bToBoss = Destination == CoopRPG::Expedition::BossPackage && bExpeditionPuzzle;
    const bool bRetryStage = !bExpeditionLobby && CoopRPG::Expedition::IsExpedition(GetWorld())
        && (State.GetRoundEndReason() == ECoopRPGRoundEndReason::Death
            || State.GetRoundEndReason() == ECoopRPGRoundEndReason::Timeout)
        && Destination == UWorld::RemovePIEPrefix(GetWorld()->GetPackage()->GetName());
    const bool bAdvanceStage = bToPuzzle || bToBoss;
    const bool bReturnToLobby = bExpeditionBoss && Destination == CoopRPG::Expedition::LobbyPackage;
    const bool bStartExpedition = bExpeditionLobby && Destination == CoopRPG::Expedition::SkirmishPackage;
    const bool bCarryExpeditionState = bRetryStage || bAdvanceStage;
    const bool bCarryInventory = bCarryExpeditionState || bReturnToLobby || bStartExpedition;
    TArray<FUniqueNetIdRepl> SlotOwners;
    TMap<FUniqueNetIdRepl, FCoopRPGRoundTravelParticipant> Participants;
    for (const auto& Slot : PlayerSlots)
    {
        const auto* Player = Cast<ACoopRPGPlayerController>(Slot.Get());
        const auto* Identity = Player ? Player->GetPlayerState<ACoopRPGPlayerState>() : nullptr;
        if (!Identity || !IsRoundParticipant(Player) || !Identity->GetUniqueId().IsValid())
        {
            return false;
        }
        const FUniqueNetIdRepl& ConnectionId = Identity->GetUniqueId();
        if (Participants.Contains(ConnectionId))
        {
            return false;
        }
        const FCoopRPGRecoverySnapshot Before = Player->GetTravelInventory(false).Recovery;
        FCoopRPGExpeditionInventory Inventory = bCarryInventory ? Player->GetTravelInventory(bRetryStage)
            : FCoopRPGExpeditionInventory{};
        Inventory.bExpeditionPowerReward &= bCarryExpeditionState;
        const FCoopRPGRecoverySnapshot Carried = Inventory.Recovery;
        SlotOwners.Add(ConnectionId);
        Participants.Add(ConnectionId, FCoopRPGRoundTravelParticipant{MoveTemp(Inventory), Identity->GetSelectedHero(), {}});
        if (bReturnToLobby || bStartExpedition)
        {
            const FIntPoint PotionBonus = GetPotionCapacityBonus(ConnectionId);
            UE_LOG(LogCoopRPGRoundTravel, Log,
                TEXT("ExpeditionBoundaryReserved Round=%s Slot=%d Returning=%d PowerBefore=%d PowerCarry=%d CapacityPassive=%d StaminaCapacityPassive=%d"),
                *State.GetRoundId().ToString(), Participants.Num(), bReturnToLobby, Player->HasExpeditionPowerReward(),
                Participants[ConnectionId].Inventory.bExpeditionPowerReward, PotionBonus.X, PotionBonus.Y);
        }
        if (bCarryExpeditionState)
        {
            const FIntPoint Supplies = Player->GetStageRecoverySupplies();
            UE_LOG(LogCoopRPGRoundTravel, Log,
                TEXT("RecoveryTravelReserved Round=%s Slot=%d Transfer=%s Before=%d Carry=%d SupplyHP=%d SupplySP=%d BeforeSP=%d CarrySP=%d RegisteredHP=%d RegisteredSP=%d"),
                *State.GetRoundId().ToString(), Participants.Num(), bRetryStage ? TEXT("Retry") : TEXT("Advance"),
                Before.Personal.X, Carried.Personal.X, Supplies.X, Supplies.Y, Before.Personal.Y, Carried.Personal.Y,
                Carried.Registration.bHealth, Carried.Registration.bStamina);
        }
        if (bAdvanceStage)
        {
            UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("%sRecoveryReserved Slot=%d Before=%d Carry=%d"),
                bToBoss ? TEXT("Boss") : TEXT("Puzzle"), Participants.Num(), Before.Personal.X, Carried.Personal.X);
        }
    }

    const FName Map(*FPackageName::GetShortName(Destination));
    GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>()->Reserve(Map, SlotOwners, MoveTemp(Participants), bRetryStage);
    return true;
}

bool ACoopRPGGameMode::IsRoundParticipant(const AController* Controller) const
{
    return Controller && RoundPlayers.ContainsByPredicate(
        [Controller](const auto& Player) { return Player.Get() == Controller; });
}

void ACoopRPGGameMode::TickPuzzleRewards(ACoopRPGGameState& State)
{
    if (bParticipantDisconnected || bTravelAttempted || NextTravelTime > 0.0 || RoundPlayers.Num() != RoundCapacity
        || RoundPlayers.ContainsByPredicate([](const auto& Player) { return !Player.IsValid(); }))
    {
        return;
    }

    const double Now = State.GetServerWorldTimeSeconds();
    if (State.GetPuzzleRewardDeadline() <= 0.0)
    {
        State.SetPuzzleRewardDeadline(Now + PuzzleRewardSeconds);
        UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("PuzzleRewardsOpened Round=%s Start=%.3f Deadline=%.3f Players=%d"),
            *State.GetRoundId().ToString(), Now, State.GetPuzzleRewardDeadline(), RoundPlayers.Num());
    }
    if (Now >= State.GetPuzzleRewardDeadline())
    {
        for (const auto& Player : RoundPlayers)
        {
            if (Player->GetPuzzleReward() == ECoopRPGPuzzleReward::None)
            {
                FinalizePuzzleReward(State, *Player.Get(), ECoopRPGPuzzleReward::HealthPotionCapacity, true);
            }
        }
    }
    if (!RoundPlayers.ContainsByPredicate([](const auto& Player) { return Player->GetPuzzleReward() == ECoopRPGPuzzleReward::None; }))
    {
        NextTravelTime = Now + ResultDisplaySeconds;
    }
}

void ACoopRPGGameMode::FinalizePuzzleReward(ACoopRPGGameState& State, ACoopRPGPlayerController& Player,
    ECoopRPGPuzzleReward Reward, bool bTimedOut)
{
    const APlayerState* Identity = Player.GetPlayerState<APlayerState>();
    const int32 PlayerId = Identity ? Identity->GetPlayerId() : INDEX_NONE;
    if (!Player.FinalizePuzzleReward(Reward))
    {
        UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("PuzzleRewardRejected Round=%s PlayerId=%d Reason=AlreadyFinalized"),
            *State.GetRoundId().ToString(), PlayerId);
        return;
    }

    const bool bStaminaReward = Reward == ECoopRPGPuzzleReward::StaminaPotionCapacity;
    if ((Reward == ECoopRPGPuzzleReward::HealthPotionCapacity || bStaminaReward) && Identity && Identity->GetUniqueId().IsValid())
    {
        GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>()->GrantPotionCapacityReward(Identity->GetUniqueId(), bStaminaReward);
        Player.RefreshPotionCapacityBonus();
    }
    UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("PuzzleRewardFinalized Round=%s PlayerId=%d Reward=%s Source=%s At=%.3f"),
        *State.GetRoundId().ToString(), PlayerId, *UEnum::GetValueAsString(Reward),
        bTimedOut ? TEXT("Timeout") : TEXT("Choice"), State.GetServerWorldTimeSeconds());
}

void ACoopRPGGameMode::OpenBossRewards(ACoopRPGGameState& State)
{
    const FVector ChestLocation(1200.0, 0.0, 28.0);
    // Matches the placed Joint Extraction in L_ExpeditionSkirmish.
    const FVector PortalLocation(2440.0, 0.0, 6.0);
    auto* RewardChest = GetWorld()->SpawnActor<ACoopRPGSharedChest>(ChestLocation, FRotator::ZeroRotator);
    auto* Portal = GetWorld()->SpawnActor<ACoopRPGExtractionZone>(PortalLocation, FRotator::ZeroRotator);
    if (!IsValid(RewardChest) || !IsValid(Portal))
    {
        if (IsValid(RewardChest))
        {
            RewardChest->Destroy();
        }
        if (IsValid(Portal))
        {
            Portal->Destroy();
        }
        return;
    }

    auto* SupplyChest = State.GetSharedChest();
    RewardChest->SetBossRewardStock();
    for (const auto& Participant : RoundPlayers)
    {
        if (auto* Player = Participant.Get(); IsValid(Player) && Player->GetOpenedChest() == SupplyChest)
        {
            Player->CloseOpenedChest(State.GetRoundId(), TEXT("BossRewards"));
        }
    }
    State.StartBossRewards(*RewardChest, *Portal);
    if (IsValid(SupplyChest))
    {
        SupplyChest->Destroy();
    }
}

void ACoopRPGGameMode::AdvanceRoundIfReady(ACoopRPGGameState& State, const FString& Destination)
{
    if (Destination.IsEmpty() || bTravelAttempted || bParticipantDisconnected || NextTravelTime <= 0.0
        || RoundPlayers.Num() != RoundCapacity
        || State.GetRoundEndReason() == ECoopRPGRoundEndReason::Disconnected
        || State.GetServerWorldTimeSeconds() < NextTravelTime
        || RoundPlayers.ContainsByPredicate([](const auto& Player) { return !Player.IsValid(); }))
    {
        return;
    }

    if (bExpeditionLobby && RoundPlayers.ContainsByPredicate(
        [](const auto& Player) { return !Player->GetPawn() || !Player->IsRoundReady(); }))
    {
        UpdateLootRoundPhase();
        return;
    }

    bTravelAttempted = true;
    const bool bDestinationAvailable = FPackageName::DoesPackageExist(Destination);
    if (bDestinationAvailable && ReserveRoundTravelSeats(State, Destination))
    {
        const FString URL = Destination + TEXT("?EnemyCombat?NoSeamlessTravel");
        UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("RoundTravelStarted Round=%s TargetMap=%s"),
            *State.GetRoundId().ToString(), *Destination);
        if (GetWorld()->ServerTravel(URL, true))
        {
            return;
        }

        if (GetNetMode() == NM_DedicatedServer)
        {
            GetGameInstance()->GetSubsystem<UCoopRPGRoundTravelSeats>()->CancelTravel();
        }
        UE_LOG(LogCoopRPGRoundTravel, Error, TEXT("RoundTravelFailed Round=%s TargetMap=%s Reason=ServerTravelRejected"),
            *State.GetRoundId().ToString(), *Destination);
    }
    else
    {
        UE_LOG(LogCoopRPGRoundTravel, Error, TEXT("RoundTravelFailed Round=%s TargetMap=%s Reason=%s"),
            *State.GetRoundId().ToString(), *Destination,
            bDestinationAvailable ? TEXT("ParticipantIdentityUnavailable") : TEXT("DestinationUnavailable"));
    }

    if (bExpeditionLobby)
    {
        for (const auto& Participant : RoundPlayers)
        {
            if (auto* Player = Participant.Get())
            {
                Player->ResetLobbyReady();
            }
        }
        NextTravelTime = 0.0;
        State.SetLobbyDepartureDeadline(0.0);
        RoundPlayers.Reset();
        bTravelAttempted = false;
        PublishReadySlots(State);
    }
}
