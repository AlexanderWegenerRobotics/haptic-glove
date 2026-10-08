#include "Recording/SessionRecorder.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HapticGloveLog.h"
#include "HapticGloveSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Networking/SimLinkSubsystem.h"
#include "RHIGPUReadback.h"
#include "Recording/LogCameraComponent.h"
#include "Recording/MediaIO.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"
#include <atomic>

struct FPendingFrame
{
	TSharedPtr<FRHIGPUTextureReadback> Readback;
	int32 Count = 0;
};

struct FVideoStream
{
	FString Name;
	TWeakObjectPtr<ULogCameraComponent> Camera;
	int32 Width = 0;
	int32 Height = 0;
	FString RawPath;
	FString FinalPath;
	FVideoPipe Pipe;
	TQueue<TSharedPtr<TArray<FColor>>, EQueueMode::Mpsc> Frames;
	std::atomic<int32> Queued{0};
	std::atomic<bool> bRunning{true};
	TFuture<void> Worker;
	TArray<FPendingFrame> Readbacks;
	TSharedPtr<TArray<FColor>> Last;
	int64 Written = 0;
};

namespace
{
	/** Seconds since the Unix epoch. */
	double UnixNow()
	{
		return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
	}

	/** Encoder thread: write queued frames to ffmpeg until the stream is stopped and drained, then finish the file. */
	void RunEncoder(TSharedPtr<FVideoStream> Stream)
	{
		const int32 Bytes = Stream->Width * Stream->Height * sizeof(FColor);
		TSharedPtr<TArray<FColor>> Frame;
		while (Stream->bRunning || !Stream->Frames.IsEmpty())
		{
			if (!Stream->Frames.Dequeue(Frame))
			{
				FPlatformProcess::Sleep(0.002f);
				continue;
			}
			Stream->Queued--;
			if (Frame.IsValid() && Frame->Num() * static_cast<int32>(sizeof(FColor)) == Bytes)
			{
				Stream->Pipe.Write(reinterpret_cast<const uint8*>(Frame->GetData()), Bytes);
				Stream->Written++;
			}
		}
		Stream->Pipe.Close();
	}
}

bool USessionRecorder::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USessionRecorder::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USessionRecorder, STATGROUP_Tickables);
}

void USessionRecorder::Deinitialize()
{
	Stop();
	for (TFuture<void>& Task : Finishing)
	{
		Task.Wait();
	}
	Finishing.Reset();
	Super::Deinitialize();
}

void USessionRecorder::RegisterCamera(ULogCameraComponent* Camera)
{
	Cameras.AddUnique(Camera);
}

void USessionRecorder::UnregisterCamera(ULogCameraComponent* Camera)
{
	Cameras.Remove(Camera);
}

