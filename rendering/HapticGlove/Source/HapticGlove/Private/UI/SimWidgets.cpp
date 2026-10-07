#include "UI/SimWidgets.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"
#include "EngineUtils.h"
#include "HapticGloveLog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Networking/SimLinkSubsystem.h"
#include "Player/OperatorPawn.h"

USimWidgetBase::USimWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ModeColors.Add(ESimMode::Disconnected, FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
	ModeColors.Add(ESimMode::Waiting, FLinearColor(0.95f, 0.65f, 0.1f, 1.0f));
	ModeColors.Add(ESimMode::Engaged, FLinearColor(0.15f, 0.8f, 0.3f, 1.0f));
	ModeColors.Add(ESimMode::Stopped, FLinearColor(0.85f, 0.1f, 0.08f, 1.0f));
}

void USimWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();
	ButtonFeedback.Reset();
	WidgetTree->ForEachWidget([this](UWidget* Widget)
	{
		if (UButton* Button = Cast<UButton>(Widget))
		{
			Button->OnClicked.AddUniqueDynamic(this, &USimWidgetBase::HandleAnyButtonClicked);
			ButtonFeedback.Add({Button});
		}
	});
}

void USimWidgetBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	AnimateButtons(InDeltaTime);
	SinceUpdate += InDeltaTime;
	if (SinceUpdate < UpdateInterval)
	{
		return;
	}
	SinceUpdate = 0.0f;
	if (USimLinkSubsystem* Sim = Link())
	{
		Refresh(Sim->GetStatus());
	}
}

void USimWidgetBase::AnimateButtons(float DeltaTime)
{
	Clock += DeltaTime;
	const float Blink = 0.5f + 0.5f * FMath::Cos(2.0f * UE_PI * BlinkRate * Clock);
	for (FButtonFeedback& Entry : ButtonFeedback)
	{
		UButton* Button = Entry.Button.Get();
		if (!Button)
		{
			continue;
		}
		const bool bHovered = Button->IsHovered() && Button->GetIsEnabled();
		Entry.Hover = FMath::FInterpConstantTo(Entry.Hover, bHovered ? 1.0f : 0.0f, DeltaTime, 8.0f);
		float Scale = FMath::Lerp(1.0f, HoverScale, Entry.Hover);
		if (Entry.PressAge >= 0.0f)
		{
			Entry.PressAge += DeltaTime;
			const float T = FMath::Clamp(Entry.PressAge / PressDuration, 0.0f, 1.0f);
			Scale *= FMath::Lerp(PressScale, 1.0f, FMath::InterpEaseOut(0.0f, 1.0f, T, 2.0f));
			if (T >= 1.0f)
			{
				Entry.PressAge = -1.0f;
			}
		}
		Button->SetRenderScale(FVector2D(Scale));
		Button->SetRenderOpacity(1.0f - HoverBlink * Entry.Hover * Blink);
	}
}

void USimWidgetBase::HandleAnyButtonClicked()
{
	for (FButtonFeedback& Entry : ButtonFeedback)
	{
		if (Entry.Button.IsValid() && Entry.Button->IsHovered())
		{
			Entry.PressAge = 0.0f;
		}
	}
	if (bClickSound)
	{
		if (USimLinkSubsystem* Sim = Link())
		{
			Sim->PlayUiSound(EUiSound::Click);
		}
	}
}

USimLinkSubsystem* USimWidgetBase::Link()
{
	if (!CachedLink.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			CachedLink = World->GetSubsystem<USimLinkSubsystem>();
		}
	}
	return CachedLink.Get();
}

FLinearColor USimWidgetBase::ModeColor(ESimMode Mode) const
{
	const FLinearColor* Color = ModeColors.Find(Mode);
	return Color ? *Color : FLinearColor::White;
}

FText USimWidgetBase::ModeName(ESimMode Mode)
{
	switch (Mode)
	{
	case ESimMode::Waiting:
		return FText::FromString(TEXT("WAITING"));
	case ESimMode::Engaged:
		return FText::FromString(TEXT("ENGAGED"));
	case ESimMode::Stopped:
		return FText::FromString(TEXT("STOPPED"));
	default:
		return FText::FromString(TEXT("NO SIM"));
	}
}

