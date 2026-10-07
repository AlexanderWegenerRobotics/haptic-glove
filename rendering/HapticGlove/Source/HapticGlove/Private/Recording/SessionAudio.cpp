#include "Recording/SessionAudio.h"

#include "AudioCaptureCore.h"
#include "Engine/World.h"
#include "HapticGloveLog.h"
#include "HapticGloveSettings.h"
#include "ISubmixBufferListener.h"
#include "Sound/SoundSubmix.h"

class FSubmixTap : public ISubmixBufferListener
{
public:
	explicit FSubmixTap(TSharedPtr<FPcmSink, ESPMode::ThreadSafe> InSink) : Sink(MoveTemp(InSink)) {}

	/** Forward each rendered buffer of the submix to the sink. */
	virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override
	{
		Sink->Push(AudioData, NumSamples, NumChannels, SampleRate);
	}

	/** Name shown by the audio mixer for this listener. */
	const FString& GetListenerName() const
	{
		static const FString Name(TEXT("HapticGloveSessionRecorder"));
		return Name;
	}

private:
	TSharedPtr<FPcmSink, ESPMode::ThreadSafe> Sink;
};

FSessionAudio::FSessionAudio()
	: GameSink(MakeShared<FPcmSink, ESPMode::ThreadSafe>())
	, MicSink(MakeShared<FPcmSink, ESPMode::ThreadSafe>())
{
}

FSessionAudio::~FSessionAudio()
{
	Stop();
}

void FSessionAudio::Start(UWorld* World, const FString& Directory, const FString& Suffix, bool bMicrophone)
{
	GamePath = Directory / FString::Printf(TEXT("audio_game%s.wav"), *Suffix);
	MicPath = Directory / FString::Printf(TEXT("audio_mic%s.wav"), *Suffix);
	Device = World ? World->GetAudioDevice() : FAudioDeviceHandle();
	if (FAudioDevice* AudioDevice = Device.GetAudioDevice())
	{
		Tap = MakeShared<FSubmixTap, ESPMode::ThreadSafe>(GameSink);
		AudioDevice->RegisterSubmixBufferListener(Tap.ToSharedRef(), AudioDevice->GetMainSubmixObject());
	}
	else
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Recording: no audio device, game audio is not recorded"));
	}
	if (!bMicrophone)
	{
		return;
	}
	Mic = MakeUnique<Audio::FAudioCapture>();
	Audio::FAudioCaptureDeviceParams Params;
	const FString& Wanted = GetDefault<UHapticGloveSettings>()->MicrophoneName;
	TArray<Audio::FCaptureDeviceInfo> Devices;
	Mic->GetCaptureDevicesAvailable(Devices);
	for (int32 i = 0; i < Devices.Num(); ++i)
	{
		const bool bPick = !Wanted.IsEmpty() && Params.DeviceIndex == INDEX_NONE && Devices[i].DeviceName.Contains(Wanted);
		if (bPick)
		{
			Params.DeviceIndex = i;
		}
		UE_LOG(LogHapticGlove, Log, TEXT("Recording: input %d '%s'%s"), i, *Devices[i].DeviceName, bPick ? TEXT(" <- used") : TEXT(""));
	}
	if (!Wanted.IsEmpty() && Params.DeviceIndex == INDEX_NONE)
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Recording: no input matches '%s', using the Windows default"), *Wanted);
	}
	Params.PCMAudioEncoding = Audio::EPCMAudioEncoding::FLOATING_POINT_32;
	TSharedPtr<FPcmSink, ESPMode::ThreadSafe> Sink = MicSink;
	const bool bOpened = Mic->OpenAudioCaptureStream(Params, [Sink](const void* Data, int32 NumFrames, int32 NumChannels, int32 SampleRate, double StreamTime, bool bOverflow)
	{
		Sink->Push(static_cast<const float*>(Data), NumFrames * NumChannels, NumChannels, SampleRate);
	}, 1024);
	if (bOpened && Mic->StartStream())
	{
		UE_LOG(LogHapticGlove, Log, TEXT("Recording: microphone open"));
	}
	else
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Recording: could not open the default microphone"));
		Mic.Reset();
	}
}

void FSessionAudio::Flush()
{
	GameSink->Flush(GameWav, GamePath);
	MicSink->Flush(MicWav, MicPath);
}

void FSessionAudio::Stop()
{
	if (Tap.IsValid())
	{
		if (FAudioDevice* AudioDevice = Device.GetAudioDevice())
		{
			AudioDevice->UnregisterSubmixBufferListener(Tap.ToSharedRef(), AudioDevice->GetMainSubmixObject());
		}
		Tap.Reset();
	}
	if (Mic)
	{
		Mic->StopStream();
		Mic->CloseStream();
		Mic.Reset();
	}
	Flush();
	GameWav.Close();
	MicWav.Close();
	Device = FAudioDeviceHandle();
}

double FSessionAudio::GameStart() const
{
	return GameSink->FirstSampleTime();
}

double FSessionAudio::MicStart() const
{
	return MicSink->FirstSampleTime();
}
