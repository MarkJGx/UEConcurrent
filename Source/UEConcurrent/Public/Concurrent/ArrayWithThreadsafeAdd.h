// Copyright 2024 @MarkJGx
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"

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
					new(this->GetData() + Index) typename ArrayType::ElementType(Forward<ArgsType>(Args)...);
					return Index;
				}


				/**
				 * Adds a given number of uninitialized elements into the array using an atomic increment on the array num
				 *
				 * Caution, the array must have sufficient slack or this will assert/crash. You must presize the array.
				 *
				 * Caution, AddUninitialized() will create elements without calling
				 * the constructor and this is not appropriate for element types that
				 * require a constructor to function properly.
				 *
				 * @param Count Number of elements to add.
				 *
				 * @returns Number of elements in array before addition.
				 */
				int32 AddUninitializedThreadsafe(int32 Count = 1)
				{
					checkSlow(Count >= 0);
					const int32 OldNum = FPlatformAtomics::InterlockedAdd(&this->ArrayNum, Count);
					check(OldNum + Count <= this->ArrayMax);
					return OldNum;
				}

			public:
				/**
				 * Adds a new item to the end of the array, using atomics to update the current size of the array
				 *
				 * Caution, the array must have sufficient slack or this will assert/crash. You must presize the array.
				 *
				 * @param Item	The item to add
				 * @return		Index to the new item
				 */
				int32 AddThreadsafe(const typename ArrayType::ElementType& Item)
				{
					CheckAddress(&Item);
					return EmplaceThreadsafe(Item);
				}

				typename ArrayType::SizeType AddThreadSafe(typename ArrayType::ElementType&& Item)
				{
					CheckAddress(&Item);
					return EmplaceThreadsafe(MoveTempIfPossible(Item));
				}
			};
		}

		template <typename ContainerType, typename ContainerElement>
		void AddToArrayThreadSafe(ContainerType& Array,
		                          const ContainerElement& Element)
		{
			static_assert(TIsContiguousContainer<ContainerType>::Value, "AddToArrayThreadSafe cannot be used on a non Unreal container!");
			static_assert(std::negation_v<std::is_const<std::remove_reference_t<ContainerType>>>,
				"AddToArrayThreadSafe has to be used with a non const container reference!");

			using ArrayType = std::remove_cv_t<std::remove_reference_t<decltype(Array)>>;
			Private::TArrayWithThreadsafeAddHack<ArrayType>* ThreadSafeArray = reinterpret_cast<Private::TArrayWithThreadsafeAddHack<ArrayType>
				*>(&Array);

#if DO_CHECK
			auto* DataPreAdd = Array.GetData();
#endif
			ThreadSafeArray->AddThreadsafe(Forward<ContainerElement>(Element));
#if DO_CHECK
			auto* DataBeforeAdd = Array.GetData();
			checkf(DataPreAdd == DataBeforeAdd,
			       TEXT(
				       "Container has been realoocated during thread safe add. Undefined behavior. You must Reserve container amount pre-addition!"
			       ));
#endif
		}

		/**
		 * @brief Container must have enough memory
		 * @tparam ContainerType
		 * @tparam ContainerElement
		 * @param Array
		 * @param Element
		 */
		template <typename ContainerType, typename ContainerElement>
		void AddToArrayThreadSafe(ContainerType& Array,
		                          ContainerElement&& Element)
		{
			static_assert(TIsContiguousContainer<ContainerType>::Value, "AddToArrayThreadSafe cannot be used on a non Unreal container!");
			static_assert(std::negation_v<std::is_const<std::remove_reference_t<ContainerType>>>,
				"AddToArrayThreadSafe has to be used with a non const container reference!");

			using ArrayType = std::remove_cv_t<std::remove_reference_t<decltype(Array)>>;
			Private::TArrayWithThreadsafeAddHack<ArrayType>* ThreadSafeArray = reinterpret_cast<Private::TArrayWithThreadsafeAddHack<ArrayType>
				*>(&Array);

#if DO_CHECK
			auto* DataPreAdd = Array.GetData();
#endif
			ThreadSafeArray->AddThreadsafe(Forward<ContainerElement>(Element));
#if DO_CHECK
			auto* DataBeforeAdd = Array.GetData();
			checkf(DataPreAdd == DataBeforeAdd,
			       TEXT(
				       "Container has been realoocated during thread safe add. Undefined behavior. You must Reserve() container amount pre-addition!"
			       ));
#endif
		}
	}
}
