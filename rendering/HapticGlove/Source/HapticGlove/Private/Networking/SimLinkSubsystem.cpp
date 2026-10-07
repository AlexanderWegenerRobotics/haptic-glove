#include "Networking/SimLinkSubsystem.h"

#include "HapticGloveLog.h"
#include "HapticGloveSettings.h"
#include "Kismet/GameplayStatics.h"

void USimLinkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Status.Closure.Init(0.0f, 3);
	Status.Feedback.Init(0.0f, 3);
}

void USimLinkSubsystem::Deinitialize()
{
	Link.Close();
	Super::Deinitialize();
}

bool USimLinkSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USimLinkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const UHapticGloveSettings* Settings = GetDefault<UHapticGloveSettings>();
	Link.Open(Settings->StatePort, Settings->ScenePort, Settings->SimHost, Settings->CommandPort);
}

TStatId USimLinkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USimLinkSubsystem, STATGROUP_Tickables);
}

void USimLinkSubsystem::Tick(float DeltaTime)
{
	Refresh();
}

void USimLinkSubsystem::Refresh()
{
	FSimScene Incoming;
	if (Link.ConsumeScene(Incoming))
	{
		const bool bNewScene = !bHasScene || Incoming.SceneId != Scene.SceneId;
		const bool bFirstScene = !bHasScene;
		const bool bNewTracking = bFirstScene || Incoming.Tracking.Created != Scene.Tracking.Created;
		Scene = MoveTemp(Incoming);
		bHasScene = true;
		if (bNewScene)
		{
			bHasState = false;
			bWarnedSceneMismatch = false;
			UE_LOG(LogHapticGlove, Log, TEXT("Scene %s (%s, %s hand, %d objects)"), *Scene.SceneId, *Scene.SceneName, *Scene.Hand, Scene.Objects.Num());
			OnSceneChanged.Broadcast(Scene);
		}
		if (bNewTracking)
		{
			if (!bFirstScene && Scene.Tracking.bValid)
			{
				DoneSoundAt = FPlatformTime::Seconds() + GetDefault<UHapticGloveSettings>()->CalibrateDoneDelay;
			}
			OnTrackingChanged.Broadcast(Scene.Tracking);
		}
	}

	FSimState Latest;
	if (Link.ConsumeState(Latest) && bHasScene)
	{
		if (Latest.SceneId == Scene.SceneId)
		{
			if (!bHasState)
			{
				UE_LOG(LogHapticGlove, Log, TEXT("First state for scene %s: %d bodies, %d objects, flags %d"), *Scene.SceneId, Latest.Bodies.Num(), Latest.Objects.Num(), Latest.Flags);
			}
			State = MoveTemp(Latest);
			bHasState = true;
		}
		else if (!bWarnedSceneMismatch)
		{
			UE_LOG(LogHapticGlove, Warning, TEXT("State packets belong to scene '%s' but the built scene is '%s'"), *Latest.SceneId, *Scene.SceneId);
			bWarnedSceneMismatch = true;
		}
	}
	UpdateStatus();
}

void USimLinkSubsystem::UpdateStatus()
{
	const ESimMode Previous = Status.Mode;
	const FSimLinkStats Stats = Link.GetStats();
	Status.bConnected = bHasState && Link.SecondsSinceState() < GetDefault<UHapticGloveSettings>()->DisconnectTimeout;
	Status.PacketRate = Status.bConnected ? Stats.PacketRate : 0.0f;
	Status.PacketsLost = Stats.PacketsLost;
	Status.LatencyMs = Stats.LatencyMs;
	Status.Hand = Scene.Hand;
	Status.SceneName = Scene.SceneName;
	if (Status.bConnected)
	{
		const uint8 F = State.Flags;
		Status.Mode = (F & SimProtocol::FlagStopped) ? ESimMode::Stopped
			: (F & SimProtocol::FlagEngaged) ? ESimMode::Engaged : ESimMode::Waiting;
		Status.bGhostVisible = (F & SimProtocol::FlagGhost) != 0;
		Status.bTracked = (F & SimProtocol::FlagTracked) != 0;
		Status.bCalibrated = (F & SimProtocol::FlagCalibrated) != 0;
		Status.bCommandPending = static_cast<int32>(State.CommandSeq) < CommandSeq;
		Status.Trial = static_cast<int32>(State.Trial);
		Status.Countdown = State.Countdown;
		Status.SimTime = static_cast<float>(State.SimTime);
		for (int32 i = 0; i < 3; ++i)
		{
			Status.Closure[i] = State.Closure[i];
			Status.Feedback[i] = State.Feedback[i];
		}
	}
	else
	{
		Status.Mode = ESimMode::Disconnected;
		Status.bGhostVisible = false;
	}
	if (Status.Mode != Previous)
	{
		if (Status.Mode == ESimMode::Disconnected)
		{
			bOutagePending = Previous == ESimMode::Engaged && !bExpectOutage;
			OutageSince = FPlatformTime::Seconds();
		}
		else
		{
			bOutagePending = false;
			bExpectOutage = false;
		}
		OnModeChanged.Broadcast(Status.Mode, Previous);
	}
	if (bOutagePending && FPlatformTime::Seconds() - OutageSince > GetDefault<UHapticGloveSettings>()->OutageWarningDelay)
	{
		bOutagePending = false;
		PlayUiSound(EUiSound::Warning);
	}
	UpdatePendingCommand();
	SpeakCountdown();
}

