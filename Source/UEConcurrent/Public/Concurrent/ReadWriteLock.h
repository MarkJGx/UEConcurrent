// Copyright @MarkJGx 2024 

#pragma once

#include "CoreTypes.h"

namespace UE
{
	namespace Private
	{
		struct FDisabledConcurrentCheck
		{
			struct FScopedConcurrentWriteCheck
			{
				UE_NODISCARD_CTOR FScopedConcurrentWriteCheck(const FDisabledConcurrentCheck&)
				{
				}

				~FScopedConcurrentWriteCheck()
				{
				}
			};

			struct FScopedConcurrentReadCheck
			{
				UE_NODISCARD_CTOR FScopedConcurrentReadCheck(const FDisabledConcurrentCheck&)
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

				UE_NODISCARD_CTOR FScopedConcurrentWriteCheck(const FEnabledConcurrentCheck& State) : State(State)
				{
					int32 Writers = FPlatformAtomics::InterlockedIncrement(&State.ConcurrentWriters);
					check(Writers == 1);
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

				UE_NODISCARD_CTOR FScopedConcurrentReadCheck(const FEnabledConcurrentCheck& State) : State(State)
				{
					int32 Readers = FPlatformAtomics::InterlockedIncrement(&State.ConcurrentReaders);
					check(Readers == 1);
				}

				~FScopedConcurrentReadCheck()
				{
					int32 Readers = FPlatformAtomics::InterlockedDecrement(&State.ConcurrentReaders);
					check(Readers == 0);
				}
			};
		};
	}
}

namespace UE
{
	namespace Concurrent
	{
		// Enable concurrency read/write checks in development builds.
		template <typename T, bool bConcurrencyCheckEnabled = UE_BUILD_DEVELOPMENT>
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
				FWriteOnlyScope Scope(ReadWriteState);
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
				FWriteOnlyScope WriteScope(ReadWriteState);
				FReadOnlyScope ReadScope(ReadWriteState);

				Function((const T&)Type);
			}

			template <typename FunctionBody>
			inline void ReadWriteLocked(FunctionBody&& Function)
			{
				FScopeLock Lock(&Mutex);
				FWriteOnlyScope WriteScope(ReadWriteState);
				FReadOnlyScope ReadScope(ReadWriteState);

				Function(Type);
			}
		};
	}
}
