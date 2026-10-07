#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HapticGameMode.generated.h"

class ASimSceneActor;

UCLASS()
class HAPTICGLOVE_API AHapticGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHapticGameMode();

	virtual void StartPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Haptic Glove")
	TSubclassOf<ASimSceneActor> SceneClass;
};
