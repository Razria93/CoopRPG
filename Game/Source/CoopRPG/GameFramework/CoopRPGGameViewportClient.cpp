#include "CoopRPGGameViewportClient.h"

#include "CoopRPGExpeditionHUDText.h"
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
    using namespace CoopRPG::ExpeditionUI;
    Text(*Canvas, Notice, Position + FVector2D(12.0f, 5.0f), Font(13.0f, true), FLinearColor::White);
    Text(*Canvas, TEXT("다른 세션에 참가하려면 게임을 다시 실행해 주세요."), Position + FVector2D(12.0f, 30.0f),
        Font(11.5f), FLinearColor(0.65f, 0.8f, 0.94f));
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
        Notice = TEXT("이미 진행 중인 스테이지예요.");
        NoticeReason = Error;
    }
    else if (Error == TEXT("RoundFull"))
    {
        Notice = TEXT("서버에 이미 두 사람이 참가했어요.");
        NoticeReason = Error;
    }
    else if (Error == TEXT("RoundTravelReserved"))
    {
        Notice = TEXT("파티가 다음 스테이지로 이동 중이에요.");
        NoticeReason = Error;
    }
    else if (Error == TEXT("RoundFinished"))
    {
        Notice = TEXT("이미 끝난 스테이지예요.");
        NoticeReason = Error;
    }
    else if (Error == TEXT("UnsupportedJoin"))
    {
        Notice = TEXT("한 클라이언트에 한 명씩 참가해 주세요.");
        NoticeReason = Error;
    }
    else if (Driver->NetDriverName == NAME_GameNetDriver
        && (Failure == ENetworkFailure::ConnectionLost || Failure == ENetworkFailure::ConnectionTimeout
            || Failure == ENetworkFailure::FailureReceived))
    {
        Notice = TEXT("서버와의 연결이 끊겼어요.");
        NoticeReason = TEXT("ConnectionEnded");
    }
    else
    {
        Notice = TEXT("서버에 접속할 수 없어요.");
        NoticeReason = TEXT("ConnectionFailed");
    }
    UE_LOG(LogCoopRPGConnectionNotice, Log, TEXT("ConnectionNoticeQueued Reason=%s Failure=%s"),
        *NoticeReason, ENetworkFailure::ToString(Failure));
}
