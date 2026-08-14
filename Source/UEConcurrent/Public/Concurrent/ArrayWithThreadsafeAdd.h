// Copyright 2024 @MarkJGx
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "HAL/PlatformAtomics.h"
#include "Misc/AssertionMacros.h"
#include "Misc/EngineVersionComparison.h"
#include "Templates/AndOrNot.h"
#include "Templates/IsConst.h"
#include "Templates/RemoveCV.h"
#include "Templates/RemoveReference.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/UnrealTypeTraits.h"

#include <type_traits>

namespace UE
{
	namespace Concurrent
	{
		namespace Private
		{
			// Ripped from TickTaskManager with slight modifications
			template <typename ArrayType>
			class TArrayWithThreadsafeAddHack : public ArrayType
			{
				template <typename... ArgsType>
				int32 EmplaceThreadsafe(ArgsType&&... Args)
				{
					const int32 Index = AddUninitializedThreadsafe(1);
					if (Index == INDEX_NONE)
					{
						return INDEX_NONE;
					}

					new(this->GetData() + Index) typename ArrayType::ElementType(Forward<ArgsType>(Args)...);
					return Index;
				}

				/**
				 * Atomically reserves a given number of uninitialized slots at the end of the
				 * array by CAS-ing the array num. When the array lacks slack the reservation
				 * fails cleanly with INDEX_NONE instead of corrupting the array (the engine
				 * original asserted after the fact, which was silent out of bounds in shipping).
				 *
				 * Caution, the array must have sufficient slack or this returns INDEX_NONE.
				 * You must presize the array.
				 *
				 * Caution, AddUninitialized() will create elements without calling
				 * the constructor and this is not appropriate for element types that
				 * require a constructor to function properly.
				 *
				 * @param Count Number of elements to add.
				 * @return Number of elements in array before addition, or INDEX_NONE when the
				 *         reservation failed.
				 */
				int32 AddUninitializedThreadsafe(int32 Count = 1)
				{
					check(Count >= 0);
					while (true)
					{
						const int32 OldNum = FPlatformAtomics::AtomicRead(&this->ArrayNum);
						if (OldNum > this->ArrayMax - Count)
						{
							return INDEX_NONE;
						}

						if (FPlatformAtomics::InterlockedCompareExchange(&this->ArrayNum, OldNum + Count, OldNum) == OldNum)
						{
							return OldNum;
						}
					}
				}

			public:
				/**
				 * Adds a new item to the end of the array, using atomics to update the current size of the array.
				 *
				 * Caution, the array must have sufficient slack or this returns INDEX_NONE.
				 * You must presize the array.
				 *
				 * @param Item	The item to add
				 * @return		Index to the new item, or INDEX_NONE when the reservation failed
				 */
				int32 AddThreadsafe(const typename ArrayType::ElementType& Item)
				{
					this->CheckAddress(&Item);
					return EmplaceThreadsafe(Item);
				}

				/**
				 * Adds a new item to the end of the array, using atomics to update the current size of the array.
				 * Move semantics version.
				 *
				 * Caution, the array must have sufficient slack or this returns INDEX_NONE.
				 * You must presize the array.
				 *
				 * @param Item	The item to add
				 * @return		Index to the new item, or INDEX_NONE when the reservation failed
				 */
				int32 AddThreadsafe(typename ArrayType::ElementType&& Item)
				{
					this->CheckAddress(&Item);
					return EmplaceThreadsafe(MoveTempIfPossible(Item));
				}
			};

			/**
			 * @brief Restricts AddToArrayThreadSafe to the TArray family: a non-const container
			 * whose type derives from TArray with matching element and allocator, is
			 * standard-layout (the thread-safe add view adds no members), and has non-const
			 * elements.
			 */
			template <typename ContainerType>
			struct TIsThreadsafeAddableArray
			{
				using FArrayType = typename TRemoveCV<typename TRemoveReference<ContainerType>::Type>::Type;
				using FArrayBase = TArray<typename FArrayType::ElementType, typename FArrayType::Allocator>;

