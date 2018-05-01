#pragma once

struct MatchInfo
{
	std::string   mFileName;
	unsigned long mOffset;
	bool          mbFoundMatch;

	MatchInfo() : mOffset(0), mbFoundMatch(false) {}
	MatchInfo(const std::string& inString, unsigned long inOffset) : mFileName(inString), mOffset(inOffset) {}
};

struct FileNameContainer
{
	FileNameContainer(const char* pFileName, const char* pFullPath) : mFileName(pFileName), mFullPath(pFullPath) 
	{
		const size_t lastIndex = mFileName.find_last_of(".");
		mNoExtension           = mFileName.substr(0, lastIndex);
	}

	std::string mFileName;
	std::string mFullPath;
	std::string mNoExtension;
};

class FileData
{
	std::string   mFileName;
	std::string   mFullPath;
	unsigned long mFileSize;
	unsigned long mBufferSize;
	const char*   mpData;

private:
	bool IsDataTheSame(const char* pData1, const char* pData2, const unsigned long memSize) const;
	bool ReadInFileData(const char* pFileName);

public:
	FileData() : mFileSize(0), mBufferSize(0), mpData(nullptr) {}
	~FileData();
	
	bool InitializeFileData(const FileNameContainer& inFileData);
	bool InitializeFileData(const char* pFileName, const char* pFullPath);
	bool DoesThisFileContain(const FileData& otherFile, std::vector<unsigned long>& outOffsets, bool bFindMultiple) const;

	const char*   GetData() const {return mpData;}
	unsigned long GetDataSize() const {return mBufferSize;}
};

class FileWriter
{
	FILE*         mpFileHandle = nullptr;
	unsigned long mDataSize    = 0;
	std::string   mFileName;
	
public:
	~FileWriter();

	bool OpenFileForWrite(const std::string& outFileName);
	bool WriteData(const void* pData, unsigned long size);
	void Close();
};

struct BitmapData
{
#pragma pack(push, 1)
	struct FileHeader
	{
		short mType;
		int   mSize;
		short mReserved1;
		short mReserved2;
		int   mOffsetToData;

		void Initialize(int fileSizeInBytes, int offsetToData)
		{
			mType         = 19778;
			mSize         = fileSizeInBytes;
			mReserved1    = 0;
			mReserved2    = 0;
			mOffsetToData = offsetToData;
		}
	};
#pragma pack(pop)

	struct InfoHeader
	{
		int   mInfoHeaderSize;
		int   mImageWidth;
		int   mImageHeight;
		short mNumPlanes;
		short mBitCount;
		int   mCompression;
		int   mImageDataSize;
		int   mXPelsPerMeter;
		int   mYPelsPerMeter;
		int   mNumColors;
		int   mNumImportantColors;

		void Initialize(int width, int height, int bitCount)
		{
			mInfoHeaderSize     = sizeof(InfoHeader);
			mImageWidth         = width;
			mImageHeight        = height;
			mNumPlanes          = 1;
			mBitCount           = bitCount;
			mCompression        = 0;
			mImageDataSize      = 0;
			mXPelsPerMeter      = 0;
			mYPelsPerMeter      = 0;
			mNumColors          = 16;
			mNumImportantColors = 0;
		}
	};

	struct ColorData
	{
		int  mSizeInBytes;
		int* mpBGR;
	};

	FileHeader mFileHeader;
	InfoHeader mInfoHeader;
	ColorData  mPaletteData;
	ColorData  mColorData;
};

class BitmapWriter
{
	struct BulkColorData
	{
		const char*   pData;
		unsigned long size;

		BulkColorData(const char* pInData, unsigned long inSize) : pData(pInData), size(inSize){}
	};

	FileWriter                 mOutFile;
	std::vector<BulkColorData> mColorData;
	const char*                mpPaletteData = nullptr;
	int                        mPaletteSize  = 0;
	int                        mBitsPerPixel = 0;
	int                        mWidth        = 0;
	int                        mHeight       = 0;

	unsigned long GetColorDataSize() const;
	
public:
	bool CreateBitmap(const std::string& inFileName, int width, int height, int bitsPerPixel, const char* pPaletteData, int paletteSize);
	void WriteData(const char* pColorData, int dataSize);
	void Close();
};

void FindAllFilesWithinDirectory(const std::string& inDirectoryPath, std::vector<FileNameContainer>& outFileNames);
bool CreateDirectoryHelper(const std::string& dirName);