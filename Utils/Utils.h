#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include "Shims.h"

typedef unsigned long uint64;
typedef unsigned int   uint32;
typedef unsigned short uint16;
typedef unsigned char  uint8;
extern const std::string Seperators;

template<typename T>
T SwapByteOrder(const T inData)
{
	T reversedValue = inData;

	const size_t numBytes = sizeof(inData);
	std::reverse((char*)&reversedValue, ((char*)&reversedValue + numBytes));

	return reversedValue;
}

void SwapByteOrderInPlace(char* pData, unsigned int numBytes);
int FourByteAlign(int InValue);

extern uint32 CalculateDataCRC(char* pData, uint32 dataSize);

struct MatchInfo
{
	std::string   mFileName;
	unsigned long mOffset;
	bool          mbFoundMatch;

	MatchInfo() : mOffset(0), mbFoundMatch(false) {}
	MatchInfo(const std::string& inString, unsigned long inOffset) : mFileName(inString), mOffset(inOffset), mbFoundMatch(false) {}
};

struct FileNameContainer
{
	FileNameContainer()
	{
	}

	FileNameContainer(const std::string& inPath) : mFullPath(inPath)
	{
		InitializeFromFullPath();
	}

	FileNameContainer(const char* pFullPath) : mFullPath(pFullPath) 
	{
		InitializeFromFullPath();
	}

	FileNameContainer(const char* pFileName, const char* pFullPath) : mFileName(pFileName), mFullPath(pFullPath) 
	{
		const size_t lastIndex = mFileName.find_last_of(".");
		mNoExtension           = mFileName.substr(0, lastIndex);
		mExtention             = lastIndex != std::string::npos ? mFileName.substr(lastIndex, mFileName.size() - 1) : "";

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
		mExtention             = lastIndex != std::string::npos ? mFileName.substr(lastIndex, mFileName.size() - 1) : "";
	}

	bool operator < (const FileNameContainer& rhs) const
	{		
		bool bResult = (atoi(mNoExtension.c_str()) < atoi(rhs.mNoExtension.c_str()));
		return bResult;
	}

	std::string mFileName;
	std::string mFullPath;
	std::string mPathOnly;
	std::string mNoExtension;
	std::string mExtention;

private:
	void InitializeFromFullPath()
	{
		mFileName = mFullPath.substr(mFullPath.find_last_of("/\\") + 1);

		const size_t lastIndex = mFileName.find_last_of(".");
		mNoExtension           = mFileName.substr(0, lastIndex);
		mExtention             = lastIndex != std::string::npos ? mFileName.substr(lastIndex, mFileName.size() - 1) : "";

		const char sep = '\\';
		size_t i = mFullPath.rfind(sep, mFullPath.length());
		if( i != std::string::npos )
		{
			mPathOnly = mFullPath.substr(0, i);
		}
	}
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
	bool ReadInFileData(const char* pFileName, bool bInErrorOnFail);

public:
	FileData() : mFileSize(0), mBufferSize(0), mCrc(0xffffffff), mpData(nullptr), mbCrcCalculated(false) {}
	~FileData();
	
	void  Close();
	bool  InitializeFileData(const FileNameContainer& inFileData, bool bInErrorOnFail = true);
	bool  InitializeFileData(const std::string& inFullPath, bool bInFailOnError = true);
	bool  InitializeFileData(const char* pFileName, const char* pFullPath, bool bInErrorOnFail = true);
	void  WriteToFile(const char* pFileName) const;
	bool  DoesThisFileContain(const FileData& otherFile, std::vector<unsigned long>* pOutOffsets, bool bFindMultiple) const;	
	bool  FindDataIndex(const char* pData, unsigned long dataLength, unsigned long& outIndex) const;
	unsigned long GetCRC();
	bool ReadData(unsigned long inDataOffset, char* pOutData, unsigned long inDataSize, bool bSwapEndianness);

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

	private:
		int                      mNumberOfLines = 1;

	public:
		int GetNumberOfLines() const
		{
			return mNumberOfLines;
		}

