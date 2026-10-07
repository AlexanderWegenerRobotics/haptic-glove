#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include <atomic>
#include "Networking/SimProtocol.h"

class FSocket;
class FRunnableThread;
class FInternetAddr;

class HAPTICGLOVE_API FSimLink : public FRunnable
{
public:
	~FSimLink();

	/** Bind the state and scene ports, prepare the command socket and start the receive thread. */
	bool Open(int32 StatePort, int32 ScenePort, const FString& SimHost, int32 CommandPort);

	/** Stop the receive thread and release all sockets. */
	void Close();

	/** Copy the newest state if one arrived since the last call. */
	bool ConsumeState(FSimState& Out);

	/** Copy the newest scene if it changed since the last call. */
	bool ConsumeScene(FSimScene& Out);

	/** Send one JSON command datagram to the sim. */
	bool SendCommand(const FString& Json);

	/** Packet rate, loss and latency over the last second. */
	FSimLinkStats GetStats() const;

	/** Seconds since the last valid state packet. */
	double SecondsSinceState() const;

	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	/** Read every pending datagram from one socket into the buffer and handle it. */
	bool Drain(FSocket* Socket, bool bScene);

	/** Parse a state packet, update the statistics and store it as the latest. */
	void HandleState(const uint8* Data, int32 Size);

	/** Parse a scene message when its bytes differ from the last one. */
	void HandleScene(const uint8* Data, int32 Size);

	FSocket* StateSocket = nullptr;
	FSocket* SceneSocket = nullptr;
	FSocket* CommandSocket = nullptr;
	TSharedPtr<FInternetAddr> CommandAddr;
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> bStopping{false};
	TArray<uint8> Buffer;

	mutable FCriticalSection Mutex;
	FSimState LatestState;
	bool bNewState = false;
	FSimScene LatestScene;
	TArray<uint8> LastSceneBytes;
	bool bNewScene = false;
	bool bWarnedParse = false;
	double LastStateTime = 0.0;

	FSimLinkStats Stats;
	uint32 LastSeq = 0;
	int32 WindowCount = 0;
	int32 WindowLost = 0;
	double WindowLatency = 0.0;
	double WindowStart = 0.0;
};
