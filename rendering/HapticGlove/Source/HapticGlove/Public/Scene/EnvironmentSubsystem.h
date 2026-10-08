#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HapticGloveSettings.h"
#include "Networking/SimProtocol.h"
#include "EnvironmentSubsystem.generated.h"

class ULevelStreamingDynamic;

UCLASS()
class HAPTICGLOVE_API UEnvironmentSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Stream in the environment named by the scene around the sim table, unloading the previous one. */
	void Apply(const FSimScene& Scene);

	/** Settings of the active environment, nullptr for the default grid scene. */
	const FHapticEnvironment* Current() const;

	/** Name of the active environment, "none" for the default grid scene. */
	UFUNCTION(BlueprintPure, Category = "Environment")
	FString GetEnvironmentName() const { return LoadedName; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	/** Remove the streamed environment level, if any. */
	void Unload();

	/** Hide actors tagged DefaultEnvironment while a real environment is shown, show them otherwise. */
	void SetDefaultVisible(bool bVisible);

	UPROPERTY() TObjectPtr<ULevelStreamingDynamic> Streamed;

	FString LoadedName = TEXT("none");
	FVector LoadedOrigin = FVector::ZeroVector;
};
