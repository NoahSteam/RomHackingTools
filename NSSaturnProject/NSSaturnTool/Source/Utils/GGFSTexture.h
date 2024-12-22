#pragma once

///////////////////////////
//     GGFSImageData     //
///////////////////////////
class GGFSImageData
{
public:
	~GGFSImageData();
	bool CreateImage(const char* pInFilePath);

	const unsigned int* GetImageData() const {return mpImageData;}
	unsigned int GetImageSize() const {return mImageSize;}
	unsigned int GetWidth() const {return mWidth;}
	unsigned int GetHeight() const {return mHeight;}

private:
	unsigned int* mpImageData{nullptr};
	unsigned int* mpModifiedImageData{nullptr};
	unsigned int mImageSize{0};
	unsigned int mWidth{0};
	unsigned int mHeight{0};
};
