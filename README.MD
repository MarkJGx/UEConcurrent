# Unreal Concurrent
> [!NOTE]
> A code library that aims to make Unreal concurrency code safer and easier.

## Examples:
### TReadWriteLock<FYourType>
```cpp
// A wrapper for a default constructed type (e.g., array, map) that ensures
// thread safety through its accessors, allowing for locked writes and
// unsafe reads with runtime thread safety assertions.

ActorToPrimitiveComponentLayout.ReadWriteLocked(
[&UniqueEncounteredActors](typename decltype(ActorToPrimitiveComponentLayout)::ElementType& Map)
{
	// Map writing isn't thread safe! So read write locked.
	Map.Reserve(UniqueEncounteredActors.Num());
});
```


```cpp
ActorToPrimitiveComponentLayout.ReadUnsafe(
	[&ActorToPrimitiveComponentLayout, 
	&PrimitiveLayouts, 
	&SpawnTask]
	(const auto& Map)
{
	// However reading is totally safe! No lock required.
	// ReadUnsafe will assert if a write is being performed,
	// since ReadUnsafe is completely lockless.
	PrimitiveLayouts = Map.Find(SpawnTask.ActorToSpawn);
});
```

### UE::Concurrent::InlineParallelFor
```cpp
// Allows for quick testing of compiler vectorized
//  non parallel variant through template magic, simply swap to <EParallelForFlags::ForceSingleThread>

UE::Concurrent::TReadWriteLock<
	Experimental::TRobinHoodHashMap<TSubclassOf<AActor>, FPaddedBox3f>> BoundsMap;
{
	TArray<TSubclassOf<AActor>> ActorsEncountered = UniqueActorsEncountered.Array();
	UE::Concurrent::InlineParallelFor<EParallelForFlags::None>(
		ActorsEncountered.Num(), [this, &ActorsEncountered, &BoundsMap](int32 Index)
		{
			const TSubclassOf<AActor>& ActorClass = ActorsEncountered[Index];
			// Read actor bounds (thread safe and expensive)
			FPaddedBox3f CalculatedBounds = UE::Actor::GetActorTemplateLocalBounds(ActorClass);

			BoundsMap.ReadWriteLocked([&ActorClass, &CalculatedBounds](auto& Map)
			{
				// Write results into map, inexpensive and not thread safe.
				Map.Update(ActorClass, CalculatedBounds);
			});
		});
}
```

### UE::Concurrent::AddToArrayThreadSafe 
```cpp
// Allows for a thread safe add on any mutable TArray variant 
// (including custom allocators or non project owned arrays) 
PrimitiveKeys.Reserve(Components.Num());
UE::Concurrent::InlineParallelForEach<EParallelForFlags::None>(
	Components,
	[&PrimitiveKeys, &ActorToPrimitiveComponentLayout, this](UStaticMeshComponent* MeshComponent)
	{
		if (MeshComponent->GetStaticMesh())
		{
			FProceduralStaticMeshComponentPrimitive PrimitiveKey(MeshComponent);

			// Thread safe add on any container assuming you have space reserved.
			// Supports move!
			UE::Concurrent::AddToArrayThreadSafe(PrimitiveKeys, MoveTemp(PrimitiveKey));
		}
	});

```


### Complete snippet of most primitives being used in tandem:

```cpp
using FActorComponentPrimitivesLayout = TArray<FProceduralStaticMeshComponentPrimitive, TInlineAllocator<3>>;
UE::Concurrent::TReadWriteLock
	<Experimental::TRobinHoodHashMap<TSubclassOf<AActor>, FActorComponentPrimitivesLayout>> ActorToPrimitiveComponentLayout;

// Extract primitive 
{
	ActorToPrimitiveComponentLayout.ReadWriteLocked(
	[&UniqueEncounteredActors](auto& Map)
	{
		// Map writing isn't thread safe! So read write locked.
		Map.Reserve(UniqueEncounteredActors.Num());
	});


	// Iterate through unique actors
	UE::Concurrent::InlineParallelFor<EParallelForFlags::None>(UniqueEncounteredActors.Num(),
	 [this, &ActorToPrimitiveComponentLayout, UniqueEncounteredActors](int32 Index)
	{
		const TSubclassOf<AActor>& ActorClass = UniqueEncounteredActors[Index];
		auto Components = UE::Actor::GetClassComponentTemplates<UStaticMeshComponent>(ActorClass);

		if(Components.Num() > 0)
		{
			FActorComponentPrimitivesLayout PrimitiveKeys;
			
			// Reserve space for parallel addition.
			PrimitiveKeys.Reserve(Components.Num());
			UE::Concurrent::InlineParallelForEach<EParallelForFlags::None>(
				Components,
				[&PrimitiveKeys, &ActorToPrimitiveComponentLayout, this](UStaticMeshComponent* MeshComponent)
				{
					if (MeshComponent->GetStaticMesh())
					{
						FProceduralStaticMeshComponentPrimitive PrimitiveKey(MeshComponent);

						// Thread safe add on any container assuming you have space reserved.
						UE::Concurrent::AddToArrayThreadSafe(PrimitiveKeys, MoveTemp(PrimitiveKey));
					}
				});

			ActorToPrimitiveComponentLayout.ReadWriteLocked(
				[&ActorClass, &PrimitiveKeys](auto& Map)
				{

					// Map writing isn't thread safe! So read write locked.
					Map.Update(ActorClass, PrimitiveKeys);
				});
		}
	});
}

UE::Concurrent::InlineParallelFor<EParallelForFlags::ForceSingleThread>(ActorsGrid.GetData().Num(),
 [this, &ActorsGrid, &ActorToPrimitiveComponentLayout](int32 TileIndex)
{
	FBadLadsProceduralGenerationState::FActorGrid::ElementType& SpawnGroup = ActorsGrid.GetData()[TileIndex];
	
	for (const FBadLadsAsyncActorSpawnTask& SpawnTask : SpawnGroup)
	{
		const FActorComponentPrimitivesLayout* PrimitiveLayouts;

		ActorToPrimitiveComponentLayout.ReadUnsafe([&ActorToPrimitiveComponentLayout, &PrimitiveLayouts, &SpawnTask](const auto& Map)
		{
			PrimitiveLayouts = Map.Find(SpawnTask.ActorToSpawn);
		});


		if (PrimitiveLayouts)
		{
```

