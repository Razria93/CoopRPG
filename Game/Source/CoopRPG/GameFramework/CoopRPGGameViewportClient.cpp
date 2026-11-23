#include "CoopRPGGameViewportClient.h"

#include "CoopRPGExpeditionDashboard.h"
#include "CoopRPGExpeditionHUDText.h"
#include "CoopRPGExpeditionMaps.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGConnectionNotice, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGTravelScreen, Log, All);

namespace
{
    constexpr double TravelRevealSeconds = 0.18;

    int32 GetDestinationStage(const FString& Destination)
    {
        using namespace CoopRPG::Expedition;
        if (Destination == LobbyPackage)
        {
            return 0;
        }
        if (Destination == SkirmishPackage)
        {
            return 1;
        }
        if (Destination == PuzzlePackage)
        {
            return 2;
        }
        return Destination == BossPackage ? 3 : INDEX_NONE;
    }

    const TCHAR* GetTravelTitle(int32 Stage, bool bRetry)
    {
        if (Stage == 0)
        {
            return TEXT("원정 대기소로 복귀");
        }
        if (bRetry)
        {
            constexpr const TCHAR* RetryTitles[] = {
                TEXT("버섯 군락 재도전"), TEXT("쌍둥이 발판 재도전"), TEXT("흑기사 결전 재도전")
            };
            return RetryTitles[Stage - 1];
        }
        return FCoopRPGExpeditionDashboard::StageTitle(Stage);
    }

    void ConfigureTravelSprite(FSlateBrush& Brush, UTexture2D& Texture, const FVector2f& Min,
        const FVector2f& Max, const FVector2f& DisplaySize)
    {
        const FVector2f AtlasSize(Texture.GetSizeX(), Texture.GetSizeY());
        Brush.SetResourceObject(&Texture);
        Brush.DrawAs = ESlateBrushDrawType::Image;
        Brush.SetImageSize(DisplaySize);
        Brush.SetUVRegion(FBox2f(Min / AtlasSize, Max / AtlasSize));
    }
}

void UCoopRPGGameViewportClient::ClearConnectionNotice()
{
    Notice.Reset();
    NoticeReason.Reset();
    bNoticeDrawn = false;
}

void UCoopRPGGameViewportClient::Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice)
{
    Super::Init(WorldContext, OwningGameInstance, bCreateNewAudioDevice);
    PreClientTravelHandle = OwningGameInstance->OnNotifyPreClientTravel().AddUObject(this, &ThisClass::OnPreClientTravel);
    TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::OnTravelFailure);
    NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::OnConnectionFailure);
}

void UCoopRPGGameViewportClient::DetachViewportClient()
{
    ClearTravelScreen(TEXT("ViewportDetached"));
    if (UGameInstance* Instance = GetGameInstance())
    {
        Instance->OnNotifyPreClientTravel().Remove(PreClientTravelHandle);
    }
    if (GEngine)
    {
        GEngine->OnTravelFailure().Remove(TravelFailureHandle);
        GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
    }
    PreClientTravelHandle.Reset();
    TravelFailureHandle.Reset();
    NetworkFailureHandle.Reset();
    Super::DetachViewportClient();
}

