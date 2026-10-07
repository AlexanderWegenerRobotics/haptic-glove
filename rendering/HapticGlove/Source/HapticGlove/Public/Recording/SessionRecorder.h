#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Subsystems/WorldSubsystem.h"
#include "Networking/SimProtocol.h"
#include "Recording/SessionAudio.h"
#include "SessionRecorder.generated.h"

class ULogCameraComponent;
class USimLinkSubsystem;
struct FVideoStream;

UCLASS()
class HAPTICGLOVE_API USessionRecorder : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Log this camera as its own video stream from the next session on. */
	void RegisterCamera(ULogCameraComponent* Camera);

	/** Stop logging this camera, e.g. when its actor ends play. */
	void UnregisterCamera(ULogCameraComponent* Camera);

	/** True while a session is being recorded. */
	UFUNCTION(BlueprintPure, Category = "Recording")
	bool IsRecording() const { return bRecording; }

	/** Folder of the current or last recorded session. */
	UFUNCTION(BlueprintPure, Category = "Recording")
	FString GetSessionDirectory() const { return Directory; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Open one video pipe per camera and start the audio recording for a new sim session. */
	void Start(const FSimScene& Scene);

	/** Finish the raw files and mux video, audio and trial chapters into one mp4 per camera in the background. */
	void Stop();

	/** Capture every camera when the next frame is due, repeating frames after a hitch to keep a constant rate. */
	void CaptureFrames(double Now, const FSimStatus& Status);

	/** Hand finished GPU readbacks to the encoder threads; with bWait, block until all are done. */
	void DrainReadbacks(bool bWait);

	/** Write the chapter file and per stream metadata, then return the shared mux input arguments. */
	FString WriteSidecars(double Duration);

	/** File name suffix of the current take: empty for the first recording of a session, _2, _3 after that. */
	FString TakeSuffix() const;

	/** True when files of the current take already exist in the session folder. */
	bool TakeExists() const;

	TArray<TWeakObjectPtr<ULogCameraComponent>> Cameras;
	TArray<TSharedPtr<FVideoStream>> Streams;
	TUniquePtr<FSessionAudio> AudioLog;
	TArray<TFuture<void>> Finishing;
	TArray<TPair<double, int32>> Chapters;
	TArray<TSharedPtr<FJsonValue>> Captures;

	UPROPERTY() TObjectPtr<USimLinkSubsystem> Link;

	FString SessionId;
	FString Directory;
	bool bRecording = false;
	int32 Fps = 30;
	int32 Take = 1;
	int64 FrameIndex = 0;
	int32 LastTrial = INDEX_NONE;
	double StartTime = 0.0;
	double StartUnix = 0.0;
	double LastConnected = 0.0;
};
