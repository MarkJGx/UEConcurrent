# Unreal Concurrent

UEConcurrent is a header-only library of concurrency primitives for Unreal Engine, created for ease of use and development runtime safety.

## Currently Available primitives:
### TReadWriteLock<FYourType>
```cpp
// A wrapper for a default constructed type (e.g., array, map) that ensures
// thread safety through its accessors, allowing for locked writes and
// unsafe reads with runtime thread safety assertions.


using FActorComponentPrimitivesLayout = TArray<FProceduralStaticMeshComponentPrimitive, TInlineAllocator<3>>;
UE::Concurrent::TReadWriteLock
	<Experimental::TRobinHoodHashMap<TSubclassOf<AActor>, FActorComponentPrimitivesLayout>> ActorToPrimitiveComponentLayout;

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
	// However, reading is free, assuming nothing else is writing! No lock required.
	// ReadUnsafe will assert if a write is being performed.
	PrimitiveLayouts = Map.Find(SpawnTask.ActorToSpawn);
});
```

```cpp
// TReadWriteLock supports recursion.
void AnothaFunction()
{
	ActorToPrimitiveComponentLayout.ReadLocked(
		[&PrimitiveLayouts, &SpawnTask](const auto& ReadOnlyMap)
	{
		PrimitiveLayouts = ReadOnlyMap.Find(SpawnTask.ActorToSpawn);
	});
}

ActorToPrimitiveComponentLayout.ReadWriteLocked(
	[&ActorToPrimitiveComponentLayout](auto& Map)
{
	Map.Reserve(UniqueEncounteredActors.Num());

	AnothaFunction();
});
```

```cpp
// TReadWriteLockView locks a field you don't own, like an engine-owned global.
// Use one view per field, shared by reference: separate views of the same field
// do not coordinate with each other.
TArray<FProceduralStaticMeshComponentPrimitive>& GlobalPrimitiveLayout = ...;

UE::Concurrent::TReadWriteLockView<TArray<FProceduralStaticMeshComponentPrimitive>> PrimitiveLayoutView(GlobalPrimitiveLayout);

PrimitiveLayoutView.ReadWriteLocked(
	[&SpawnTask](auto& PrimitiveLayout)
{
	PrimitiveLayout.Add(PrimitiveKey);
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

			// Thread-safe add on any TArray variant assuming you have space reserved.
			// Supports move!
			UE::Concurrent::AddToArrayThreadSafe(PrimitiveKeys, MoveTemp(PrimitiveKey));
		}
	});

```

```cpp
// TArray64 is supported: the returned index uses the array's 64-bit SizeType.
TArray64<FProceduralStaticMeshComponentPrimitive> PrimitiveKeys64;
PrimitiveKeys64.Reserve(Components.Num());

const int64 Index = UE::Concurrent::AddToArrayThreadSafe(PrimitiveKeys64, MoveTemp(PrimitiveKey));
check(Index != INDEX_NONE);
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
> The plan is to keep polishing and adding necessary primitive types, polish includes proper documentation.

### What's up with the `Experimental` directory in `Source/Private`?
> Primitives types that aren't quite game ready but are usable are going to exist inside of that directory.

### What engine version does this support?
> This has been tested on [Unreal 4.27](https://github.com/EpicGames/UnrealEngine/tree/4.27) and the release tag of [Unreal 5.3](https://github.com/EpicGames/UnrealEngine/tree/5.3.2-release).

### How do I run the tests?
> The plugin ships an Automation test suite (`UEConcurrentTestSuite`). Run it headless with your engine's
> editor executable (`UE4Editor` on 4.x, `UnrealEditor` on 5.x), pointing at a project with the plugin enabled:

```
UE4Editor <PathToProject>/<ProjectName>.uproject -unattended -nullrhi -ExecCmds="Automation RunTests Now UEConcurrent" -TestExit="Automation Test Queue Empty"
```

> Run it again with `-onethread` to cover the threading-disabled path (the same path single-core machines,
> `-noperfthreads` and dedicated servers take):

```
UE4Editor <PathToProject>/<ProjectName>.uproject -unattended -nullrhi -onethread -ExecCmds="Automation RunTests Now UEConcurrent" -TestExit="Automation Test Queue Empty"
```
# Project goals:
- Add perfect constructor forwarding to TReadWriteLock to allow for non default constructor types. I haven't had the need for this, but it might prove useful. 
- TReadWriteLock is explicitly non-copyable and non-movable (matching FCriticalSection and std::mutex); the guarded T works correctly with move operators inside the accessors. 
- Add TReadWriteLockView, primarily for concurrent operations on engine owned fields.
- Come up with cleaner examples for the README.MD
- Unit testing
- Note the implementation details and measure the cost on target platforms for locking strategies.
- Create drop in wrapper for robin hood (round robin) hash maps/set that is API compatible with TSet/TMap. Matching allocator strategy might be tricky. 
- Measure compile time impact of templates here and make sure it's kept on record. A template permutation could make supported compilers spit out exponentially more code gen and worsen compile times substantially. 
