#include "CoopRPGCharacter.h"

#include "CoopRPGHeroAnimInstance.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Abilities/CoopRPGMeleeAbility.h"
#include "CoopRPG/AbilitySystem/Abilities/CoopRPGSlashAbility.h"
#include "CoopRPG/AbilitySystem/Abilities/CoopRPGTauntAbility.h"
#include "CoopRPG/Debug/Logging/CoopRPGTauntLog.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Debug/CoopRPGCombatProbeComponent.h"
#include "CoopRPG/GameFramework/CoopRPGExpeditionMaps.h"
#include "CoopRPG/GameFramework/CoopRPGGameplayFeedback.h"
#include "CoopRPG/GameFramework/CoopRPGGameState.h"

#include "CoopRPG/GameFramework/CoopRPGPlayerState.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace1D.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/WeakObjectPtrTemplates.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGInput, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGEquipment, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGLocomotion, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGMeleeMotion, Log, All);

namespace
{
    constexpr float SwingEffectsLeadSeconds = 0.06f;
    constexpr float SwingEffectsCatchUpSeconds = 0.15f;
    constexpr int32 DefaultMeleeHitStopMs = 60;
    constexpr double MeleeHitStopCatchUpSeconds = 0.10;
    constexpr float OwnerMeleePresentationBufferSeconds = 0.080f;
    constexpr float MeleeHitStopMaxPoseCorrectionSeconds = 0.10f;

    int32 GetMeleeHitStopDurationMs()
    {
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
        const TCHAR* CommandLine = FCommandLine::Get();
        int32 ComparisonMs = 0;
        if (FParse::Param(CommandLine, TEXT("CoopCombatTest"))
            && FParse::Value(CommandLine, TEXT("CoopMeleeHitStopMs="), ComparisonMs)
            && (ComparisonMs == 80 || ComparisonMs == 100))
        {
            return ComparisonMs;
        }
#endif
        return DefaultMeleeHitStopMs;
    }

    struct FHeroAttackProfile
    {
        const TCHAR* ClipPath;
        float ContactTime;
    };

    FHeroAttackProfile GetHeroAttackProfile(ECoopRPGHero Hero)
    {
        switch (Hero)
        {
        case ECoopRPGHero::Adventurer:
            return {TEXT("/Game/ThirdParty/RPGHeroSquad/Animation/RPGHero/Anim_NormalAttack01_RPGHero.Anim_NormalAttack01_RPGHero"), 0.30f};
        case ECoopRPGHero::AnimalHero:
            return {TEXT("/Game/ThirdParty/RPGHeroSquad/Animation/AnimalHero/Anim_Attack01_AnimalHero.Anim_Attack01_AnimalHero"), 0.39f};
        default:
            return {nullptr, 0.65f};
        }
    }
}

ACoopRPGCharacter::ACoopRPGCharacter()
{
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->MaxWalkSpeed = 300.0f;

    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    BodyMesh->SetupAttachment(GetRootComponent());
    BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BodyMesh->SetRelativeScale3D(FVector(0.60f, 0.60f, 1.20f));
    BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -18.0f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    BodyMesh->SetStaticMesh(Cylinder.Object);

    HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
    HeadMesh->SetupAttachment(GetRootComponent());
    HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HeadMesh->SetRelativeScale3D(FVector(0.48f));
    HeadMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    HeadMesh->SetStaticMesh(Sphere.Object);

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> TintableMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    BodyMesh->SetMaterial(0, TintableMaterial.Object);
    HeadMesh->SetMaterial(0, TintableMaterial.Object);

#if !UE_SERVER
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> HitDarkenMaterial(
        TEXT("/Game/Presentation/Materials/M_HeroHitDarken.M_HeroHitDarken"));
    HeroHitDarkenMaterial = HitDarkenMaterial.Object;
#endif

    WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeleeWeapon"));
    WeaponMesh->SetupAttachment(GetRootComponent());
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMesh->SetRelativeScale3D(FVector(0.9f, 0.11f, 0.08f));
    WeaponMesh->SetRelativeLocation(FVector(30.0f, 43.0f, 16.0f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    WeaponMesh->SetStaticMesh(Cube.Object);
    WeaponMesh->SetVisibility(false);

    HeroWeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeroMeleeWeapon"));
    HeroWeaponMesh->SetupAttachment(GetMesh());
    HeroWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HeroWeaponMesh->SetVisibility(false);

    HeroShieldMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeroShield"));
    HeroShieldMesh->SetupAttachment(GetMesh());
    HeroShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HeroShieldMesh->SetVisibility(false);

    auto* CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(GetRootComponent());
    CameraBoom->SetUsingAbsoluteRotation(true);
    CameraBoom->SetRelativeRotation(FRotator(-48.0f, 0.0f, 0.0f));
    CameraBoom->TargetArmLength = 650.0f;
    CameraBoom->bDoCollisionTest = false;

    auto* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);

    static ConstructorHelpers::FObjectFinder<UInputAction> MoveAsset(TEXT("/Game/Input/IA_Move.IA_Move"));
    MoveAction = MoveAsset.Object;

    static ConstructorHelpers::FObjectFinder<UInputAction> AttackAsset(TEXT("/Game/Input/IA_Attack.IA_Attack"));
    AttackAction = AttackAsset.Object;

    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
    AbilitySystem->SetIsReplicated(true);
    // Owning client receives cooldown effects; observers receive their tags/cues.
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    CombatAttributes = CreateDefaultSubobject<UCoopRPGCombatAttributes>(TEXT("CombatAttributes"));
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    CreateDefaultSubobject<UCoopRPGCombatProbeComponent>(TEXT("CombatProbe"));
#endif
}

UAbilitySystemComponent* ACoopRPGCharacter::GetAbilitySystemComponent() const
{
    return AbilitySystem;
}

void ACoopRPGCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGCharacter, bHasMeleeWeapon);
    DOREPLIFETIME(ACoopRPGCharacter, bShieldEquipped);
    DOREPLIFETIME(ACoopRPGCharacter, bGuarding);
    DOREPLIFETIME(ACoopRPGCharacter, DisplaySlot);
    DOREPLIFETIME_CONDITION(ACoopRPGCharacter, PartyStamina, COND_SkipOwner);
    DOREPLIFETIME(ACoopRPGCharacter, MeleePresentation);
}

