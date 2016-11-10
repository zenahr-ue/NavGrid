// Fill out your copyright notice in the Description page of Project Settings.

#include "NavGridPrivatePCH.h"
#include <limits>

UNavTileComponent::UNavTileComponent(const FObjectInitializer &ObjectInitializer)
	:Super(ObjectInitializer)
{
	SetComponentTickEnabled(false);
	bUseAttachParentBound = true;

	Extent = ObjectInitializer.CreateDefaultSubobject<UBoxComponent>(this, "Extent");
	Extent->SetupAttachment(this);
	Extent->SetBoxExtent(FVector(100, 100, 5));
	Extent->ShapeColor = FColor::Magenta;
	
	Extent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Extent->SetCollisionResponseToChannel(ECollisionChannel::ECC_Visibility, ECollisionResponse::ECR_Block); // So we get mouse over events
	Extent->SetCollisionResponseToChannel(ANavGrid::ECC_Walkable, ECollisionResponse::ECR_Block); // So we can find the floor with a line trace
	Extent->OnBeginCursorOver.AddDynamic(this, &UNavTileComponent::CursorOver);
	Extent->OnEndCursorOver.AddDynamic(this, &UNavTileComponent::EndCursorOver);
	Extent->OnClicked.AddDynamic(this, &UNavTileComponent::Clicked);

	PawnLocationOffset = FVector::ZeroVector;

	TArray<USceneComponent *> Components;
	GetChildrenComponents(true, Components);
	for (USceneComponent *Comp : Components)
	{
		Comp->SetComponentTickEnabled(false);
		Comp->bUseAttachParentBound = true;
	}
}

void UNavTileComponent::BeginPlay()
{
	if (!Grid)
	{
		Grid = ANavGrid::GetNavGrid(GetWorld());
		if (!Grid)
		{
			UE_LOG(NavGrid, Error, TEXT("%s: Unable to find NavGrid"), *GetName());
		}
	}
}

void UNavTileComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	Grid = ANavGrid::GetNavGrid(GetWorld());
}