void UStatusPillWidget::Refresh(const FSimStatus& Status)
{
	const FLinearColor Color = ModeColor(Status.Mode);
	ModeText->SetText(ModeName(Status.Mode));
	ModeText->SetColorAndOpacity(FSlateColor(Color));
	if (ModeIndicator)
	{
		ModeIndicator->SetColorAndOpacity(Color);
	}
	if (TrialText)
	{
		TrialText->SetText(FText::FromString(FString::Printf(TEXT("Trial %d"), Status.Trial)));
	}
	if (TimeText)
	{
		const int32 Seconds = FMath::FloorToInt(Status.SimTime);
		TimeText->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60)));
	}
	if (LatencyText)
	{
		LatencyText->SetText(FText::FromString(Status.bConnected ? FString::Printf(TEXT("%.1f ms"), Status.LatencyMs) : FString(TEXT("-- ms"))));
	}
	if (HandText)
	{
		HandText->SetText(FText::FromString(Status.Hand.Replace(TEXT("_"), TEXT(" "))));
	}
	if (HintText)
	{
		FString Hint;
		if (Status.Countdown > 0.0f)
		{
			Hint = FString::Printf(TEXT("Calibrating in %d - look forward, hold your hand at rest"), FMath::CeilToInt(Status.Countdown));
		}
		else if (Status.bConnected && !Status.bCalibrated)
		{
			Hint = TEXT("Not calibrated");
		}
		else if (Status.bConnected && !Status.bTracked && Status.Mode != ESimMode::Stopped)
		{
			Hint = TEXT("Tracking lost");
		}
		HintText->SetText(FText::FromString(Hint));
		HintText->SetVisibility(Hint.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UStopButtonWidget::NativeConstruct()
{
	Super::NativeConstruct();
	StopButton->OnClicked.AddUniqueDynamic(this, &UStopButtonWidget::HandleStopClicked);
	if (CalibrateButton)
	{
		CalibrateButton->OnClicked.AddUniqueDynamic(this, &UStopButtonWidget::HandleCalibrateClicked);
	}
}

void UStopButtonWidget::Refresh(const FSimStatus& Status)
{
	const bool bStopped = Status.Mode == ESimMode::Stopped;
	StopButton->SetIsEnabled(Status.bConnected);
	if (ShownLook != static_cast<int32>(bStopped))
	{
		ApplyStopLook(bStopped);
	}
	if (StopLabel)
	{
		StopLabel->SetText(bStopped ? ResumeText : StopText);
	}
	if (CalibrateButton)
	{
		CalibrateButton->SetIsEnabled(Status.bConnected && Status.Countdown <= 0.0f);
	}
	if (CalibrateLabel)
	{
		CalibrateLabel->SetText(Status.Countdown > 0.0f ? FText::AsNumber(FMath::CeilToInt(Status.Countdown)) : CalibrateText);
	}
}

void UStopButtonWidget::HandleStopClicked()
{
	if (USimLinkSubsystem* Sim = Link())
	{
		Sim->ToggleStop();
	}
}

void UStopButtonWidget::ApplyStopLook(bool bStopped)
{
	ShownLook = static_cast<int32>(bStopped);
	UTexture2D* Image = bStopped ? StartImage.Get() : StopImage.Get();
	if (!Image)
	{
		StopButton->SetBackgroundColor(bStopped ? ResumeColor : StopColor);
		return;
	}
	StopButton->SetBackgroundColor(FLinearColor::White);
	FButtonStyle Style = StopButton->GetStyle();
	const FVector2D Size(Image->GetSizeX(), Image->GetSizeY());
	for (FSlateBrush* Brush : {&Style.Normal, &Style.Hovered, &Style.Pressed, &Style.Disabled})
	{
		Brush->SetResourceObject(Image);
		Brush->ImageSize = Size;
		Brush->DrawAs = ESlateBrushDrawType::Image;
	}
	Style.Pressed.TintColor = FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f));
	Style.Disabled.TintColor = FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 0.6f));
	StopButton->SetStyle(Style);
}

void UStopButtonWidget::HandleCalibrateClicked()
{
	if (USimLinkSubsystem* Sim = Link())
	{
		Sim->Calibrate();
	}
}

void UTrayWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ResetButton)
	{
		ResetButton->OnClicked.AddUniqueDynamic(this, &UTrayWidget::HandleResetClicked);
	}
	if (NewSceneButton)
	{
		NewSceneButton->OnClicked.AddUniqueDynamic(this, &UTrayWidget::HandleNewSceneClicked);
	}
	if (HandButton)
	{
		HandButton->OnClicked.AddUniqueDynamic(this, &UTrayWidget::HandleHandClicked);
	}
	if (DebugButton)
	{
		DebugButton->OnClicked.AddUniqueDynamic(this, &UTrayWidget::HandleDebugClicked);
	}
}

void UTrayWidget::Refresh(const FSimStatus& Status)
{
	for (UButton* Button : {ResetButton.Get(), NewSceneButton.Get(), HandButton.Get()})
	{
		if (Button)
		{
			Button->SetIsEnabled(Status.bConnected);
		}
	}
	if (HandText)
	{
		HandText->SetText(FText::FromString(Status.Hand.Replace(TEXT("_"), TEXT(" "))));
	}
}

void UTrayWidget::HandleResetClicked()
{
	if (USimLinkSubsystem* Sim = Link())
	{
		Sim->SendCommand(TEXT("reset_same"));
	}
}

void UTrayWidget::HandleNewSceneClicked()
{
	if (USimLinkSubsystem* Sim = Link())
	{
		Sim->SendCommand(TEXT("reset_new"));
	}
}

void UTrayWidget::HandleHandClicked()
{
	if (USimLinkSubsystem* Sim = Link())
	{
		Sim->SetHand(FString());
	}
}

void UTrayWidget::HandleDebugClicked()
{
	AOperatorPawn* Pawn = Cast<AOperatorPawn>(GetOwningPlayerPawn());
	if (!Pawn && GetWorld())
	{
		TActorIterator<AOperatorPawn> It(GetWorld());
		Pawn = It ? *It : nullptr;
	}
	if (!Pawn)
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Debug button: no operator pawn found"));
		return;
	}
	Pawn->ToggleDebugPanel();
}

void UDebugPanelWidget::Refresh(const FSimStatus& Status)
{
	if (RateValue)
	{
		RateValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f Hz"), Status.PacketRate)));
	}
	if (LossValue)
	{
		LossValue->SetText(FText::FromString(FString::Printf(TEXT("%d lost/s"), Status.PacketsLost)));
	}
	if (LatencyValue)
	{
		LatencyValue->SetText(FText::FromString(FString::Printf(TEXT("%.2f ms"), Status.LatencyMs)));
	}
	if (SimTimeValue)
	{
		SimTimeValue->SetText(FText::FromString(FString::Printf(TEXT("%.1f s"), Status.SimTime)));
	}
	UProgressBar* Closure[3] = {ThumbClosure, IndexClosure, MiddleClosure};
	UProgressBar* Feedback[3] = {ThumbFeedback, IndexFeedback, MiddleFeedback};
	for (int32 i = 0; i < 3; ++i)
	{
		if (Closure[i] && Status.Closure.IsValidIndex(i))
		{
			Closure[i]->SetPercent(FMath::Clamp(Status.Closure[i], 0.0f, 1.0f));
		}
		if (Feedback[i] && Status.Feedback.IsValidIndex(i))
		{
			Feedback[i]->SetPercent(FMath::Clamp(Status.Feedback[i] / FullScaleFeedback, 0.0f, 1.0f));
		}
	}
}

UCountdownWidget::UCountdownWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UpdateInterval = 0.0f;
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCountdownWidget::Refresh(const FSimStatus& Status)
{
	const float Second = FMath::CeilToFloat(Status.Countdown);
	if (Status.Countdown <= 0.0f || Second > static_cast<float>(MaxShown))
	{
		SetRenderOpacity(0.0f);
		return;
	}
	CountText->SetText(FText::AsNumber(static_cast<int32>(Second)));
	SetRenderOpacity(FMath::Lerp(MinOpacity, 1.0f, 1.0f - (Second - Status.Countdown)));
}

void UDwellCursorWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Ring)
	{
		RingMaterial = Ring->GetDynamicMaterial();
	}
}

void UDwellCursorWidget::SetProgress(float Progress)
{
	if (RingMaterial)
	{
		RingMaterial->SetScalarParameterValue(ProgressParameter, Progress);
	}
	if (Bar)
	{
		Bar->SetPercent(Progress);
	}
	OnProgress(Progress);
}