void USimLinkSubsystem::SpeakCountdown()
{
	if (DoneSoundAt > 0.0 && FPlatformTime::Seconds() >= DoneSoundAt)
	{
		DoneSoundAt = 0.0;
		PlayUiSound(EUiSound::CalibrateDone);
	}
	const int32 Second = Status.Countdown > 0.0f ? FMath::CeilToInt(Status.Countdown) : 0;
	if (Second == SpokenSecond)
	{
		return;
	}
	SpokenSecond = Second;
	if (Second == 3)
	{
		PlayUiSound(EUiSound::CalibrateThree);
	}
	else if (Second == 2)
	{
		PlayUiSound(EUiSound::CalibrateTwo);
	}
	else if (Second == 1)
	{
		PlayUiSound(EUiSound::CalibrateOne);
	}
}

void USimLinkSubsystem::UpdatePendingCommand()
{
	if (PendingSeq == 0)
	{
		return;
	}
	const bool bAcknowledged = bHasState && static_cast<int32>(State.CommandSeq) >= PendingSeq;
	if (!bAcknowledged && FPlatformTime::Seconds() - PendingSince < GetDefault<UHapticGloveSettings>()->CommandTimeout)
	{
		return;
	}
	if (!bAcknowledged)
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Command '%s' (seq %d) was not acknowledged by the sim"), *PendingCommand, PendingSeq);
	}
	PendingSeq = 0;
	PlayUiSound(bAcknowledged ? EUiSound::Confirm : EUiSound::Reject);
	OnCommandResult.Broadcast(PendingCommand, bAcknowledged);
}

void USimLinkSubsystem::PlayUiSound(EUiSound Sound)
{
	const UHapticGloveSettings* Settings = GetDefault<UHapticGloveSettings>();
	if (!Settings->bUiSounds)
	{
		return;
	}
	TObjectPtr<USoundBase>& Loaded = LoadedSounds.FindOrAdd(Sound);
	if (!Loaded)
	{
		if (const TSoftObjectPtr<USoundBase>* Path = Settings->UiSounds.Find(Sound))
		{
			Loaded = Path->LoadSynchronous();
		}
	}
	if (Loaded)
	{
		UGameplayStatics::PlaySound2D(GetWorld(), Loaded);
	}
}

void USimLinkSubsystem::Send(const FString& Command, const FString& ExtraFields)
{
	CommandSeq += 1;
	PendingSeq = CommandSeq;
	PendingCommand = Command;
	PendingSince = FPlatformTime::Seconds();
	bExpectOutage = bExpectOutage || Command == TEXT("reset_same") || Command == TEXT("reset_new") || Command == TEXT("set_hand") || Command == TEXT("quit");
	const FString Json = FString::Printf(TEXT("{\"seq\": %d, \"command\": \"%s\"%s}"), CommandSeq, *Command, *ExtraFields);
	if (!Link.SendCommand(Json))
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Command not sent: %s"), *Json);
	}
}

void USimLinkSubsystem::SendCommand(const FString& Command)
{
	Send(Command);
}

void USimLinkSubsystem::Calibrate(float Delay)
{
	const float Seconds = Delay < 0.0f ? GetDefault<UHapticGloveSettings>()->CalibrateDelay : Delay;
	PlayUiSound(EUiSound::CalibrateIntro);
	Send(TEXT("calibrate"), FString::Printf(TEXT(", \"delay\": %.3f"), Seconds));
}

void USimLinkSubsystem::SetHand(const FString& Hand)
{
	Send(TEXT("set_hand"), Hand.IsEmpty() ? FString() : FString::Printf(TEXT(", \"hand\": \"%s\""), *Hand));
}

void USimLinkSubsystem::ToggleStop()
{
	Send(Status.Mode == ESimMode::Stopped ? TEXT("resume") : TEXT("stop"));
}
