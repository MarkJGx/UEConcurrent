# Unreal Concurrent
> [!NOTE]
> A code library plugin that aims to make Unreal concurrency code safer and easier.

The code primitives here aim to be zero cost abstractions for general concurrency operations in Unreal Engine code bases.
When something has a runtime cost compared to an alternative it will be noted, but so far these are zero cost!

## Currently Available primitives:
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
	// However reading is free, assuming nothing else is writing! No lock required.
	// ReadUnsafe will assert if a write is being performed,
	PrimitiveLayouts = Map.Find(SpawnTask.ActorToSpawn);
});
```

### UE::Concurrent::InlineParallelFor
```cpp
// Allows for quick testing of compiler vectorized
// non parallel variant through template magic, simply swap to <EParallelForFlags::ForceSingleThread>
// No runtime cost at all.

UE::Concurrent::TReadWriteLock<
	Experimental::TRobinHoodHashMap<TSubclassOf<AActor>, FPaddedBox3f>> BoundsMap;
{
	TArray<TSubclassOf<AActor>> ActorsEncountered = UniqueActorsEncountered.Array();
	UE::Concurrent::InlineParallelFor<EParallelForFlags::None>(
		ActorsEncountered.Num(), [this, &ActorsEncountered, &BoundsMap](int32 Index)
		{
			const TSubclassOf<AActor>& ActorClass = ActorsEncountered[Index];
			// Read actor bounds (thread safe and expensive)

			// NB: UE::Actor::GetActorTemplateLocalBounds is not included! 
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

# Q&A:
### Why does this project exist?
> Most of these concurrency primitives were originally purpose built for a early access game called [BadLads](https://badlads.chemicalheads.com/). They proved to be useful so I decided to extract them from the codebase and make them into a code library plugin.

### Where is this project headed?
> The plan is to keep polishing and adding necessary primitive types, polish includes proper docuemntation.

### What's up with the `Experimental` directory in `Source/Private`?
> Primitives types that aren't quite game ready but are usable are going to exist inside of that directory.

### What engine version does this support?
> This has only been tested on the release tag of [Unreal 5.3](https://github.com/EpicGames/UnrealEngine/commit/072300df18a94f18077ca20a14224b5d99fee872). There are currently no plans to provide support for older versions of the engine, but I wouldn't rule it out.
# Project goals:
- Add perfect constructor forwarding to TReadWriteLock to allow for non default constructor types. I haven't had the need for this, but it might prove useful. 
- Ensure copying and moving works on all types. 
- Add TReadWriteLockView, primarily for concurrent operations on engine owned fields.
- Come up with cleaner examples for the README.MD
- Unit testing
- Ensure all types work correctly with move operators.
- Note implementation details and measure cost on target platforms for locking strategies. Consider new Unreal Engine 5 locking primitives instead of FCriticalSection.
- Create drop in wrapper for robin hood (round robin) hash maps/set that is API compatible with TSet/TMap. Matching allocator strategy might be tricky. 
- Measure compile time impact of templates here and make sure it's kept on record. A template permutation could make supported compilers spit out exponentially more code gen and worsen compile times substantially. 
