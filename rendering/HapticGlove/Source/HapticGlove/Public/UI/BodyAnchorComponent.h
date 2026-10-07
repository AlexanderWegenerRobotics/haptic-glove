#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "BodyAnchorComponent.generated.h"

class UCameraComponent;

UCLASS(ClassGroup = (HapticGlove), meta = (BlueprintSpawnableComponent))
class HAPTICGLOVE_API UBodyAnchorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UBodyAnchorComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float Azimuth = -50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor", meta = (ClampMin = "-90.0", ClampMax = "90.0"))
	float Elevation = -20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor", meta = (ClampMin = "10.0"))
	float Distance = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	bool bAlwaysVisible = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float RevealAngle = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "0.0"))
	float HideAngleMargin = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "0.1"))
	float FadeSpeed = 5.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Visibility")
	float Visibility = 0.0f;

	/** Angle in degrees between the head direction and the direction to the anchor. */
	UFUNCTION(BlueprintPure, Category = "Anchor")
	float ViewAngle() const { return LastViewAngle; }

private:
	/** Fade, show and enable the attached widget components according to Visibility. */
	void ApplyVisibility();

	TWeakObjectPtr<UCameraComponent> Camera;
	float LastViewAngle = 180.0f;
	bool bShown = false;
};
