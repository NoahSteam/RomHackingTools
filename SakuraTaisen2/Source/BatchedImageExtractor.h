#pragma once

void BatchExtractImagesFromFile(const char* pInFileName, const char* pInPaletteFileName, const string& outDirectory)
{
	FileNameContainer fileNameContainer(pInFileName);
	TextFileData fileReader(fileNameContainer);
	if( !fileReader.InitializeTextFile(false))
	{
		return;
	}

	CreateDirectoryHelper(outDirectory);

	const FileNameContainer paletteFile(pInPaletteFileName);
	FileData paletteFileData;
	if (!paletteFileData.InitializeFileData(paletteFile))
	{
		printf("Unable to load palette file\n");
		return;
	}

	PaletteData paletteData;
	if (!MicrosoftPaletteData::CreatePaletteData(paletteFileData, paletteData))
	{
		return;
	}

	const string inputDirectory = fileNameContainer.mPathOnly + Seperators;
	const string imageExtension(".png");
	const int NumFields = 3;
	for(const TextFileData::TextLine& entry : fileReader.mLines)
	{
		if (entry.mWords.size() != NumFields)
		{
			continue;
		}
		
		const FileNameContainer colorFile(inputDirectory + entry.mWords[0]);
		const int width  = atoi(entry.mWords[1].c_str());
		const int height = atoi(entry.mWords[2].c_str());

		const string outFileName = outDirectory + colorFile.mNoExtension + imageExtension;
		ExtractImage(colorFile, outFileName, paletteFileData, width, height, 1, true, 256, 0, true, false, &paletteData);
	}
}
