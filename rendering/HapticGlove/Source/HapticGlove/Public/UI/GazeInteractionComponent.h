#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetInteractionComponent.h"
#include "GazeInteractionComponent.generated.h"

class UCameraComponent;
class UWidgetComponent;
class SWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGazeDwellCompleted, UWidgetComponent*, Component);

UCLASS(ClassGroup = (HapticGlove), meta = (BlueprintSpawnableComponent))
class HAPTICGLOVE_API UGazeInteractionComponent : public UWidgetInteractionComponent
{
	GENERATED_BODY()

public:
	UGazeInteractionComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze")
	bool bUseEyeTracking = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinConfidence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze|Dwell", meta = (ClampMin = "0.05"))
	float DefaultDwellTime = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze|Dwell")
	TMap<FName, float> DwellTimeByTag;

	UPROPERTY(BlueprintReadOnly, Category = "Gaze|Dwell")
	float DwellProgress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Gaze")
	bool bEyeGaze = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze|Cursor")
	FName CursorTag = TEXT("GazeCursor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze|Cursor", meta = (ClampMin = "0.0"))
	float CursorOffset = 0.5f;

	UPROPERTY(BlueprintAssignable, Category = "Gaze|Dwell")
	FOnGazeDwellCompleted OnDwellCompleted;

	/** World point the gaze ray hits on a widget, or the ray end when nothing is hit. */
	UFUNCTION(BlueprintPure, Category = "Gaze")
	FVector GetGazePoint() const;

private:
	/** Aim the interaction ray along the eye gaze, falling back to the head direction. */
	void UpdateRay();

	/** Advance the dwell timer on the hovered widget and click it when the time is reached. */
	void UpdateDwell(float DeltaTime);

	/** Place the dwell cursor on the gazed widget and show the progress, hidden while idle. */
	void UpdateCursor();

	/** Dwell time for a widget component: the first matching component tag, else the default. */
	float DwellTimeFor(const UWidgetComponent* Component) const;

	TWeakObjectPtr<UCameraComponent> Camera;
	TWeakObjectPtr<UWidgetComponent> Cursor;
	TWeakPtr<SWidget> DwellTarget;
	float DwellElapsed = 0.0f;
	bool bFired = false;
};