		friend class TextFileData;
	};

	FileNameContainer     mFileNameInfo;
	std::vector<TextLine> mLines;

	TextFileData(const FileNameContainer& inFileNameInfo) : mFileNameInfo(inFileNameInfo){}

	bool InitializeTextFile(bool bFixupSpecialCharacters = true, bool bCollapseDots = true);
};

class FileWriter
{
	FILE*         mpFileHandle = nullptr;
	unsigned long mDataSize    = 0;
	std::string   mFileName;
	
public:
	~FileWriter();

	bool          OpenFileForWrite(const std::string& outFileName);
	bool          WriteData(const void* pData, unsigned long size, bool bSwapEndinanness = false);
	bool          WriteDataAtOffset(const void* pData, unsigned long size, unsigned long offset, bool bSwapEndianness = false);
	void          Close();
	unsigned long GetFileSize() const {return mDataSize;}
	FILE*         GetHandle() {return mpFileHandle;}
	const std::string& GetFileName() const {return mFileName;}
	void          SeekToStart();
};

class TextFileWriter
{
	FILE*       mpFileHandle = nullptr;
	std::string mFileName;

public:
	~TextFileWriter();

	bool  OpenFileForWrite(const std::string& outFileName);
	void  Printf(const char* fmt, ...);
	void  WriteString(const std::string& inString);
	void  WriteStringWithNewLine(const std::string& inString);
	void  AddNewLine();
	void  Close();
	FILE* GetFileHandle() { return mpFileHandle; }
};

class FileReadWriter
{
	FILE*       mpFileHandle = nullptr;
	std::string mFileName;
	unsigned long mFileSize{0};

public:
	~FileReadWriter();

	bool  OpenFile(const std::string& inFileName, bool bInOutputError = true);
	void  Close();
	bool  WriteData(const char* pInData, unsigned long inDataSize);
	bool  WriteData(unsigned long fileOffset, const char* pData, unsigned long dataSize);
	bool  WriteData(unsigned long fileOffset, const char* pData, unsigned long dataSize, bool bSwapEndianness);
	bool  ReadData(unsigned long inFileOffset, char* pData, unsigned long dataSize, bool bSwapEndianness = false);
	FILE* GetFileHandle() {return mpFileHandle;}
	unsigned long GetFileSize() const {return mFileSize;}
};

class PaletteData
{
	char* mpPaletteData      = nullptr;
	int   mNumBytesInPalette = 0; 
	int   mNumColors         = 0;
public:
	~PaletteData();

	//These functions will create a palette into a BGR 5:5:5 format
	bool        CreateFrom15BitData(const char* pInData, int inSize);
	bool        CreateFrom24BitData(const char* pInData, int inSize);
	bool        CreateFrom32BitData(const char* pInData, int inSize, bool bDropFirstBit);
	
	//This one will create a standard palette from a MS 32bit rbga palette
	bool        CreateFromMicrosoftPalette(const char* pInData, int inSize);

	const char* GetData() const {return mpPaletteData;}
	int         GetSize() const {return mNumBytesInPalette;}
	int         GetNumColors() const {return mNumColors;}
	void        SetValue(int index, unsigned short value);
};

class MicrosoftPaletteData
{
public:
#pragma pack(push, 1)
	struct Header
	{
		char riff[4];
		int fileSize;
		char palData[8];
		int fileSize2;
		char zero;
		char three;
		unsigned short paletteSize;
	};
#pragma pack(pop)

	Header mHeader;

	static bool CreatePaletteData(const FileData& inData, PaletteData& outData);
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
			mNumColors          = 1u << bitCount;
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
	bool CreateBitmap(const std::string& inFileName, int width, int height, int bitsPerPixel, const char* pInColorData, int inColorSize, const char* pPaletteData, int paletteSize, bool bForceBMPFormat = false);
};

class BitmapReader
{
	std::string mFilename;

public:
	BitmapData mBitmapData;