float ACoopRPGCharacter::GetHealth() const
{
    return CombatAttributes->GetHealth();
}

float ACoopRPGCharacter::GetStamina() const
{
    return CombatAttributes->GetStamina();
}

void ACoopRPGCharacter::SetMeleeWeaponEquipped(bool bEquipped)
{
    if (HasAuthority() && bHasMeleeWeapon != bEquipped)
    {
        bHasMeleeWeapon = bEquipped;
        if (!bEquipped)
        {
            ServerCancelAttack_Implementation();
        }
        RefreshWeaponVisibility();
        ForceNetUpdate();
    }
}

bool ACoopRPGCharacter::IsGuardian() const
{
    const auto* Identity = GetPlayerState<ACoopRPGPlayerState>();
    return Identity && Identity->GetSelectedHero() == ECoopRPGHero::AnimalHero;
}

double ACoopRPGCharacter::GetTauntPulseAge() const
{
    return GetWorld()->GetTimeSeconds() - TauntPulseStartedAt;
}

void ACoopRPGCharacter::MulticastTauntPulse_Implementation(FVector_NetQuantize Origin)
{
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    TauntPulseOrigin = Origin;
    TauntPulseStartedAt = GetWorld()->GetTimeSeconds();
    static const FSoftObjectPath EffectPath(TEXT("/Game/ThirdParty/Effects/Taunt/WarCry/MedievalRPGVFX/Niagara/Combat/Melee/NS_MRPG_War_Cry.NS_MRPG_War_Cry"));
    auto* Effect = Cast<UNiagaraSystem>(EffectPath.TryLoad());
    // Calibrated radius of War Cry's visible ground wave at unit scale.
    constexpr double WarCryGroundWaveRadiusCm = 615.0;
    constexpr double WarCryScale = UCoopRPGTauntAbility::RangeCm / WarCryGroundWaveRadiusCm;
    const auto* WarCry = Effect ? UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        this, Effect, Origin, FRotator::ZeroRotator, FVector(WarCryScale)) : nullptr;
    CoopRPG::Debug::TauntLog::Pulse(*this, WarCry != nullptr);
}

bool ACoopRPGCharacter::IsGuarding() const
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    return bGuarding && bShieldEquipped && GetHealth() > 0.0f && State && State->IsRoundPlaying();
}

void ACoopRPGCharacter::SetShieldEquipped(bool bEquipped)
{
    if (!HasAuthority() || bShieldEquipped == bEquipped)
    {
        return;
    }
    bShieldEquipped = bEquipped;
    bGuarding = bEquipped && bGuarding;
    OnRep_ShieldState();
    ForceNetUpdate();
}

void ACoopRPGCharacter::ReleaseGuardInput()
{
    if (bGuardKeyHeld)
    {
        bGuardKeyHeld = false;
        ServerSetGuarding(FGuid(), false);
    }
}

void ACoopRPGCharacter::ClearGuard()
{
    if (HasAuthority())
    {
        SetGuarding(false);
    }
}

double ACoopRPGCharacter::GetGuardAngle(const FVector& SourcePosition) const
{
    const FVector Offset = (SourcePosition - GetActorLocation()).GetSafeNormal2D();
    const double Dot = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), Offset);
    return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0, 1.0)));
}

void ACoopRPGCharacter::SetDisplaySlot(int32 Slot)
{
    check(HasAuthority());
    DisplaySlot = Slot;
    OnRep_DisplaySlot();
    ForceNetUpdate();
}

void ACoopRPGCharacter::FlashPresentationHit()
{
    SetPresentationTint(true);
    GetWorldTimerManager().SetTimer(HitFlashTimer, this, &ThisClass::RestorePresentationTint, 0.22f);
}

