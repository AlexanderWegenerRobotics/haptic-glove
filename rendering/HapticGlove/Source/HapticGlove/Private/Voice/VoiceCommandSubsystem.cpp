#include "Voice/VoiceCommandSubsystem.h"

#include "Common/UdpSocketBuilder.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HapticGloveLog.h"
#include "HapticGloveSettings.h"
#include "Misc/Paths.h"
#include "Networking/SimLinkSubsystem.h"
#include "Player/OperatorPawn.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

bool UVoiceCommandSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UVoiceCommandSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVoiceCommandSubsystem, STATGROUP_Tickables);
}

void UVoiceCommandSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Link = InWorld.GetSubsystem<USimLinkSubsystem>();
	if (GetDefault<UHapticGloveSettings>()->bVoiceCommands)
	{
		Launch();
	}
}

void UVoiceCommandSubsystem::Deinitialize()
{
	Shutdown();
	Super::Deinitialize();
}

void UVoiceCommandSubsystem::Launch()
{
	const UHapticGloveSettings* Settings = GetDefault<UHapticGloveSettings>();
	Socket = FUdpSocketBuilder(TEXT("VoiceCommands")).AsNonBlocking().BoundToAddress(FIPv4Address(127, 0, 0, 1)).BoundToPort(Settings->VoicePort).Build();
	if (!Socket)
	{
		UE_LOG(LogHapticGlove, Error, TEXT("Voice: could not bind port %d, is another instance running?"), Settings->VoicePort);
		return;
	}
	Buffer.SetNumUninitialized(4096);

	const FString Script = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(Settings->VoiceScript) ? FPaths::ProjectDir() / Settings->VoiceScript : Settings->VoiceScript);
	if (!FPaths::FileExists(Script))
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Voice: script not found at %s, voice commands off"), *Script);
		return;
	}
	if (!FPaths::IsRelative(Settings->VoicePython) && !FPaths::FileExists(Settings->VoicePython))
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Voice: Python not found at %s, voice commands off (see Voice/README.md)"), *Settings->VoicePython);
		return;
	}

	LogPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir() / TEXT("voice_commands.log"));
	FString Args = FString::Printf(TEXT("\"%s\" --port %d --model %s --compute %s --min-confidence %.2f --parent-pid %u --log \"%s\""),
		*Script, Settings->VoicePort, *Settings->VoiceModel, Settings->bVoiceOnGpu ? TEXT("cuda") : TEXT("cpu"),
		Settings->VoiceMinConfidence, FPlatformProcess::GetCurrentProcessId(), *LogPath);
	if (!Settings->MicrophoneName.IsEmpty())
	{
		Args += FString::Printf(TEXT(" --microphone \"%s\""), *Settings->MicrophoneName);
	}

	uint32 ProcessId = 0;
	Process = FPlatformProcess::CreateProc(*Settings->VoicePython, *Args, false, true, true, &ProcessId, 0, nullptr, nullptr);
	bReportedExit = false;
	if (Process.IsValid())
	{
		UE_LOG(LogHapticGlove, Log, TEXT("Voice: sidecar started (pid %u), loading the model, log %s"), ProcessId, *LogPath);
	}
	else
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Voice: could not start %s %s"), *Settings->VoicePython, *Args);
	}
}

void UVoiceCommandSubsystem::Shutdown()
{
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	if (Process.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(Process))
		{
			FPlatformProcess::TerminateProc(Process);
		}
		FPlatformProcess::CloseProc(Process);
	}
	bListening = false;
}

void UVoiceCommandSubsystem::Tick(float DeltaTime)
{
	Receive();
	WatchProcess();
}

void UVoiceCommandSubsystem::Receive()
{
	uint32 Pending = 0;
	while (Socket && Socket->HasPendingData(Pending))
	{
		int32 Read = 0;
		if (!Socket->Recv(Buffer.GetData(), Buffer.Num() - 1, Read) || Read <= 0)
		{
			break;
		}
		Buffer[Read] = 0;
		Handle(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Buffer.GetData())));
	}
}

void UVoiceCommandSubsystem::Handle(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Voice: unreadable message %s"), *Json);
		return;
	}
	FString Status;
	if (Root->TryGetStringField(TEXT("status"), Status))
	{
		bListening = Status == TEXT("ready");
		UE_LOG(LogHapticGlove, Log, TEXT("Voice: listening (%s on %s)"), *Root->GetStringField(TEXT("model")), *Root->GetStringField(TEXT("compute")));
		return;
	}
	FString Command;
	FString Text;
	Root->TryGetStringField(TEXT("text"), Text);
	if (Root->TryGetStringField(TEXT("command"), Command))
	{
		UE_LOG(LogHapticGlove, Log, TEXT("Voice: '%s' -> %s (confidence %.2f)"), *Text, *Command, Root->GetNumberField(TEXT("confidence")));
		Execute(Command, Text);
	}
}

void UVoiceCommandSubsystem::Execute(const FString& Command, const FString& Text)
{
	if (!Link)
	{
		return;
	}
	const ESimMode Mode = Link->GetStatus().Mode;
	bool bDone = true;
	if (Command == TEXT("stop"))
	{
		bDone = Mode != ESimMode::Stopped;
		if (bDone)
		{
			Link->SendCommand(TEXT("stop"));
		}
	}
	else if (Command == TEXT("start"))
	{
		bDone = Mode == ESimMode::Stopped;
		if (bDone)
		{
			Link->SendCommand(TEXT("resume"));
		}
	}
	else if (Command == TEXT("reset"))
	{
		Link->SendCommand(TEXT("reset_same"));
	}
	else if (Command == TEXT("new_scene"))
	{
		Link->SendCommand(TEXT("reset_new"));
	}
	else if (Command == TEXT("calibrate"))
	{
		Link->Calibrate();
	}
	else if (Command == TEXT("switch_hand"))
	{
		Link->SetHand(FString());
	}
	else if (Command == TEXT("debug"))
	{
		APlayerController* Player = GetWorld()->GetFirstPlayerController();
		AOperatorPawn* Pawn = Player ? Cast<AOperatorPawn>(Player->GetPawn()) : nullptr;
		bDone = Pawn != nullptr;
		if (Pawn)
		{
			Pawn->ToggleDebugPanel();
		}
	}
	else
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Voice: unknown command '%s'"), *Command);
		return;
	}
	Link->PlayUiSound(bDone ? EUiSound::Click : EUiSound::Reject);
	if (bDone)
	{
		OnVoiceCommand.Broadcast(Command, Text);
	}
}

void UVoiceCommandSubsystem::WatchProcess()
{
	if (!Process.IsValid() || bReportedExit || FPlatformProcess::IsProcRunning(Process))
	{
		return;
	}
	int32 Code = 0;
	FPlatformProcess::GetProcReturnCode(Process, &Code);
	bReportedExit = true;
	bListening = false;
	UE_LOG(LogHapticGlove, Warning, TEXT("Voice: sidecar exited with code %d, see %s"), Code, *LogPath);
}
