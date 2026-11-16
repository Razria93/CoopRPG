#include "CoopRPGGameViewportClient.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGConnectionNotice, Log, All);

void UCoopRPGGameViewportClient::ClearConnectionNotice()
{
    Notice.Reset();
    NoticeReason.Reset();
    bNoticeDrawn = false;
}

void UCoopRPGGameViewportClient::Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice)
{
    Super::Init(WorldContext, OwningGameInstance, bCreateNewAudioDevice);
    NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::OnConnectionFailure);
}

void UCoopRPGGameViewportClient::DetachViewportClient()
{
    if (GEngine && NetworkFailureHandle.IsValid())
    {
        GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
        NetworkFailureHandle.Reset();
    }
    Super::DetachViewportClient();
}

void UCoopRPGGameViewportClient::PostRender(UCanvas* Canvas)
{
    Super::PostRender(Canvas);
    if (!Canvas || Notice.IsEmpty())
    {
        return;
    }

    const float Height = 55.0f;
    const FVector2D Position(16.0f, FMath::Max(0.0f, Canvas->ClipY - Height - 16.0f));
    FCanvasTileItem Background(Position, FVector2D(FMath::Max(0.0f, Canvas->ClipX - 32.0f), Height),
        FLinearColor(0.02f, 0.04f, 0.08f, 0.94f));
    Background.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Background);
    FCanvasTextItem Title(Position + FVector2D(12.0f, 8.0f), FText::FromString(Notice), GEngine->GetMediumFont(), FLinearColor::White);
    Canvas->DrawItem(Title);
    FCanvasTextItem Hint(Position + FVector2D(12.0f, 32.0f), FText::FromString(TEXT("Open the client again to try another session.")),
        GEngine->GetSmallFont(), FLinearColor(0.65f, 0.8f, 0.94f));
    Canvas->DrawItem(Hint);
    if (!bNoticeDrawn)
    {
        bNoticeDrawn = true;
        UE_LOG(LogCoopRPGConnectionNotice, Log, TEXT("ConnectionNoticeDisplayed Reason=%s"), *NoticeReason);
    }
}

void UCoopRPGGameViewportClient::OnConnectionFailure(UWorld* FailureWorld, UNetDriver* Driver, ENetworkFailure::Type Failure, const FString& Error)
{
    if (!Driver || (Driver->NetDriverName != NAME_PendingNetDriver
        && (Driver->NetDriverName != NAME_GameNetDriver || Driver->GetNetMode() != NM_Client)))
    {
        return;
    }

    const FWorldContext* Context = FailureWorld ? GEngine->GetWorldContextFromWorld(FailureWorld)
        : GEngine->GetWorldContextFromPendingNetGameNetDriver(Driver);
    if (!Context || Context->GameViewport != this || !Notice.IsEmpty())
    {
        return;
    }

    if (Error == TEXT("RoundInProgress"))
    {
        Notice = TEXT("This round is already in progress.");
        NoticeReason = Error;
    }
    else if (Error == TEXT("RoundFull"))
    {
        Notice = TEXT("The server already has two players.");
        NoticeReason = Error;
    }
    else if (Error == TEXT("RoundTravelReserved"))
    {
        Notice = TEXT("The current players are moving to the next map.");
        NoticeReason = Error;
    }
    else if (Error == TEXT("RoundFinished"))
    {
        Notice = TEXT("This round has ended.");
        NoticeReason = Error;
    }
    else if (Error == TEXT("UnsupportedJoin"))
    {
        Notice = TEXT("Join with one player per client.");
        NoticeReason = Error;
    }
    else if (Failure == ENetworkFailure::ConnectionLost || Failure == ENetworkFailure::ConnectionTimeout
        || (Driver->NetDriverName == NAME_GameNetDriver && Failure == ENetworkFailure::FailureReceived))
    {
        Notice = TEXT("Connection to the server ended.");
        NoticeReason = TEXT("ConnectionEnded");
    }
    else
    {
        Notice = TEXT("Could not connect to the server.");
        NoticeReason = TEXT("ConnectionFailed");
    }
    UE_LOG(LogCoopRPGConnectionNotice, Log, TEXT("ConnectionNoticeQueued Reason=%s Failure=%s"),
        *NoticeReason, ENetworkFailure::ToString(Failure));
}
