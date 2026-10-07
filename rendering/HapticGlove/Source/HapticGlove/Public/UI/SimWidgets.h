#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Networking/SimProtocol.h"
#include "SimWidgets.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UProgressBar;
class UTextBlock;
class UTexture2D;
class USimLinkSubsystem;

UCLASS(Abstract)
class HAPTICGLOVE_API USimWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	USimWidgetBase(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TMap<ESimMode, FLinearColor> ModeColors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Update", meta = (ClampMin = "0.0"))
	float UpdateInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "1.0"))
	float HoverScale = 1.06f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HoverBlink = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "0.1"))
	float BlinkRate = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float PressScale = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "0.05"))
	float PressDuration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	bool bClickSound = true;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Update the bound elements from the latest sim status. */
	virtual void Refresh(const FSimStatus& Status) {}

	/** The sim link of this world, cached after the first lookup. */
	USimLinkSubsystem* Link();

	/** Palette color for a mode, white when the mode has no entry. */
	FLinearColor ModeColor(ESimMode Mode) const;

	/** Display name of a mode. */
	static FText ModeName(ESimMode Mode);

private:
	/** Animate hover blink and press pop on every button of this widget. */
	void AnimateButtons(float DeltaTime);

	/** Pop the clicked button and play the click sound. */
	UFUNCTION()
	void HandleAnyButtonClicked();

	struct FButtonFeedback
	{
		TWeakObjectPtr<UButton> Button;
		float PressAge = -1.0f;
		float Hover = 0.0f;
	};

	TArray<FButtonFeedback> ButtonFeedback;
	TWeakObjectPtr<USimLinkSubsystem> CachedLink;
	float SinceUpdate = 0.0f;
	float Clock = 0.0f;
};

UCLASS(Abstract)
class HAPTICGLOVE_API UStatusPillWidget : public USimWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ModeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> ModeIndicator;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TrialText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TimeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> LatencyText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HandText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HintText;

protected:
	virtual void Refresh(const FSimStatus& Status) override;
};

UCLASS(Abstract)
class HAPTICGLOVE_API UStopButtonWidget : public USimWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> StopButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StopLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CalibrateButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> CalibrateLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FText StopText = FText::FromString(TEXT("STOP"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FText ResumeText = FText::FromString(TEXT("START"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FText CalibrateText = FText::FromString(TEXT("CALIBRATE"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLinearColor StopColor = FLinearColor(0.75f, 0.08f, 0.06f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLinearColor ResumeColor = FLinearColor(0.1f, 0.55f, 0.2f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<UTexture2D> StopImage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<UTexture2D> StartImage;

protected:
	virtual void NativeConstruct() override;
	virtual void Refresh(const FSimStatus& Status) override;

private:
	/** Stop when running, resume when stopped. */
	UFUNCTION()
	void HandleStopClicked();

	/** Start the calibration countdown. */
	UFUNCTION()
	void HandleCalibrateClicked();

	/** Show the stop or start image on all button states, or tint the background when no images are set. */
	void ApplyStopLook(bool bStopped);

	int32 ShownLook = -1;
};

UCLASS(Abstract)
class HAPTICGLOVE_API UTrayWidget : public USimWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ResetButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> NewSceneButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> HandButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> DebugButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HandText;

protected:
	virtual void NativeConstruct() override;
	virtual void Refresh(const FSimStatus& Status) override;

private:
	/** Reset objects and hand to the start. */
	UFUNCTION()
	void HandleResetClicked();

	/** Draw a new random scene. */
	UFUNCTION()
	void HandleNewSceneClicked();

	/** Switch between the two hands. */
	UFUNCTION()
	void HandleHandClicked();

	/** Show or hide the debug panel. */
	UFUNCTION()
	void HandleDebugClicked();
};

UCLASS(Abstract)
class HAPTICGLOVE_API UDebugPanelWidget : public USimWidgetBase
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> RateValue;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> LossValue;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> LatencyValue;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SimTimeValue;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> ThumbClosure;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> IndexClosure;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> MiddleClosure;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> ThumbFeedback;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> IndexFeedback;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> MiddleFeedback;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "0.01"))
	float FullScaleFeedback = 2.0f;

protected:
	virtual void Refresh(const FSimStatus& Status) override;
};

UCLASS(Abstract)
class HAPTICGLOVE_API UCountdownWidget : public USimWidgetBase
{
	GENERATED_BODY()

public:
	UCountdownWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> CountText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinOpacity = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "1"))
	int32 MaxShown = 3;

protected:
	virtual void Refresh(const FSimStatus& Status) override;
};

UCLASS(Abstract)
class HAPTICGLOVE_API UDwellCursorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> Ring;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> Bar;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FName ProgressParameter = TEXT("Progress");

	/** Show the dwell progress from 0 to 1 on the ring material and the bar. */
	UFUNCTION(BlueprintCallable, Category = "Gaze")
	void SetProgress(float Progress);

	/** Called with every progress update, for designer effects. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Gaze")
	void OnProgress(float Progress);

protected:
	virtual void NativeConstruct() override;

private:
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMaterial;
};
