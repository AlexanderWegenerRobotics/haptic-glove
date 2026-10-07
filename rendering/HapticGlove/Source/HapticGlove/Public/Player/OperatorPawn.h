#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Networking/SimProtocol.h"
#include "OperatorPawn.generated.h"

class UBodyAnchorComponent;
class UCameraComponent;
class UGazeInteractionComponent;
class USimLinkSubsystem;

UCLASS()
class HAPTICGLOVE_API AOperatorPawn : public APawn
{
	GENERATED_BODY()

public:
	AOperatorPawn();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Operator")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Operator")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Operator")
	TObjectPtr<UGazeInteractionComponent> Gaze;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Operator")
	TObjectPtr<UBodyAnchorComponent> Tray;

	/** Place the VR origin so the headset and the streamed hand share the sim calibration. */
	UFUNCTION(BlueprintCallable, Category = "Operator")
	void ApplyTracking(const FSimTracking& Tracking);

	/** Called after the VR origin moved to a new calibration. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Operator")
	void OnTrackingApplied(const FSimTracking& Tracking);

	/** Show or hide every debug panel widget component. */
	UFUNCTION(BlueprintCallable, Category = "Operator")
	void ToggleDebugPanel();

	/** Set the visibility of every widget component showing a debug panel widget or tagged "Debug". */
	UFUNCTION(BlueprintCallable, Category = "Operator")
	void SetDebugPanelVisible(bool bVisible);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Operator")
	bool bDebugPanelVisible = false;

	/** The sim link of this world, for Blueprint widgets owned by the pawn. */
	UFUNCTION(BlueprintPure, Category = "Operator")
	USimLinkSubsystem* GetSimLink() const { return Link; }

private:
	/** Follow recalibrations announced by the sim. */
	UFUNCTION()
	void HandleTrackingChanged(const FSimTracking& Tracking);

	/** Debug key: reset the scene. */
	void ResetSame();

	/** Debug key: new random scene. */
	void ResetNew();

	/** Debug key: calibrate after the configured countdown. */
	void Calibrate();

	/** Debug key: stop or resume. */
	void ToggleStop();

	/** Debug key: switch between the two hands. */
	void ToggleHand();

	UPROPERTY() TObjectPtr<USimLinkSubsystem> Link;
};
