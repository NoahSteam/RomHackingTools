#pragma once

/*
Header Size : 0x50 or 0x80 bytes
@0x08: 4 byte value - num images
@0x0c: 4 byte value num bytes for Image Data Set Header
@0x10: 4 byte value - num images
@0x14: 4 byte value num bytes for Image Data Set Header
@0x34: 4 byte num colors in palette ?
@0x38: 4 byte offset to Unkown Header 1
@0x3C: 4 byte offset to yet another header
@0x40: 4 byte offset to header
@0x44: 4 byte offset to another header
@0x48: 4 byte offset to color data
@0x50: 0x30 more bytes of unknown data sometimes.Might be compressed data.To detect if present, check to see if palette is starting(first two bytes being 7FE0, otherwise skip 0x30 lines)

Palette is 512 bytes

Each Image Set Contains :
64x56 main image(0x650 bytes)
48x16 blinking images(2 of them 0x300 bytes)

Variable number of images per character(Count is found in @0x08 in the header)

Image Data Set Header :
Width: 2 byte value
Height : 2 byte value
Unkown : 12 bytes

Unnkown Header 1 :
	After the Image Data Set Header & Before Color Data
*/

//Used to extract FACEWIN.ALL file
class FaceWinAllExtractor
{
#pragma pack (push, 1)
	struct ImageSetHeader
	{
		uint32 mUnknown1;
		uint32 mUnknown2;
		uint32 mNumImages;
		uint32 mImageBlockHeaderSize;
		uint32 mNumImages2;
		uint32 mImageBlockHeaderSize2;
		char   mUnknown[48];
		uint32 mOffsetToColorData;
		uint32 mUnknown3;

		void ConvertEndianess()
		{
			mNumImages            = SwapByteOrder(mNumImages);
			mImageBlockHeaderSize = SwapByteOrder(mImageBlockHeaderSize);
			mOffsetToColorData    = SwapByteOrder(mOffsetToColorData);
		}
	};
#pragma pack (pop)

#pragma pack (push, 1)
	struct ImageEntryHeader
	{
		uint16 mWidth;
		uint16 mHeight;
		char   mUnknown[12];

		void ConvertEndianess()
		{
			mWidth  = SwapByteOrder(mWidth);
			mHeight = SwapByteOrder(mHeight);
		}
	};
#pragma pack (pop)
public:

	void ExtractFaceWinFiles(const string& inRootSakuraDirectory, const string& inOutputDirectory)
	{
		if (!CreateDirectoryHelper(inOutputDirectory))
		{
			return;
		}

		//Create static array of these so we don't have to keep dynamically allocating memory
		const int MaxImageHeaders = 50;
		ImageEntryHeader imageEntryHeaders[MaxImageHeaders];

		//Original Obstacle image 
		FileData faceFile;
		const string faceFilePath = inRootSakuraDirectory + "\\SAKURA1\\FACEWIN.ALL";
		if (!faceFile.InitializeFileData(FileNameContainer(faceFilePath.c_str())))
		{
			return;
		}

		const char* const pFileData = faceFile.GetData();
		uint32 dataIndex = 0;
		uint32 chararacterIndex = 0;
		while (dataIndex + sizeof(ImageSetHeader) < faceFile.GetDataSize())
		{
			printf("Extracting character %i at %08x\n", chararacterIndex, dataIndex);

			const uint32 characterDataStart = dataIndex;

			//Load header data
			ImageSetHeader imageSetHeader;
			memcpy_s(&imageSetHeader, sizeof(ImageSetHeader), pFileData + dataIndex, sizeof(ImageSetHeader));
			imageSetHeader.ConvertEndianess();

			//Advance index into file data
			dataIndex += sizeof(imageSetHeader);

			//Is the palette next or is it 0x30 bytes of unknown data which starts with 0?
			const char* pPeekAhead = (pFileData + dataIndex);
			//if (*pPeekAhead == 0)
			{
				//skip unknown bytes
				dataIndex += 0x30;
			}

			//Get pointer to palette
			const int PaletteSize = 512;
			const char* pPaletteData = pFileData + dataIndex;
			dataIndex += PaletteSize;

			//Copy image header
			const char* pImageEntryHeader = pFileData + dataIndex;
			memset(&imageEntryHeaders, 0, sizeof(imageEntryHeaders));
			memcpy_s(&imageEntryHeaders, sizeof(imageEntryHeaders), pImageEntryHeader, sizeof(ImageEntryHeader) * imageSetHeader.mNumImages);
			for (ImageEntryHeader& entry : imageEntryHeaders)
			{
				entry.ConvertEndianess();
			}

			//Get pointer to image data
			dataIndex = characterDataStart + imageSetHeader.mOffsetToColorData;

			//Output all full size images.  Ignore the lips images (48 x 16)
			int faceIndex = 0;
			for (uint8 imageIndex = 0; imageIndex < imageSetHeader.mNumImages; ++imageIndex)
			{
				const ImageEntryHeader& entry = imageEntryHeaders[imageIndex];
				const char* pImageData        = pFileData + dataIndex;
				dataIndex += entry.mWidth * entry.mHeight;

				if (entry.mWidth != 0x40)
				{
					continue;
				}

				const string baseImageName = inOutputDirectory + string("Char") + std::to_string(chararacterIndex) + string("_") + std::to_string(faceIndex);
				const string outFileName = baseImageName + string(".png");
				ExtractImageFromData(pImageData, entry.mWidth * entry.mHeight, outFileName, pPaletteData, PaletteSize, entry.mWidth, entry.mHeight, 1, 256, 0, false, false);

				++faceIndex;
			}

			++chararacterIndex;

			dataIndex = FindNextImageEntry(pFileData, dataIndex, faceFile.GetDataSize());
		}
	}

	uint32 FindNextImageEntry(const char* const pInFileData, uint32 inDataIndex, const uint32 inLastIndex)
	{
		while (inDataIndex < inLastIndex)
		{
			const uint32* pValue = (uint32*)(pInFileData + inDataIndex);
			
			if (SwapByteOrder(*pValue) == 0x00000100)
			{
				break;
			}

			inDataIndex += 4;
		}

		return inDataIndex;
	}
};