	bool ReadBitmap(const std::string& inFileName, bool bInErrorOnFail = true);
	bool Save();
	const std::string& GetFileName() const {return mFilename; }
	char* GetColorData() const {return mBitmapData.mColorData.mpRGBA;}
	int GetColorDataSize() const {return mBitmapData.mColorData.mSizeInBytes;}
	char* GetPaletteData() const {return mBitmapData.mPaletteData.mpRGBA;}
	int GetPaletteDataSize() const {return mBitmapData.mPaletteData.mSizeInBytes;}
	int GetWidth() const {return mBitmapData.mInfoHeader.mImageWidth;}
	int GetHeight() const {return mBitmapData.mInfoHeader.mImageHeight;}
	int GetBitCount() const {return mBitmapData.mInfoHeader.mBitCount;}
	void SetPaletteValueAtIndex(int inIndex, uint32 inColor);
	void ReplaceColors(const char inNewColor, std::unordered_set<char>& inIgnoreColors);
	void SwapColors(const char inSearchColor, const char inNewColor);
};

class BitmapSurface
{
public:
	enum EBitsPerPixel : int
	{
		kBPP_4 = 4,
		kBPP_8 = 8
	};

	enum EFlipFlag : uint8
	{
		kFlipNone = 0,
		kFlipHoriz = 1 << 1,
		kFlipVert  = 1 << 2,
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
	int           mNumTiles     = 0;
	bool          mbOwnsPalette = false;

public:
	~BitmapSurface();

	bool CreateSurface(int width, int height, EBitsPerPixel bitsPerPixel, const char* pPalette, int paletteSize, bool bDuplicatePalette = false);
	void AddTile(const char* pData, int dataSize, int x, int y, int width, int height, EFlipFlag flipFlag = kFlipNone);
	void AddPartialTile(const char* pData, int dataSize, int x, int y, int numBytesInWidthToCopy, int width, int height);
	bool WriteToFile(const std::string& fileName, bool bForceBitmap = false) const;
	
	const char* GetPaletteData() const {return mpPalette;}
	char* GetColorData() const {return mpBuffer;}
	int GetColorDataSize() const {return mBufferSize;}
	EBitsPerPixel GetBitsPerPixel() const {return mBitsPerPixel;}
	int GetWidth() const {return mWidth;}
	int GetHeight() const {return mHeight;}
	int GetNumTiles() const {return mNumTiles;}
};

class BitmapFormatConverter
{
public:
	~BitmapFormatConverter();

	bool ConvertFrom15BitTo32Bit(const char* pInData, int InDataSize);
	bool ConvertFrom32BitTo15Bit(const char* pInBGRAData, int InDataSize);

	bool ConvertFrom4BitTo8Bit(const char* pIn8BitBmpPath);
	bool ConvertFrom4BitTo8Bit(const BitmapReader& InSourceBitmap);

	bool ConvertFrom8BitTo4Bit(const char* pIn8BitBmpPath);
	
	char* GetConvertedData() {return mpConvertedData;}
	size_t GetConvertedDataSize() const {return mConvertedDataSize;}

	char* GetPaletteData() const {return mpPaletteData;}
	size_t GetPaletteDataSize() const {return mPaletteDataSize;}

	void WriteToFile(const char* pInOutputPath);

	bool ConvertColorDataFrom4BitTo8Bit(const char* pInData, const size_t inDataSize);
	bool ConvertColorDataFrom8BitTo4Bit(const char* pInData, const size_t inDataSize);

private:
	char* mpConvertedData = nullptr;
	char* mpPaletteData = nullptr;
	size_t mConvertedDataSize = 0;
	size_t mPaletteDataSize = 0;

	int mWidth = 0;
	int mHeight = 0;
	int mBitCount = 0;
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
		unsigned int mTileWidth = 0;
		unsigned int mTileHeight = 0;
		unsigned int mWidthOfContent = 0;
		unsigned int mBytesInWidthOfContent = 0;

		Tile()
		{

		}

