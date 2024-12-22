#include "NSSaturnTool.h"

//TODO remove once there is a cross platform math library
#include <directxmath.h>
using namespace DirectX;

#include "imgui.h"
#include "Platform\Platform.h"

using std::string;
using std::vector;

#include "Rendering\API\GraphicsInterface.h"

NSSaturnTool::~NSSaturnTool()
{
}

void NSSaturnTool::InitializeApp()
{
}

void NSSaturnTool::UpdateUI()
{
	GGraphicsInterface->BeginImGuiScene();

	ImGui::NewFrame();

	ImGui::Begin("Edit Window");
	ImGui::End();

	GGraphicsInterface->EndImGuiScene();
}

void NSSaturnTool::UpdateApp()
{
	GGraphicsInterface->BeginScene();

	UpdateUI();

	GGraphicsInterface->EndScene();
}

void GetOpenFileStrings(NSFileTypes InFileType, std::vector<std::string>& outStrings)
{
	switch (InFileType)
	{
		case NSFileTypes::Memory:
		{
			outStrings.push_back(".bin Files (.bin)\0*.bin\0\0");
			outStrings.push_back("*.bin");
			outStrings.push_back("Open Memory File");
		}break;
	}
}

bool NSSaturnTool::LoadMemoryFile(const std::string& InFileName)
{
	return true;
}
