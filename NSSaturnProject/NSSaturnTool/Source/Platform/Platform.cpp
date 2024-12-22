#include "Platform\Platform.h"

#include "Rendering\API\D3D11\D3D11Interface.h"

void PlatformInterface::InitializePlatform()
{
	D3D11Interface::CreateGraphicsInterface();
}