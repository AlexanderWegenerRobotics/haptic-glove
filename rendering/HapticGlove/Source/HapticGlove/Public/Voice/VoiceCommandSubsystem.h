#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Subsystems/WorldSubsystem.h"
#include "VoiceCommandSubsystem.generated.h"

class FSocket;
class USimLinkSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVoiceCommand, const FString&, Command, const FString&, Text);

UCLASS()
class HAPTICGLOVE_API UVoiceCommandSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Run a command as if it was spoken: stop, start, reset, new_scene, calibrate, switch_hand, debug. */
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void Execute(const FString& Command, const FString& Text = TEXT(""));

	/** True once the sidecar has loaded its model and listens. */
	UFUNCTION(BlueprintPure, Category = "Voice")
	bool IsListening() const { return bListening; }

	UPROPERTY(BlueprintAssignable, Category = "Voice")
	FOnVoiceCommand OnVoiceCommand;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Start the Python sidecar with the project settings, or log why it cannot start. */
	void Launch();

	/** Close the socket and end the sidecar. */
	void Shutdown();

	/** Handle every datagram that arrived since the last tick. */
	void Receive();

	/** Parse one JSON datagram from the sidecar. */
	void Handle(const FString& Json);

	/** Log once when the sidecar exits on its own. */
	void WatchProcess();

	FSocket* Socket = nullptr;
	FProcHandle Process;
	bool bListening = false;
	bool bReportedExit = false;
	FString LogPath;
	TArray<uint8> Buffer;

	UPROPERTY() TObjectPtr<USimLinkSubsystem> Link;
};
