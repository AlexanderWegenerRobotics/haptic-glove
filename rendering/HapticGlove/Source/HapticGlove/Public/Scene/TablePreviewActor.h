#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TablePreviewActor.generated.h"

class UArrowComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class HAPTICGLOVE_API ATablePreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ATablePreviewActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Table")
	FVector TableSize = FVector(80.0, 60.0, 4.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Table")
	float TableHeight = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Table")
	float OperatorDistance = 50.0f;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Top;
	UPROPERTY() TObjectPtr<UArrowComponent> Operator;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
};