void UCoopRPGGameViewportClient::PostRender(UCanvas* Canvas)
{
    Super::PostRender(Canvas);
    if (!Canvas)
    {
        return;
    }

    UpdateTravelScreen();
    if (Notice.IsEmpty())
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

void UCoopRPGGameViewportClient::OnPreClientTravel(const FString& PendingURL, ETravelType, bool)
{
    UWorld* CurrentWorld = GetWorld();
    if (!CoopRPG::Expedition::IsExpedition(CurrentWorld) || CurrentWorld->GetNetMode() != NM_Client)
    {
        return;
    }

    const FURL URL(nullptr, *PendingURL, TRAVEL_Absolute);
    const FString Destination = UWorld::RemovePIEPrefix(URL.Map);
    const int32 Stage = GetDestinationStage(Destination);
    if (Stage == INDEX_NONE)
    {
        return;
    }

    ClearTravelScreen(TEXT("Replaced"));
    DepartingWorld = CurrentWorld;
    TravelDestination = Destination;
    const bool bRetry = UWorld::RemovePIEPrefix(CurrentWorld->GetPackage()->GetName()) == Destination;
    ShowTravelScreen(GetTravelTitle(Stage, bRetry));
}

void UCoopRPGGameViewportClient::ShowTravelScreen(const FString& Title)
{
    if (!TravelSkin)
    {
        TravelSkin = FImageUtils::ImportFileAsTexture2D(FPaths::Combine(
            FPaths::ProjectContentDir(), TEXT("UI/ExpeditionDashboard/DashboardSkin.png")));
        if (TravelSkin)
        {
            ConfigureTravelSprite(TravelIcon, *TravelSkin, FVector2f(284, 200), FVector2f(316, 232), FVector2f(23, 23));
            ConfigureTravelSprite(TravelDivider, *TravelSkin, FVector2f(0, 285), FVector2f(250, 293), FVector2f(214, 7));
        }
    }

    static const FSlateColorBrush Backdrop(FLinearColor(FColor(24, 33, 26)));
    static const FSlateRoundedBoxBrush Panel(FLinearColor(FColor(50, 68, 48)), 11.0f,
        FLinearColor(FColor(218, 184, 111)), 1.5f);
    const FLinearColor Gold(FColor(224, 198, 143));
    const FLinearColor Cream(FColor(255, 240, 204));
    const FLinearColor Muted(FColor(226, 226, 202));
    using CoopRPG::ExpeditionUI::Font;

    TravelScreen = SNew(SOverlay)
        .Visibility(EVisibility::HitTestInvisible)
        + SOverlay::Slot()
        [
            SNew(SImage).Image(&Backdrop)
        ]
        + SOverlay::Slot()
        [
            SNew(SScaleBox)
            .Stretch(EStretch::ScaleToFit)
            [
                SNew(SBox)
                .WidthOverride(960.0f)
                .HeightOverride(540.0f)
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(444.0f)
                    .HeightOverride(190.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(&Panel)
                        .Padding(FMargin(24.0f, 0.0f))
                        .HAlign(HAlign_Center)
                        .VAlign(VAlign_Center)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 7.0f, 0.0f)
                                [
                                    SNew(SImage).Image(&TravelIcon)
                                ]
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                [
                                    SNew(STextBlock).Text(FText::FromString(TEXT("숲 원정"))).Font(Font(15.0f, true)).ColorAndOpacity(Gold)
                                ]
                            ]
                            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 13.0f)
                            [
                                SNew(STextBlock).Text(FText::FromString(Title)).Font(Font(27.0f, true)).ColorAndOpacity(Cream)
                            ]
                            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                            [
                                SNew(SImage).Image(&TravelDivider)
                            ]
                            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 10.0f, 0.0f, 0.0f)
                            [
                                SNew(STextBlock).Text(FText::FromString(TEXT("맵 이동 중"))).Font(Font(15.0f)).ColorAndOpacity(Muted)
                            ]
                        ]
                    ]
                ]
            ]
        ];
    AddViewportWidgetContent(TravelScreen.ToSharedRef(), 100);
    UE_LOG(LogCoopRPGTravelScreen, Log, TEXT("TravelScreenShown Destination=%s Title=%s"),
        *TravelDestination, *Title);
}

void UCoopRPGGameViewportClient::UpdateTravelScreen()
{
    UWorld* CurrentWorld = GetWorld();
    if (!TravelScreen || !CurrentWorld || CurrentWorld == DepartingWorld.Get()
        || UWorld::RemovePIEPrefix(CurrentWorld->GetPackage()->GetName()) != TravelDestination)
    {
        return;
    }

    const APlayerController* Controller = CurrentWorld->GetFirstPlayerController();
    const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    if (!Controller || !Controller->IsLocalController() || !IsValid(Pawn) || Pawn->GetWorld() != CurrentWorld
        || Controller->GetViewTarget() != Pawn)
    {
        CameraReadyAt = 0.0;
        TravelScreen->SetRenderOpacity(1.0f);
        return;
    }

    const double Now = FPlatformTime::Seconds();
    if (CameraReadyAt == 0.0)
    {
        CameraReadyAt = Now;
        UE_LOG(LogCoopRPGTravelScreen, Log, TEXT("TravelCameraReady Destination=%s Pawn=%s"),
            *TravelDestination, *GetNameSafe(Pawn));
        return;
    }

    const double Progress = (Now - CameraReadyAt) / TravelRevealSeconds;
    if (Progress >= 1.0)
    {
        ClearTravelScreen(TEXT("CameraReady"));
    }
    else
    {
        TravelScreen->SetRenderOpacity(static_cast<float>(1.0 - Progress));
    }
}

void UCoopRPGGameViewportClient::ClearTravelScreen(const TCHAR* Reason)
{
    if (TravelScreen)
    {
        RemoveViewportWidgetContent(TravelScreen.ToSharedRef());
        TravelScreen.Reset();
        UE_LOG(LogCoopRPGTravelScreen, Log, TEXT("TravelScreenHidden Destination=%s Reason=%s"),
        *TravelDestination, Reason);
    }
    DepartingWorld.Reset();
    TravelDestination.Reset();
    CameraReadyAt = 0.0;
}

void UCoopRPGGameViewportClient::OnTravelFailure(UWorld* FailureWorld, ETravelFailure::Type Failure, const FString&)
{
    if (!TravelScreen || (FailureWorld && FailureWorld != GetWorld()))
    {
        return;
    }

    ClearTravelScreen(TEXT("TravelFailed"));
    if (Notice.IsEmpty())
    {
        Notice = TEXT("스테이지로 이동할 수 없어요.");
        NoticeReason = TEXT("TravelFailed");
        UE_LOG(LogCoopRPGConnectionNotice, Log, TEXT("ConnectionNoticeQueued Reason=%s Failure=%s"),
            *NoticeReason, ETravelFailure::ToString(Failure));
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
    if (!Context || Context->GameViewport != this)
    {
        return;
    }

    ClearTravelScreen(TEXT("ConnectionFailed"));
    if (!Notice.IsEmpty())
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