		Tile(const Tile& other) : mTileSize(other.mTileSize), mX(other.mX), mY(other.mY), mTileWidth(other.mTileWidth),
			mTileHeight(other.mTileHeight), mWidthOfContent(other.mWidthOfContent), mBytesInWidthOfContent(other.mBytesInWidthOfContent)
		{
			mTileSize = other.mTileSize;
			mpTile = new char[mTileSize];
			
			memcpy_s(mpTile, mTileSize, other.mpTile, mTileSize);
		}

		Tile(Tile&& other) : mpTile(other.mpTile), mTileSize(other.mTileSize), mX(other.mX), mY(other.mY), mTileWidth(other.mTileWidth),
							 mTileHeight(other.mTileHeight), mWidthOfContent(other.mWidthOfContent), mBytesInWidthOfContent(other.mBytesInWidthOfContent)
		{
			other.mpTile = nullptr;
		}

		Tile& operator=(Tile&& other)
		{
			if (this != &other)
			{
				delete[] mpTile;
				mpTile = other.mpTile;
				other.mpTile = nullptr;

				mTileSize              = other.mTileSize;
				mX                     = other.mX;
				mY                     = other.mY;
				mTileWidth             = other.mTileWidth;
				mTileHeight            = other.mTileHeight;
				mWidthOfContent        = other.mWidthOfContent;
				mBytesInWidthOfContent = other.mBytesInWidthOfContent;
			}
			return *this;
		}

		~Tile()
		{
			delete[] mpTile;
			mpTile = nullptr;
		}

		uint32 GetFontTileCRC() const
		{
			return CalculateDataCRC(mpTile, mTileSize);
		}
	};

	std::vector<Tile> mTiles;
	unsigned int mImageWidth = 0;
	unsigned int mImageHeight = 0;

private:
	int mDataSize = 0;
	unsigned int mTileByteSize = 0;
	
public:
	bool ExtractTiles(unsigned int inTileDimX, int inTileDimY, unsigned int outTileDimX, unsigned int outTileDimY, const BitmapReader& inBitmap, int inAlphaIndex = 0);
	void AddTile(const Tile& InTile);
	void FixupIndexOfAlphaColor(const unsigned short inIndexOfAlphaColor, bool bInIs4bit);
	int GetDataSize() const {return mDataSize;}
	void OutputTiles(FileWriter& outFile, int inStartingTile) const;
	void OutputTiles(FileReadWriter& outFile, int inStartingTile, int inOffset) const;
	unsigned int GetSizeOfSingleTile() const {return mTileByteSize;}
	uint32 GetTileCRC(int inTileIndex) const;
	int GetNumTiles() const {return (int)mTiles.size();}
	void SwapColorsOnTiles(const char inSearchColor, const char inReplaceColor);
};

struct RGBBmpToSaturnConverter
{
	~RGBBmpToSaturnConverter();

	bool ConvertBmpToSakuraFormat(const std::string& inBmpPath);
	int GetImageWidth() const { return mWidth; }
	int GetImageHeight() const { return mHeight; }
	const char* GetImageData() const {return mpBGR;}
	unsigned int GetImageDataSize() const {return mColorDataSize;}

private:
	const char* mpBGR = nullptr;
	int mColorDataSize{0};
	int mWidth{ 0 };
	int mHeight{ 0 };
	int mBitCount{ 0 };
};

struct BmpToSaturnConverter
{
	TileExtractor mTileExtractor;
	PaletteData   mPalette;
	char*         mpPackedTiles = nullptr;
	unsigned int  mPackedTileSize = 0;
	
	static const unsigned short CYAN = 0xe07f; //In little endian order
	static const unsigned short WHITE = 0xff7f;
	static const unsigned short BLACK = 0;

	~BmpToSaturnConverter();

	bool ConvertBmpToSakuraFormat(const std::string& inBmpPath, bool bFixupAlphaColor, const unsigned short inAlphaColor = CYAN, const unsigned int* pTileWidth = 0, const unsigned int* pTileHeight = 0, bool bInMustExist = true);
	void PackTiles();
	int GetImageWidth() const {return mTileExtractor.mImageWidth;}
	int GetImageHeight() const {return (int)mTileExtractor.mImageHeight < 0 ? -1 * mTileExtractor.mImageHeight : mTileExtractor.mImageHeight;}
	const char* GetImageData() const;
	unsigned int GetImageDataSize() const;
};

