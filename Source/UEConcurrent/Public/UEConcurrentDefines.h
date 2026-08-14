// Copyright @MarkJGx 2024

#pragma once

#include "HAL/Platform.h"
#include "Misc/EngineVersionComparison.h"

/**
 * @brief Marks a constructor whose resulting object must not be discarded, for scope guards
 * such as UE::Concurrent::TReadWriteLock's read/write checks.
 *
 * [[nodiscard]] on a constructor only carries meaning from C++20 (P1771); UE 5.2+ ships
 * UE_NODISCARD_CTOR for exactly that. Pre-C++20 the attribute is a no-op at best and a
 * pedantic warning on GCC at worst, so it is dropped for C++14 consumers.
 */
#if defined(UE_NODISCARD_CTOR)
	#define UE_CONCURRENT_NODISCARD_CTOR UE_NODISCARD_CTOR
#elif __cplusplus >= 202002L
	#define UE_CONCURRENT_NODISCARD_CTOR [[nodiscard]]
#else
	#define UE_CONCURRENT_NODISCARD_CTOR
#endif

// PLATFORM_COMPILER_HAS_IF_CONSTEXPR is deprecated on UE5 (HAL/Platform.h) — UE5 always
// supports if constexpr, so it is assumed there; the macro remains the correct gate on 4.2x.
#if ENGINE_MAJOR_VERSION >= 5
	#define UE_CONCURRENT_HAS_IF_CONSTEXPR 1
#else
	#define UE_CONCURRENT_HAS_IF_CONSTEXPR PLATFORM_COMPILER_HAS_IF_CONSTEXPR
#endif
