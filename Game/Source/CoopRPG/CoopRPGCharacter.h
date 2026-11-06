#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CoopRPGCharacter.generated.h"

class UInputAction;
struct FInputActionValue;

UCLASS()
class COOPRPG_API ACoopRPGCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ACoopRPGCharacter();

protected:
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
    UPROPERTY()
    TObjectPtr<UInputAction> MoveAction;

    void Move(const FInputActionValue& Value);
};
