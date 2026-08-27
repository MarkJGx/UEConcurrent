// Copyright MarkJGx 2024-2026

#pragma once

#include "Modules/ModuleInterface.h"

class UECONCURRENT_API FUEConcurrentModule : public IModuleInterface
{
	void StartupModule() override;

	void ShutdownModule() override;
};
