#include "UI/BodyAnchorComponent.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "HapticGloveLog.h"
#include "Math/RotationMatrix.h"
#include "Networking/SimLinkSubsystem.h"

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
	Link = GetWorld() ? GetWorld()->GetSubsystem<USimLinkSubsystem>() : nullptr;
	if (Link)
	{
		Link->OnSceneChanged.AddDynamic(this, &UBodyAnchorComponent::HandleSceneChanged);
		Link->OnTrackingChanged.AddDynamic(this, &UBodyAnchorComponent::HandleTrackingChanged);
	}
	RequestPlace();
	Visibility = bAlwaysVisible ? 1.0f : 0.0f;
	ApplyVisibility();
}

void UBodyAnchorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Link)
	{
		Link->OnSceneChanged.RemoveDynamic(this, &UBodyAnchorComponent::HandleSceneChanged);
		Link->OnTrackingChanged.RemoveDynamic(this, &UBodyAnchorComponent::HandleTrackingChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void UBodyAnchorComponent::HandleSceneChanged(const FSimScene& Scene)
{
	RequestPlace();
}

void UBodyAnchorComponent::HandleTrackingChanged(const FSimTracking& Tracking)
{
	RequestPlace();
}

void UBodyAnchorComponent::RequestPlace()
{
	bPlaced = false;
	PlaceCountdown = PlaceDelay;
}

bool UBodyAnchorComponent::TableLocation(FVector& Out) const
{
	if (!Link || !Link->HasScene())
	{
		return false;
	}
	const FSimTable& Table = Link->GetSceneRef().Table;
	if (Table.Size.IsNearlyZero())
	{
		return false;
	}
	const FVector Top = Table.TopCenter + FVector(0.0, 0.0, 0.5 * Table.Size.Z);
	const FVector Local(TableAnchor.X * 0.5 * Table.Size.X + TableOffset.X, TableAnchor.Y * 0.5 * Table.Size.Y + TableOffset.Y, TableOffset.Z);
	Out = Top + FRotator(0.0, TableYaw, 0.0).RotateVector(Local);
	return true;
}

void UBodyAnchorComponent::Place()
{
	AActor* Owner = GetOwner();
	if (!Camera.IsValid() || !Owner)
	{
		return;
	}
	FVector Location;
	FRotator Rotation;
	const bool bOnTable = Mode == EAnchorMode::Table && TableLocation(Location);
	if (bOnTable)
	{
		Rotation = FRotator(PanelPitch, TableYaw + PanelYaw, 0.0);
	}
	else
	{
		const FVector Eye = Camera->GetComponentLocation();
		const FRotator HeadYaw(0.0, Owner->GetActorRotation().Yaw, 0.0);
		Location = Eye + HeadYaw.RotateVector(FRotator(Elevation, Azimuth, 0.0).Vector()) * Distance;
		Rotation = FRotationMatrix::MakeFromX((Location - Eye).GetSafeNormal()).Rotator();
	}
	SetUsingAbsoluteLocation(bOnTable);
	SetUsingAbsoluteRotation(bOnTable);
	SetWorldLocationAndRotation(Location, Rotation);
	if (Mode != EAnchorMode::FollowHead)
	{
		UE_LOG(LogHapticGlove, Log, TEXT("%s placed at (%.0f, %.0f, %.0f) cm"), *GetName(), Location.X, Location.Y, Location.Z);
	}
	bPlaced = true;
}

void UBodyAnchorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Camera.IsValid() || !GetOwner())
	{
		return;
	}

	if (Mode == EAnchorMode::FollowHead)
	{
		Place();
	}
	else if (!bPlaced)
	{
		PlaceCountdown -= DeltaTime;
		if (PlaceCountdown > 0.0f)
		{
			return;
		}
		Place();
	}
	UpdateReveal(DeltaTime);
}

void UBodyAnchorComponent::SetAnchorEnabled(bool bEnabled)
{
	bAnchorEnabled = bEnabled;
	ApplyVisibility();
}

void UBodyAnchorComponent::UpdateReveal(float DeltaTime)
{
	const FVector ToAnchor = (GetComponentLocation() - Camera->GetComponentLocation()).GetSafeNormal();
	const double Cos = FVector::DotProduct(Camera->GetForwardVector(), ToAnchor);
	LastViewAngle = static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cos, -1.0, 1.0))));
	LastHeadPitch = static_cast<float>(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Camera->GetForwardVector().Z, -1.0, 1.0))));
	const bool bLookUp = RevealMode == ERevealMode::LookUp;
	const float Value = bLookUp ? -LastHeadPitch : LastViewAngle;
	const float Threshold = bLookUp ? -RevealPitch : RevealAngle;
	if (bAlwaysVisible || Value < Threshold)
	{
		bShown = true;
	}
	else if (Value > Threshold + HideAngleMargin)
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
	const float Shown = bAnchorEnabled ? Visibility : 0.0f;
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
			UserWidget->SetRenderOpacity(Shown);
		}
		Widget->SetVisibility(Shown > 0.01f);
		Widget->SetCollisionEnabled(Shown > 0.5f ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}
