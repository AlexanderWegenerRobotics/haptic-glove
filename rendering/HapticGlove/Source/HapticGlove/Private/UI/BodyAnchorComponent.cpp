#include "UI/BodyAnchorComponent.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "Math/RotationMatrix.h"

UBodyAnchorComponent::UBodyAnchorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UBodyAnchorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		Camera = Owner->FindComponentByClass<UCameraComponent>();
	}
	Visibility = bAlwaysVisible ? 1.0f : 0.0f;
	ApplyVisibility();
}

void UBodyAnchorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	if (!Camera.IsValid() || !Owner)
	{
		return;
	}

	const FVector Eye = Camera->GetComponentLocation();
	const FRotator TableYaw(0.0, Owner->GetActorRotation().Yaw, 0.0);
	const FVector Direction = TableYaw.RotateVector(FRotator(Elevation, Azimuth, 0.0).Vector());
	const FVector Location = Eye + Direction * Distance;
	SetWorldLocationAndRotation(Location, FRotationMatrix::MakeFromX(Direction).Rotator());

	const double Cos = FVector::DotProduct(Camera->GetForwardVector(), Direction);
	LastViewAngle = static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cos, -1.0, 1.0))));
	if (bAlwaysVisible || LastViewAngle < RevealAngle)
	{
		bShown = true;
	}
	else if (LastViewAngle > RevealAngle + HideAngleMargin)
	{
		bShown = false;
	}
	const float Target = bShown ? 1.0f : 0.0f;
	const float Next = FMath::FInterpConstantTo(Visibility, Target, DeltaTime, FadeSpeed);
	if (Next != Visibility)
	{
		Visibility = Next;
		ApplyVisibility();
	}
}

void UBodyAnchorComponent::ApplyVisibility()
{
	TArray<USceneComponent*> Children;
	GetChildrenComponents(true, Children);
	for (USceneComponent* Child : Children)
	{
		UWidgetComponent* Widget = Cast<UWidgetComponent>(Child);
		if (!Widget)
		{
			continue;
		}
		if (UUserWidget* UserWidget = Widget->GetUserWidgetObject())
		{
			UserWidget->SetRenderOpacity(Visibility);
		}
		Widget->SetVisibility(Visibility > 0.01f);
		Widget->SetCollisionEnabled(Visibility > 0.5f ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}
