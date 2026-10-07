#include "Recording/MediaIO.h"

#include "HAL/FileManager.h"
#include "HapticGloveLog.h"
#include "HapticGloveSettings.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* EncoderCandidates[] = {
		TEXT("-c:v h264_nvenc -preset p4 -tune ll -rc vbr -cq 23 -b:v 0 -pix_fmt yuv420p"),
		TEXT("-c:v h264_nvenc -preset llhq -rc vbr_hq -cq 23 -b:v 0 -pix_fmt yuv420p"),
		TEXT("-c:v h264_mf -hw_encoding true -b:v 8M -pix_fmt yuv420p"),
		TEXT("-c:v libx264 -preset veryfast -crf 23 -threads 4 -pix_fmt yuv420p"),
	};
}

FString MediaIO::FfmpegPath()
{
	const FString& Configured = GetDefault<UHapticGloveSettings>()->FfmpegPath;
	if (!Configured.IsEmpty() && FPaths::FileExists(Configured))
	{
		return Configured;
	}
	const FString Bundled = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("ThirdParty/ffmpeg/ffmpeg.exe"));
	return FPaths::FileExists(Bundled) ? Bundled : FString(TEXT("ffmpeg"));
}

int32 MediaIO::RunFfmpeg(const FString& Args, FString& Output)
{
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		Output = TEXT("CreatePipe failed");
		return -1;
	}
	FProcHandle Proc = FPlatformProcess::CreateProc(*FfmpegPath(), *Args, false, true, true, nullptr, 0, nullptr, WritePipe);
	if (!Proc.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		Output = FString::Printf(TEXT("could not start '%s'"), *FfmpegPath());
		return -1;
	}
	while (FPlatformProcess::IsProcRunning(Proc))
	{
		Output += FPlatformProcess::ReadPipe(ReadPipe);
		FPlatformProcess::Sleep(0.01f);
	}
	Output += FPlatformProcess::ReadPipe(ReadPipe);
	int32 Code = -1;
	FPlatformProcess::GetProcReturnCode(Proc, &Code);
	FPlatformProcess::CloseProc(Proc);
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	return Code;
}

const FString& MediaIO::VideoEncoderArgs()
{
	static FString Selected;
	if (!Selected.IsEmpty())
	{
		return Selected;
	}
	for (const TCHAR* Candidate : EncoderCandidates)
	{
		FString Output;
		const FString Probe = FString::Printf(TEXT("-hide_banner -nostdin -loglevel error -f lavfi -i color=c=black:s=256x256:d=0.1 -r 30 %s -f null -"), Candidate);
		if (RunFfmpeg(Probe, Output) == 0)
		{
			Selected = Candidate;
			UE_LOG(LogHapticGlove, Log, TEXT("Recording: video encoder '%s'"), Candidate);
			return Selected;
		}
		UE_LOG(LogHapticGlove, Log, TEXT("Recording: encoder rejected '%s': %s"), Candidate, *Output.TrimStartAndEnd().Left(300));
	}
	Selected = EncoderCandidates[UE_ARRAY_COUNT(EncoderCandidates) - 1];
	UE_LOG(LogHapticGlove, Warning, TEXT("Recording: no encoder passed the probe, is ffmpeg at '%s'?"), *FfmpegPath());
	return Selected;
}

FVideoPipe::~FVideoPipe()
{
	Close();
}

bool FVideoPipe::Open(const FString& Path, int32 Width, int32 Height, int32 Fps)
{
	if (!FPlatformProcess::CreatePipe(ChildRead, ParentWrite, true))
	{
		return false;
	}
	const FString Args = FString::Printf(TEXT("-y -hide_banner -loglevel error -f rawvideo -pix_fmt bgra -s %dx%d -r %d -i - %s -movflags +faststart \"%s\""),
		Width, Height, Fps, *MediaIO::VideoEncoderArgs(), *Path);
	Process = FPlatformProcess::CreateProc(*MediaIO::FfmpegPath(), *Args, false, true, true, nullptr, 0, nullptr, nullptr, ChildRead);
	if (!Process.IsValid())
	{
		FPlatformProcess::ClosePipe(ChildRead, ParentWrite);
		ChildRead = ParentWrite = nullptr;
		return false;
	}
	return true;
}

