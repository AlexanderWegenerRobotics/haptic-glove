#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

namespace MediaIO
{
	/** Path of the ffmpeg executable: project setting, then ThirdParty/ffmpeg in the project, then PATH. */
	FString FfmpegPath();

	/** Run ffmpeg to completion and return its exit code; Output collects what it printed. */
	int32 RunFfmpeg(const FString& Args, FString& Output);

	/** Video encoder arguments this machine accepts, probed once: NVENC, Media Foundation, then x264. */
	const FString& VideoEncoderArgs();
}

class HAPTICGLOVE_API FVideoPipe
{
public:
	~FVideoPipe();

	/** Start ffmpeg reading raw BGRA frames from stdin and encoding them into an mp4. */
	bool Open(const FString& Path, int32 Width, int32 Height, int32 Fps);

	/** Write one frame; blocks while ffmpeg catches up. */
	bool Write(const uint8* Data, int32 Bytes);

	/** Close stdin and wait for ffmpeg to finish the file. */
	void Close();

private:
	FProcHandle Process;
	void* ChildRead = nullptr;
	void* ParentWrite = nullptr;
};

class HAPTICGLOVE_API FWavWriter
{
public:
	~FWavWriter();

	/** Create a 16 bit PCM wav file with a placeholder header. */
	bool Open(const FString& Path, int32 Channels, int32 SampleRate);

	/** Append interleaved samples. */
	void Write(const TArray<int16>& Samples);

	/** Patch the header sizes and close the file. */
	void Close();

	/** True while the file is open. */
	bool IsOpen() const { return Archive.IsValid(); }

private:
	/** Write the RIFF header for the current data size. */
	void WriteHeader();

	TUniquePtr<FArchive> Archive;
	int32 NumChannels = 0;
	int32 Rate = 0;
	int64 DataBytes = 0;
};

class HAPTICGLOVE_API FPcmSink
{
public:
	/** Convert float samples to 16 bit and queue them; called from audio threads. */
	void Push(const float* Data, int32 NumSamples, int32 Channels, int32 SampleRate);

	/** Move queued samples into the wav file, opening it with the format of the first buffer. */
	void Flush(FWavWriter& Writer, const FString& Path);

	/** Platform time of the first sample, 0 before any audio arrived. */
	double FirstSampleTime() const;

private:
	mutable FCriticalSection Mutex;
	TArray<int16> Pending;
	int32 Channels = 0;
	int32 Rate = 0;
	double FirstTime = 0.0;
};
