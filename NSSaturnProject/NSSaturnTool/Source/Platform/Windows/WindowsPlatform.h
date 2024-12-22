#pragma once

#include "Platform\Platform.h"

class WindowsPlatformInterface : public PlatformInterface
{
public:
	virtual void InitializePlatform() override;
	virtual bool GetCurrentWorkingDirectory(std::string& OutDirectory) const override;
	virtual bool GetLoadFileName(std::string& OutName, NSFileTypes InFileTypes) override;
};

class WinPlatformWindow
{
	HWND mHwnd;

public:
	WinPlatformWindow();

	bool Initialize(const char* pID, const char* pName, int x, int y, int width, int height, void* pData);
};