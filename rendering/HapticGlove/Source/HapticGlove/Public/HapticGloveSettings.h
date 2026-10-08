#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Sound/SoundBase.h"
#include "HapticGloveSettings.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UWorld;

UENUM(BlueprintType)
enum class EUiSound : uint8
{
	Click,
	Confirm,
	Reject,
	Warning,
	CalibrateIntro,
	CalibrateOne,
	CalibrateTwo,
	CalibrateThree,
	CalibrateDone
};

USTRUCT()
struct FHapticEnvironment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Environment")
	TSoftObjectPtr<UWorld> Level;

	UPROPERTY(EditAnywhere, Category = "Environment")
	TSoftObjectPtr<UMaterialInterface> TableMaterial;

	/** Material of the table legs, the table material when empty. */
	UPROPERTY(EditAnywhere, Category = "Environment")
	TSoftObjectPtr<UMaterialInterface> LegMaterial;

	UPROPERTY(EditAnywhere, Category = "Environment")
	bool bTableLegs = true;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Haptic Glove"))
class HAPTICGLOVE_API UHapticGloveSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Sim Link")
	int32 StatePort = 9870;

	UPROPERTY(Config, EditAnywhere, Category = "Sim Link")
	int32 ScenePort = 9871;

	UPROPERTY(Config, EditAnywhere, Category = "Sim Link")
	FString SimHost = TEXT("127.0.0.1");

	UPROPERTY(Config, EditAnywhere, Category = "Sim Link")
	int32 CommandPort = 9872;

	UPROPERTY(Config, EditAnywhere, Category = "Sim Link", meta = (ClampMin = "0.1"))
	float DisconnectTimeout = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (ClampMin = "0.0"))
	float CalibrateDelay = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (ClampMin = "0.0"))
	float CalibrateDoneDelay = 0.8f;

	UPROPERTY(Config, EditAnywhere, Category = "Commands", meta = (ClampMin = "0.1"))
	float CommandTimeout = 3.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Sound", meta = (ClampMin = "0.0"))
	float OutageWarningDelay = 3.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Recording")
	bool bRecordSessions = true;

	UPROPERTY(Config, EditAnywhere, Category = "Recording", meta = (ClampMin = "1", ClampMax = "90"))
	int32 VideoFps = 30;

	UPROPERTY(Config, EditAnywhere, Category = "Recording")
	bool bRecordMicrophone = true;

	UPROPERTY(Config, EditAnywhere, Category = "Recording")
	FString MicrophoneName;

	UPROPERTY(Config, EditAnywhere, Category = "Recording")
	bool bEnhanceSpeech = true;

	UPROPERTY(Config, EditAnywhere, Category = "Recording", meta = (ClampMin = "-70.0", ClampMax = "-10.0"))
	float NoiseGateThresholdDb = -38.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Recording", meta = (ClampMin = "20.0", ClampMax = "2000.0"))
	float NoiseGateReleaseMs = 200.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Recording", meta = (ClampMin = "-20.0", ClampMax = "30.0"))
	float MicGainDb = 0.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Recording", meta = (ClampMin = "-20.0", ClampMax = "30.0"))
	float GameAudioGainDb = 0.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Recording")
	bool bKeepIntermediates = false;

	UPROPERTY(Config, EditAnywhere, Category = "Recording", meta = (ClampMin = "1.0"))
	float SessionEndTimeout = 20.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Recording")
	FString FfmpegPath;

	UPROPERTY(Config, EditAnywhere, Category = "Environments")
	TMap<FString, FHapticEnvironment> Environments;

	UPROPERTY(Config, EditAnywhere, Category = "Environments")
	TMap<FString, TSoftObjectPtr<UStaticMesh>> ObjectVisuals;

	UPROPERTY(Config, EditAnywhere, Category = "Sound")
	bool bUiSounds = true;

	UPROPERTY(Config, EditAnywhere, Category = "Sound")
	TMap<EUiSound, TSoftObjectPtr<USoundBase>> UiSounds = {
		{EUiSound::Click, TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Sounds/click.click")))},
		{EUiSound::Confirm, TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Sounds/confirm.confirm")))},
		{EUiSound::Reject, TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Sounds/reject_2.reject_2")))},
		{EUiSound::Warning, TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Sounds/warning.warning")))},
		{EUiSound::CalibrateIntro, TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Sounds/Voice/calib_intro.calib_intro")))},
		{EUiSound::CalibrateOne, TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Sounds/Voice/calib_1.calib_1")))},
		{EUiSound::CalibrateTwo, TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Sounds/Voice/calib_2.calib_2")))},
		{EUiSound::CalibrateThree, TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Sounds/Voice/calib_3.calib_3")))},
		{EUiSound::CalibrateDone, TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Sounds/Voice/calib_done.calib_done")))}};
};
