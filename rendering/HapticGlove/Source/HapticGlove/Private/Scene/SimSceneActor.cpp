#include "Scene/SimSceneActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "HapticGloveLog.h"
#include "Networking/SimLinkSubsystem.h"
#include "Scene/EnvironmentSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FVector GhostPalmHalf(4.5, 4.0, 1.2);
	const FVector GhostPalmCenter(4.5, 0.0, 0.0);
	const FVector GhostFingersHalf(3.5, 3.5, 0.8);
	const FVector GhostFingersCenter(12.5, 0.0, 0.0);
	constexpr double LegHalfWidth = 2.0;
	constexpr double LegInset = 4.0;
}

ASimSceneActor::ASimSceneActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Basic(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object;
	SphereMesh = Sphere.Object;
	CylinderMesh = Cylinder.Object;
	SolidMaterial = Basic.Object;
}

void ASimSceneActor::BeginPlay()
{
	Super::BeginPlay();
	Link = GetWorld()->GetSubsystem<USimLinkSubsystem>();
	if (Link)
	{
		Link->OnSceneChanged.AddDynamic(this, &ASimSceneActor::HandleSceneChanged);
		if (Link->HasScene())
		{
			Build(Link->GetSceneRef());
		}
	}
}

void ASimSceneActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Link)
	{
		Link->OnSceneChanged.RemoveDynamic(this, &ASimSceneActor::HandleSceneChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void ASimSceneActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!Link)
	{
		return;
	}
	Link->Refresh();
	if (Link->HasState() && Link->GetSceneRef().SceneId == BuiltSceneId)
	{
		ApplyState(Link->GetState());
	}
}

void ASimSceneActor::HandleSceneChanged(const FSimScene& Scene)
{
	Build(Scene);
}

void ASimSceneActor::Clear()
{
	for (USceneComponent* Component : Created)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	Created.Reset();
	BodyNodes.Reset();
	ObjectNodes.Reset();
	SquashMeshes.Reset();
	FingerTints.Reset();
	ObjectRadius.Reset();
	SquashRest.Reset();
	GhostNode = nullptr;
	BuiltSceneId.Reset();
}

void ASimSceneActor::Build(const FSimScene& Scene)
{
	Clear();
	if (!CubeMesh || !SphereMesh || !CylinderMesh || !SolidMaterial)
	{
		UE_LOG(LogHapticGlove, Error, TEXT("SimScene: missing assets (cube %d, sphere %d, cylinder %d, material %d)"), CubeMesh != nullptr, SphereMesh != nullptr, CylinderMesh != nullptr, SolidMaterial != nullptr);
	}

	const FHapticEnvironment* Environment = nullptr;
	if (UEnvironmentSubsystem* Environments = GetWorld()->GetSubsystem<UEnvironmentSubsystem>())
	{
		Environments->Apply(Scene);
		Environment = Environments->Current();
	}
	UMaterialInterface* TableBase = Environment ? Environment->TableMaterial.LoadSynchronous() : nullptr;

	const FSimTable& T = Scene.Table;
	UMaterialInstanceDynamic* TableMaterial = MakeMaterial(TableBase ? TableBase : SolidMaterial.Get(), TableBase ? FLinearColor::White : T.Color);
	USceneComponent* Table = AddNode(RootComponent);
	AddMesh(Table, CubeMesh, FTransform(FQuat::Identity, T.TopCenter, T.Size / 100.0), TableMaterial);
	if (Environment ? Environment->bTableLegs : bTableLegs)
	{
		UMaterialInterface* LegBase = Environment ? Environment->LegMaterial.LoadSynchronous() : nullptr;
		UMaterialInstanceDynamic* LegMaterial = LegBase ? MakeMaterial(LegBase, FLinearColor::White) : TableMaterial;
		const double LegHeight = T.Height - T.Size.Z;
		for (const FVector2D Sign : {FVector2D(1.0, 1.0), FVector2D(1.0, -1.0), FVector2D(-1.0, 1.0), FVector2D(-1.0, -1.0)})
		{
			const FVector Center(T.TopCenter.X + Sign.X * (0.5 * T.Size.X - LegInset), T.TopCenter.Y + Sign.Y * (0.5 * T.Size.Y - LegInset), 0.5 * LegHeight);
			AddMesh(Table, CubeMesh, FTransform(FQuat::Identity, Center, FVector(LegHalfWidth / 50.0, LegHalfWidth / 50.0, LegHeight / 100.0)), LegMaterial);
		}
	}

	for (const FSimObject& Object : Scene.Objects)
	{
		USceneComponent* Node = AddNode(RootComponent);
		Node->SetWorldTransform(Object.Initial);
		UStaticMeshComponent* Mesh = AddVisual(Node, Object);
		if (!Mesh)
		{
			Mesh = AddShape(Node, Object.Shape, Object.Size, FTransform::Identity, MakeMaterial(SolidMaterial, Object.Color));
		}
		const bool bSquash = Mesh && Object.bDeformable && Object.Shape == TEXT("sphere");
		ObjectNodes.Add(Node);
		SquashMeshes.Add(bSquash ? Mesh : nullptr);
		SquashRest.Add(Mesh ? Mesh->GetRelativeTransform() : FTransform::Identity);
		ObjectRadius.Add(Object.Size.X);
	}

	for (int32 b = 0; b < Scene.HandBodies.Num(); ++b)
	{
		BodyNodes.Add(AddNode(RootComponent));
	}
	for (const FSimGeom& Geom : Scene.HandGeoms)
	{
		if (!BodyNodes.IsValidIndex(Geom.Body))
		{
			continue;
		}
		UMaterialInstanceDynamic* Material = MakeMaterial(SolidMaterial, Geom.Color);
		AddShape(BodyNodes[Geom.Body], Geom.Type, Geom.Size, Geom.Local, Material);
		const int32 Channel = ChannelForBody(Scene.HandBodies[Geom.Body]);
		if (Channel >= 0)
		{
			FFingerTint& Tint = FingerTints.AddDefaulted_GetRef();
			Tint.Material = Material;
			Tint.Base = Geom.Color;
			Tint.Channel = Channel;
		}
	}

	GhostNode = AddNode(RootComponent);
	UMaterialInstanceDynamic* Ghost = MakeMaterial(GhostMaterial ? GhostMaterial.Get() : SolidMaterial.Get(), GhostColor);
	AddShape(GhostNode, TEXT("box"), GhostPalmHalf, FTransform(GhostPalmCenter), Ghost);
	AddShape(GhostNode, TEXT("box"), GhostFingersHalf, FTransform(GhostFingersCenter), Ghost);
	GhostNode->SetVisibility(false, true);

	BuiltSceneId = Scene.SceneId;
	UE_LOG(LogHapticGlove, Log, TEXT("SimScene: built %s with %d objects, %d hand bodies, %d hand geoms, %d components"), *Scene.SceneId, ObjectNodes.Num(), BodyNodes.Num(), Scene.HandGeoms.Num(), Created.Num());
	OnSceneBuilt(Scene);
}

