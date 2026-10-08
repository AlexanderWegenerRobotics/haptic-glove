#include "Scene/TablePreviewActor.h"

#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ATablePreviewActor::ATablePreviewActor()
{
	bIsEditorOnlyActor = true;
	SetHidden(true);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Origin"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Top = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TableTop"));
	Top->SetupAttachment(RootComponent);
	Top->SetStaticMesh(Cube.Object);
	Top->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Top->SetCastShadow(false);
	Top->bIsEditorOnly = true;

	Operator = CreateDefaultSubobject<UArrowComponent>(TEXT("OperatorView"));
	Operator->SetupAttachment(RootComponent);
	Operator->ArrowSize = 1.5f;
	Operator->ArrowColor = FColor(255, 140, 40);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("OperatorLabel"));
	Label->SetupAttachment(RootComponent);
	Label->SetText(FText::FromString(TEXT("operator")));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(8.0f);
	Label->bIsEditorOnly = true;
}

void ATablePreviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Top->SetRelativeLocation(FVector(0.0, 0.0, TableHeight - 0.5 * TableSize.Z));
	Top->SetRelativeScale3D(TableSize / 100.0);
	const double Eye = TableHeight + 45.0;
	Operator->SetRelativeLocationAndRotation(FVector(-0.5 * TableSize.X - OperatorDistance, 0.0, Eye), FRotator(-30.0, 0.0, 0.0));
	Label->SetRelativeLocationAndRotation(FVector(-0.5 * TableSize.X - OperatorDistance, 0.0, Eye + 12.0), FRotator(0.0, 180.0, 0.0));
}