void ACoopRPGCharacter::RefreshHeroAppearance()
{
    const auto* Identity = GetPlayerState<ACoopRPGPlayerState>();
    if (GetNetMode() == NM_DedicatedServer || !Identity || Identity->GetSelectedHero() == ECoopRPGHero::Unassigned)
    {
        return;
    }

    const TCHAR* AssetPath = Identity->GetSelectedHero() == ECoopRPGHero::Adventurer
        ? TEXT("/Game/ThirdParty/RPGHeroSquad/Mesh/Character/SK_RPGHeroPolyart.SK_RPGHeroPolyart")
        : TEXT("/Game/ThirdParty/RPGHeroSquad/Mesh/Character/SK_DogPolyart.SK_DogPolyart");
    USkeletalMesh* HeroMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(AssetPath)).LoadSynchronous();
    if (HeroMesh != GetMesh()->GetSkeletalMeshAsset())
    {
        GetMesh()->EmptyOverrideMaterials();
        GetMesh()->SetSkeletalMeshAsset(HeroMesh);
        HeroLocomotion = nullptr;
        HeroAttackClip = nullptr;
        if (HeroMesh)
        {
            if (HeroHitDarkenMaterial && !GetMesh()->GetOverlayMaterial())
            {
                GetMesh()->SetOverlayMaterial(UMaterialInstanceDynamic::Create(HeroHitDarkenMaterial, GetMesh()));
            }

            // Ground the reference pose and match the existing capsule and overhead nameplate.
            const FBoxSphereBounds Bounds = HeroMesh->GetImportedBounds();
            const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
            const float Scale = HalfHeight * 0.90f / FMath::Max(1.0f, static_cast<float>(Bounds.BoxExtent.Z));
            const FVector FeetOffset(0.0f, 0.0f, -HalfHeight - Scale * (Bounds.Origin.Z - Bounds.BoxExtent.Z));
            const FRotator Facing(0.0f, -90.0f, 0.0f);
            GetMesh()->SetRelativeScale3D(FVector(Scale));
            GetMesh()->SetRelativeLocationAndRotation(FeetOffset, Facing);
            CacheInitialMeshOffset(FeetOffset, Facing);

            const TCHAR* LocomotionPath = Identity->GetSelectedHero() == ECoopRPGHero::Adventurer
                ? TEXT("/Game/Characters/Heroes/Adventurer/Animation/BS_Adventurer.BS_Adventurer")
                : TEXT("/Game/Characters/Heroes/AnimalHero/Animation/BS_AnimalHero.BS_AnimalHero");
            UBlendSpace1D* Locomotion = TSoftObjectPtr<UBlendSpace1D>(FSoftObjectPath(LocomotionPath)).LoadSynchronous();
            if (Locomotion && Locomotion->GetSkeleton() == HeroMesh->GetSkeleton())
            {
                HeroLocomotion = Locomotion;
                GetMesh()->SetAnimInstanceClass(UCoopRPGHeroAnimInstance::StaticClass());
                auto* Animation = CastChecked<UCoopRPGHeroAnimInstance>(GetMesh()->GetAnimInstance());
                Animation->ConfigureLocomotion(HeroLocomotion);
                if (const TCHAR* AttackPath = GetHeroAttackProfile(Identity->GetSelectedHero()).ClipPath)
                {
                    HeroAttackClip = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(AttackPath)).LoadSynchronous();
                }
            }
            else
            {
                UE_LOG(LogCoopRPGLocomotion, Warning, TEXT("Missing or incompatible locomotion %s for %s"),
                    LocomotionPath, *GetNameSafe(HeroMesh));
            }
        }
        SetPresentationTint(GetWorldTimerManager().IsTimerActive(HitFlashTimer));
        ConfigureHeroWeapon(Identity->GetSelectedHero());
        ConfigureHeroShield(Identity->GetSelectedHero());
        OnRep_MeleePresentation();
    }
    BodyMesh->SetVisibility(!HeroMesh);
    HeadMesh->SetVisibility(!HeroMesh);
    UE_LOG(LogCoopRPGEquipment, Log, TEXT("HeroAppearance PlayerId=%d Hero=%s Mesh=%s Role=%s"),
        Identity->GetPlayerId(), *UEnum::GetValueAsString(Identity->GetSelectedHero()), *GetNameSafe(HeroMesh),
        *UEnum::GetValueAsString(GetLocalRole()));
}

float ACoopRPGCharacter::GetMeleeWindup() const
{
    const auto* Identity = GetPlayerState<ACoopRPGPlayerState>();
    return GetHeroAttackProfile(Identity ? Identity->GetSelectedHero() : ECoopRPGHero::Unassigned).ContactTime;
}