void ASimSceneActor::ApplyState(const FSimState& State)
{
	for (int32 i = 0; i < FMath::Min(BodyNodes.Num(), State.Bodies.Num()); ++i)
	{
		BodyNodes[i]->SetWorldTransform(State.Bodies[i]);
	}
	for (int32 i = 0; i < FMath::Min(ObjectNodes.Num(), State.Objects.Num()); ++i)
	{
		ObjectNodes[i]->SetWorldTransform(State.Objects[i].Pose);
		ApplySquash(i, State.Objects[i]);
	}
	const bool bGhost = (State.Flags & SimProtocol::FlagGhost) != 0;
	if (GhostNode)
	{
		GhostNode->SetVisibility(bGhost, true);
		if (bGhost)
		{
			GhostNode->SetWorldTransform(State.Ghost);
		}
	}
	for (const FFingerTint& Tint : FingerTints)
	{
		const float Alpha = FMath::Clamp(State.Feedback[Tint.Channel] / FullScaleFeedback, 0.0f, 1.0f);
		Tint.Material->SetVectorParameterValue(ColorParameter, FMath::Lerp(Tint.Base, ForceColor, Alpha));
	}
}

void ASimSceneActor::ApplySquash(int32 Index, const FSimObjectState& Object)
{
	UStaticMeshComponent* Mesh = SquashMeshes[Index];
	if (!Mesh)
	{
		return;
	}
	const double Radius = ObjectRadius[Index];
	const FTransform& Rest = SquashRest[Index];
	if (Object.SquashDepth > 0.01f && !Object.SquashDir.IsNearlyZero())
	{
		const FVector Dir = Object.SquashDir.GetSafeNormal();
		const double Squeeze = FMath::Clamp(1.0 - Object.SquashDepth / (2.0 * Radius), 1.0 - MaxSquash, 1.0);
		const double Bulge = 1.0 / FMath::Sqrt(Squeeze);
		Mesh->SetWorldLocationAndRotation(ObjectNodes[Index]->GetComponentLocation() + Dir * (0.5 * Object.SquashDepth), FRotationMatrix::MakeFromZ(Dir).ToQuat());
		Mesh->SetWorldScale3D(FVector(Bulge, Bulge, Squeeze) * Rest.GetScale3D().X);
	}
	else
	{
		Mesh->SetRelativeTransform(Rest);
	}
}

USceneComponent* ASimSceneActor::AddNode(USceneComponent* Parent)
{
	USceneComponent* Node = NewObject<USceneComponent>(this);
	Node->SetMobility(EComponentMobility::Movable);
	Node->SetupAttachment(Parent);
	Node->RegisterComponent();
	Created.Add(Node);
	return Node;
}