/////////////////////////////////
//        TileOptimizer        //
/////////////////////////////////
class TileSetOptimizer
{
public:
	void OptimizeTileSet(const BmpToSaturnConverter& InSourceImage);
	int GetNumOptimizedTiles() const { return mOptimizedTileSet.GetNumTiles(); }
	void OutputImage(const std::string& InOutputDir, bool bInForceBmp) const;
	void PackTiles();
	const char* GetPackedTiles() const {return mpPackedTiles;}
	uint32 GetPackedTileSize() const {return mPackedTileSize;}
	const std::vector<int>& GetTileIndices() const {return mTiledIndicesForOriginalImage;}
	void GetTiledIndicesInSaturnFormat(std::vector<int>& OutIndices) const;

private:
	TileExtractor    mOptimizedTileSet;
	std::vector<int> mTiledIndicesForOriginalImage;
	char*            mpPackedTiles = nullptr;
	uint32           mPackedTileSize = 0;
};

class TileMap
{
	class TileData
	{
		char* mpData;

	public:
		TileData(const char* pInData, int dataSize)
		{
			mpData = new char[dataSize];
			memcpy(mpData, pInData, dataSize);
		}

		TileData(TileData&& other) : mpData(other.mpData)
		{
			other.mpData = nullptr;
		}

		TileData& operator=(TileData&& other)
		{
			if (this != &other)
			{
				delete[] mpData;
				mpData = other.mpData;
				other.mpData = nullptr;
			}

			return *this;
		}

		~TileData()
		{
			delete[] mpData;
			mpData = nullptr;
		}

		const char* GetFontTileData() const
		{
			return mpData;
		}
	};

	std::vector<TileData> mTiles;

public:
	bool          CreateFontSheetFromData(const char* pInData, unsigned int inDataSize);
	bool          CreateFontSheet(const FileNameContainer& inFileNameInfo);
	const char*   GetTileData(int inIndex) const;
	int           GetNumTiles() const { return static_cast<int>(mTiles.size());}	
};

class MemoryBlocks
{
public:
	struct Block
	{
		char*        pData = nullptr;
		unsigned int blockSize = 0;
	};

private:
	std::vector<Block> mBlocks;
	char* mpCombinedBlocks = nullptr;
	unsigned int mCombinedSize = 0;

public:
	~MemoryBlocks();

	char*        AddBlock(const char* pOriginalData, unsigned int offset, unsigned int blockSize, bool bReverseBytes = false);
	void         AddBlock(const MemoryBlocks& inBlock);
	void         CombineBlocks();
	const char*  GetCombinedData() const {return mpCombinedBlocks;}
	unsigned int GetTotalSize() const {return mCombinedSize;}
	size_t       GetNumberOfBlocks() const;
	const Block& GetBlock(unsigned int blockIndex) const;
	bool         WriteInBlock(unsigned int blockIndex, unsigned int offset, const char* pData, unsigned int dataSize);
};

struct PuyoPrsCompressor
{
	void CompressData(const void* pInData, const unsigned long inDataSize);
	const std::vector<uint8>& GetCompressedData() const {return mCompressedData;}

private:
	// TODO: Why is that method even declared?
	// byte ReadByte(std::vector<uint8>& stream);
	void Flush(uint8& controlByte, uint8& bitPos, std::vector<uint8>& data, std::vector<uint8>& destination);
	void PutControlBit(int bit, uint8& controlByte, uint8& bitPos, std::vector<uint8>& data, std::vector<uint8>& destination);
	void Copy(int offset, int size, uint8& controlByte, uint8& bitPos, std::vector<uint8>& data, std::vector<uint8>& destination);

	std::vector<uint8> mCompressedData;
};

struct PRSCompressor
{
	enum ECompressOption
	{
		kCompressOption_None,
		kCompressOption_TwoByteAlign,
		kCompressOption_FourByteAlign,
	};

