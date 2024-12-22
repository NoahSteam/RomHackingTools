#pragma once

enum class NSFileTypes
{
	Memory
};

class PlatformInterface
{
public:
	~PlatformInterface(){}

	virtual void InitializePlatform() = 0;
	virtual bool GetCurrentWorkingDirectory(std::string& OutDirectory) const = 0;
	virtual bool GetLoadFileName(std::string& OutName, NSFileTypes InFileTypes) = 0;
};

extern PlatformInterface* GPlatformInterface;
