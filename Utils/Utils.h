#pragma once

struct MatchInfo
{
	std::string   mFileName;
	unsigned long mOffset;
	bool          mbFoundMatch;

	MatchInfo() : mOffset(0), mbFoundMatch(false) {}
	MatchInfo(const std::string& inString, unsigned long inOffset) : mFileName(inString), mOffset(inOffset) {}
};

struct FileName
{
	FileName(const char* pFileName, const char* pFullPath) : mFileName(pFileName), mFullPath(pFullPath) {}

	std::string mFileName;
	std::string mFullPath;
};

class FileData
{
	std::string   mFileName;
	std::string   mFullPath;
	unsigned long mFileSize;
	unsigned long mBufferSize;
	const char*   mpData;

private:
	bool OpenFile(const char* pFileName);
	bool IsDataTheSame(const char* pData1, const char* pData2, const unsigned long memSize) const;

public:
	FileData() : mFileSize(0), mBufferSize(0), mpData(nullptr) {}

	~FileData();

	bool InitializeFileData(const FileName& inFileData);
	bool InitializeFileData(const char* pFileName, const char* pFullPath);
	bool DoesThisFileContain(const FileData& otherFile, std::vector<unsigned long>& outOffsets, bool bFindMultiple) const;
};

void FindAllFilesWithinDirectory(const std::string& inDirectoryPath, std::vector<FileName>& outFileNames);