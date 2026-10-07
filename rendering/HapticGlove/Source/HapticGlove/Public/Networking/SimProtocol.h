#pragma once

#include "CoreMinimal.h"
#include "SimProtocol.generated.h"

UENUM(BlueprintType)
enum class ESimMode : uint8
{
	Disconnected,
	Waiting,
	Engaged,
	Stopped
};

USTRUCT(BlueprintType)
struct FSimGeom
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 Body = -1;
	UPROPERTY(BlueprintReadOnly) FString Type;
	UPROPERTY(BlueprintReadOnly) FVector Size = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FTransform Local;
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct FSimObject
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Type;
	UPROPERTY(BlueprintReadOnly) FString Shape;
	UPROPERTY(BlueprintReadOnly) FVector Size = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly) bool bDeformable = false;
	UPROPERTY(BlueprintReadOnly) FTransform Initial;
};

USTRUCT(BlueprintType)
struct FSimTable
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FVector TopCenter = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector Size = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float Height = 0.0f;
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct FSimTracking
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bValid = false;
	UPROPERTY(BlueprintReadOnly) float YawRad = 0.0f;
	UPROPERTY(BlueprintReadOnly) FVector Position = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FString Created;
};

USTRUCT(BlueprintType)
struct FSimScene
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString SceneId;
	UPROPERTY(BlueprintReadOnly) FString SceneName;
	UPROPERTY(BlueprintReadOnly) FString Hand;
	UPROPERTY(BlueprintReadOnly) TArray<FString> HandBodies;
	UPROPERTY(BlueprintReadOnly) TArray<FSimGeom> HandGeoms;
	UPROPERTY(BlueprintReadOnly) FSimTable Table;
	UPROPERTY(BlueprintReadOnly) TArray<FSimObject> Objects;
	UPROPERTY(BlueprintReadOnly) FSimTracking Tracking;
	UPROPERTY(BlueprintReadOnly) FString SessionId;
	UPROPERTY(BlueprintReadOnly) FString SessionDirectory;
};

USTRUCT(BlueprintType)
struct FSimStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bConnected = false;
	UPROPERTY(BlueprintReadOnly) ESimMode Mode = ESimMode::Disconnected;
	UPROPERTY(BlueprintReadOnly) bool bGhostVisible = false;
	UPROPERTY(BlueprintReadOnly) bool bTracked = false;
	UPROPERTY(BlueprintReadOnly) bool bCalibrated = false;
	UPROPERTY(BlueprintReadOnly) bool bCommandPending = false;
	UPROPERTY(BlueprintReadOnly) int32 Trial = 0;
	UPROPERTY(BlueprintReadOnly) float Countdown = 0.0f;
	UPROPERTY(BlueprintReadOnly) float SimTime = 0.0f;
	UPROPERTY(BlueprintReadOnly) FString Hand;
	UPROPERTY(BlueprintReadOnly) FString SceneName;
	UPROPERTY(BlueprintReadOnly) float LatencyMs = 0.0f;
	UPROPERTY(BlueprintReadOnly) float PacketRate = 0.0f;
	UPROPERTY(BlueprintReadOnly) int32 PacketsLost = 0;
	UPROPERTY(BlueprintReadOnly) TArray<float> Closure;
	UPROPERTY(BlueprintReadOnly) TArray<float> Feedback;
};

struct FSimObjectState
{
	FTransform Pose;
	float SquashDepth = 0.0f;
	FVector SquashDir = FVector::ZeroVector;
};

struct FSimState
{
	uint32 Seq = 0;
	uint8 HandType = 0;
	uint8 Flags = 0;
	double SimTime = 0.0;
	double SendTime = 0.0;
	FString SceneId;
	uint32 Trial = 0;
	uint32 CommandSeq = 0;
	float Countdown = 0.0f;
	FTransform Wrist;
	FTransform Ghost;
	TArray<float> Joints;
	TArray<FTransform> Bodies;
	float Closure[3] = {0.0f, 0.0f, 0.0f};
	float Feedback[3] = {0.0f, 0.0f, 0.0f};
	TArray<FSimObjectState> Objects;
};

struct FSimLinkStats
{
	float PacketRate = 0.0f;
	int32 PacketsLost = 0;
	float LatencyMs = 0.0f;
};

namespace SimProtocol
{
	constexpr uint8 FlagEngaged = 1;
	constexpr uint8 FlagGhost = 2;
	constexpr uint8 FlagStopped = 4;
	constexpr uint8 FlagTracked = 8;
	constexpr uint8 FlagCalibrated = 16;
	constexpr uint16 Version = 3;

	/** MuJoCo position in meters to Unreal centimeters with y flipped. */
	FVector ToUnreal(double X, double Y, double Z);

	/** MuJoCo quaternion (w, x, y, z) to an Unreal rotation in the mirrored frame. */
	FQuat ToUnreal(double W, double X, double Y, double Z);

	/** Current wall clock as unix seconds, matching the sim's send_time. */
	double UnixNow();

	/** Decode a binary state packet; false when it is malformed or has the wrong version. */
	bool ParseState(const uint8* Data, int32 Size, FSimState& Out);

	/** Decode a scene message (magic plus JSON); false when it is malformed. */
	bool ParseScene(const uint8* Data, int32 Size, FSimScene& Out);
}
