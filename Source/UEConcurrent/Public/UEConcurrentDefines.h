// Copyright @MarkJGx 2024

#pragma once

#include "HAL/Platform.h"

/**
 * @brief Marks a constructor whose resulting object must not be discarded, for scope guards
 * such as UE::Concurrent::TReadWriteLock's read/write checks.
 *
 * [[nodiscard]] on a constructor only carries meaning from C++20 (P1771); UE 5.2+ ships
 * UE_NODISCARD_CTOR for exactly that. Older engines have UE_NODISCARD (HAL/Platform.h),
 * which expands to nothing when the attribute is unavailable, so this is safe on 4.27+.
 */
#if defined(UE_NODISCARD_CTOR)
	#define UE_CONCURRENT_NODISCARD_CTOR UE_NODISCARD_CTOR
#else
	#define UE_CONCURRENT_NODISCARD_CTOR UE_NODISCARD
#endif
