#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Networking/SimProtocol.h"
#include "BodyAnchorComponent.generated.h"

class UCameraComponent;
class USimLinkSubsystem;

UENUM(BlueprintType)
enum class EAnchorMode : uint8
{
	FollowHead,
	HeadSnapshot,
	Table
};

UENUM(BlueprintType)
enum class ERevealMode : uint8
{
	LookAt,
	LookUp
};

UCLASS(ClassGroup = (HapticGlove), meta = (BlueprintSpawnableComponent))
class HAPTICGLOVE_API UBodyAnchorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UBodyAnchorComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** FollowHead keeps the anchor at a fixed angle from the head, HeadSnapshot places it once from the head, Table places it relative to the sim table. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	EAnchorMode Mode = EAnchorMode::Table;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor|Head", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float Azimuth = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor|Head", meta = (ClampMin = "-90.0", ClampMax = "90.0"))
	float Elevation = -20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor|Head", meta = (ClampMin = "10.0"))
	float Distance = 55.0f;

	/** Point on the table top in half extents: X -1 near edge (operator) to +1 far edge, Y -1 left edge to +1 right edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor|Table")
	FVector2D TableAnchor = FVector2D(-0.3, 1.0);

	/** Offset in cm from the table anchor, in the table frame: X away from the operator, Y to the right, Z up from the table top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor|Table")
	FVector TableOffset = FVector(0.0, 20.0, 30.0);

	/** Yaw of the table frame in degrees, 0 when the operator stands at the table's -X side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor|Table")
	float TableYaw = 0.0f;

	/** Turn of the panel around the vertical axis in the table frame, 0 faces the operator side straight on, positive turns its face toward the left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor|Table", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float PanelYaw = 0.0f;

	/** Tilt of the panel around its horizontal axis, 0 is upright, negative leans the top back away from the operator. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor|Table", meta = (ClampMin = "-90.0", ClampMax = "90.0"))
	float PanelPitch = 0.0f;

	/** Seconds to wait before placing, after play starts or the scene or calibration changed, so the head pose is current. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor", meta = (ClampMin = "0.0"))
	float PlaceDelay = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	bool bAlwaysVisible = false;

	/** LookAt shows the widgets while the head points at the anchor, LookUp while the head pitch is above RevealPitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	ERevealMode RevealMode = ERevealMode::LookAt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "1.0", ClampMax = "90.0", EditCondition = "RevealMode == ERevealMode::LookAt"))
	float RevealAngle = 25.0f;

	/** Head pitch in degrees above which the widgets show in LookUp mode, 0 is level, negative is looking down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "-90.0", ClampMax = "90.0", EditCondition = "RevealMode == ERevealMode::LookUp"))
	float RevealPitch = -10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "0.0"))
	float HideAngleMargin = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "0.1"))
	float FadeSpeed = 5.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Visibility")
	float Visibility = 0.0f;

	/** Place the anchor now from the current head pose and table. */
	UFUNCTION(BlueprintCallable, Category = "Anchor")
	void Place();

	/** Place the anchor again after PlaceDelay. */
	UFUNCTION(BlueprintCallable, Category = "Anchor")
	void RequestPlace();

	/** Allow or block the attached widgets from showing, independent of the reveal fade. */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void SetAnchorEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Visibility")
	bool IsAnchorEnabled() const { return bAnchorEnabled; }

	/** Angle in degrees between the head direction and the direction to the anchor. */
	UFUNCTION(BlueprintPure, Category = "Anchor")
	float ViewAngle() const { return LastViewAngle; }

	/** Head pitch in degrees, positive when looking up. */
	UFUNCTION(BlueprintPure, Category = "Anchor")
	float HeadPitch() const { return LastHeadPitch; }

private:
	UFUNCTION()
	void HandleSceneChanged(const FSimScene& Scene);

	UFUNCTION()
	void HandleTrackingChanged(const FSimTracking& Tracking);

	/** Anchor location relative to the sim table, false when no scene is known. */
	bool TableLocation(FVector& Out) const;

	/** Fade, show and enable the attached widget components according to Visibility. */
	void ApplyVisibility();

	/** Update Visibility from the angle between the head direction and the anchor. */
	void UpdateReveal(float DeltaTime);

	TWeakObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<USimLinkSubsystem> Link;
	float LastViewAngle = 180.0f;
	float LastHeadPitch = 0.0f;
	float PlaceCountdown = 0.0f;
	bool bPlaced = false;
	bool bShown = false;
	bool bAnchorEnabled = true;
};
