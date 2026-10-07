#include "Player/HapticGameMode.h"

#include "EngineUtils.h"
#include "Player/OperatorPawn.h"
#include "Scene/SimSceneActor.h"

AHapticGameMode::AHapticGameMode()
{
	DefaultPawnClass = AOperatorPawn::StaticClass();
	SceneClass = ASimSceneActor::StaticClass();
}

void AHapticGameMode::StartPlay()
{
	Super::StartPlay();
	if (!SceneClass || TActorIterator<ASimSceneActor>(GetWorld()))
	{
		return;
	}
	GetWorld()->SpawnActor<ASimSceneActor>(SceneClass, FTransform::Identity);
}
