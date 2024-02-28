// Copyright 2024 @MarkJGx 

#pragma once

#include "CoreTypes.h"
#include "Async/ParallelFor.h"

namespace UE
{
	namespace Concurrent
	{
		/**
		 * @brief A inline parallel for that supports inline an for() when not running in parallel (EParallelForFlags::ForceSingleThread).
		 * Sometimes running in single thread is better, in terms of memory latency and auto vectorization. Test on a case by case basis.
		 * @tparam FunctionBody 
		 * @tparam ParallelMode 
		 * @param Num 
		 * @param Function 
		 */
		template <EParallelForFlags ParallelMode, typename FunctionBody>
		void InlineParallelFor(int32 Num, const FunctionBody& Function)
		{
			if constexpr (ParallelMode == EParallelForFlags::ForceSingleThread)
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
			if (ParallelMode == EParallelForFlags::ForceSingleThread)
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

		template <EParallelForFlags ParallelMode, typename ContainerType, typename FunctionBody>
		void InlineParallelForEach(ContainerType& Container, const FunctionBody& Function)
		{
			InlineParallelFor<ParallelMode>(Container.Num(), [&](int32 Index)
			{
				Function(Container[Index]);
			});
		}
	}
}