bool FVideoPipe::Write(const uint8* Data, int32 Bytes)
{
	int32 Total = 0;
	while (ParentWrite && Total < Bytes)
	{
		int32 Written = 0;
		if (!FPlatformProcess::WritePipe(ParentWrite, Data + Total, Bytes - Total, &Written) || Written <= 0)
		{
			return false;
		}
		Total += Written;
	}
	return Total == Bytes;
}

void FVideoPipe::Close()
{
	if (ParentWrite || ChildRead)
	{
		FPlatformProcess::ClosePipe(ChildRead, ParentWrite);
		ChildRead = ParentWrite = nullptr;
	}
	if (Process.IsValid())
	{
		const double Deadline = FPlatformTime::Seconds() + 30.0;
		while (FPlatformProcess::IsProcRunning(Process) && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.02f);
		}
		if (FPlatformProcess::IsProcRunning(Process))
		{
			FPlatformProcess::TerminateProc(Process);
		}
		FPlatformProcess::CloseProc(Process);
	}
}

FWavWriter::~FWavWriter()
{
	Close();
}

bool FWavWriter::Open(const FString& Path, int32 Channels, int32 SampleRate)
{
	Archive.Reset(IFileManager::Get().CreateFileWriter(*Path));
	if (!Archive)
	{
		return false;
	}
	NumChannels = Channels;
	Rate = SampleRate;
	DataBytes = 0;
	WriteHeader();
	return true;
}

void FWavWriter::Write(const TArray<int16>& Samples)
{
	if (Archive && Samples.Num() > 0)
	{
		Archive->Serialize(const_cast<int16*>(Samples.GetData()), Samples.Num() * sizeof(int16));
		DataBytes += Samples.Num() * sizeof(int16);
	}
}

void FWavWriter::Close()
{
	if (!Archive)
	{
		return;
	}
	Archive->Seek(0);
	WriteHeader();
	Archive->Close();
	Archive.Reset();
}

void FWavWriter::WriteHeader()
{
	auto Put32 = [this](uint32 Value) { Archive->Serialize(&Value, 4); };
	auto Put16 = [this](uint16 Value) { Archive->Serialize(&Value, 2); };
	auto PutTag = [this](const char* Tag) { Archive->Serialize(const_cast<char*>(Tag), 4); };
	const uint32 Data = static_cast<uint32>(FMath::Min<int64>(DataBytes, MAX_uint32 - 36));
	PutTag("RIFF");
	Put32(36 + Data);
	PutTag("WAVE");
	PutTag("fmt ");
	Put32(16);
	Put16(1);
	Put16(static_cast<uint16>(NumChannels));
	Put32(static_cast<uint32>(Rate));
	Put32(static_cast<uint32>(Rate * NumChannels * 2));
	Put16(static_cast<uint16>(NumChannels * 2));
	Put16(16);
	PutTag("data");
	Put32(Data);
}

void FPcmSink::Push(const float* Data, int32 NumSamples, int32 InChannels, int32 SampleRate)
{
	if (!Data || NumSamples <= 0 || InChannels <= 0 || SampleRate <= 0)
	{
		return;
	}
	FScopeLock Lock(&Mutex);
	if (FirstTime == 0.0)
	{
		FirstTime = FPlatformTime::Seconds() - static_cast<double>(NumSamples / InChannels) / SampleRate;
		Channels = InChannels;
		Rate = SampleRate;
	}
	if (InChannels != Channels)
	{
		return;
	}
	const int32 Start = Pending.AddUninitialized(NumSamples);
	for (int32 i = 0; i < NumSamples; ++i)
	{
		Pending[Start + i] = static_cast<int16>(FMath::Clamp(Data[i] * 32767.0f, -32768.0f, 32767.0f));
	}
}

void FPcmSink::Flush(FWavWriter& Writer, const FString& Path)
{
	TArray<int16> Ready;
	int32 FlushChannels = 0;
	int32 FlushRate = 0;
	{
		FScopeLock Lock(&Mutex);
		Swap(Ready, Pending);
		FlushChannels = Channels;
		FlushRate = Rate;
	}
	if (Ready.Num() == 0)
	{
		return;
	}
	if (!Writer.IsOpen() && !Writer.Open(Path, FlushChannels, FlushRate))
	{
		return;
	}
	Writer.Write(Ready);
}

double FPcmSink::FirstSampleTime() const
{
	FScopeLock Lock(&Mutex);
	return FirstTime;
}
