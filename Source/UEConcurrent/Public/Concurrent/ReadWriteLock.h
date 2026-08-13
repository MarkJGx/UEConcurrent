// Copyright @MarkJGx 2024

#pragma once

#include "CoreTypes.h"
#include "UEConcurrentDefines.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformAtomics.h"
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
					// A write is exclusive against every other write and against every read.
					int32 Writers = FPlatformAtomics::InterlockedIncrement(&State.ConcurrentWriters);
					check(Writers == 1);
					check(FPlatformAtomics::AtomicRead(&State.ConcurrentReaders) == 0);
				}

				~FScopedConcurrentWriteCheck()
				{
					int32 Writers = FPlatformAtomics::InterlockedDecrement(&State.ConcurrentWriters);
					check(Writers == 0);
				}
			};

			struct FScopedConcurrentReadCheck
			{
				const FEnabledConcurrentCheck& State;

				UE_CONCURRENT_NODISCARD_CTOR FScopedConcurrentReadCheck(const FEnabledConcurrentCheck& State) : State(State)
				{
					// Any number of readers may overlap, so long as nothing is writing.
					FPlatformAtomics::InterlockedIncrement(&State.ConcurrentReaders);
					check(FPlatformAtomics::AtomicRead(&State.ConcurrentWriters) == 0);
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

			FConcurrent ReadWriteState;
			FCriticalSection Mutex;
			T Type;

		public:
			using ElementType = T;

			/**
			 * @brief Read unsafe can be used when nothing else is being written to this type.
			 * @tparam FunctionBody
			 * @param Function
			 */
			template <typename FunctionBody>
			inline void ReadUnsafe(FunctionBody&& Function)
			{
				// We cannot have anything writing to the Type while we are reading.
				// Other concurrent readers are fine, which is the entire point of this accessor.
				FReadOnlyScope Scope(ReadWriteState);
				Function((const T&)Type);
			}

			/**
			* @brief Read unsafe can be used when nothing else is being written to this type.
			* @tparam FunctionBody
			* @param Function
			*/
			template <typename FunctionBody>
			inline void ReadLocked(FunctionBody&& Function)
			{
				// We cannot have anything writing to the Type while we are reading.
				FScopeLock Lock(&Mutex);
				FReadOnlyScope ReadScope(ReadWriteState);

				Function((const T&)Type);
			}

			template <typename FunctionBody>
			inline void ReadWriteLocked(FunctionBody&& Function)
			{
				FScopeLock Lock(&Mutex);
				FWriteOnlyScope WriteScope(ReadWriteState);

				Function(Type);
			}
		};
	}
}
