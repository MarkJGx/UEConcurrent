// Copyright @MarkJGx 2024

#pragma once

#include "Modules/ModuleInterface.h"

class UECONCURRENT_API FUEConcurrentModule : public IModuleInterface
{
	void StartupModule() override;

	void ShutdownModule() override;
};
