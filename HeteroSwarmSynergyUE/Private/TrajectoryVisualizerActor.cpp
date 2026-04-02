// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryVisualizerActor.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrajectoryVisualizerActor, Log, All);

ATrajectoryVisualizerActor::ATrajectoryVisualizerActor()
    : TrajectorySegmentMesh(nullptr)
    , PlannedTrajectoryMaterial(nullptr)
    , ActualTrajectoryMaterial(nullptr)
    , PlannedWidthCm(20.0f)
    , ActualWidthCm(14.0f)
    , PlannedVerticalOffsetCm(40.0f)
    , ActualVerticalOffsetCm(0.0f)
    , ForwardAxis(ESplineMeshAxis::X)
    , RefreshIntervalSeconds(0.20f)
    , DeviceID(INDEX_NONE)
    , bHasPlannedTrajectory(false)
    , bHasActualTrajectory(false)
    , TrajectoryManager(nullptr)
    , TimeSinceLastRefresh(0.0f)
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
    RootComponent = RootSceneComponent;

    PlannedSplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("PlannedSplineComponent"));
    PlannedSplineComponent->SetupAttachment(RootSceneComponent);
    PlannedSplineComponent->SetClosedLoop(false);

    ActualSplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("ActualSplineComponent"));
    ActualSplineComponent->SetupAttachment(RootSceneComponent);
    ActualSplineComponent->SetClosedLoop(false);
}

void ATrajectoryVisualizerActor::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTrajectoryVisualizerActor, Log,
        TEXT("TrajectoryVisualizer BeginPlay: %s (DeviceID=%d)"),
        *GetName(), DeviceID);
}

void ATrajectoryVisualizerActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!TrajectoryManager || DeviceID <= 0)
    {
        return;
    }

    TimeSinceLastRefresh += DeltaSeconds;
    if (TimeSinceLastRefresh >= RefreshIntervalSeconds)
    {
        RefreshAllTrajectories();
        TimeSinceLastRefresh = 0.0f;
    }
}

void ATrajectoryVisualizerActor::InitializeVisualizer(int32 InDeviceID, UTrajectoryManager* InTrajectoryManager)
{
    DeviceID = InDeviceID;
    TrajectoryManager = InTrajectoryManager;
    TimeSinceLastRefresh = 0.0f;

    UE_LOG(LogTrajectoryVisualizerActor, Log,
        TEXT("InitializeVisualizer: Actor=%s DeviceID=%d"),
        *GetName(), DeviceID);

    RefreshAllTrajectories();
}

void ATrajectoryVisualizerActor::RefreshAllTrajectories()
{
    RefreshTrajectory(ETrajectoryType::Planned);
    RefreshTrajectory(ETrajectoryType::Actual);
}

void ATrajectoryVisualizerActor::RefreshTrajectory(ETrajectoryType TrajectoryType)
{
    if (!TrajectoryManager || DeviceID <= 0)
    {
        return;
    }

    if (TrajectoryType == ETrajectoryType::Planned)
    {
        RefreshTrajectoryInternal(
            ETrajectoryType::Planned,
            PlannedSplineComponent,
            PlannedSplineMeshes,
            PlannedTrajectoryMaterial,
            PlannedWidthCm,
            PlannedVerticalOffsetCm,
            bHasPlannedTrajectory);
    }
    else
    {
        RefreshTrajectoryInternal(
            ETrajectoryType::Actual,
            ActualSplineComponent,
            ActualSplineMeshes,
            ActualTrajectoryMaterial,
            ActualWidthCm,
            ActualVerticalOffsetCm,
            bHasActualTrajectory);
    }
}

void ATrajectoryVisualizerActor::ClearTrajectory(ETrajectoryType TrajectoryType)
{
    if (TrajectoryType == ETrajectoryType::Planned)
    {
        ClearSpline(PlannedSplineComponent, PlannedSplineMeshes, bHasPlannedTrajectory);
    }
    else
    {
        ClearSpline(ActualSplineComponent, ActualSplineMeshes, bHasActualTrajectory);
    }
}