uint32 ACoopRPGCharacter::StartMeleePresentation(float FacingYaw, float Windup, bool bSlash)
{
    check(HasAuthority());
    ++MeleePresentation.Sequence;
    const auto* State = GetWorld()->GetGameState();
    MeleePresentation.StartedAt = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    MeleePresentation.FacingYaw = FacingYaw;
    MeleePresentation.Windup = Windup;
    MeleePresentation.bCancelled = false;
    MeleePresentation.bSlash = bSlash;
    GetCharacterMovement()->bOrientRotationToMovement = false;
    bMeleeFacingLocked = true;
    if (GetNetMode() != NM_DedicatedServer)
    {
        OnRep_MeleePresentation();
    }
    ForceNetUpdate();
    return MeleePresentation.Sequence;
}

void ACoopRPGCharacter::FinishMeleeWindup(bool bInterrupted)
{
    check(HasAuthority());
    ReleaseMeleeFacing();
    if (bInterrupted)
    {
        MeleePresentation.bCancelled = true;
        if (GetNetMode() != NM_DedicatedServer)
        {
            OnRep_MeleePresentation();
        }
        ForceNetUpdate();
    }
}

void ACoopRPGCharacter::RequestMeleeHitStop(uint32 Swing)
{
    if (GetNetMode() != NM_Client || !IsLocallyControlled() || Swing == 0)
    {
        return;
    }

    if (Swing == PresentedMeleeSequence)
    {
        ApplyMeleeHitStop(Swing);
    }
    else if (Swing > MeleePresentation.Sequence && Swing > PendingHitStopSwing)
    {
        // Feedback can arrive a frame before the pawn's replicated swing.
        PendingHitStopSwing = Swing;
        PendingHitStopDeadline = FPlatformTime::Seconds() + MeleeHitStopCatchUpSeconds;
    }
}

void ACoopRPGCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() != NM_DedicatedServer)
    {
        OnCharacterMovementUpdated.AddDynamic(this, &ThisClass::UpdateLocomotion);
    }
    AbilitySystem->InitAbilityActorInfo(this, this);
    if (HasAuthority())
    {
        if (CoopRPG::Expedition::IsExpedition(GetWorld()))
        {
            PartyStamina = FMath::CeilToInt(GetStamina());
            AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCoopRPGCombatAttributes::GetStaminaAttribute())
                .AddUObject(this, &ThisClass::UpdatePartyStamina);
        }
        AbilitySystem->GiveAbility(FGameplayAbilitySpec(UCoopRPGMeleeAbility::StaticClass()));
        if (CoopRPG::Expedition::IsSkirmish(GetWorld()) || CoopRPG::Expedition::IsBoss(GetWorld()))
        {
            AbilitySystem->GiveAbility(FGameplayAbilitySpec(UCoopRPGSlashAbility::StaticClass()));
            AbilitySystem->GiveAbility(FGameplayAbilitySpec(UCoopRPGTauntAbility::StaticClass()));
        }
    }
}

void ACoopRPGCharacter::OnPlayerStateChanged(APlayerState* NewPlayerState, APlayerState* OldPlayerState)
{
    Super::OnPlayerStateChanged(NewPlayerState, OldPlayerState);
    RefreshHeroAppearance();
}

void ACoopRPGCharacter::NotifyControllerChanged()
{
    Super::NotifyControllerChanged();
    ClearGuard();
    AbilitySystem->InitAbilityActorInfo(this, this);
}

void ACoopRPGCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACoopRPGCharacter::Move);
        Input->BindAction(AttackAction, ETriggerEvent::Started, this, &ACoopRPGCharacter::Attack);
        PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ACoopRPGCharacter::CancelAttack);
        PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ACoopRPGCharacter::UseSlash);
        PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ACoopRPGCharacter::UseTaunt);
        PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &ACoopRPGCharacter::PressGuardInput);
        PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &ACoopRPGCharacter::ReleaseGuardInput);
    }
}

void ACoopRPGCharacter::UpdatePartyStamina(const FOnAttributeChangeData& Change)
{
    const int32 DisplayValue = FMath::CeilToInt(Change.NewValue);
    if (PartyStamina != DisplayValue)
    {
        PartyStamina = DisplayValue;
        ForceNetUpdate();
    }
}

void ACoopRPGCharacter::Move(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
    // The fixed camera looks along world +X. Screen right is world +Y.
    AddMovementInput(FVector::ForwardVector, Axis.Y);
    AddMovementInput(FVector::RightVector, Axis.X);
}

