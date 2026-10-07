#include "Networking/SimLink.h"

#include "HapticGloveLog.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/RunnableThread.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
	constexpr int32 ReceiveBufferBytes = 65536;

	/** Non-blocking UDP socket bound to all interfaces on the given port. */
	FSocket* BindUdp(const TCHAR* Name, int32 Port)
	{
		return FUdpSocketBuilder(Name)
			.AsNonBlocking()
			.AsReusable()
			.BoundToPort(Port)
			.WithReceiveBufferSize(1 << 20)
			.Build();
	}
}

FSimLink::~FSimLink()
{
	Close();
}

bool FSimLink::Open(int32 StatePort, int32 ScenePort, const FString& SimHost, int32 CommandPort)
{
	Close();
	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	StateSocket = BindUdp(TEXT("SimState"), StatePort);
	SceneSocket = BindUdp(TEXT("SimScene"), ScenePort);
	CommandSocket = FUdpSocketBuilder(TEXT("SimCommand")).AsNonBlocking().Build();
	if (!StateSocket || !SceneSocket || !CommandSocket)
	{
		UE_LOG(LogHapticGlove, Error, TEXT("SimLink: could not open sockets (state :%d, scene :%d)"), StatePort, ScenePort);
		Close();
		return false;
	}

	bool bValidIp = false;
	CommandAddr = Subsystem->CreateInternetAddr();
	CommandAddr->SetIp(*SimHost, bValidIp);
	CommandAddr->SetPort(CommandPort);
	if (!bValidIp)
	{
		UE_LOG(LogHapticGlove, Error, TEXT("SimLink: invalid sim host '%s'"), *SimHost);
	}

	Buffer.SetNumUninitialized(ReceiveBufferBytes);
	bStopping = false;
	WindowStart = FPlatformTime::Seconds();
	Thread = FRunnableThread::Create(this, TEXT("SimLinkReceive"), 0, TPri_AboveNormal);
	UE_LOG(LogHapticGlove, Log, TEXT("SimLink: state :%d, scene :%d, commands -> %s:%d"), StatePort, ScenePort, *SimHost, CommandPort);
	return Thread != nullptr;
}

void FSimLink::Close()
{
	if (Thread)
	{
		bStopping = true;
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	for (FSocket** Socket : {&StateSocket, &SceneSocket, &CommandSocket})
	{
		if (*Socket)
		{
			(*Socket)->Close();
			Subsystem->DestroySocket(*Socket);
			*Socket = nullptr;
		}
	}
}

void FSimLink::Stop()
{
	bStopping = true;
}

uint32 FSimLink::Run()
{
	while (!bStopping)
	{
		const bool bState = Drain(StateSocket, false);
		const bool bScene = Drain(SceneSocket, true);
		if (!bState && !bScene)
		{
			StateSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(2.0));
		}
	}
	return 0;
}

bool FSimLink::Drain(FSocket* Socket, bool bScene)
{
	bool bAny = false;
	uint32 Pending = 0;
	while (Socket->HasPendingData(Pending))
	{
		int32 Read = 0;
		if (!Socket->Recv(Buffer.GetData(), Buffer.Num(), Read) || Read <= 0)
		{
			break;
		}
		bAny = true;
		if (bScene)
		{
			HandleScene(Buffer.GetData(), Read);
		}
		else
		{
			HandleState(Buffer.GetData(), Read);
		}
	}
	return bAny;
}

void FSimLink::HandleState(const uint8* Data, int32 Size)
{
	FSimState State;
	if (!SimProtocol::ParseState(Data, Size, State))
	{
		if (!bWarnedParse)
		{
			const uint16 PacketVersion = Size >= 6 ? static_cast<uint16>(Data[4] | (Data[5] << 8)) : 0;
			UE_LOG(LogHapticGlove, Warning, TEXT("SimLink: rejected state packet (%d bytes, version %d, expected %d)"), Size, PacketVersion, SimProtocol::Version);
			bWarnedParse = true;
		}
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (LastSeq != 0 && State.Seq > LastSeq + 1)
	{
		WindowLost += static_cast<int32>(State.Seq - LastSeq - 1);
	}
	LastSeq = State.Seq;
	WindowCount += 1;
	WindowLatency += SimProtocol::UnixNow() - State.SendTime;

	FScopeLock Lock(&Mutex);
	LatestState = MoveTemp(State);
	bNewState = true;
	LastStateTime = Now;
	if (Now - WindowStart >= 1.0)
	{
		Stats.PacketRate = static_cast<float>(WindowCount / (Now - WindowStart));
		Stats.PacketsLost = WindowLost;
		Stats.LatencyMs = WindowCount > 0 ? static_cast<float>(1000.0 * WindowLatency / WindowCount) : 0.0f;
		WindowCount = 0;
		WindowLost = 0;
		WindowLatency = 0.0;
		WindowStart = Now;
	}
}

void FSimLink::HandleScene(const uint8* Data, int32 Size)
{
	if (LastSceneBytes.Num() == Size && FMemory::Memcmp(LastSceneBytes.GetData(), Data, Size) == 0)
	{
		return;
	}
	FSimScene Scene;
	if (!SimProtocol::ParseScene(Data, Size, Scene))
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("SimLink: malformed scene message (%d bytes)"), Size);
		return;
	}
	LastSceneBytes = TArray<uint8>(Data, Size);
	FScopeLock Lock(&Mutex);
	LatestScene = MoveTemp(Scene);
	bNewScene = true;
}

bool FSimLink::ConsumeState(FSimState& Out)
{
	FScopeLock Lock(&Mutex);
	if (!bNewState)
	{
		return false;
	}
	Out = LatestState;
	bNewState = false;
	return true;
}

bool FSimLink::ConsumeScene(FSimScene& Out)
{
	FScopeLock Lock(&Mutex);
	if (!bNewScene)
	{
		return false;
	}
	Out = LatestScene;
	bNewScene = false;
	return true;
}

bool FSimLink::SendCommand(const FString& Json)
{
	if (!CommandSocket || !CommandAddr.IsValid())
	{
		return false;
	}
	const FTCHARToUTF8 Utf8(*Json);
	int32 Sent = 0;
	return CommandSocket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent, *CommandAddr);
}

FSimLinkStats FSimLink::GetStats() const
{
	FScopeLock Lock(&Mutex);
	return Stats;
}

double FSimLink::SecondsSinceState() const
{
	FScopeLock Lock(&Mutex);
	return LastStateTime > 0.0 ? FPlatformTime::Seconds() - LastStateTime : TNumericLimits<double>::Max();
}