				// Epic's usage guidelines specify std type traits on UE5; pre-5 the guidelines
				// were indeterminate, so the Unreal traits are kept there. Purely stylistic.
#if ENGINE_MAJOR_VERSION >= 5
				static constexpr bool Value =
					!std::is_const_v<typename TRemoveReference<ContainerType>::Type> &&
					std::is_base_of_v<FArrayBase, FArrayType> &&
					std::is_standard_layout_v<FArrayType> &&
					!std::is_const_v<typename FArrayType::ElementType>;
#else
				static constexpr bool Value =
					TNot<TIsConst<typename TRemoveReference<ContainerType>::Type>>::Value &&
					TIsDerivedFrom<FArrayType, FArrayBase>::Value &&
					std::is_standard_layout<FArrayType>::value &&
					!TIsConst<typename FArrayType::ElementType>::Value;
#endif
			};

			/**
			 * @brief Shared body for both AddToArrayThreadSafe overloads. ContainerElement is deduced
			 * here rather than in the public overloads, so forwarding is safe: this is a single
			 * function, not an overload set a forwarding reference could hijack.
			 */
			template <typename ContainerType, typename ContainerElement>
			int32 AddToArrayThreadSafeImpl(ContainerType& Array, ContainerElement&& Element)
			{
				static_assert(TIsThreadsafeAddableArray<ContainerType>::Value,
					"AddToArrayThreadSafe requires a non-const, standard-layout TArray with non-const elements!");

				using ArrayType = typename TRemoveCV<typename TRemoveReference<decltype(Array)>::Type>::Type;
				TArrayWithThreadsafeAddHack<ArrayType>* ThreadSafeArray = reinterpret_cast<TArrayWithThreadsafeAddHack<ArrayType>
					*>(&Array);

#if DO_CHECK
				auto* DataPreAdd = Array.GetData();
#endif
				const int32 Index = ThreadSafeArray->AddThreadsafe(Forward<ContainerElement>(Element));
#if DO_CHECK
				auto* DataPostAdd = Array.GetData();
				checkf(DataPreAdd == DataPostAdd,
				       TEXT(
					       "Container has been reallocated during thread safe add. Undefined behavior. You must Reserve() container amount pre-addition!"
				       ));
#endif
				return Index;
			}
		}

		/**
		 * @brief Thread-safe add on any TArray variant. The container must have enough memory
		 * reserved before addition, otherwise INDEX_NONE is returned and nothing is added, in
		 * any build config.
		 *
		 * The container must not be resized, emptied, iterated, or otherwise touched through
		 * the regular TArray API while adds are in flight; consumers must join all producers
		 * before reading.
		 * @code
		 * Container.Reserve(EntryAmount);
		 * @endcode
		 * @tparam ContainerType
		 * @param Array
		 * @param Element
		 * @return Index of the added element, or INDEX_NONE when the reservation failed
		 */
		template <typename ContainerType>
		int32 AddToArrayThreadSafe(ContainerType& Array,
		                           const typename ContainerType::ElementType& Element)
		{
			return Private::AddToArrayThreadSafeImpl(Array, Element);
		}

		/**
		 * @brief Thread-safe add on any TArray variant. Move semantics version. The container
		 * must have enough memory reserved before addition, otherwise INDEX_NONE is returned
		 * and nothing is added, in any build config.
		 *
		 * The container must not be resized, emptied, iterated, or otherwise touched through
		 * the regular TArray API while adds are in flight; consumers must join all producers
		 * before reading.
		 * @code
		 * Container.Reserve(EntryAmount);
		 * @endcode
		 * @tparam ContainerType
		 * @param Array
		 * @param Element
		 * @return Index of the added element, or INDEX_NONE when the reservation failed
		 */
		template <typename ContainerType>
		int32 AddToArrayThreadSafe(ContainerType& Array,
		                           typename ContainerType::ElementType&& Element)
		{
			return Private::AddToArrayThreadSafeImpl(Array, MoveTempIfPossible(Element));
		}
	}
}
