// Copyright @MarkJGx 2024

#pragma once

#include "CoreTypes.h"
#include "UEConcurrentDefines.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/PlatformTLS.h"
#include "Misc/AssertionMacros.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/ScopeLock.h"
#include "Templates/AreTypesEqual.h"
#include "Templates/Decay.h"
#include "Templates/EnableIf.h"

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

	namespace Concurrent
	{
		// Enable concurrency read/write checks wherever check() itself is enabled, which covers
		// Debug, DebugGame and Development. UE_BUILD_DEVELOPMENT would have excluded Debug.
		namespace Private
		{
			/**
			 * @brief Shared lock machinery for TReadWriteLock and TReadWriteLockView: the mutex,
			 * the concurrency counters, the recursion state, and the six accessor bodies. The
			 * guarded value is passed in per call, so this base is instantiated once per check
			 * state rather than once per guarded type.
			 */
			template <bool bConcurrencyCheckEnabled>
			class TReadWriteLockBase
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
					int32& OwnerThreadId;
					int32& WriteDepth;
					const int32 ThreadId;

					explicit FWriteOwnerScope(int32& InOwnerThreadId, int32& InWriteDepth, uint32 InThreadId)
						: OwnerThreadId(InOwnerThreadId), WriteDepth(InWriteDepth), ThreadId(int32(InThreadId))
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

				// Runtime recursion state, used only in checked builds: the thread holding the
				// outermost write scope (0 = none; thread ids are never 0) and the same-thread
				// write re-entry depth.
				uint32 RuntimeWriteOwnerThreadId = 0;
				int32 RuntimeWriteDepth = 0;

				bool IsWriteOwner(uint32 CurrentThreadId) const
				{
					return FPlatformAtomics::AtomicRead(&RuntimeWriteOwnerThreadId) == CurrentThreadId;
				}

#if !UE_CONCURRENT_HAS_IF_CONSTEXPR
				// Fallback for compilers without if constexpr: plain if compiles both branches,
				// so the state-dependent checks must be well-formed for the disabled state too.
				// Only the matching overload is ever instantiated.
				void CheckReadSafety(const UE::Private::FEnabledConcurrentCheck& State)
				{
					check(FPlatformAtomics::AtomicRead(&State.ConcurrentWriters) == 0);
				}

				void CheckReadSafety(const UE::Private::FDisabledConcurrentCheck&)
				{
				}

				void CheckWriteExclusivity(const UE::Private::FEnabledConcurrentCheck& State)
				{
					check(FPlatformAtomics::AtomicRead(&State.ConcurrentWriters) == 1);
					check(FPlatformAtomics::AtomicRead(&State.ConcurrentReaders) == 0);
				}

				void CheckWriteExclusivity(const UE::Private::FDisabledConcurrentCheck&)
				{
				}
#endif

			protected:
				/**
				 * @brief Read unsafe can be used when nothing else is being written to this type.
				 * Re-entrant: safe from inside a write scope held by this thread.
				 * @tparam T
				 * @tparam FunctionBody
				 * @param Type
				 * @param Function
				 */
				template <typename T, typename FunctionBody>
				void ReadUnsafeImpl(T& Type, FunctionBody&& Function)
				{
#if UE_CONCURRENT_HAS_IF_CONSTEXPR
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
#else
					if (bConcurrencyCheckEnabled)
					{
						const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
						if (IsWriteOwner(CurrentThreadId))
						{
							Function((const T&)Type);
							return;
						}

						FReadOnlyScope Scope(ReadWriteState);

						// Concurrency check: nothing may be writing while we read.
						CheckReadSafety(ReadWriteState);

						Function((const T&)Type);
					}
					else
					{
						Function((const T&)Type);
					}
#endif
				}

				/**
				 * @brief ReadUnsafe that returns whatever the callable returns.
				 * Re-entrant: safe from inside a write scope held by this thread.
				 * Pointers and references into the type are allowed here, unlike the locked variants:
				 * there is no lock to expire, so their validity rests entirely on your own guarantee
				 * that no writer is live.
				 * @tparam T
				 * @tparam FunctionBody
				 * @param Type
				 * @param Function
				 */
				template <typename T, typename FunctionBody>
				decltype(auto) ReadUnsafe_GetImpl(T& Type, FunctionBody&& Function)
				{
#if UE_CONCURRENT_HAS_IF_CONSTEXPR
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
#else
					if (bConcurrencyCheckEnabled)
					{
						const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
						if (IsWriteOwner(CurrentThreadId))
						{
							return Function((const T&)Type);
						}

						FReadOnlyScope Scope(ReadWriteState);

						// Concurrency check: nothing may be writing while we read.
						CheckReadSafety(ReadWriteState);

						return Function((const T&)Type);
					}
					else
					{
						return Function((const T&)Type);
					}
#endif
				}

				/**
				 * @brief Reads under an exclusive lock, so no writer can run for the duration.
				 * Re-entrant: safe from inside a write scope held by this thread.
				 * @tparam T
				 * @tparam FunctionBody
				 * @param Type
				 * @param Function
				 */
				template <typename T, typename FunctionBody>
				void ReadLockedImpl(T& Type, FunctionBody&& Function)
				{
#if UE_CONCURRENT_HAS_IF_CONSTEXPR
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
#else
					if (bConcurrencyCheckEnabled)
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
						CheckReadSafety(ReadWriteState);

						Function((const T&)Type);
					}
					else
					{
						FScopeLock Lock(&Mutex);
						Function((const T&)Type);
					}
#endif
				}

				/**
				 * @brief ReadLocked that returns whatever the callable returns.
				 * Re-entrant: safe from inside a write scope held by this thread.
				 * @tparam T
				 * @tparam FunctionBody
				 * @param Type
				 * @param Function
				 */
				template <typename T, typename FunctionBody>
				decltype(auto) ReadLocked_GetImpl(T& Type, FunctionBody&& Function)
				{
					using FGuardReturn = decltype(Function((const T&)Type));
					static_assert(!std::is_reference<FGuardReturn>::value,
						"Cannot return a reference out of ReadLocked_Get, the lock is released before you can use it. "
						"Consume it inside the callable, or use ReadUnsafe_Get if you can guarantee no writer is live.");
					static_assert(!std::is_pointer<FGuardReturn>::value,
						"Cannot return a pointer out of ReadLocked_Get, the lock is released before you can use it. "
						"Consume it inside the callable, or use ReadUnsafe_Get if you can guarantee no writer is live.");

#if UE_CONCURRENT_HAS_IF_CONSTEXPR
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
#else
					if (bConcurrencyCheckEnabled)
					{
						const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
						if (IsWriteOwner(CurrentThreadId))
						{
							return Function((const T&)Type);
						}

						FScopeLock Lock(&Mutex);
						FReadOnlyScope ReadScope(ReadWriteState);

						// Concurrency check: nothing may be writing while we read.
						CheckReadSafety(ReadWriteState);

						return Function((const T&)Type);
					}
					else
					{
						FScopeLock Lock(&Mutex);
						return Function((const T&)Type);
					}
#endif
				}

				/**
				 * @brief Runs the callable with exclusive write access under the mutex.
				 * Re-entrant: safe from inside a write scope held by this thread.
				 * @tparam T
				 * @tparam FunctionBody
				 * @param Type
				 * @param Function
				 */
				template <typename T, typename FunctionBody>
				void ReadWriteLockedImpl(T& Type, FunctionBody&& Function)
				{
					static_assert(!std::is_const<T>::value,
						"Cannot write through a const view; use a non-const TReadWriteLockView or read-only accessors.");

#if UE_CONCURRENT_HAS_IF_CONSTEXPR
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
#else
					if (bConcurrencyCheckEnabled)
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
						CheckWriteExclusivity(ReadWriteState);
						check(FPlatformAtomics::AtomicRead(&RuntimeWriteOwnerThreadId) == 0);

						FWriteOwnerScope OwnerScope(RuntimeWriteOwnerThreadId, RuntimeWriteDepth, CurrentThreadId);

						Function(Type);
					}
					else
					{
						FScopeLock Lock(&Mutex);
						Function(Type);
					}
#endif
				}

				/**
				 * @brief ReadWriteLocked that returns whatever the callable returns.
				 * Re-entrant: safe from inside a write scope held by this thread.
				 * @tparam T
				 * @tparam FunctionBody
				 * @param Type
				 * @param Function
				 */
				template <typename T, typename FunctionBody>
				decltype(auto) ReadWriteLocked_GetImpl(T& Type, FunctionBody&& Function)
				{
					using FGuardReturn = decltype(Function(Type));
					static_assert(!std::is_const<T>::value,
						"Cannot write through a const view; use a non-const TReadWriteLockView or read-only accessors.");
					static_assert(!std::is_reference<FGuardReturn>::value,
						"Cannot return a reference out of ReadWriteLocked_Get, the lock is released before you can use it "
						"and the reference is mutable. Consume it inside the callable.");
					static_assert(!std::is_pointer<FGuardReturn>::value,
						"Cannot return a pointer out of ReadWriteLocked_Get, the lock is released before you can use it "
						"and the pointer is mutable. Consume it inside the callable.");

#if UE_CONCURRENT_HAS_IF_CONSTEXPR
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
#else
					if (bConcurrencyCheckEnabled)
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
						CheckWriteExclusivity(ReadWriteState);
						check(FPlatformAtomics::AtomicRead(&RuntimeWriteOwnerThreadId) == 0);

						FWriteOwnerScope OwnerScope(RuntimeWriteOwnerThreadId, RuntimeWriteDepth, CurrentThreadId);

						return Function(Type);
					}
					else
					{
						FScopeLock Lock(&Mutex);
						return Function(Type);
					}