void ACoopRPGCharacter::Attack()
{
    UE_LOG(LogCoopRPGInput, Verbose, TEXT("AttackInput Actor=%s %s"),
        *GetNameSafe(this), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    const bool bSubmitted = AbilitySystem->TryActivateAbilityByClass(UCoopRPGMeleeAbility::StaticClass());
    UE_LOG(LogCoopRPGInput, Verbose, TEXT("AttackDispatch Actor=%s Submitted=%d %s"),
        *GetNameSafe(this), bSubmitted ? 1 : 0,
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGCharacter::CancelAttack()
{
    if (GetNetMode() == NM_Client)
    {
        UE_LOG(LogCoopRPGInput, Verbose, TEXT("CancelRPCCalled Actor=%s %s"),
            *GetNameSafe(this), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
    ServerCancelAttack();
}

void ACoopRPGCharacter::UseSlash()
{
    AbilitySystem->TryActivateAbilityByClass(UCoopRPGSlashAbility::StaticClass());
}

void ACoopRPGCharacter::UseTaunt()
{
    AbilitySystem->TryActivateAbilityByClass(UCoopRPGTauntAbility::StaticClass());
}

void ACoopRPGCharacter::PressGuardInput()
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!bGuardKeyHeld && State && State->IsRoundPlaying())
    {
        bGuardKeyHeld = true;
        ServerSetGuarding(State->GetRoundId(), true);
    }
}

void ACoopRPGCharacter::SetGuarding(bool bActive)
{
    if (bGuarding != bActive)
    {
        bGuarding = bActive;
        OnRep_ShieldState();
        ForceNetUpdate();
    }
}

void ACoopRPGCharacter::ServerSetGuarding_Implementation(FGuid RoundId, bool bRequested)
{
    if (!bRequested)
    {
        SetGuarding(false);
        return;
    }

    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!bShieldEquipped || GetHealth() <= 0.0f || bMeleeFacingLocked || !RoundId.IsValid()
        || !State || State->GetRoundId() != RoundId || !State->IsRoundPlaying())
    {
        UE_LOG(LogCoopRPGEquipment, Log, TEXT("PlayerGuardRejected Slot=%d Equipped=%d Round=%s %s"),
            GetDisplaySlot(), bShieldEquipped, *RoundId.ToString(),
            *CoopRPG::Debug::DescribePlayerRequestContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }
    SetGuarding(true);
}

void ACoopRPGCharacter::OnRep_ShieldState()
{
    HeroShieldMesh->SetVisibility(bShieldEquipped && HeroShieldMesh->GetStaticMesh() != nullptr);
    if (bShieldEquipped || GetDisplaySlot() > 0)
    {
        UE_LOG(LogCoopRPGEquipment, Log, TEXT("PlayerShieldState Slot=%d Equipped=%d Guard=%d Visible=%d Mesh=%s Socket=%s %s"),
            GetDisplaySlot(), bShieldEquipped, bGuarding, HeroShieldMesh->IsVisible(),
            *GetNameSafe(HeroShieldMesh->GetStaticMesh()), *HeroShieldMesh->GetAttachSocketName().ToString(),
            *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void ACoopRPGCharacter::ServerCancelAttack_Implementation()
{
    if (GetNetConnection())
    {
        UE_LOG(LogCoopRPGInput, Verbose, TEXT("CancelRPCReceived Actor=%s %s"),
            *GetNameSafe(this), *CoopRPG::Debug::DescribePlayerRequestContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }

    if (const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromClass(UCoopRPGMeleeAbility::StaticClass());
        Spec && Spec->IsActive())
    {
        AbilitySystem->CancelAbilityHandle(Spec->Handle);
    }
    else if (const FGameplayAbilitySpec* Slash = AbilitySystem->FindAbilitySpecFromClass(UCoopRPGSlashAbility::StaticClass());
        Slash && Slash->IsActive())
    {
        AbilitySystem->CancelAbilityHandle(Slash->Handle);
    }
    else
    {
        UE_LOG(LogCoopRPGInput, Verbose, TEXT("CancelIgnored Actor=%s Reason=NoActiveAttack %s"),
            *GetNameSafe(this), *CoopRPG::Debug::DescribePlayerRequestContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void ACoopRPGCharacter::OnRep_MeleeWeapon()
{
    RefreshWeaponVisibility();
    const APlayerState* State = GetPlayerState();
    UE_LOG(LogCoopRPGEquipment, Log, TEXT("MeleeWeaponReplicated Actor=%s Equipped=%d EquippedPlayerId=%d %s"),
        *GetNameSafe(this), bHasMeleeWeapon ? 1 : 0, State ? State->GetPlayerId() : INDEX_NONE,
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGCharacter::UpdateLocomotion(float, FVector, FVector)
{
    if (UE_LOG_ACTIVE(LogCoopRPGLocomotion, VeryVerbose))
    {
        if (const auto* Animation = Cast<UCoopRPGHeroAnimInstance>(GetMesh()->GetAnimInstance()))
        {
            float Blend = 0.0f;
            float Time = 0.0f;
            Animation->ReadLocomotion(Blend, Time);
            UE_LOG(LogCoopRPGLocomotion, VeryVerbose,
                TEXT("HeroLocomotion Slot=%d Role=%s Asset=%s Speed=%.2f Blend=%.2f Time=%.3f Yaw=%.1f"),
                DisplaySlot, *UEnum::GetValueAsString(GetLocalRole()), *GetNameSafe(HeroLocomotion),
                GetCharacterMovement()->Velocity.Size2D(), Blend, Time, GetActorRotation().Yaw);
        }
    }
}

void ACoopRPGCharacter::OnRep_DisplaySlot()
{
    RestorePresentationTint();
    UE_LOG(LogCoopRPGEquipment, Log, TEXT("PlayerDisplaySlot Actor=%s Slot=%d %s"),
        *GetNameSafe(this), DisplaySlot, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGCharacter::OnRep_MeleePresentation()
{
    if (MeleePresentation.Sequence == 0)
    {
        return;
    }

    auto* Animation = Cast<UCoopRPGHeroAnimInstance>(GetMesh()->GetAnimInstance());
    if (MeleePresentation.bCancelled)
    {
        ReleaseMeleeFacing();
        if (PresentedMeleeSequence == MeleePresentation.Sequence)
        {
            GetWorldTimerManager().ClearTimer(MeleeSwingEffectsTimer);
        }
        if (Animation && LocalMeleeMontage.IsValid() && PresentedMeleeSequence == MeleePresentation.Sequence)
        {
            Animation->Montage_Stop(0.06f, LocalMeleeMontage.Get());
            LocalMeleeMontage.Reset();
            UE_LOG(LogCoopRPGMeleeMotion, Log, TEXT("MeleeMotionStopped Slot=%d Swing=%u %s"),
                DisplaySlot, MeleePresentation.Sequence, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        }
        return;
    }

    if (!Animation || !HeroAttackClip || PresentedMeleeSequence == MeleePresentation.Sequence)
    {
        return;
    }

    const auto* State = GetWorld()->GetGameState();
    const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    const float Elapsed = FMath::Max(0.0, Now - MeleePresentation.StartedAt);
    GetWorldTimerManager().ClearTimer(MeleeSwingEffectsTimer);
    PresentedMeleeSequence = MeleePresentation.Sequence;
    if (Elapsed >= HeroAttackClip->GetPlayLength())
    {
        return;
    }

    const float FacingTimeLeft = MeleePresentation.Windup - Elapsed;
    if (FacingTimeLeft > 0.0f && !HasAuthority())
    {
        GetCharacterMovement()->bOrientRotationToMovement = false;
        bMeleeFacingLocked = true;
        SetActorRotation(FRotator(0.0f, MeleePresentation.FacingYaw, 0.0f));
        GetWorldTimerManager().SetTimer(MeleeFacingTimer, this, &ThisClass::ReleaseMeleeFacing, FacingTimeLeft, false);
    }

    // A late replicated swing must still show the approach to contact for its attacker.
    const float LatestWindupPose = FMath::Max(0.0f, MeleePresentation.Windup - OwnerMeleePresentationBufferSeconds);
    const float ClipOffset = GetNetMode() == NM_Client && IsLocallyControlled()
        ? FMath::Clamp(Elapsed - OwnerMeleePresentationBufferSeconds, 0.0f, LatestWindupPose)
        : Elapsed;
    LocalMeleeMontage = Animation->PlaySlotAnimationAsDynamicMontage(
        HeroAttackClip, UCoopRPGHeroAnimInstance::MeleeSlot, 0.045f, 0.08f, 1.0f, 1, -1.0f, ClipOffset);
    if (LocalMeleeMontage.IsValid())
    {
        UE_LOG(LogCoopRPGMeleeMotion, Log, TEXT("MeleeMotionStarted Slot=%d Swing=%u Clip=%s Offset=%.3f Windup=%.3f Yaw=%.1f ServerElapsed=%.3f Slash=%d %s"),
            DisplaySlot, MeleePresentation.Sequence, *GetNameSafe(HeroAttackClip), ClipOffset, MeleePresentation.Windup,
            MeleePresentation.FacingYaw, Elapsed, MeleePresentation.bSlash,
            *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));

        const float SwingTime = FMath::Max(MeleePresentation.Windup - SwingEffectsLeadSeconds, 0.0f);
        if (const float Delay = SwingTime - ClipOffset; Delay > 0.0f)
        {
            GetWorldTimerManager().SetTimer(MeleeSwingEffectsTimer, this, &ThisClass::PlayMeleeSwingEffects, Delay, false);
        }
        else if (ClipOffset - SwingTime <= SwingEffectsCatchUpSeconds)
        {
            PlayMeleeSwingEffects();
        }

        if (PendingHitStopSwing == PresentedMeleeSequence)
        {
            if (FPlatformTime::Seconds() <= PendingHitStopDeadline)
            {
                ApplyMeleeHitStop(PendingHitStopSwing);
            }
            PendingHitStopSwing = 0;
        }
    }
}

void ACoopRPGCharacter::PlayMeleeSwingEffects()
{
    auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const auto* Animation = GetMesh()->GetAnimInstance();
    if (State && Animation && LocalMeleeMontage.IsValid() && Animation->Montage_IsPlaying(LocalMeleeMontage.Get())
        && !MeleePresentation.bCancelled && PresentedMeleeSequence == MeleePresentation.Sequence)
    {
        State->PlayMeleeEffect(ECoopRPGMeleeEffect::SwordSwing, HeroWeaponMesh->GetComponentLocation(),
            MeleePresentation.FacingYaw, DisplaySlot, PresentedMeleeSequence, MeleePresentation.bSlash ? 1.8f : 1.0f);
    }
}

void ACoopRPGCharacter::ApplyMeleeHitStop(uint32 Swing)
{
    if (!IsLocallyControlled() || Swing == LastHitStoppedMeleeSequence || Swing != MeleePresentation.Sequence
        || MeleePresentation.bCancelled)
    {
        return;
    }

    auto* Animation = GetMesh()->GetAnimInstance();
    auto* Montage = LocalMeleeMontage.Get();
    if (!Animation || !Montage || Montage->HasRootMotion())
    {
        return;
    }

    auto* Instance = Animation->GetActiveInstanceForMontage(Montage);
    if (!Instance || !Instance->IsPlaying() || Instance->GetPlayRate() <= 0.0f)
    {
        return;
    }

    const float ContactPosition = MeleePresentation.Windup;
    const float ReceivedPosition = Instance->GetPosition();
    if (ContactPosition <= 0.0f || ContactPosition >= Montage->GetPlayLength()
        || ReceivedPosition < ContactPosition - MeleeHitStopMaxPoseCorrectionSeconds)
    {
        UE_LOG(LogCoopRPGMeleeMotion, Log,
            TEXT("MeleeHitStopSkipped Slot=%d Swing=%u ContactPosition=%.3f ReceivedPosition=%.3f"),
            DisplaySlot, Swing, ContactPosition, ReceivedPosition);
        return;
    }

    LastHitStoppedMeleeSequence = Swing;
    const int32 InstanceId = Instance->GetInstanceID();
    const float PreviousRate = Instance->GetPlayRate();
    const int32 Slot = DisplaySlot;
    // Late confirmations still hold the current attack pose without rewinding through recovery.
    const bool bUseContactPose = ReceivedPosition <= ContactPosition + MeleeHitStopMaxPoseCorrectionSeconds;
    if (bUseContactPose)
    {
        Instance->SetPosition(ContactPosition);
    }
    Instance->SetPlayRate(0.0f);
    const int32 DurationMs = GetMeleeHitStopDurationMs();
    const double HoldSeconds = DurationMs / 1000.0;
    const double StartedAt = FPlatformTime::Seconds();
    UE_LOG(LogCoopRPGMeleeMotion, Log,
        TEXT("MeleeHitStopStarted Slot=%d Swing=%u RequestedMs=%d ActiveRate=%.2f "
             "GameFrame=%llu ReceivedPosition=%.3f Position=%.3f Pose=%s"),
        Slot, Swing, DurationMs, Instance->GetPlayRate(),
        GFrameCounter, ReceivedPosition, Instance->GetPosition(), bUseContactPose ? TEXT("Contact") : TEXT("Current"));

    // Restore only this local montage instance if it still exists when the hold expires.
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [WeakAnimation = TWeakObjectPtr<UAnimInstance>(Animation),
            InstanceId, PreviousRate, StartedAt, HoldSeconds, Slot, Swing](float)
        {
            const double Elapsed = FPlatformTime::Seconds() - StartedAt;
            if (Elapsed < HoldSeconds)
            {
                return true;
            }

            auto* ResumingAnimation = WeakAnimation.Get();
            auto* ResumingInstance = ResumingAnimation ? ResumingAnimation->GetMontageInstanceForID(InstanceId) : nullptr;
            const bool bHeroResumed = ResumingInstance && ResumingInstance->IsActive()
                && ResumingInstance->GetPlayRate() == 0.0f;
            if (bHeroResumed)
            {
                ResumingInstance->SetPlayRate(PreviousRate);
            }
            UE_LOG(LogCoopRPGMeleeMotion, Log,
                TEXT("MeleeHitStopEnded Slot=%d Swing=%u ElapsedMs=%.2f RestoredRate=%.2f "
                     "GameFrame=%llu Position=%.3f"),
                Slot, Swing, Elapsed * 1000.0, bHeroResumed ? ResumingInstance->GetPlayRate() : 0.0f,
                GFrameCounter, bHeroResumed ? ResumingInstance->GetPosition() : -1.0f);
            return false;
        }));
}

void ACoopRPGCharacter::ReleaseMeleeFacing()
{
    GetWorldTimerManager().ClearTimer(MeleeFacingTimer);
    if (bMeleeFacingLocked)
    {
        GetCharacterMovement()->bOrientRotationToMovement = true;
        bMeleeFacingLocked = false;
    }
}

void ACoopRPGCharacter::ConfigureHeroWeapon(ECoopRPGHero Hero)
{
    HeroWeaponMesh->SetStaticMesh(nullptr);

    const bool bAdventurer = Hero == ECoopRPGHero::Adventurer;
    const FName Socket = bAdventurer ? FName(TEXT("Sword")) : FName(TEXT("WeaponSocket"));
    if (GetMesh()->DoesSocketExist(Socket))
    {
        const TCHAR* SwordPath = bAdventurer
            ? TEXT("/Game/ThirdParty/RPGHeroSquad/Mesh/Weapon/SM_RPGHero_Sword01_Polyart.SM_RPGHero_Sword01_Polyart")
            : TEXT("/Game/ThirdParty/RPGHeroSquad/Mesh/Weapon/SM_AnimalHero_Sword01_Polyart.SM_AnimalHero_Sword01_Polyart");
        UStaticMesh* Sword = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(SwordPath)).LoadSynchronous();
        if (Sword && HeroWeaponMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, Socket))
        {
            HeroWeaponMesh->SetStaticMesh(Sword);
            // Match the local grip offsets of the two source hero Blueprints.
            const FVector GripLocation = bAdventurer ? FVector(4.8856f, 0.0f, 0.0f) : FVector::ZeroVector;
            const FRotator GripRotation(0.0f, 0.0f, bAdventurer ? 90.0f : -90.0f);
            HeroWeaponMesh->SetRelativeTransform(FTransform(GripRotation, GripLocation));
        }
    }

    RefreshWeaponVisibility();
}

void ACoopRPGCharacter::RefreshWeaponVisibility()
{
    const bool bHasHeroSword = HeroWeaponMesh->GetStaticMesh() != nullptr;
    HeroWeaponMesh->SetVisibility(bHasMeleeWeapon && bHasHeroSword);
    WeaponMesh->SetVisibility(bHasMeleeWeapon && !bHasHeroSword);

    if (GetNetMode() != NM_DedicatedServer)
    {
        const APlayerState* State = GetPlayerState();
        UE_LOG(LogCoopRPGEquipment, Verbose,
            TEXT("MeleeWeaponPresentation Actor=%s EquippedPlayerId=%d Equipped=%d Sword=%s Socket=%s HeroVisible=%d FallbackVisible=%d GripX=%.4f GripRoll=%.1f %s"),
            *GetName(), State ? State->GetPlayerId() : INDEX_NONE, bHasMeleeWeapon ? 1 : 0,
            *GetNameSafe(HeroWeaponMesh->GetStaticMesh()), *HeroWeaponMesh->GetAttachSocketName().ToString(),
            HeroWeaponMesh->IsVisible() ? 1 : 0, WeaponMesh->IsVisible() ? 1 : 0,
            HeroWeaponMesh->GetRelativeLocation().X, HeroWeaponMesh->GetRelativeRotation().Roll,
            *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void ACoopRPGCharacter::ConfigureHeroShield(ECoopRPGHero Hero)
{
    HeroShieldMesh->SetStaticMesh(nullptr);
    const bool bAdventurer = Hero == ECoopRPGHero::Adventurer;
    const FName Socket = bAdventurer ? FName(TEXT("Shield")) : FName(TEXT("ShieldSocket"));
    if (GetMesh()->DoesSocketExist(Socket))
    {
        const TCHAR* ShieldPath = bAdventurer
            ? TEXT("/Game/ThirdParty/RPGHeroSquad/Mesh/Weapon/SM_RPGHero_Shield01_Polyart.SM_RPGHero_Shield01_Polyart")
            : TEXT("/Game/ThirdParty/RPGHeroSquad/Mesh/Weapon/SM_AnimalHero_Shield01_Polyart.SM_AnimalHero_Shield01_Polyart");
        UStaticMesh* Shield = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(ShieldPath)).LoadSynchronous();
        if (Shield && HeroShieldMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, Socket))
        {
            HeroShieldMesh->SetStaticMesh(Shield);
            const FVector Grip = bAdventurer ? FVector(0.0, 0.0, 6.1316) : FVector(-0.512, 0.0, 0.0);
            const FRotator Rotation = bAdventurer ? FRotator::ZeroRotator : FRotator(90.0, 90.0, 180.0);
            HeroShieldMesh->SetRelativeTransform(FTransform(Rotation, Grip));
        }
    }
    OnRep_ShieldState();
}

void ACoopRPGCharacter::SetPresentationTint(bool bDarkened)
{
    if (DisplaySlot == 0 || GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    if (!PresentationMaterial)
    {
        PresentationMaterial = BodyMesh->CreateDynamicMaterialInstance(0);
        HeadMesh->SetMaterial(0, PresentationMaterial);
    }
    if (PresentationMaterial)
    {
        const FLinearColor Color = CoopRPG::Presentation::PlayerColor(DisplaySlot);
        PresentationMaterial->SetVectorParameterValue(TEXT("Color"), bDarkened ? Color * 0.25f : Color);
    }

    auto* HitOverlay = Cast<UMaterialInstanceDynamic>(GetMesh()->GetOverlayMaterial());
    if (HitOverlay && HitOverlay->Parent == HeroHitDarkenMaterial)
    {
        const auto* Identity = GetPlayerState<ACoopRPGPlayerState>();
        const bool bAnimalHero = Identity && Identity->GetSelectedHero() == ECoopRPGHero::AnimalHero;
        const float HitOpacity = bAnimalHero ? 0.58f : 0.30f;
        HitOverlay->SetScalarParameterValue(TEXT("HitOpacity"), bDarkened ? HitOpacity : 0.0f);
    }
}

void ACoopRPGCharacter::RestorePresentationTint()
{
    SetPresentationTint(false);
}