bool UNavTileComponent::Traversable(float MaxWalkAngle, const TArray<EGridMovementMode>& AvailableMovementModes) const
{
	FRotator TileRot = GetComponentRotation();
	float MaxAngle = FMath::Max3<float>(TileRot.Pitch, TileRot.Yaw, TileRot.Roll);
	float MinAngle = FMath::Min3<float>(TileRot.Pitch, TileRot.Yaw, TileRot.Roll);
	if (AvailableMovementModes.Contains(EGridMovementMode::Walking) &&
		(MaxAngle < MaxWalkAngle && MinAngle > -MaxWalkAngle))
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool UNavTileComponent::LegalPositionAtEndOfTurn(float MaxWalkAngle, const TArray<EGridMovementMode> &AvailableMovementModes) const
{
	return Traversable(MaxWalkAngle, AvailableMovementModes);
}

FVector UNavTileComponent::GetPawnLocation()
{
	return GetComponentLocation() + GetComponentRotation().RotateVector(PawnLocationOffset);
}

void UNavTileComponent::SetPawnLocationOffset(const FVector &Offset)
{
	PawnLocationOffset = Offset;
}

void UNavTileComponent::ResetPath()
{
	Distance = std::numeric_limits<float>::infinity();
	Backpointer = NULL;
	Visited = false;
}

TArray<FVector>* UNavTileComponent::GetContactPoints()
{	
	if (!ContactPoints.Num())
	{
		int32 XExtent = Extent->GetScaledBoxExtent().X;
		int32 YExtent = Extent->GetScaledBoxExtent().Y;
		for (int32 X = -XExtent; X <= XExtent; X += XExtent)
		{
			for (int32 Y = -YExtent; Y <= YExtent; Y += YExtent)
			{
				FVector PointLocation = GetComponentRotation().RotateVector(FVector(X, Y, 0));
				FVector WorldLocation = GetComponentLocation() + PointLocation;
				ContactPoints.Add(WorldLocation);
			}
		}
	}
	return &ContactPoints;
}

TArray<UNavTileComponent*>* UNavTileComponent::GetNeighbours()
{
	float MaxDistance = Extent->GetScaledBoxExtent().X * 0.9;

	// Find neighbours if we have not already done so
	if (!Neighbours.Num())
	{
		for (TObjectIterator<UNavTileComponent> Itr; Itr; ++Itr)
		{
			if (Itr->GetWorld() == GetWorld() && *Itr != this)
			{
				bool Added = false; // stop comparing CPs when we know a tile is a neighbour
				for (const FVector &OtherCP : *Itr->GetContactPoints())
				{
					for (const FVector &MyCP : *GetContactPoints())
					{
						if ((OtherCP - MyCP).Size() < MaxDistance)
						{
							Neighbours.Add(*Itr);
							Added = true;
							break;
						}
					}
					if (Added) { break; }
				}
			}
		}
	}
	return &Neighbours;
}

bool UNavTileComponent::Obstructed(const FVector &FromPos, const UCapsuleComponent &CollisionCapsule)
{
	return Obstructed(FromPos, PawnLocationOffset + GetComponentLocation(), CollisionCapsule);
}

bool UNavTileComponent::Obstructed(const FVector & From, const FVector & To, const UCapsuleComponent & CollisionCapsule)
{
	FHitResult OutHit;
	FVector Start = From + CollisionCapsule.RelativeLocation;
	FVector End = To + CollisionCapsule.RelativeLocation;
	FQuat Rot = FQuat::Identity;
	FCollisionShape CollisionShape = CollisionCapsule.GetCollisionShape();
	FCollisionQueryParams CQP;
	CQP.AddIgnoredActor(CollisionCapsule.GetOwner());
	FCollisionResponseParams CRP;
	bool HitSomething = CollisionCapsule.GetWorld()->SweepSingleByChannel(OutHit, Start, End, Rot, ECollisionChannel::ECC_Pawn, CollisionShape, CQP, CRP);
/*
	if (HitSomething)
	{
		DrawDebugLine(CollisionCapsule.GetWorld(), Start, End, FColor::Red, true);
	}
	else
	{
		DrawDebugLine(CollisionCapsule.GetWorld(), Start, End, FColor::Green, true);
	}
*/
	return HitSomething;
}

void UNavTileComponent::GetUnobstructedNeighbours(const UCapsuleComponent &CollisionCapsule, TArray<UNavTileComponent *> &OutNeighbours)
{
	OutNeighbours.Empty();
	for (auto N : *GetNeighbours())
	{
		if (!N->Obstructed(PawnLocationOffset + GetComponentLocation(), CollisionCapsule)) 
		{ 
			OutNeighbours.Add(N);
		}
	}
}

void UNavTileComponent::Clicked(UPrimitiveComponent* TouchedComponent, FKey Key)
{
	if (Grid)
	{
		Grid->TileClicked(*this);
	}
}

void UNavTileComponent::CursorOver(UPrimitiveComponent* TouchedComponent)
{
	if (Grid)
	{
		Grid->Cursor->SetWorldLocation(GetPawnLocation() + FVector(0, 0, 30));
		Grid->Cursor->SetVisibility(true);
		Grid->TileCursorOver(*this);
	}
}

void UNavTileComponent::EndCursorOver(UPrimitiveComponent* TouchedComponent)
{
	if (Grid)
	{
		Grid->Cursor->SetVisibility(false);
		Grid->EndTileCursorOver(*this);
	}
}

void UNavTileComponent::AddSplinePoints(const FVector &FromPos, USplineComponent &OutSpline, bool EndTile)
{
	OutSpline.AddSplinePoint(GetComponentLocation() + PawnLocationOffset, ESplineCoordinateSpace::Local);
}

FVector UNavTileComponent::GetSplineMeshUpVector()
{
	return FVector(0, 0, 1);
}

void UNavTileComponent::DestroyComponent(bool PromoteChildren /*= false*/)
{
	Extent->DestroyComponent();

	Super::DestroyComponent(PromoteChildren);
}
