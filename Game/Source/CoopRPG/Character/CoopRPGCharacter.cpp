#include "CoopRPGCharacter.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Abilities/CoopRPGMeleeAbility.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Debug/CoopRPGCombatProbeComponent.h"
#include "CoopRPG/GameFramework/CoopRPGGameplayFeedback.h"

#include "CoopRPG/GameFramework/CoopRPGPlayerState.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace1D.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGInput, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGEquipment, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGLocomotion, Log, All);

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

    WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeleeWeapon"));
    WeaponMesh->SetupAttachment(GetRootComponent());
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMesh->SetRelativeScale3D(FVector(0.9f, 0.11f, 0.08f));
    WeaponMesh->SetRelativeLocation(FVector(30.0f, 43.0f, 16.0f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    WeaponMesh->SetStaticMesh(Cube.Object);
    WeaponMesh->SetVisibility(false);

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
    DOREPLIFETIME(ACoopRPGCharacter, DisplaySlot);
}

float ACoopRPGCharacter::GetHealth() const
{
    return CombatAttributes->GetHealth();
}

float ACoopRPGCharacter::GetStamina() const
{
    return CombatAttributes->GetStamina();
}

void ACoopRPGCharacter::EquipMeleeWeapon()
{
    if (HasAuthority() && !bHasMeleeWeapon)
    {
        bHasMeleeWeapon = true;
        WeaponMesh->SetVisibility(true);
        ForceNetUpdate();
    }
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
        if (HeroMesh)
        {
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
                GetMesh()->PlayAnimation(Locomotion, true);
            }
            else
            {
                UE_LOG(LogCoopRPGLocomotion, Warning, TEXT("Missing or incompatible locomotion %s for %s"),
                    LocomotionPath, *GetNameSafe(HeroMesh));
            }
        }
        SetPresentationTint(GetWorldTimerManager().IsTimerActive(HitFlashTimer));
    }
    BodyMesh->SetVisibility(!HeroMesh);
    HeadMesh->SetVisibility(!HeroMesh);
    UE_LOG(LogCoopRPGEquipment, Log, TEXT("HeroAppearance PlayerId=%d Hero=%s Mesh=%s Role=%s"),
        Identity->GetPlayerId(), *UEnum::GetValueAsString(Identity->GetSelectedHero()), *GetNameSafe(HeroMesh),
        *UEnum::GetValueAsString(GetLocalRole()));
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
        AbilitySystem->GiveAbility(FGameplayAbilitySpec(UCoopRPGMeleeAbility::StaticClass()));
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
    else
    {
        UE_LOG(LogCoopRPGInput, Verbose, TEXT("CancelIgnored Actor=%s Reason=NoActiveAttack %s"),
            *GetNameSafe(this), *CoopRPG::Debug::DescribePlayerRequestContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void ACoopRPGCharacter::OnRep_MeleeWeapon()
{
    WeaponMesh->SetVisibility(bHasMeleeWeapon);
    const APlayerState* State = GetPlayerState();
    UE_LOG(LogCoopRPGEquipment, Log, TEXT("MeleeWeaponReplicated Actor=%s Equipped=%d EquippedPlayerId=%d %s"),
        *GetNameSafe(this), bHasMeleeWeapon ? 1 : 0, State ? State->GetPlayerId() : INDEX_NONE,
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGCharacter::UpdateLocomotion(float, FVector, FVector)
{
    UAnimSingleNodeInstance* Animation = GetMesh()->GetSingleNodeInstance();
    if (!Animation)
    {
        return;
    }

    // The owner and simulated proxies both have movement velocity after collision resolution.
    const float GroundSpeed = GetCharacterMovement()->Velocity.Size2D();
    Animation->SetBlendSpacePosition(FVector(GroundSpeed, 0.0f, 0.0f));

    if (UE_LOG_ACTIVE(LogCoopRPGLocomotion, VeryVerbose))
    {
        FVector Input, Filtered;
        Animation->GetBlendSpaceState(Input, Filtered);
        UE_LOG(LogCoopRPGLocomotion, VeryVerbose,
            TEXT("HeroLocomotion Slot=%d Role=%s Asset=%s Speed=%.2f Blend=%.2f Time=%.3f Yaw=%.1f"),
            DisplaySlot, *UEnum::GetValueAsString(GetLocalRole()), *GetNameSafe(Animation->GetCurrentAsset()),
            GroundSpeed, Filtered.X, Animation->GetCurrentTime(), GetActorRotation().Yaw);
    }
}

void ACoopRPGCharacter::OnRep_DisplaySlot()
{
    RestorePresentationTint();
    UE_LOG(LogCoopRPGEquipment, Log, TEXT("PlayerDisplaySlot Actor=%s Slot=%d %s"),
        *GetNameSafe(this), DisplaySlot, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
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
        if (GetMesh()->GetSkeletalMeshAsset())
        {
            for (int32 Index = 0; Index < GetMesh()->GetNumMaterials(); ++Index)
            {
                GetMesh()->SetMaterial(Index, bDarkened ? PresentationMaterial.Get() : nullptr);
            }
        }
    }
}

void ACoopRPGCharacter::RestorePresentationTint()
{
    SetPresentationTint(false);
}
