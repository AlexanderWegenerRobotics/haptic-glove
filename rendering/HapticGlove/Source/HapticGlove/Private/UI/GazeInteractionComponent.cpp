#include "UI/GazeInteractionComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "EyeTrackerFunctionLibrary.h"
#include "EyeTrackerTypes.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "UI/SimWidgets.h"

UGazeInteractionComponent::UGazeInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	InteractionSource = EWidgetInteractionSource::World;
	InteractionDistance = 300.0f;
}

void UGazeInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		Camera = Owner->FindComponentByClass<UCameraComponent>();
		TArray<UWidgetComponent*> Widgets;
		Owner->GetComponents<UWidgetComponent>(Widgets);
		for (UWidgetComponent* Widget : Widgets)
		{
			if (Widget->ComponentHasTag(CursorTag))
			{
				Cursor = Widget;
				Widget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Widget->SetVisibility(false);
				break;
			}
		}
	}
}

void UGazeInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn && !Pawn->IsLocallyControlled())
	{
		DwellProgress = 0.0f;
		return;
	}
	UpdateRay();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateDwell(DeltaTime);
	UpdateCursor();
}

void UGazeInteractionComponent::UpdateDwell(float DeltaTime)
{
	TSharedPtr<SWidget> Hovered;
	if (IsOverInteractableWidget() && LastWidgetPath.IsValid())
	{
		Hovered = LastWidgetPath.GetLastWidget().Pin();
	}
	if (!Hovered.IsValid() || Hovered != DwellTarget.Pin())
	{
		DwellTarget = Hovered;
		DwellElapsed = 0.0f;
		bFired = false;
	}
	if (!Hovered.IsValid() || bFired)
	{
		DwellProgress = 0.0f;
		return;
	}

	DwellElapsed += DeltaTime;
	const float Needed = DwellTimeFor(GetHoveredWidgetComponent());
	DwellProgress = FMath::Clamp(DwellElapsed / Needed, 0.0f, 1.0f);
	if (DwellElapsed >= Needed)
	{
		PressPointerKey(EKeys::LeftMouseButton);
		ReleasePointerKey(EKeys::LeftMouseButton);
		bFired = true;
		DwellProgress = 0.0f;
		OnDwellCompleted.Broadcast(GetHoveredWidgetComponent());
	}
}

void UGazeInteractionComponent::UpdateRay()
{
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	bEyeGaze = false;

	if (bUseEyeTracking && UEyeTrackerFunctionLibrary::IsEyeTrackerConnected())
	{
		FEyeTrackerGazeData Gaze;
		if (UEyeTrackerFunctionLibrary::GetGazeData(Gaze) && Gaze.ConfidenceValue >= MinConfidence && !Gaze.GazeDirection.IsNearlyZero())
		{
			Origin = Gaze.GazeOrigin;
			Direction = Gaze.GazeDirection;
			bEyeGaze = true;
		}
	}
	if (!bEyeGaze && Camera.IsValid())
	{
		Origin = Camera->GetComponentLocation();
		Direction = Camera->GetForwardVector();
	}
	SetWorldLocationAndRotation(Origin, Direction.Rotation());
}

void UGazeInteractionComponent::UpdateCursor()
{
	if (!Cursor.IsValid())
	{
		return;
	}
	const bool bActive = DwellProgress > 0.0f;
	Cursor->SetVisibility(bActive);
	if (!bActive)
	{
		return;
	}
	const FVector Point = GetGazePoint();
	const FVector ToEye = (GetComponentLocation() - Point).GetSafeNormal();
	Cursor->SetWorldLocationAndRotation(Point + ToEye * CursorOffset, ToEye.Rotation());
	if (UDwellCursorWidget* Widget = Cast<UDwellCursorWidget>(Cursor->GetUserWidgetObject()))
	{
		Widget->SetProgress(DwellProgress);
	}
}

float UGazeInteractionComponent::DwellTimeFor(const UWidgetComponent* Component) const
{
	if (Component)
	{
		for (const FName& Tag : Component->ComponentTags)
		{
			if (const float* Time = DwellTimeByTag.Find(Tag))
			{
				return FMath::Max(*Time, 0.05f);
			}
		}
	}
	return DefaultDwellTime;
}

FVector UGazeInteractionComponent::GetGazePoint() const
{
	const FHitResult& Hit = GetLastHitResult();
	return Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : GetComponentLocation() + GetForwardVector() * InteractionDistance;
}
