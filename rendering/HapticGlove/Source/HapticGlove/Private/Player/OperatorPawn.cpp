#include "Player/OperatorPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "HapticGloveLog.h"
#include "IXRTrackingSystem.h"
#include "InputCoreTypes.h"
#include "Networking/SimLinkSubsystem.h"
#include "Recording/LogCameraComponent.h"
#include "UI/BodyAnchorComponent.h"
#include "UI/GazeInteractionComponent.h"
#include "UI/SimWidgets.h"

AOperatorPawn::AOperatorPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	RootComponent = VROrigin;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);
	Camera->bLockToHmd = true;

	Gaze = CreateDefaultSubobject<UGazeInteractionComponent>(TEXT("Gaze"));
	Gaze->SetupAttachment(VROrigin);

	Tray = CreateDefaultSubobject<UBodyAnchorComponent>(TEXT("Tray"));
	Tray->SetupAttachment(VROrigin);

	OperatorView = CreateDefaultSubobject<ULogCameraComponent>(TEXT("OperatorView"));
	OperatorView->SetupAttachment(Camera);
	OperatorView->StreamName = TEXT("operator");
}

void AOperatorPawn::BeginPlay()
{
	Super::BeginPlay();
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		GEngine->XRSystem->SetTrackingOrigin(EHMDTrackingOrigin::Stage);
	}
	SetDebugPanelVisible(bDebugPanelVisible);
	Link = GetWorld()->GetSubsystem<USimLinkSubsystem>();
	if (Link)
	{
		Link->OnTrackingChanged.AddDynamic(this, &AOperatorPawn::HandleTrackingChanged);
		if (Link->HasScene())
		{
			ApplyTracking(Link->GetSceneRef().Tracking);
		}
	}
}

void AOperatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Link)
	{
		Link->OnTrackingChanged.RemoveDynamic(this, &AOperatorPawn::HandleTrackingChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void AOperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AOperatorPawn::ResetSame);
	PlayerInputComponent->BindKey(EKeys::N, IE_Pressed, this, &AOperatorPawn::ResetNew);
	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &AOperatorPawn::Calibrate);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AOperatorPawn::ToggleStop);
	PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &AOperatorPawn::ToggleHand);
	PlayerInputComponent->BindKey(EKeys::D, IE_Pressed, this, &AOperatorPawn::ToggleDebugPanel);
}

void AOperatorPawn::ToggleDebugPanel()
{
	SetDebugPanelVisible(!bDebugPanelVisible);
}

void AOperatorPawn::SetDebugPanelVisible(bool bVisible)
{
	bDebugPanelVisible = bVisible;
	int32 Count = 0;
	TArray<UWidgetComponent*> Widgets;
	GetComponents<UWidgetComponent>(Widgets);
	for (UWidgetComponent* Widget : Widgets)
	{
		const UClass* WidgetClass = Widget->GetWidgetClass();
		if (Widget->ComponentHasTag(TEXT("Debug")) || (WidgetClass && WidgetClass->IsChildOf(UDebugPanelWidget::StaticClass())))
		{
			Widget->SetVisibility(bVisible, true);
			Widget->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			Count += 1;
		}
	}
	UE_LOG(LogHapticGlove, Log, TEXT("Debug panel %s (%d components on %s)"), bVisible ? TEXT("shown") : TEXT("hidden"), Count, *GetName());
}

void AOperatorPawn::ApplyTracking(const FSimTracking& Tracking)
{
	if (!Tracking.bValid)
	{
		UE_LOG(LogHapticGlove, Log, TEXT("No tracking calibration yet, VR origin stays at the player start"));
		return;
	}
	SetActorLocationAndRotation(Tracking.Position, FRotator(0.0, -FMath::RadiansToDegrees(static_cast<double>(Tracking.YawRad)), 0.0));
	UE_LOG(LogHapticGlove, Log, TEXT("VR origin aligned to calibration %s"), *Tracking.Created);
	OnTrackingApplied(Tracking);
}

void AOperatorPawn::HandleTrackingChanged(const FSimTracking& Tracking)
{
	ApplyTracking(Tracking);
}

void AOperatorPawn::ResetSame()
{
	if (Link)
	{
		Link->SendCommand(TEXT("reset_same"));
	}
}

void AOperatorPawn::ResetNew()
{
	if (Link)
	{
		Link->SendCommand(TEXT("reset_new"));
	}
}

void AOperatorPawn::Calibrate()
{
	if (Link)
	{
		Link->Calibrate();
	}
}

void AOperatorPawn::ToggleStop()
{
	if (Link)
	{
		Link->ToggleStop();
	}
}

void AOperatorPawn::ToggleHand()
{
	if (Link)
	{
		Link->SetHand(FString());
	}
}
