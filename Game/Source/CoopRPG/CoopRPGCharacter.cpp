#include "CoopRPGCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "UObject/ConstructorHelpers.h"

ACoopRPGCharacter::ACoopRPGCharacter()
{
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->MaxWalkSpeed = 500.0f;

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
}

void ACoopRPGCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACoopRPGCharacter::Move);
    }
}

void ACoopRPGCharacter::Move(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
    // The fixed camera looks along world +X. Screen right is world +Y.
    AddMovementInput(FVector::ForwardVector, Axis.Y);
    AddMovementInput(FVector::RightVector, Axis.X);
}
