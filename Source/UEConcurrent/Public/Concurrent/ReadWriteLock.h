// Copyright @MarkJGx 2024

#pragma once

#include "CoreTypes.h"
#include "UEConcurrentDefines.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/PlatformTLS.h"
#include "Misc/AssertionMacros.h"
#include "Misc/ScopeLock.h"

#include <type_traits>

namespace UE
{
	namespace Private
	{
		struct FDisabledConcurrentCheck
		{
			struct FScopedConcurrentWriteCheck
			{
				UE_CONCURRENT_NODISCARD_CTOR FScopedConcurrentWriteCheck(const FDisabledConcurrentCheck&)
				{
				}

				~FScopedConcurrentWriteCheck()
				{
				}
			};

			struct FScopedConcurrentReadCheck
			{
				UE_CONCURRENT_NODISCARD_CTOR FScopedConcurrentReadCheck(const FDisabledConcurrentCheck&)
				{
				}

				~FScopedConcurrentReadCheck()
				{
				}
			};
		};

		struct FEnabledConcurrentCheck
		{
			mutable int32 ConcurrentReaders;
			mutable int32 ConcurrentWriters;

			FEnabledConcurrentCheck() : ConcurrentReaders(0), ConcurrentWriters(0)
			{
			}

			struct FScopedConcurrentWriteCheck
			{
				const FEnabledConcurrentCheck& State;

				UE_CONCURRENT_NODISCARD_CTOR FScopedConcurrentWriteCheck(const FEnabledConcurrentCheck& State) : State(State)
				{
					// Counter only; the exclusivity checks run in the accessor after this increment.
					FPlatformAtomics::InterlockedIncrement(&State.ConcurrentWriters);
				}

				~FScopedConcurrentWriteCheck()
				{
					FPlatformAtomics::InterlockedDecrement(&State.ConcurrentWriters);
				}
			};

			struct FScopedConcurrentReadCheck
			{
				const FEnabledConcurrentCheck& State;

				UE_CONCURRENT_NODISCARD_CTOR FScopedConcurrentReadCheck(const FEnabledConcurrentCheck& State) : State(State)
				{
					// Counter only; the exclusivity checks run in the accessor after this increment.
					FPlatformAtomics::InterlockedIncrement(&State.ConcurrentReaders);
				}

				~FScopedConcurrentReadCheck()
				{
					FPlatformAtomics::InterlockedDecrement(&State.ConcurrentReaders);
				}
			};
		};
	}
}

namespace UE
{
	namespace Concurrent
	{
		// Enable concurrency read/write checks wherever check() itself is enabled, which covers
		// Debug, DebugGame and Development. UE_BUILD_DEVELOPMENT would have excluded Debug.
		template <typename T, bool bConcurrencyCheckEnabled = DO_CHECK>
		class TReadWriteLock
		{
			using FConcurrent = std::conditional_t<bConcurrencyCheckEnabled, UE::Private::FEnabledConcurrentCheck,
			                                       UE::Private::FDisabledConcurrentCheck>;

			using FReadOnlyScope = typename FConcurrent::FScopedConcurrentReadCheck;
			using FWriteOnlyScope = typename FConcurrent::FScopedConcurrentWriteCheck;

			struct FRecursiveWriteScope
			{
				int32& Depth;

				explicit FRecursiveWriteScope(int32& InDepth) : Depth(InDepth)
				{
					Depth++;
				}

				~FRecursiveWriteScope()
				{
					Depth--;
				}
			};

			struct FWriteOwnerScope
			{
				uint32& OwnerThreadId;
				int32& WriteDepth;
				const uint32 ThreadId;

				explicit FWriteOwnerScope(uint32& InOwnerThreadId, int32& InWriteDepth, uint32 InThreadId)
					: OwnerThreadId(InOwnerThreadId), WriteDepth(InWriteDepth), ThreadId(InThreadId)
				{
					FPlatformAtomics::InterlockedExchange(&OwnerThreadId, ThreadId);
					WriteDepth = 1;
				}

				~FWriteOwnerScope()
				{
					WriteDepth = 0;
					FPlatformAtomics::InterlockedExchange(&OwnerThreadId, 0);
				}
			};

