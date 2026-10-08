#include "Recording/LogCameraComponent.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Recording/SessionRecorder.h"

ULogCameraComponent::ULogCameraComponent()
{
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;
	bAlwaysPersistRenderingState = true;
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	FOVAngle = 100.0f;
	ShowFlags.SetMotionBlur(false);
	ShowFlags.SetTemporalAA(false);
}

UTextureRenderTarget2D* ULogCameraComponent::GetLogTarget()
{
	if (!TextureTarget)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
		Target->ClearColor = FLinearColor::Black;
		Target->InitCustomFormat(Width, Height, PF_B8G8R8A8, true);
		Target->UpdateResourceImmediate(true);
		TextureTarget = Target;
	}
	return TextureTarget;
}

void ULogCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	if (USessionRecorder* Recorder = GetWorld()->GetSubsystem<USessionRecorder>())
	{
		Recorder->RegisterCamera(this);
	}
}

void ULogCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USessionRecorder* Recorder = GetWorld()->GetSubsystem<USessionRecorder>())
	{
		Recorder->UnregisterCamera(this);
	}
	Super::EndPlay(EndPlayReason);
}
