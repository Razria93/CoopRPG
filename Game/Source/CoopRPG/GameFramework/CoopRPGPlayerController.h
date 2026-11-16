#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CoopRPGPlayerController.generated.h"

UCLASS()
class COOPRPG_API ACoopRPGPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    virtual void ReceivedPlayer() override;
    virtual bool ShouldFlushKeysWhenViewportFocusChanges() const override;
    virtual void FlushPressedKeys() override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;

private:
    // Local validation only; never replicated and disabled in Shipping/Test.
    bool bManualInput = false;
    bool bAwaitFocusClick = false;
    bool bConsumeFocusClickRelease = false;
};
