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
	typedef map<int, string> LinesOfText;
	typedef unordered_map<SourceFileName, LinesOfText> FileDataMap;
	FileDataMap fileDataMap;

	printf("--Populating map--\n");
	const string Quote("\"");
	const string UntranslatedText("Untranslated");
	const string TxtExtension(".txt");
	const string CompletedFilesDirectory = OutputDirectory + Seperators + "CompletedFiles\\";
	CreateDirectoryHelper(CompletedFilesDirectory);

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
		string translatedText = sourceEntry.mFullLine.substr(position2 + 1);

		if(translatedText.size() > 2)
		{
			if (translatedText.front() == '\"' && translatedText.back() == '\"') 
			{
				translatedText = translatedText.substr(1, translatedText.length() - 2);
			}
		}

		if(prevFileName != fileName)
		{
			prevTextEntryNum = 1;
			prevFileName = fileName;
		}

		if(fileDataMap.find(fileName) == fileDataMap.end())
		{
			printf("%s\n", fileName.c_str());
		}

		fileDataMap[fileName][textEntryNum] = translatedText;
		++lineNum;
		prevTextEntryNum = textEntryNum;
	}

	//Now write out the files
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

		//Write out lines
		int expectedLine = 1;
		bool bIsFileCompleted = true;
		for(LinesOfText::iterator linesIter = iter->second.begin(); linesIter != iter->second.end(); ++linesIter)
		{
			//Write out "Untranslated" for any lines that were not translated
			for( int missingIndex = expectedLine; missingIndex < linesIter->first; ++missingIndex )
			{
				outFile.WriteStringWithNewLine(UntranslatedText);
				bIsFileCompleted = false;
			}

			const string& translation = linesIter->second;
			outFile.WriteStringWithNewLine(translation);

			expectedLine = linesIter->first + 1;
		}

		//Write out all completed files
		if(bIsFileCompleted)
		{
			const string destFile = CompletedFilesDirectory + iter->first + TxtExtension;
			TextFileWriter outFile;
			if( outFile.OpenFileForWrite(destFile) )
			{
				for( LinesOfText::iterator linesIter = iter->second.begin(); linesIter != iter->second.end(); ++linesIter )
				{
					const string& translation = linesIter->second;
					outFile.WriteStringWithNewLine(translation);
				}
			}
		}
	}
}
