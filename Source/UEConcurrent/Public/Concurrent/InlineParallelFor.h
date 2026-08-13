// Copyright 2024 @MarkJGx 

#pragma once

#include "CoreTypes.h"
#include "Async/ParallelFor.h"
#include "Misc/EnumClassFlags.h"

namespace UE
{
	namespace Concurrent
	{
		/**
		 * @brief A parallel for that falls back to an inline for() when not running in parallel (EParallelForFlags::ForceSingleThread).
		 * Sometimes running in single thread is better, in terms of memory latency and auto vectorization. Test on a case by case basis.
		 * @tparam FunctionBody 
		 * @tparam ParallelMode 
		 * @param Num 
		 * @param Function 
		 */
		template <EParallelForFlags ParallelMode, typename FunctionBody>
		void InlineParallelFor(int32 Num, const FunctionBody& Function)
		{
			// EParallelForFlags is a bitmask, so ForceSingleThread may arrive combined with
			// Unbalanced, PumpRenderingThread or BackgroundPriority. An equality test would miss those
			// and fall through to ParallelForTemplate, which still runs serially but costs us the
			// inline loop this whole function exists to provide.
			if constexpr (EnumHasAnyFlags(ParallelMode, EParallelForFlags::ForceSingleThread))
			{
				// Allow for auto vectorization, ParallelForTemplate doesn't support.
				for (int32 Index = 0; Index < Num; Index++)
				{
					Function(Index);
				}
			}
			else
			{
				ParallelForTemplate(Num, Function, ParallelMode);
			}
		}

		template <typename FunctionBody>
		void InlineParallelFor(int32 Num, const FunctionBody& Function,
		                       EParallelForFlags ParallelMode = EParallelForFlags::None)
		{
			if (EnumHasAnyFlags(ParallelMode, EParallelForFlags::ForceSingleThread))
			{
				// Allow for auto vectorization, ParallelForTemplate doesn't support.
				for (int32 Index = 0; Index < Num; Index++)
				{
					Function(Index);
				}
			}
			else
			{
				ParallelForTemplate(Num, Function, ParallelMode);
			}
		}

		template <typename FunctionBody>
		void InlineParallelFor(int32 Num, const FunctionBody& Function,
		                       bool bRunInParallel)
		{
			InlineParallelFor(Num, Function,
			                  bRunInParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
		}

		/**
		 * @brief InlineParallelFor over a container's elements. Indexes the container, so it requires
		 * random access (TArray, TArrayView and friends) rather than any iterable container.
		 * @tparam ParallelMode
		 * @tparam ContainerType
		 * @tparam FunctionBody
		 * @param Container
		 * @param Function
		 */
		template <EParallelForFlags ParallelMode, typename ContainerType, typename FunctionBody>
		void InlineParallelForEach(ContainerType& Container, const FunctionBody& Function)
		{
			InlineParallelFor<ParallelMode>(Container.Num(), [&](int32 Index)
			{
				Function(Container[Index]);
			});
		}

		template <typename ContainerType, typename FunctionBody>
		void InlineParallelForEach(ContainerType& Container, const FunctionBody& Function,
		                           EParallelForFlags ParallelMode = EParallelForFlags::None)
		{
			InlineParallelFor(Container.Num(), [&](int32 Index)
			{
				Function(Container[Index]);
			}, ParallelMode);
		}

		template <typename ContainerType, typename FunctionBody>
		void InlineParallelForEach(ContainerType& Container, const FunctionBody& Function,
		                           bool bRunInParallel)
		{
			InlineParallelForEach(Container, Function,
			                      bRunInParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
		}
	}
}
