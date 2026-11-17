#include "CoopRPGCharacter.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Abilities/CoopRPGMeleeAbility.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Debug/CoopRPGCombatProbeComponent.h"

#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
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

ACoopRPGCharacter::ACoopRPGCharacter()
{
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->MaxWalkSpeed = 300.0f;

    auto* Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(GetRootComponent());
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetRelativeScale3D(FVector(0.60f, 0.60f, 1.20f));
    Body->SetRelativeLocation(FVector(0.0f, 0.0f, -18.0f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Body->SetStaticMesh(Cylinder.Object);

    auto* Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
    Head->SetupAttachment(GetRootComponent());
    Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Head->SetRelativeScale3D(FVector(0.48f));
    Head->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Head->SetStaticMesh(Sphere.Object);

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
}

float ACoopRPGCharacter::GetHealth() const
{
    return CombatAttributes->GetHealth();
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

void ACoopRPGCharacter::BeginPlay()
{
    Super::BeginPlay();
    AbilitySystem->InitAbilityActorInfo(this, this);
    if (HasAuthority())
    {
        AbilitySystem->GiveAbility(FGameplayAbilitySpec(UCoopRPGMeleeAbility::StaticClass()));
    }
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