#endif
				}
			};
		}

		/**
		 * @brief A thread-safe wrapper around a value of type T. Accessors:
		 * - ReadUnsafe / ReadUnsafe_Get: read without a lock, asserting (in checked builds) that
		 *   no writer is live.
		 * - ReadLocked / ReadLocked_Get: read under the mutex.
		 * - ReadWriteLocked / ReadWriteLocked_Get: write under the mutex.
		 *
		 * Recursion: any accessor may be called from inside a callable that already holds this
		 * lock's write scope on the same thread. Re-entry skips the mutex and the concurrency
		 * checks. Upgrades (a write accessor from inside a read scope) are not supported.
		 * @tparam T
		 * @tparam bConcurrencyCheckEnabled
		 */
		template <typename T, bool bConcurrencyCheckEnabled = DO_CHECK>
		class TReadWriteLock : public Private::TReadWriteLockBase<bConcurrencyCheckEnabled>
		{
		public:
			using ElementType = T;

			TReadWriteLock() = default;

			// Perfect forwarding ctor: constructs T from the given arguments, never
			// hijacking copy/move construction of the lock itself.
			template <
				typename ArgType,
				typename... ArgsType,
#if ENGINE_MAJOR_VERSION >= 5
				std::enable_if_t<!std::is_same<std::decay_t<ArgType>, TReadWriteLock>::value, int> = 0>
#else
				typename TEnableIf<!TAreTypesEqual<typename TDecay<ArgType>::Type, TReadWriteLock>::Value, int>::Type = 0>
#endif
			explicit TReadWriteLock(ArgType&& Arg, ArgsType&&... Args)
				: Type(Forward<ArgType>(Arg), Forward<ArgsType>(Args)...)
			{
			}

			TReadWriteLock(const TReadWriteLock&) = delete;
			TReadWriteLock& operator=(const TReadWriteLock&) = delete;
			TReadWriteLock(TReadWriteLock&&) = delete;
			TReadWriteLock& operator=(TReadWriteLock&&) = delete;

			template <typename FunctionBody>
			inline void ReadUnsafe(FunctionBody&& Function)
			{
				ReadUnsafeImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline decltype(auto) ReadUnsafe_Get(FunctionBody&& Function)
			{
				return ReadUnsafe_GetImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline void ReadLocked(FunctionBody&& Function)
			{
				ReadLockedImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline decltype(auto) ReadLocked_Get(FunctionBody&& Function)
			{
				return ReadLocked_GetImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline void ReadWriteLocked(FunctionBody&& Function)
			{
				ReadWriteLockedImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline decltype(auto) ReadWriteLocked_Get(FunctionBody&& Function)
			{
				return ReadWriteLocked_GetImpl(Type, Function);
			}

		private:
			T Type;
		};

		/**
		 * @brief A lock over an engine-owned field that the caller does not own: same accessors
		 * and semantics as TReadWriteLock, but the guarded value is referenced rather than held.
		 *
		 * Use one view per guarded field, shared by reference: two views of the same field carry
		 * separate mutexes and do not coordinate, and recursion only works through the same view
		 * instance. The referenced field must outlive the view. Read-only views are spelled
		 * TReadWriteLockView<const T>; writing through one is a compile error.
		 * @tparam T
		 * @tparam bConcurrencyCheckEnabled
		 */
		template <typename T, bool bConcurrencyCheckEnabled = DO_CHECK>
		class TReadWriteLockView : public Private::TReadWriteLockBase<bConcurrencyCheckEnabled>
		{
		public:
			using ElementType = T;

			explicit TReadWriteLockView(T& InType) : Type(InType)
			{
			}

			TReadWriteLockView(const TReadWriteLockView&) = delete;
			TReadWriteLockView& operator=(const TReadWriteLockView&) = delete;
			TReadWriteLockView(TReadWriteLockView&&) = delete;
			TReadWriteLockView& operator=(TReadWriteLockView&&) = delete;

			template <typename FunctionBody>
			inline void ReadUnsafe(FunctionBody&& Function)
			{
				ReadUnsafeImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline decltype(auto) ReadUnsafe_Get(FunctionBody&& Function)
			{
				return ReadUnsafe_GetImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline void ReadLocked(FunctionBody&& Function)
			{
				ReadLockedImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline decltype(auto) ReadLocked_Get(FunctionBody&& Function)
			{
				return ReadLocked_GetImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline void ReadWriteLocked(FunctionBody&& Function)
			{
				ReadWriteLockedImpl(Type, Function);
			}

			template <typename FunctionBody>
			inline decltype(auto) ReadWriteLocked_Get(FunctionBody&& Function)
			{
				return ReadWriteLocked_GetImpl(Type, Function);
			}

		private:
			T& Type;
		};
	}
}
