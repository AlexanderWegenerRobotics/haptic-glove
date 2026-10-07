#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Networking/SimProtocol.h"
#include "SimSceneActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class USimLinkSubsystem;

USTRUCT()
struct FFingerTint
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
	FLinearColor Base = FLinearColor::White;
	int32 Channel = -1;
};

UCLASS()
class HAPTICGLOVE_API ASimSceneActor : public AActor
{
	GENERATED_BODY()

public:
	ASimSceneActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene|Materials")
	TObjectPtr<UMaterialInterface> SolidMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene|Materials")
	TObjectPtr<UMaterialInterface> GhostMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene|Materials")
	FName ColorParameter = TEXT("Color");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene|Ghost")
	FLinearColor GhostColor = FLinearColor(0.3f, 0.55f, 1.0f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene|Feedback")
	FLinearColor ForceColor = FLinearColor(1.0f, 0.1f, 0.05f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene|Feedback", meta = (ClampMin = "0.01"))
	float FullScaleFeedback = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene|Objects", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float MaxSquash = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scene|Table")
	bool bTableLegs = true;

	/** Called after a scene was built, for extra Blueprint decoration. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Scene")
	void OnSceneBuilt(const FSimScene& Scene);

private:
	/** Rebuild everything when the sim announces a new scene. */
	UFUNCTION()
	void HandleSceneChanged(const FSimScene& Scene);

	/** Spawn table, objects, hand and ghost components for a scene. */
	void Build(const FSimScene& Scene);

	/** Destroy every component created by the last build. */
	void Clear();

	/** Move bodies, objects and ghost to the latest state and tint the fingers by feedback. */
	void ApplyState(const FSimState& State);

	/** Squash a deformable sphere along the deepest contact direction, keeping the far side in place. */
	void ApplySquash(int32 Index, const FSimObjectState& Object);

	/** Create a movable scene node under a parent. */
	USceneComponent* AddNode(USceneComponent* Parent);

	/** Create a mesh without collision under a parent. */
	UStaticMeshComponent* AddMesh(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Local, UMaterialInstanceDynamic* Material);

	/** Create the meshes for one MuJoCo primitive (sizes in cm, MuJoCo conventions); returns the main mesh. */
	UStaticMeshComponent* AddShape(USceneComponent* Parent, const FString& Type, const FVector& Size, const FTransform& Local, UMaterialInstanceDynamic* Material);

	/** Dynamic material with the given color, from the given base or the solid material. */
	UMaterialInstanceDynamic* MakeMaterial(UMaterialInterface* Base, const FLinearColor& Color);

	/** Glove channel a hand body belongs to (0 thumb, 1 index, 2 middle), -1 for the palm. */
	static int32 ChannelForBody(const FString& Name);

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Created;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> BodyNodes;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> ObjectNodes;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SquashMeshes;
	UPROPERTY() TObjectPtr<USceneComponent> GhostNode;
	UPROPERTY() TArray<FFingerTint> FingerTints;
	UPROPERTY() TObjectPtr<USimLinkSubsystem> Link;

	TArray<float> ObjectRadius;
	FString BuiltSceneId;
};
