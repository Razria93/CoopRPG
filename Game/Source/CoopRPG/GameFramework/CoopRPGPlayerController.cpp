#include "CoopRPGPlayerController.h"

#include "CoopRPG/Debug/CoopRPGLogContext.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGManualInput, Log, All);

void ACoopRPGPlayerController::ReceivedPlayer()
{
    Super::ReceivedPlayer();
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (IsLocalController() && FParse::Param(FCommandLine::Get(), TEXT("CoopManualInput")))
    {
        bManualInput = true;
        bAwaitFocusClick = true;
        bShowMouseCursor = true;
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
        SetInputMode(Mode);
        UE_LOG(LogCoopRPGManualInput, Log, TEXT("ManualInputEnabled Cursor=Visible Lock=None FocusClick=Consumed %s"),
            *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
#endif
}

bool ACoopRPGPlayerController::ShouldFlushKeysWhenViewportFocusChanges() const
{
    return bManualInput || Super::ShouldFlushKeysWhenViewportFocusChanges();
}

void ACoopRPGPlayerController::FlushPressedKeys()
{
    const bool bHeldW = bManualInput && IsInputKeyDown(EKeys::W);
    Super::FlushPressedKeys();
    if (bManualInput)
    {
        bAwaitFocusClick = true;
        bConsumeFocusClickRelease = false;
        // The engine calls this on viewport deactivation/focus loss, including Alt+Tab.
        UE_LOG(LogCoopRPGManualInput, Log, TEXT("ManualInputFlushed Pawn=%s HeldWBefore=%d W=%d %s"),
            *GetNameSafe(GetPawn()), bHeldW ? 1 : 0, IsInputKeyDown(EKeys::W) ? 1 : 0,
            *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

bool ACoopRPGPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    if (bManualInput && Params.Key == EKeys::LeftMouseButton)
    {
        if (bAwaitFocusClick && (Params.Event == IE_Pressed || Params.Event == IE_DoubleClick))
        {
            bAwaitFocusClick = false;
            bConsumeFocusClickRelease = true;
            UE_LOG(LogCoopRPGManualInput, Log, TEXT("ManualFocusClickConsumed Pawn=%s %s"),
                *GetNameSafe(GetPawn()), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
            return true;
        }
        if (bConsumeFocusClickRelease)
        {
            if (Params.Event == IE_Released)
            {
                bConsumeFocusClickRelease = false;
            }
            return true;
        }
    }
    return Super::InputKey(Params);
}
