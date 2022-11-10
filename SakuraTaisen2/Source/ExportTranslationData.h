#pragma once

void ExportTranslationData(const string& InSourceFilePath, const string& OutputDirectory)
{
	CreateDirectoryHelper(OutputDirectory);

	printf("--Reading in Source--\n");

	FileNameContainer fileNameContainer(InSourceFilePath);
	TextFileData sourceData(fileNameContainer);
	if(!sourceData.InitializeTextFile())
	{
		printf("Unable to export\n");
		return;
	}

	typedef string SourceFileName;
	typedef vector<string> LinesOfText;
	typedef unordered_map<SourceFileName, LinesOfText> FileDataMap;
	FileDataMap fileDataMap;

	printf("--Populting map--\n");
	const string Quote("\"");
	const string UntranslatedText("Untranslated");

	//Populate map
	int lineNum = 0;
	int prevTextEntryNum = 1;
	SourceFileName prevFileName;
	for(const TextFileData::TextLine& sourceEntry : sourceData.mLines)
	{
		size_t position1 = sourceEntry.mFullLine.find_first_of(",");
		size_t position2 = sourceEntry.mFullLine.find_first_of(",", position1 + 1);
		const SourceFileName fileName = sourceEntry.mFullLine.substr(0, position1);
		const int textEntryNum = atoi(sourceEntry.mFullLine.substr(position1 + 1, position2).c_str());
		const string translatedText = sourceEntry.mFullLine.substr(position2 + 1);

		if(prevFileName != fileName)
		{
			prevTextEntryNum = 1;
			prevFileName = fileName;
		}

		if(fileDataMap.find(fileName) == fileDataMap.end())
		{
			printf("%s\n", fileName.c_str());
		}

		size_t numQuotes = 0;
		size_t quoteLoc = translatedText.find(Quote, 0);
		while(quoteLoc != std::string::npos)
		{
			++numQuotes;
			quoteLoc = translatedText.find(Quote, quoteLoc + 1);
		}

		if(numQuotes%2 != 0)
		{
			printf("Line with a missing quote: %i: %s\n", lineNum, translatedText.c_str());
		}

		for(int k = prevTextEntryNum + 1; k < textEntryNum; ++k)
		{
			fileDataMap[fileName].push_back(UntranslatedText);
		}

		fileDataMap[fileName].push_back(translatedText);
		++lineNum;
		prevTextEntryNum = textEntryNum;
	}

	const string txtExt(".txt");
	for(FileDataMap::iterator iter = fileDataMap.begin(); iter != fileDataMap.end(); ++iter)
	{
		TextFileWriter outFile;
		const string outFileName = OutputDirectory + iter->first + txtExt;
		if(!outFile.OpenFileForWrite(outFileName))
		{
			continue;
		}

		printf("Writing file: %s\n", iter->first.c_str());

		for(string& translation : iter->second)
		{
			outFile.WriteStringWithNewLine(translation);
		}
	}
}
