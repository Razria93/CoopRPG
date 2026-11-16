#include "CoopRPGCharacter.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Abilities/CoopRPGMeleeAbility.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"

#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGInput, Log, All);

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
}

UAbilitySystemComponent* ACoopRPGCharacter::GetAbilitySystemComponent() const
{
    return AbilitySystem;
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