void USessionRecorder::Tick(float DeltaTime)
{
	const UHapticGloveSettings* Settings = GetDefault<UHapticGloveSettings>();
	if (!Link)
	{
		Link = GetWorld()->GetSubsystem<USimLinkSubsystem>();
	}
	if (!Settings->bRecordSessions || !Link)
	{
		Stop();
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const FSimStatus Status = Link->GetStatus();
	const bool bSimStopped = Status.bConnected && Status.Mode == ESimMode::Stopped;
	if (Link->HasScene())
	{
		const FSimScene& Scene = Link->GetSceneRef();
		const bool bNewSession = !Scene.SessionId.IsEmpty() && Scene.SessionId != SessionId;
		const bool bResume = bPausedForStop && !bRecording && Scene.SessionId == SessionId && Status.bConnected && !bSimStopped;
		if (bNewSession)
		{
			Stop();
			SessionId = Scene.SessionId;
			bPausedForStop = bSimStopped;
		}
		if ((bNewSession && !bSimStopped) || bResume)
		{
			Start(Scene);
			bPausedForStop = false;
		}
	}
	if (!bRecording)
	{
		return;
	}
	if (bSimStopped)
	{
		UE_LOG(LogHapticGlove, Log, TEXT("Recording: sim stopped, closing take %d of session %s"), Take, *SessionId);
		Stop();
		bPausedForStop = true;
		return;
	}
	if (Status.bConnected)
	{
		LastConnected = Now;
		if (Status.Trial != LastTrial)
		{
			LastTrial = Status.Trial;
			Chapters.Emplace(Now - StartTime, LastTrial);
		}
	}
	else if (Now - LastConnected > Settings->SessionEndTimeout)
	{
		UE_LOG(LogHapticGlove, Log, TEXT("Recording: sim gone for %.0f s, closing session %s"), Settings->SessionEndTimeout, *SessionId);
		Stop();
		return;
	}
	AudioLog->Flush();
	CaptureFrames(Now, Status);
	DrainReadbacks(false);
}

void USessionRecorder::Start(const FSimScene& Scene)
{
	const UHapticGloveSettings* Settings = GetDefault<UHapticGloveSettings>();
	SessionId = Scene.SessionId;
	Directory = Scene.SessionDirectory.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Sessions") / SessionId : Scene.SessionDirectory;
	Directory = FPaths::ConvertRelativePathToFull(Directory);
	IFileManager::Get().MakeDirectory(*Directory, true);

	Fps = FMath::Max(1, Settings->VideoFps);
	Take = 1;
	while (TakeExists())
	{
		++Take;
	}
	const FString Suffix = TakeSuffix();
	FrameIndex = 0;
	LastTrial = INDEX_NONE;
	Chapters.Reset();
	Captures.Reset();
	Streams.Reset();

	for (const TWeakObjectPtr<ULogCameraComponent>& Weak : Cameras)
	{
		ULogCameraComponent* Camera = Weak.Get();
		if (!Camera || !Camera->bLogVideo)
		{
			continue;
		}
		TSharedPtr<FVideoStream> Stream = MakeShared<FVideoStream>();
		Stream->Name = Camera->StreamName;
		Stream->Camera = Camera;
		Stream->Width = Camera->Width;
		Stream->Height = Camera->Height;
		Stream->RawPath = Directory / FString::Printf(TEXT("video_%s%s.raw.mp4"), *Stream->Name, *Suffix);
		Stream->FinalPath = Directory / FString::Printf(TEXT("video_%s%s.mp4"), *Stream->Name, *Suffix);
		Camera->GetLogTarget();
		if (!Stream->Pipe.Open(Stream->RawPath, Stream->Width, Stream->Height, Fps))
		{
			UE_LOG(LogHapticGlove, Error, TEXT("Recording: could not start ffmpeg for stream '%s' (%s)"), *Stream->Name, *MediaIO::FfmpegPath());
			continue;
		}
		Stream->Worker = Async(EAsyncExecution::Thread, [Stream]() { RunEncoder(Stream); });
		Streams.Add(Stream);
	}

	AudioLog = MakeUnique<FSessionAudio>();
	AudioLog->Start(GetWorld(), Directory, Suffix, Settings->bRecordMicrophone);
	StartTime = FPlatformTime::Seconds();
	StartUnix = UnixNow();
	LastConnected = StartTime;
	bRecording = true;
	UE_LOG(LogHapticGlove, Log, TEXT("Recording: session %s, %d video stream(s) at %d fps -> %s"), *SessionId, Streams.Num(), Fps, *Directory);
}

FString USessionRecorder::TakeSuffix() const
{
	return Take > 1 ? FString::Printf(TEXT("_%d"), Take) : FString();
}

bool USessionRecorder::TakeExists() const
{
	const FString Suffix = TakeSuffix();
	IFileManager& Files = IFileManager::Get();
	if (Files.FileExists(*(Directory / FString::Printf(TEXT("audio_game%s.wav"), *Suffix))))
	{
		return true;
	}
	for (const TWeakObjectPtr<ULogCameraComponent>& Weak : Cameras)
	{
		if (const ULogCameraComponent* Camera = Weak.Get())
		{
			if (Files.FileExists(*(Directory / FString::Printf(TEXT("video_%s%s.json"), *Camera->StreamName, *Suffix))))
			{
				return true;
			}
		}
	}
	return false;
}

void USessionRecorder::CaptureFrames(double Now, const FSimStatus& Status)
{
	const int64 Due = static_cast<int64>((Now - StartTime) * Fps) + 1;
	const int32 Count = static_cast<int32>(Due - FrameIndex);
	if (Count <= 0)
	{
		return;
	}
	for (const TSharedPtr<FVideoStream>& Stream : Streams)
	{
		ULogCameraComponent* Camera = Stream->Camera.Get();
		UTextureRenderTarget2D* Target = Camera ? Camera->GetLogTarget() : nullptr;
		if (!Target)
		{
			Stream->Readbacks.Add({nullptr, Count});
			continue;
		}
		Camera->CaptureScene();
		TSharedPtr<FRHIGPUTextureReadback> Readback = MakeShared<FRHIGPUTextureReadback>(TEXT("SessionRecorder"));
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		ENQUEUE_RENDER_COMMAND(SessionRecorderReadback)([Readback, Resource](FRHICommandListImmediate& RHICmdList)
		{
			Readback->EnqueueCopy(RHICmdList, Resource->GetRenderTargetTexture());
		});
		Stream->Readbacks.Add({Readback, Count});
	}
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetNumberField(TEXT("frame"), static_cast<double>(FrameIndex));
	Entry->SetNumberField(TEXT("count"), Count);
	Entry->SetNumberField(TEXT("unix"), StartUnix + (Now - StartTime));
	Entry->SetNumberField(TEXT("trial"), Status.Trial);
	Entry->SetNumberField(TEXT("sim_time"), Status.SimTime);
	Captures.Add(MakeShared<FJsonValueObject>(Entry));
	FrameIndex = Due;
}

void USessionRecorder::DrainReadbacks(bool bWait)
{
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	for (const TSharedPtr<FVideoStream>& Stream : Streams)
	{
		while (Stream->Readbacks.Num() > 0)
		{
			FPendingFrame Pending = Stream->Readbacks[0];
			if (Pending.Readback.IsValid() && !Pending.Readback->IsReady())
			{
				if (!bWait || FPlatformTime::Seconds() > Deadline)
				{
					break;
				}
				FlushRenderingCommands();
				FPlatformProcess::Sleep(0.001f);
				continue;
			}
			Stream->Readbacks.RemoveAt(0);
			if (Pending.Readback.IsValid())
			{
				int32 RowPitch = 0;
				const FColor* Source = static_cast<const FColor*>(Pending.Readback->Lock(RowPitch));
				if (Source && RowPitch >= Stream->Width)
				{
					TSharedPtr<TArray<FColor>> Frame = MakeShared<TArray<FColor>>();
					Frame->SetNumUninitialized(Stream->Width * Stream->Height);
					for (int32 Row = 0; Row < Stream->Height; ++Row)
					{
						FMemory::Memcpy(Frame->GetData() + Row * Stream->Width, Source + Row * RowPitch, Stream->Width * sizeof(FColor));
					}
					Stream->Last = Frame;
				}
				Pending.Readback->Unlock();
			}
			if (!Stream->Last.IsValid())
			{
				Stream->Last = MakeShared<TArray<FColor>>();
				Stream->Last->SetNumZeroed(Stream->Width * Stream->Height);
			}
			for (int32 i = 0; i < Pending.Count; ++i)
			{
				Stream->Frames.Enqueue(Stream->Last);
				Stream->Queued++;
			}
			if (Stream->Queued > 10 * Fps)
			{
				UE_LOG(LogHapticGlove, Warning, TEXT("Recording: encoder for '%s' is %d frames behind"), *Stream->Name, Stream->Queued.load());
			}
		}
	}
}

FString USessionRecorder::WriteSidecars(double Duration)
{
	FString Chapter = TEXT(";FFMETADATA1\n");
	for (int32 i = 0; i < Chapters.Num(); ++i)
	{
		const double End = i + 1 < Chapters.Num() ? Chapters[i + 1].Key : Duration;
		Chapter += FString::Printf(TEXT("[CHAPTER]\nTIMEBASE=1/1000\nSTART=%lld\nEND=%lld\ntitle=Trial %d\n"),
			static_cast<int64>(Chapters[i].Key * 1000.0), static_cast<int64>(FMath::Max(End, Chapters[i].Key) * 1000.0), Chapters[i].Value);
	}
	const FString ChapterPath = Directory / FString::Printf(TEXT("chapters%s.txt"), *TakeSuffix());
	FFileHelper::SaveStringToFile(Chapter, *ChapterPath);

	TArray<TSharedPtr<FJsonValue>> ChapterJson;
	for (const TPair<double, int32>& C : Chapters)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("trial"), C.Value);
		Entry->SetNumberField(TEXT("start_s"), C.Key);
		Entry->SetNumberField(TEXT("start_unix"), StartUnix + C.Key);
		ChapterJson.Add(MakeShared<FJsonValueObject>(Entry));
	}
	const double GameStart = AudioLog->GameStart();
	const double MicStart = AudioLog->MicStart();
	for (const TSharedPtr<FVideoStream>& Stream : Streams)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("session_id"), SessionId);
		Root->SetStringField(TEXT("stream"), Stream->Name);
		Root->SetStringField(TEXT("video"), FPaths::GetCleanFilename(Stream->FinalPath));
		Root->SetNumberField(TEXT("take"), Take);
		Root->SetNumberField(TEXT("fps"), Fps);
		Root->SetNumberField(TEXT("width"), Stream->Width);
		Root->SetNumberField(TEXT("height"), Stream->Height);
		Root->SetNumberField(TEXT("frames"), static_cast<double>(FrameIndex));
		Root->SetNumberField(TEXT("start_unix"), StartUnix);
		Root->SetNumberField(TEXT("game_audio_offset_s"), GameStart > 0.0 ? GameStart - StartTime : 0.0);
		Root->SetNumberField(TEXT("mic_offset_s"), MicStart > 0.0 ? MicStart - StartTime : 0.0);
		Root->SetArrayField(TEXT("chapters"), ChapterJson);
		Root->SetArrayField(TEXT("captures"), Captures);
		FString Json;
		FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
		FFileHelper::SaveStringToFile(Json, *(Directory / FString::Printf(TEXT("video_%s%s.json"), *Stream->Name, *TakeSuffix())));
	}

	const UHapticGloveSettings* Settings = GetDefault<UHapticGloveSettings>();
	const bool bGame = GameStart > 0.0 && FPaths::FileExists(AudioLog->GamePath);
	const bool bMic = MicStart > 0.0 && FPaths::FileExists(AudioLog->MicPath);
	FString Inputs;
	int32 Index = 1;
	FString Filter;
	if (bGame)
	{
		Inputs += FString::Printf(TEXT(" -itsoffset %.4f -i \"%s\""), GameStart - StartTime, *AudioLog->GamePath);
		Filter += FString::Printf(TEXT("[%d:a]volume=%.1fdB[game];"), Index++, Settings->GameAudioGainDb);
	}
	if (bMic)
	{
		Inputs += FString::Printf(TEXT(" -itsoffset %.4f -i \"%s\""), MicStart - StartTime, *AudioLog->MicPath);
		const FString Speech = Settings->bEnhanceSpeech
			? FString::Printf(TEXT("highpass=f=100,agate=threshold=%.5f:range=0.001:ratio=10:attack=5:release=%d,dynaudnorm=f=150:g=15:m=10:p=0.35,"),
				FMath::Pow(10.0f, Settings->NoiseGateThresholdDb / 20.0f), FMath::RoundToInt(Settings->NoiseGateReleaseMs))
			: FString();
		Filter += FString::Printf(TEXT("[%d:a]%svolume=%.1fdB[voice];"), Index++, *Speech, Settings->MicGainDb);
	}
	Inputs += FString::Printf(TEXT(" -i \"%s\" -map_metadata %d -map_chapters %d -map 0:v"), *ChapterPath, Index, Index);
	if (bGame && bMic)
	{
		Filter += TEXT("[game][voice]amix=inputs=2:duration=longest:dropout_transition=0,volume=2,alimiter=limit=0.95[mix]");
		Inputs += FString::Printf(TEXT(" -filter_complex \"%s\" -map \"[mix]\""), *Filter);
	}
	else if (bGame)
	{
		Filter += TEXT("[game]alimiter=limit=0.95[mix]");
		Inputs += FString::Printf(TEXT(" -filter_complex \"%s\" -map \"[mix]\""), *Filter);
	}
	else if (bMic)
	{
		Filter += TEXT("[voice]anull[mix]");
		Inputs += FString::Printf(TEXT(" -filter_complex \"%s\" -map \"[mix]\""), *Filter);
	}
	return Inputs;
}

