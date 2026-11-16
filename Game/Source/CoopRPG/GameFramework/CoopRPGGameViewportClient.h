#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "Net/Core/Connection/NetEnums.h"
#include "CoopRPGGameViewportClient.generated.h"

class UNetDriver;

UCLASS()
class UCoopRPGGameViewportClient : public UGameViewportClient
{
    GENERATED_BODY()

public:
    void ClearConnectionNotice();
    bool HasConnectionNotice() const { return !Notice.IsEmpty(); }

protected:
    virtual void Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice) override;
    virtual void DetachViewportClient() override;
    virtual void PostRender(UCanvas* Canvas) override;

private:
    void OnConnectionFailure(UWorld* FailureWorld, UNetDriver* Driver, ENetworkFailure::Type Failure, const FString& Error);

private:
    FDelegateHandle NetworkFailureHandle;
    FString Notice;
    FString NoticeReason;
    bool bNoticeDrawn = false;
};
