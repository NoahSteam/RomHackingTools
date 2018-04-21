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

	class DataComparer
	{
		const FileData& mFile1;
		const FileData& mFile2;
	
	public:
		DataComparer(const FileData& inFile1, const FileData& inFile2) : mFile1(inFile1), mFile2(inFile2) {}
		bool DoesFile1ContainFile2(unsigned long& outOffset) const;
	};

	friend class DataComparer;

private:
	bool OpenFile(const char* pFileName);
	bool IsDataTheSame(const char* pData1, const char* pData2, const unsigned long memSize) const;

public:
	FileData() : mFileSize(0), mBufferSize(0), mpData(nullptr) {}

	~FileData();

	bool InitializeFileData(const FileName& inFileData);
	bool InitializeFileData(const char* pFileName, const char* pFullPath);
	bool DoesThisFileContain(const FileData& otherFile, unsigned long& outOffset) const;

	static bool Async_DoesThisFileContain(const FileData& thisFile, const FileData& otherFile, unsigned long& outOffset);
};

void FindAllFilesWithinDirectory(const std::string& inDirectoryPath, std::vector<FileName>& outFileNames);