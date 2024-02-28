// Copyright @MarkJGx 2024 

#pragma once

#include "CoreMinimal.h"

class FResultError
{
	FString Message;

public:
	FResultError()
	{
	}

	FResultError(const FString& Message) : Message(Message)
	{
	}

	FResultError(FString&& Message) : Message(MoveTemp(Message))
	{
	}
};

namespace UE
{
	namespace Concurrent
	{
		/**
		 * @brief Error propagating type wrapper similar to rust's std::result
		 * @tparam OkResult
		 * 
		 * Still experimental as it doesn't exactly match rust's version!
		 */
		template <typename OkResult>
		class TResult
		{
			TOptional<OkResult> Result;
			FResultError Error;

		public:
			using FResult = TResult<OkResult>;

			explicit TResult(const FResultError& Error) : Error(Error)
			{
			};

			explicit TResult(FResultError&& Error) : Error(Error)
			{
			};

			TResult(const OkResult& Result) : Result(Result), Error()
			{
			};

			TResult(OkResult&& Result) : Result(MoveTemp(Result)), Error()
			{
			};

			bool IsOk() const
			{
				return Result.IsSet();
			}

			bool IsError() const
			{
				return !Result.IsSet();
			}

			operator bool() const
			{
				return IsOk();
			}

			OkResult& Unwrap()
			{
				checkf(Result.IsSet(), TEXT("Unwrapping an invalid result!"));
				return Result.Get();
			}

			const OkResult& Unwrap() const
			{
				checkf(Result.IsSet(), TEXT("Unwrapping an invalid result!"));
				return Result.Get();
			}

			template <typename CallableType>
			const FResult& AndThen(CallableType&& Function) const
			{
				if (IsOk())
				{
					Function(*Result);
				}

				return *this;
			}

			template <typename CallableType>
			FResult AndThen(CallableType&& Function)
			{
				if (IsOk())
				{
					Function(*Result);
				}

				return *this;
			}

			template <typename CallableType>
			FResult OnError(CallableType&& Function)
			{
				if (!IsOk())
				{
					Function(Error);
				}

				return *this;
			}
		};

		template <typename OkResult>
		TResult<OkResult> AsError(const FString& Message)
		{
			return TResult<OkResult>(Message);
		}

		template <typename OkResult>
		TResult<OkResult> AsError(FString&& Message)
		{
			return TResult<OkResult>(MoveTemp(Message));
		}

		template <typename OkResult>
		TResult<OkResult> AsOk(const OkResult& Result)
		{
			return TResult<OkResult>(Result);
		}

		template <typename OkResult>
		TResult<OkResult> AsOk(OkResult&& Result)
		{
			return TResult<OkResult>(MoveTemp(Result));
		}
	}
}
