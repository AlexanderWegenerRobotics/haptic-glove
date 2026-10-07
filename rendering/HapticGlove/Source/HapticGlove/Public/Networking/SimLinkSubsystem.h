#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HapticGloveSettings.h"
#include "Networking/SimLink.h"
#include "Networking/SimProtocol.h"
#include "SimLinkSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimSceneChanged, const FSimScene&, Scene);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimTrackingChanged, const FSimTracking&, Tracking);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSimModeChanged, ESimMode, Mode, ESimMode, Previous);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSimCommandResult, const FString&, Command, bool, bAcknowledged);

UCLASS()
class HAPTICGLOVE_API USimLinkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Pull new scene and state from the link; called every tick and by actors that need the newest state first. */
	void Refresh();

	/** Send a raw command name to the sim (reset_same, reset_new, calibrate, stop, resume, set_hand, quit). */
	UFUNCTION(BlueprintCallable, Category = "Sim")
	void SendCommand(const FString& Command);

	/** Calibrate after a delay; a negative delay uses the project setting. */
	UFUNCTION(BlueprintCallable, Category = "Sim")
	void Calibrate(float Delay = -1.0f);

	/** Switch the hand model; an empty name toggles between the two. */
	UFUNCTION(BlueprintCallable, Category = "Sim")
	void SetHand(const FString& Hand);

	/** Stop when running, resume when stopped. */
	UFUNCTION(BlueprintCallable, Category = "Sim")
	void ToggleStop();

	/** Everything the UI shows, updated every frame. */
	UFUNCTION(BlueprintPure, Category = "Sim")
	FSimStatus GetStatus() const { return Status; }

	/** The scene that is currently built. */
	UFUNCTION(BlueprintPure, Category = "Sim")
	FSimScene GetScene() const { return Scene; }

	/** The current scene without a copy, for C++ users. */
	const FSimScene& GetSceneRef() const { return Scene; }

	/** Latest state packet that belongs to the current scene. */
	const FSimState& GetState() const { return State; }

	/** True once a scene message arrived. */
	bool HasScene() const { return bHasScene; }

	/** True once a state packet for the current scene arrived. */
	bool HasState() const { return bHasState; }

	UPROPERTY(BlueprintAssignable, Category = "Sim")
	FOnSimSceneChanged OnSceneChanged;

	UPROPERTY(BlueprintAssignable, Category = "Sim")
	FOnSimTrackingChanged OnTrackingChanged;

	UPROPERTY(BlueprintAssignable, Category = "Sim")
	FOnSimModeChanged OnModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Sim")
	FOnSimCommandResult OnCommandResult;

	/** Play one of the UI sounds from the project settings, unless UI sounds are off. */
	UFUNCTION(BlueprintCallable, Category = "Sim")
	void PlayUiSound(EUiSound Sound);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Send a JSON command with the next sequence number and optional extra fields. */
	void Send(const FString& Command, const FString& ExtraFields = FString());

	/** Rebuild the status struct from the latest state and link statistics. */
	void UpdateStatus();

	/** Confirm the last command once the sim echoes its sequence number, reject it after the timeout. */
	void UpdatePendingCommand();

	/** Speak the last three seconds of a calibration countdown and the delayed confirmation. */
	void SpeakCountdown();

	FSimLink Link;
	FSimScene Scene;
	FSimState State;
	FSimStatus Status;
	bool bHasState = false;
	bool bHasScene = false;
	bool bWarnedSceneMismatch = false;
	int32 CommandSeq = 0;
	int32 PendingSeq = 0;
	FString PendingCommand;
	double PendingSince = 0.0;
	bool bExpectOutage = false;
	bool bOutagePending = false;
	int32 SpokenSecond = 0;
	double DoneSoundAt = 0.0;
	double OutageSince = 0.0;

	UPROPERTY() TMap<EUiSound, TObjectPtr<USoundBase>> LoadedSounds;
};
