#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "Net/Core/Connection/NetEnums.h"
#include "Styling/SlateBrush.h"
#include "CoopRPGGameViewportClient.generated.h"

class SWidget;
class UNetDriver;
class UTexture2D;

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
    void OnPreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel);
    void ShowTravelScreen(const FString& Title);
    void UpdateTravelScreen();
    void ClearTravelScreen(const TCHAR* Reason);
    void OnTravelFailure(UWorld* FailureWorld, ETravelFailure::Type Failure, const FString& Error);
    void OnConnectionFailure(UWorld* FailureWorld, UNetDriver* Driver, ENetworkFailure::Type Failure, const FString& Error);

private:
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> TravelSkin;

    FSlateBrush TravelIcon;
    FSlateBrush TravelDivider;
    TSharedPtr<SWidget> TravelScreen;
    TWeakObjectPtr<UWorld> DepartingWorld;
    FString TravelDestination;
    double CameraReadyAt = 0.0;

    FDelegateHandle PreClientTravelHandle;
    FDelegateHandle TravelFailureHandle;
    FDelegateHandle NetworkFailureHandle;
    FString Notice;
    FString NoticeReason;
    bool bNoticeDrawn = false;
};