void USessionRecorder::Stop()
{
	if (!bRecording)
	{
		return;
	}
	bRecording = false;
	FlushRenderingCommands();
	DrainReadbacks(true);
	AudioLog->Stop();
	const double Duration = static_cast<double>(FrameIndex) / Fps;
	const FString AudioArgs = WriteSidecars(Duration);
	const bool bKeep = GetDefault<UHapticGloveSettings>()->bKeepIntermediates;
	const FString ChapterPath = Directory / FString::Printf(TEXT("chapters%s.txt"), *TakeSuffix());
	const FString GamePath = AudioLog->GamePath;
	const FString MicPath = AudioLog->MicPath;
	TArray<TSharedPtr<FVideoStream>> Done = MoveTemp(Streams);
	for (const TSharedPtr<FVideoStream>& Stream : Done)
	{
		Stream->bRunning = false;
	}
	UE_LOG(LogHapticGlove, Log, TEXT("Recording: session %s stopped after %.1f s, muxing in the background"), *SessionId, Duration);

	Finishing.Add(Async(EAsyncExecution::Thread, [Done, AudioArgs, bKeep, ChapterPath, GamePath, MicPath]()
	{
		bool bAllMuxed = Done.Num() > 0;
		for (const TSharedPtr<FVideoStream>& Stream : Done)
		{
			Stream->Worker.Wait();
			FString Output;
			const FString Args = FString::Printf(TEXT("-y -hide_banner -nostdin -loglevel error -i \"%s\"%s -c:v copy -c:a aac -b:a 160k -movflags +faststart \"%s\""),
				*Stream->RawPath, *AudioArgs, *Stream->FinalPath);
			const int32 Code = MediaIO::RunFfmpeg(Args, Output);
			if (Code == 0)
			{
				UE_LOG(LogHapticGlove, Log, TEXT("Recording: %s (%lld frames)"), *Stream->FinalPath, Stream->Written);
				if (!bKeep)
				{
					IFileManager::Get().Delete(*Stream->RawPath);
				}
			}
			else
			{
				bAllMuxed = false;
				UE_LOG(LogHapticGlove, Error, TEXT("Recording: mux failed for %s (exit %d): %s"), *Stream->RawPath, Code, *Output.TrimStartAndEnd().Left(500));
			}
		}
		if (bAllMuxed && !bKeep)
		{
			IFileManager::Get().Delete(*GamePath);
			IFileManager::Get().Delete(*MicPath);
			IFileManager::Get().Delete(*ChapterPath);
		}
	}));
	AudioLog.Reset();
}