			FConcurrent ReadWriteState;
			FCriticalSection Mutex;
			T Type;

			// Runtime recursion state, used only in checked builds: the thread holding the
			// outermost write scope (0 = none; thread ids are never 0) and the same-thread
			// write re-entry depth.
			uint32 RuntimeWriteOwnerThreadId = 0;
			int32 RuntimeWriteDepth = 0;

			bool IsWriteOwner(uint32 CurrentThreadId) const
			{
				return FPlatformAtomics::AtomicRead(&RuntimeWriteOwnerThreadId) == CurrentThreadId;
			}

		public:
			using ElementType = T;

			/**
			 * @brief Read unsafe can be used when nothing else is being written to this type.
			 * Re-entrant: safe from inside a write scope held by this thread.
			 * @tparam FunctionBody
			 * @param Function
			 */
			template <typename FunctionBody>
			inline void ReadUnsafe(FunctionBody&& Function)
			{
				if constexpr (bConcurrencyCheckEnabled)
				{
					const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
					if (IsWriteOwner(CurrentThreadId))
					{
						Function((const T&)Type);
						return;
					}

					FReadOnlyScope Scope(ReadWriteState);

					// Concurrency check: nothing may be writing while we read.
					check(FPlatformAtomics::AtomicRead(&ReadWriteState.ConcurrentWriters) == 0);

					Function((const T&)Type);
				}
				else
				{
					Function((const T&)Type);
				}
			}

			/**
			 * @brief ReadUnsafe that returns whatever the callable returns.
			 * Re-entrant: safe from inside a write scope held by this thread.
			 * Pointers and references into the type are allowed here, unlike the locked variants:
			 * there is no lock to expire, so their validity rests entirely on your own guarantee
			 * that no writer is live.
			 * @tparam FunctionBody
			 * @param Function
			 */
			template <typename FunctionBody>
			inline decltype(auto) ReadUnsafe_Get(FunctionBody&& Function)
			{
				if constexpr (bConcurrencyCheckEnabled)
				{
					const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
					if (IsWriteOwner(CurrentThreadId))
					{
						return Function((const T&)Type);
					}

					FReadOnlyScope Scope(ReadWriteState);

					// Concurrency check: nothing may be writing while we read.
					check(FPlatformAtomics::AtomicRead(&ReadWriteState.ConcurrentWriters) == 0);

					return Function((const T&)Type);
				}
				else
				{
					return Function((const T&)Type);
				}
			}

			/**
			 * @brief Reads under an exclusive lock, so no writer can run for the duration.
			 * Re-entrant: safe from inside a write scope held by this thread.
			 * @tparam FunctionBody
			 * @param Function
			 */
			template <typename FunctionBody>
			inline void ReadLocked(FunctionBody&& Function)
			{
				if constexpr (bConcurrencyCheckEnabled)
				{
					const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
					if (IsWriteOwner(CurrentThreadId))
					{
						Function((const T&)Type);
						return;
					}

					FScopeLock Lock(&Mutex);
					FReadOnlyScope ReadScope(ReadWriteState);

					// Concurrency check: nothing may be writing while we read.
					check(FPlatformAtomics::AtomicRead(&ReadWriteState.ConcurrentWriters) == 0);

					Function((const T&)Type);
				}
				else
				{
					FScopeLock Lock(&Mutex);
					Function((const T&)Type);
				}
			}

