#pragma once

template<typename T>
T SwapByteOrder(const T inData)
{
	T reversedValue = inData;

	const size_t numBytes = sizeof(inData);
	std::reverse((char*)&reversedValue, ((char*)&reversedValue + numBytes));

	return reversedValue;
}


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
	FileNameContainer(const char* pFullPath) : mFullPath(pFullPath) 
	{
		mFileName = mFullPath.substr(mFullPath.find_last_of("/\\") + 1);

		const size_t lastIndex = mFileName.find_last_of(".");
		mNoExtension           = mFileName.substr(0, lastIndex);

		const char sep = '\\';
		size_t i = mFullPath.rfind(sep, mFullPath.length());
		if( i != std::string::npos )
		{
			mPathOnly = mFullPath.substr(0, i);
		}
	}

	FileNameContainer(const char* pFileName, const char* pFullPath) : mFileName(pFileName), mFullPath(pFullPath) 
	{
		const size_t lastIndex = mFileName.find_last_of(".");
		mNoExtension           = mFileName.substr(0, lastIndex);

		const char sep = '\\';
		size_t i = mFullPath.rfind(sep, mFullPath.length());
		if( i != std::string::npos )
		{
			 mPathOnly = mFullPath.substr(0, i);
		}
	}

	FileNameContainer(const char* pFileName, const std::string& directory) : mFileName(pFileName), mPathOnly(directory)
	{
		mFullPath = directory + std::string(pFileName);
		
		const size_t lastIndex = mFileName.find_last_of(".");
		mNoExtension           = mFileName.substr(0, lastIndex);
	}

	bool operator < (FileNameContainer& rhs) const
	{		
		bool bResult = (atoi(mNoExtension.c_str()) < atoi(rhs.mNoExtension.c_str()));
		return bResult;
	}

	std::string mFileName;
	std::string mFullPath;
	std::string mPathOnly;
	std::string mNoExtension;
};

class FileData
{
	std::string   mFileName;
	std::string   mFullPath;
	unsigned long mFileSize;
	unsigned long mBufferSize;
	unsigned long mCrc;
	const char*   mpData;
	bool          mbCrcCalculated;
	
private:
	bool ReadInFileData(const char* pFileName);

public:
	FileData() : mFileSize(0), mBufferSize(0), mCrc(0xffffffff), mpData(nullptr), mbCrcCalculated(false) {}
	~FileData();
	
	void  Close();
	bool  InitializeFileData(const FileNameContainer& inFileData);
	bool  InitializeFileData(const char* pFileName, const char* pFullPath);
	bool  DoesThisFileContain(const FileData& otherFile, std::vector<unsigned long>* pOutOffsets, bool bFindMultiple) const;	
	bool  FindDataIndex(const char* pData, unsigned long dataLength, unsigned long& outIndex) const;
	unsigned long GetCRC();

	const char*   GetData() const {return mpData;}
	unsigned long GetDataSize() const {return mBufferSize;}

	static bool IsDataTheSame(const char* pData1, const char* pData2, const unsigned long memSize);
};

class TextFileData
{
public:
	struct TextLine
	{
		std::vector<std::string> mWords;
		std::string              mFullLine;
	};

	FileNameContainer     mFileNameInfo;
	std::vector<TextLine> mLines;

	TextFileData(const FileNameContainer& inFileNameInfo) : mFileNameInfo(inFileNameInfo){}

	bool InitializeTextFile();
};

class FileWriter
{
	FILE*         mpFileHandle = nullptr;
	unsigned long mDataSize    = 0;
	std::string   mFileName;
	
public:
	~FileWriter();

	bool          OpenFileForWrite(const std::string& outFileName);
	bool          WriteData(const void* pData, unsigned long size);
	void          Close();
	unsigned long GetFileSize() const {return mDataSize;}
	FILE*         GetHandle() {return mpFileHandle;}
};

class TextFileWriter
{
	FILE*       mpFileHandle = nullptr;
	std::string mFileName;

public:
	~TextFileWriter();

	bool OpenFileForWrite(const std::string& outFileName);
	void WriteString(const std::string& inString);
	void Close();
	FILE* GetFileHandle() { return mpFileHandle; }
};

