// Copyright 2024 @MarkJGx 
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/IsEnum.h"

template <class EnumType>
constexpr auto CastToUnderlying(EnumType Type)
{
	static_assert(TIsEnum<EnumType>::Value, "CastToUnderlying requires an enum type!");
	using UnderType = __underlying_type(EnumType);
	return static_cast<UnderType>(Type);
}