	char*         mpCompressedData = nullptr;
	unsigned long mCompressedSize  = 0;

	~PRSCompressor();
	void CompressData(const void* pInData, const unsigned long inDataSize, ECompressOption compressOption = kCompressOption_None);
	void Reset();
};

struct PRSDecompressor
{
	char*         mpUncompressedData = nullptr;
	unsigned long mUncompressedDataSize = 0;
	size_t        mCompressedSize = 0;

	~PRSDecompressor();
	bool UncompressData(void* pInData, unsigned int inDataSize);
};

struct CsvTileReader
{
	std::vector<int> mTileEntries;

	bool ReadInTiles(const std::string& inFilePath);
	void SwapEndianess();
};

struct YabauseToMednafin
{
	void ConvertData(const std::string& inFileName, const std::string& outFileName);
};

struct MednafinToYabause
{
	void ConvertData(const std::string& inFileName, const std::string& outFileName);
};

void FindAllFilesWithinDirectory(const std::string& inDirectoryPath, std::vector<FileNameContainer>& outFileNames);
void FindAllDirectoriesWithinDirectory(const std::string& inDirectoryPath, std::vector<std::string>& outDirectories);
bool DoesDirectoryExist(const std::string& dirName);
bool DoesFileExist(const std::string& dirName);
bool CreateDirectoryHelper(const std::string& dirName);
bool AreFilesTheSame(const FileData& file1, const FileNameContainer& file2Name);
bool FindDataWithinBuffer(const char* pBuffer, unsigned long bufferSize, const char* pSearchData, const unsigned int searchDataSize, unsigned long& outIndex);
int CreateTemporaryDirectory(std::string& outDir);
int CreateDirectoryPortableHelper(const std::string& dirName);

void GetAllFilesOfType(const std::vector<FileNameContainer>& allFiles, const char* pInFileType, std::vector<FileNameContainer>& outFiles);
bool CopyFiles(const std::vector<FileNameContainer>& InSourceFiles, const std::string& InOutputDirectory, std::unordered_set<std::string>* pInIgnoreFiles = nullptr);
bool CopyFiles(const std::string& InSourceDirectory, const std::string& InOutputDirectory, std::unordered_set<std::string>* pInIgnoreFiles = nullptr);
bool CreateSpreadSheetForImages(const std::string& InSpreadsheetName, const std::string InExtractedImageDirName, const std::string& InImageDirectory, const std::string& InOutputPhpFilePath);

//unsigned long prs_decompress_size(void* source);
//unsigned long prs_decompress(void* source, void* dest, unsigned long destSize);
//unsigned long prs_compress(void* source, void* dest, unsigned long size);
int prs_decompress_buf(const uint8_t *src, uint8_t **dst, size_t src_len, size_t &outCompressedSize);
int prs_compress(const uint8_t *src, uint8_t **dst, size_t src_len);

template<typename T>
T ConvertFromHexString(const std::string& hexString) 
{
	T result;

	// Remove "0x" prefix if present
	std::string str = hexString;
	if (str.substr(0, 2) == "0x") 
	{
		str = str.substr(2);
	}

	// Use stringstream to convert hex string to the desired type
	std::stringstream ss;
	ss << std::hex << str;
	ss >> result;

	return result;
}

template< typename T >
std::string IntToHexString(T i)
{
	std::stringstream stream;
	stream << "0x"
		<< std::setfill('0') << std::setw(sizeof(T) * 2)
		<< std::hex << i;
	return stream.str();
}

struct CharToWChar
{
	~CharToWChar()
	{
		delete[] pWCharBuffer;

		pWCharBuffer = nullptr;
	}

	CharToWChar(const char* pInData)
	{
		const size_t srcLen = strlen(pInData);
		size_t destSize = 0;
		mbstowcs_s(&destSize, nullptr, 0, pInData, srcLen);

		size_t convertedChars = 0;;
		pWCharBuffer = new wchar_t[destSize];
		mbstowcs_s(&convertedChars, pWCharBuffer, destSize, pInData, srcLen);
	}

	wchar_t* pWCharBuffer = nullptr;
};