			/**
			 * @brief ReadLocked that returns whatever the callable returns.
			 * Re-entrant: safe from inside a write scope held by this thread.
			 * @tparam FunctionBody
			 * @param Function
			 */
			template <typename FunctionBody>
			inline decltype(auto) ReadLocked_Get(FunctionBody&& Function)
			{
				using FGuardReturn = decltype(Function((const T&)Type));
				static_assert(!std::is_reference_v<FGuardReturn>,
					"Cannot return a reference out of ReadLocked_Get, the lock is released before you can use it. "
					"Consume it inside the callable, or use ReadUnsafe_Get if you can guarantee no writer is live.");
				static_assert(!std::is_pointer_v<FGuardReturn>,
					"Cannot return a pointer out of ReadLocked_Get, the lock is released before you can use it. "
					"Consume it inside the callable, or use ReadUnsafe_Get if you can guarantee no writer is live.");

				if constexpr (bConcurrencyCheckEnabled)
				{
					const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
					if (IsWriteOwner(CurrentThreadId))
					{
						return Function((const T&)Type);
					}

					FScopeLock Lock(&Mutex);
					FReadOnlyScope ReadScope(ReadWriteState);

					// Concurrency check: nothing may be writing while we read.
					check(FPlatformAtomics::AtomicRead(&ReadWriteState.ConcurrentWriters) == 0);

					return Function((const T&)Type);
				}
				else
				{
					FScopeLock Lock(&Mutex);
					return Function((const T&)Type);
				}
			}

			/**
			 * @brief Runs the callable with exclusive write access under the mutex.
			 * Re-entrant: safe from inside a write scope held by this thread.
			 * @tparam FunctionBody
			 * @param Function
			 */
			template <typename FunctionBody>
			inline void ReadWriteLocked(FunctionBody&& Function)
			{
				if constexpr (bConcurrencyCheckEnabled)
				{
					const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();

					// Recursion: the same thread may re-enter its own write scope.
					if (IsWriteOwner(CurrentThreadId))
					{
						RuntimeWriteDepth++;
						Function(Type);
						RuntimeWriteDepth--;
						return;
					}

					FScopeLock Lock(&Mutex);
					FWriteOnlyScope WriteScope(ReadWriteState);

					// Concurrency checks: a write is exclusive against every other write and every read.
					check(FPlatformAtomics::AtomicRead(&ReadWriteState.ConcurrentWriters) == 1);
					check(FPlatformAtomics::AtomicRead(&ReadWriteState.ConcurrentReaders) == 0);
					check(FPlatformAtomics::AtomicRead(&RuntimeWriteOwnerThreadId) == 0);

					FWriteOwnerScope OwnerScope(RuntimeWriteOwnerThreadId, RuntimeWriteDepth, CurrentThreadId);

					Function(Type);
				}
				else
				{
					FScopeLock Lock(&Mutex);
					Function(Type);
				}
			}

			/**
			 * @brief ReadWriteLocked that returns whatever the callable returns.
			 * Re-entrant: safe from inside a write scope held by this thread.
			 * @tparam FunctionBody
			 * @param Function
			 */
			template <typename FunctionBody>
			inline decltype(auto) ReadWriteLocked_Get(FunctionBody&& Function)
			{
				using FGuardReturn = decltype(Function(Type));
				static_assert(!std::is_reference_v<FGuardReturn>,
					"Cannot return a reference out of ReadWriteLocked_Get, the lock is released before you can use it "
					"and the reference is mutable. Consume it inside the callable.");
				static_assert(!std::is_pointer_v<FGuardReturn>,
					"Cannot return a pointer out of ReadWriteLocked_Get, the lock is released before you can use it "
					"and the pointer is mutable. Consume it inside the callable.");

				if constexpr (bConcurrencyCheckEnabled)
				{
					const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();

					// Recursion: the same thread may re-enter its own write scope.
					if (IsWriteOwner(CurrentThreadId))
					{
						FRecursiveWriteScope RecursiveScope(RuntimeWriteDepth);
						return Function(Type);
					}

					FScopeLock Lock(&Mutex);
					FWriteOnlyScope WriteScope(ReadWriteState);

					// Concurrency checks: a write is exclusive against every other write and every read.
					check(FPlatformAtomics::AtomicRead(&ReadWriteState.ConcurrentWriters) == 1);
					check(FPlatformAtomics::AtomicRead(&ReadWriteState.ConcurrentReaders) == 0);
					check(FPlatformAtomics::AtomicRead(&RuntimeWriteOwnerThreadId) == 0);

					FWriteOwnerScope OwnerScope(RuntimeWriteOwnerThreadId, RuntimeWriteDepth, CurrentThreadId);

					return Function(Type);
				}
				else
				{
					FScopeLock Lock(&Mutex);
					return Function(Type);
				}
			}
		};
	}
}
