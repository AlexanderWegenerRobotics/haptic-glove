#include "Scene/EnvironmentSubsystem.h"

#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HapticGloveLog.h"

namespace
{
	const FName DefaultTag(TEXT("DefaultEnvironment"));
}

bool UEnvironmentSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UEnvironmentSubsystem::Deinitialize()
{
	Unload();
	Super::Deinitialize();
}

void UEnvironmentSubsystem::Apply(const FSimScene& Scene)
{
	const FString Name = Scene.Environment.IsEmpty() ? FString(TEXT("none")) : Scene.Environment;
	const FVector Origin(Scene.Table.TopCenter.X, Scene.Table.TopCenter.Y, 0.0);
	if (Name == LoadedName && Origin.Equals(LoadedOrigin, 0.1))
	{
		return;
	}
	Unload();
	LoadedName = TEXT("none");
	LoadedOrigin = Origin;

	const FHapticEnvironment* Definition = GetDefault<UHapticGloveSettings>()->Environments.Find(Name);
	if (Name != TEXT("none") && (!Definition || Definition->Level.IsNull()))
	{
		UE_LOG(LogHapticGlove, Warning, TEXT("Environment '%s' is not set up in Project Settings > Haptic Glove > Environments, showing the default scene"), *Name);
	}
	if (Definition && !Definition->Level.IsNull())
	{
		bool bLoaded = false;
		Streamed = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(this, Definition->Level, Origin, FRotator::ZeroRotator, bLoaded);
		if (bLoaded && Streamed)
		{
			LoadedName = Name;
			UE_LOG(LogHapticGlove, Log, TEXT("Environment '%s' loaded at table (%.0f, %.0f) cm"), *Name, Origin.X, Origin.Y);
		}
		else
		{
			Streamed = nullptr;
			UE_LOG(LogHapticGlove, Error, TEXT("Environment '%s': could not load %s"), *Name, *Definition->Level.ToString());
		}
	}
	SetDefaultVisible(LoadedName == TEXT("none"));
}

const FHapticEnvironment* UEnvironmentSubsystem::Current() const
{
	return LoadedName == TEXT("none") ? nullptr : GetDefault<UHapticGloveSettings>()->Environments.Find(LoadedName);
}

void UEnvironmentSubsystem::Unload()
{
	if (Streamed)
	{
		Streamed->SetShouldBeLoaded(false);
		Streamed->SetShouldBeVisible(false);
		Streamed->SetIsRequestingUnloadAndRemoval(true);
		Streamed = nullptr;
	}
}

void UEnvironmentSubsystem::SetDefaultVisible(bool bVisible)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(DefaultTag))
		{
			It->SetActorHiddenInGame(!bVisible);
		}
	}
}
