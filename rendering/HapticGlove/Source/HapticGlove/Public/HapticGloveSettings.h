#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Sound/SoundBase.h"
#include "HapticGloveSettings.generated.h"

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