UStaticMeshComponent* ASimSceneActor::AddMesh(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Local, UMaterialInstanceDynamic* Material)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetMaterial(0, Material);
	Component->SetRelativeTransform(Local);
	Component->SetupAttachment(Parent);
	Component->RegisterComponent();
	Created.Add(Component);
	return Component;
}

UStaticMeshComponent* ASimSceneActor::AddVisual(USceneComponent* Parent, const FSimObject& Object)
{
	if (Object.Visual.IsEmpty())
	{
		return nullptr;
	}
	const TSoftObjectPtr<UStaticMesh>* Entry = GetDefault<UHapticGloveSettings>()->ObjectVisuals.Find(Object.Visual);
	UStaticMesh* Mesh = Entry ? Entry->LoadSynchronous() : nullptr;
	if (!Mesh)
	{
		if (!MissingVisuals.Contains(Object.Visual))
		{
			MissingVisuals.Add(Object.Visual);
			UE_LOG(LogHapticGlove, Warning, TEXT("SimScene: visual '%s' is not mapped in Project Settings > Haptic Glove > Object Visuals, using the primitive"), *Object.Visual);
		}
		return nullptr;
	}
	const FBox Bounds = Mesh->GetBoundingBox();
	const FVector Extent = Bounds.GetExtent().ComponentMax(FVector(0.01));
	FVector Half = Object.Size;
	if (Object.Shape == TEXT("sphere"))
	{
		Half = FVector(Object.Size.X);
	}
	else if (Object.Shape == TEXT("cylinder") || Object.Shape == TEXT("capsule"))
	{
		Half = FVector(Object.Size.X, Object.Size.X, Object.Shape == TEXT("capsule") ? Object.Size.X + Object.Size.Y : Object.Size.Y);
	}
	FVector Scale = Half / Extent;
	if (Object.Shape == TEXT("sphere"))
	{
		Scale = FVector(Scale.GetMin());
	}
	UStaticMeshComponent* Component = AddMesh(Parent, Mesh, FTransform(FQuat::Identity, -Bounds.GetCenter() * Scale, Scale), nullptr);
	Component->EmptyOverrideMaterials();
	return Component;
}

UStaticMeshComponent* ASimSceneActor::AddShape(USceneComponent* Parent, const FString& Type, const FVector& Size, const FTransform& Local, UMaterialInstanceDynamic* Material)
{
	const FQuat Rotation = Local.GetRotation();
	const FVector Location = Local.GetLocation();
	if (Type == TEXT("box"))
	{
		return AddMesh(Parent, CubeMesh, FTransform(Rotation, Location, Size / 50.0), Material);
	}
	if (Type == TEXT("sphere"))
	{
		return AddMesh(Parent, SphereMesh, FTransform(Rotation, Location, FVector(Size.X / 50.0)), Material);
	}
	if (Type == TEXT("ellipsoid"))
	{
		return AddMesh(Parent, SphereMesh, FTransform(Rotation, Location, Size / 50.0), Material);
	}
	if (Type == TEXT("cylinder"))
	{
		return AddMesh(Parent, CylinderMesh, FTransform(Rotation, Location, FVector(Size.X / 50.0, Size.X / 50.0, Size.Y / 50.0)), Material);
	}
	if (Type == TEXT("capsule"))
	{
		USceneComponent* Capsule = AddNode(Parent);
		Capsule->SetRelativeTransform(FTransform(Rotation, Location));
		const double R = Size.X;
		const double H = Size.Y;
		UStaticMeshComponent* Body = AddMesh(Capsule, CylinderMesh, FTransform(FQuat::Identity, FVector::ZeroVector, FVector(R / 50.0, R / 50.0, H / 50.0)), Material);
		AddMesh(Capsule, SphereMesh, FTransform(FQuat::Identity, FVector(0.0, 0.0, H), FVector(R / 50.0)), Material);
		AddMesh(Capsule, SphereMesh, FTransform(FQuat::Identity, FVector(0.0, 0.0, -H), FVector(R / 50.0)), Material);
		return Body;
	}
	return nullptr;
}

UMaterialInstanceDynamic* ASimSceneActor::MakeMaterial(UMaterialInterface* Base, const FLinearColor& Color)
{
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base ? Base : SolidMaterial.Get(), this);
	Material->SetVectorParameterValue(ColorParameter, Color);
	return Material;
}

int32 ASimSceneActor::ChannelForBody(const FString& Name)
{
	if (Name.Contains(TEXT("thumb")) || Name == TEXT("jaw_right"))
	{
		return 0;
	}
	if (Name.Contains(TEXT("index")) || Name == TEXT("jaw_left"))
	{
		return 1;
	}
	if (Name.Contains(TEXT("middle")) || Name.Contains(TEXT("ring")) || Name.Contains(TEXT("pinky")))
	{
		return 2;
	}
	return -1;
}
