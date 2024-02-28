#include "UEConcurrentModule.h"
#include "Modules/ModuleManager.h"

void FUEConcurrentModule::StartupModule()
{
	IModuleInterface::StartupModule();
}

void FUEConcurrentModule::ShutdownModule()
{
	IModuleInterface::ShutdownModule();
}

IMPLEMENT_MODULE(FUEConcurrentModule, UEConcurrent)

