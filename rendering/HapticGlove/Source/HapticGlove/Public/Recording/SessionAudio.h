#pragma once

#include "CoreMinimal.h"
#include "AudioDevice.h"
#include "Recording/MediaIO.h"

class FSubmixTap;
class UWorld;

namespace Audio
{
	class FAudioCapture;
}

class HAPTICGLOVE_API FSessionAudio
{
public:
	FSessionAudio();
	~FSessionAudio();

	/** Record the main submix and, optionally, the microphone into audio_game<Suffix>.wav and audio_mic<Suffix>.wav in Directory. */
	void Start(UWorld* World, const FString& Directory, const FString& Suffix, bool bMicrophone);

	/** Write the audio that arrived since the last call. */
	void Flush();

	/** Stop both sources and close the files. */
	void Stop();

	/** Platform time of the first game audio sample, 0 when none arrived. */
	double GameStart() const;

	/** Platform time of the first microphone sample, 0 when none arrived. */
	double MicStart() const;

	FString GamePath;
	FString MicPath;

private:
	FAudioDeviceHandle Device;
	TSharedPtr<FPcmSink, ESPMode::ThreadSafe> GameSink;
	TSharedPtr<FPcmSink, ESPMode::ThreadSafe> MicSink;
	TSharedPtr<FSubmixTap, ESPMode::ThreadSafe> Tap;
	TUniquePtr<Audio::FAudioCapture> Mic;
	FWavWriter GameWav;
	FWavWriter MicWav;
};
