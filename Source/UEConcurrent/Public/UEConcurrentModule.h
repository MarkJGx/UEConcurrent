// Copyright @MarkJGx 2024

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FUEConcurrentModule : public IModuleInterface
{
	void StartupModule() override;

	void ShutdownModule() override;
};
