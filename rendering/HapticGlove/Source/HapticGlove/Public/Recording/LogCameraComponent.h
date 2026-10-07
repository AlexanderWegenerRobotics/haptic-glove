#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "LogCameraComponent.generated.h"

UCLASS(ClassGroup = (HapticGlove), meta = (BlueprintSpawnableComponent))
class HAPTICGLOVE_API ULogCameraComponent : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	ULogCameraComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Logging")
	FString StreamName = TEXT("operator");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Logging", meta = (ClampMin = "64"))
	int32 Width = 1280;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Logging", meta = (ClampMin = "64"))
	int32 Height = 720;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Logging")
	bool bLogVideo = true;

	/** Render target in the logging resolution, created on first use. */
	UTextureRenderTarget2D* GetLogTarget();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