class PaletteData
{
	char* mpPaletteData      = nullptr;
	int   mNumBytesInPalette = 0; 

public:
	~PaletteData();

	bool        CreateFrom15BitData(const char* pInData, int inSize);
	bool        CreateFrom24BitData(const char* pInData, int inSize);
	bool        CreateFrom32BitData(const char* pInData, int inSize, bool bDropFirstBit);
	const char* GetData() const {return mpPaletteData;}
	int         GetSize() const {return mNumBytesInPalette;}
	void        SetValue(int index, unsigned short value);
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

		void Initialize(int width, int height, short bitCount)
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
			mNumColors          = (int)pow(2, bitCount);
			mNumImportantColors = 0;
		}
	};

	struct ColorData
	{
		int  mSizeInBytes = 0;
		char* mpRGBA      = nullptr;

		~ColorData()
		{
			delete[] mpRGBA;
			mpRGBA = nullptr;
		}
	};

	FileHeader mFileHeader;
	InfoHeader mInfoHeader;
	ColorData  mPaletteData;
	ColorData  mColorData;
};

class BitmapWriter
{
	bool SaveAsPNG(const std::string& inFileName, int inWidth, int inHeight, int bitsPerPixel, const char* pInColorData, int inColorSize, const char* pInPaletteData, int inPaletteSize);
public:
	bool CreateBitmap(const std::string& inFileName, int width, int height, int bitsPerPixel, const char* pInColorData, int inColorSize, const char* pPaletteData, int paletteSize);
};

class BitmapReader
{
public:
	BitmapData mBitmapData;

	bool ReadBitmap(const std::string& inFileName);
};

class BitmapSurface
{
public:
	enum EBitsPerPixel : int
	{
		kBPP_4 = 4
	};

private:
	int           mWidth        = 0;
	int           mHeight       = 0;
	int           mBufferSize   = 0;
	int           mBytesPerRow  = 0;
	EBitsPerPixel mBitsPerPixel = kBPP_4;
	char*         mpBuffer      = nullptr;
	const char*   mpPalette     = nullptr;
	int           mPaletteSize  = 0;

public:
	~BitmapSurface();

	bool CreateSurface(int width, int height, EBitsPerPixel bitsPerPixel, const char* pPalette, int paletteSize);
	void AddTile(const char* pData, int dataSize, int x, int y, int width, int height);
	bool WriteToFile(const std::string& fileName);
};

class TileExtractor
{
public:
	struct Tile
	{
		char*  mpTile          = nullptr;
		unsigned int mTileSize = 0;
		unsigned int mX        = 0;
		unsigned int mY        = 0;

		~Tile()
		{
			delete[] mpTile;
			mpTile = nullptr;
		}
	};

	std::vector<Tile> mTiles;

	bool ExtractTiles(unsigned int inTileDimX, unsigned int inTileDimY, unsigned int outTileDimX, unsigned int outTileDimY, const BitmapReader& inBitmap);
};

struct PRSCompressor
{
	char*         mpCompressedData = nullptr;
	unsigned long mCompressedSize  = 0;

	~PRSCompressor();
	void CompressData(void* pInData, const unsigned long inDataSize);
	void Reset();
};

struct PRSDecompressor
{
	char*         mpUncompressedData = nullptr;
	unsigned long mUncompressedDataSize = 0;

	~PRSDecompressor();
	bool UncompressData(void* pInData, unsigned int inDataSize);
};

void FindAllFilesWithinDirectory(const std::string& inDirectoryPath, std::vector<FileNameContainer>& outFileNames);
bool CreateDirectoryHelper(const std::string& dirName);
bool AreFilesTheSame(const FileData& file1, const FileNameContainer& file2Name);
bool FindDataWithinBuffer(const char* pBuffer, unsigned long bufferSize, const char* pSearchData, const unsigned int searchDataSize, unsigned long& outIndex);
unsigned long prs_decompress_size(void* source);
unsigned long prs_decompress(void* source, void* dest, unsigned long destSize);
unsigned long prs_compress(void* source, void* dest, unsigned long size);
int prs_decompress_buf(const uint8_t *src, uint8_t **dst, size_t src_len);