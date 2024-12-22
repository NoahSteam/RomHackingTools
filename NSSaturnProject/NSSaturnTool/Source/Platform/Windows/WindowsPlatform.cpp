#include "Platform\Windows\WindowsPlatform.h"

#include "Rendering\API\D3D11\D3D11Interface.h"

void WindowsPlatformInterface::InitializePlatform()
{
	D3D11Interface::CreateGraphicsInterface();
}

bool WindowsPlatformInterface::GetCurrentWorkingDirectory(std::string& OutDirectory) const
{
	char buffer[MAX_PATH];
	DWORD size = GetCurrentDirectoryA(MAX_PATH, buffer);
	if (size == 0)
	{
		// Error handling
		printf("Failed to get the current working directory.\n");
		return false;
	}

	OutDirectory = buffer;

	return true;
}

bool WindowsPlatformInterface::GetLoadFileName(std::string& OutName, NSFileTypes InFileTypes)
{
	extern void GetOpenFileStrings(NSFileTypes fileType, std::vector<std::string>& outStrings);

	std::vector<std::string> paramStrings;
	GetOpenFileStrings(InFileTypes, paramStrings);

	char buffer[MAX_PATH];
	char fileName[MAX_PATH] = "";
	char initName[MAX_PATH];
	memset(initName, 0, sizeof(initName));

	size_t copySize = paramStrings[1].size();
	if (copySize > MAX_PATH)
		copySize = MAX_PATH;
	memcpy(initName, paramStrings[1].c_str(), copySize);

	GetCurrentDirectory(MAX_PATH, buffer);
	OPENFILENAME openInfo = { sizeof(OPENFILENAME), NULL, NULL,
								paramStrings[0].c_str(),
								NULL, 0, 1, initName, MAX_PATH, fileName, MAX_PATH,
								buffer, paramStrings[2].c_str(),
								0, 0, 1, NULL, 0, NULL, NULL };

	if (::GetOpenFileName(&openInfo))
	{
		OutName = initName;
		return true;
	}
	else
	{
		DWORD error = CommDlgExtendedError();
		error = 0;
	}

	return false;
}

//////////
WinPlatformWindow::WinPlatformWindow()
{

}

bool WinPlatformWindow::Initialize(const char* pID, const char* pName, int x, int y, int width, int height, void* pData)
{
	return true;
}