void ATrajectoryVisualizerActor::RefreshTrajectoryInternal(
    ETrajectoryType TrajectoryType,
    USplineComponent* SplineComponent,
    TArray<USplineMeshComponent*>& SplineMeshes,
    UMaterialInterface* Material,
    float WidthCm,
    float VerticalOffsetCm,
    bool& bOutHasTrajectory)
{
    if (!TrajectoryManager || !SplineComponent)
    {
        return;
    }

    const TArray<FVector> Points = TrajectoryManager->GetTrajectoryPoints(DeviceID, TrajectoryType);

    if (Points.Num() <= 0)
    {
        ClearSpline(SplineComponent, SplineMeshes, bOutHasTrajectory);
        return;
    }

    bOutHasTrajectory = true;

    SplineComponent->ClearSplinePoints(false);

    for (const FVector& Point : Points)
    {
        const FVector OffsetPoint = Point + FVector(0.0f, 0.0f, VerticalOffsetCm);
        SplineComponent->AddSplinePoint(OffsetPoint, ESplineCoordinateSpace::World, false);
    }

    SplineComponent->UpdateSpline();

    RebuildSplineMeshes(SplineComponent, SplineMeshes, Material, WidthCm);
}

void ATrajectoryVisualizerActor::RebuildSplineMeshes(
    USplineComponent* SplineComponent,
    TArray<USplineMeshComponent*>& SplineMeshes,
    UMaterialInterface* Material,
    float WidthCm)
{
    ClearSplineMeshes(SplineMeshes);

    if (!SplineComponent)
    {
        return;
    }

    const int32 NumPoints = SplineComponent->GetNumberOfSplinePoints();
    if (NumPoints < 2)
    {
        return;
    }

    if (!TrajectorySegmentMesh)
    {
        UE_LOG(LogTrajectoryVisualizerActor, Warning,
            TEXT("TrajectorySegmentMesh is null for %s, spline meshes will not be created"),
            *GetName());
        return;
    }

    for (int32 Index = 0; Index < NumPoints - 1; ++Index)
    {
        USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>(this);
        if (!SplineMesh)
        {
            continue;
        }

        SplineMesh->SetMobility(EComponentMobility::Movable);
        SplineMesh->SetStaticMesh(TrajectorySegmentMesh);
        SplineMesh->SetForwardAxis(static_cast<ESplineMeshAxis::Type>(ForwardAxis.GetValue()), true);
        SplineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SplineMesh->SetCastShadow(false);
        SplineMesh->SetupAttachment(SplineComponent);

        if (Material)
        {
            SplineMesh->SetMaterial(0, Material);
        }

        FVector StartPos, StartTangent;
        FVector EndPos, EndTangent;

        SplineComponent->GetLocationAndTangentAtSplinePoint(
            Index, StartPos, StartTangent, ESplineCoordinateSpace::Local);

        SplineComponent->GetLocationAndTangentAtSplinePoint(
            Index + 1, EndPos, EndTangent, ESplineCoordinateSpace::Local);

        SplineMesh->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent);
        SplineMesh->SetStartScale(FVector2D(WidthCm, WidthCm));
        SplineMesh->SetEndScale(FVector2D(WidthCm, WidthCm));
        SplineMesh->RegisterComponent();

        SplineMeshes.Add(SplineMesh);
    }
}

void ATrajectoryVisualizerActor::ClearSplineMeshes(TArray<USplineMeshComponent*>& SplineMeshes)
{
    for (USplineMeshComponent* MeshComp : SplineMeshes)
    {
        if (IsValid(MeshComp))
        {
            MeshComp->DestroyComponent();
        }
    }

    SplineMeshes.Empty();
}

void ATrajectoryVisualizerActor::ClearSpline(
    USplineComponent* SplineComponent,
    TArray<USplineMeshComponent*>& SplineMeshes,
    bool& bHasTrajectory)
{
    bHasTrajectory = false;

    if (SplineComponent)
    {
        SplineComponent->ClearSplinePoints(false);
        SplineComponent->UpdateSpline();
    }

    ClearSplineMeshes(SplineMeshes);
}