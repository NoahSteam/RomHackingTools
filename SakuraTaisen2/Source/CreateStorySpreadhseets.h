#pragma once

bool CreateStoryTextSpreadsheets(const string& dialogImageDirectory, const string& duplicatesFileName, const string& sakura1Directory, int discNumber, const string& translatedTextDirectory, bool bForRelease)
{
	printf("Parsing duplicates file\n");

	//Load duplicate info
	FileNameContainer dupFileNameContainer(duplicatesFileName.c_str());
	TextFileData duplicatesFile(dupFileNameContainer);
	if (!duplicatesFile.InitializeTextFile())
	{
		printf("Unable to open duplicates file.\n");
		return false;
	}

	map<unsigned long, vector<string>> dupCrcMap; //crc, fileNames
	map<string, unsigned long> dupFilenameCrcMap; //fileName, crc
	const size_t numDupLines = duplicatesFile.mLines.size();
	for (size_t i = 0; i < numDupLines; ++i)
	{
		if (duplicatesFile.mLines[i].mWords.size() != 2)
		{
			printf("Invalid duplicate file format");
			return false;
		}

		const unsigned long crc = strtoul(duplicatesFile.mLines[i].mWords[1].c_str(), nullptr, 0);
		dupCrcMap[crc].push_back(duplicatesFile.mLines[i].mWords[0]);
		dupFilenameCrcMap[duplicatesFile.mLines[i].mWords[0]] = crc;
	}

	vector<FileNameContainer> allFilesInImageDirectory;
	FindAllFilesWithinDirectory(dialogImageDirectory, allFilesInImageDirectory);

	vector<FileNameContainer> imageFiles;
	GetAllFilesOfType(allFilesInImageDirectory, ".png", imageFiles);

	vector<FileNameContainer> allSakura1Files;
	FindAllFilesWithinDirectory(sakura1Directory, allSakura1Files);

	//Find all dialog files
	vector<FileNameContainer> scenarioFiles;
	GetAllFilesOfType(allSakura1Files, "SK0", scenarioFiles);
	GetAllFilesOfType(allSakura1Files, "SK1", scenarioFiles);
	GetAllFilesOfType(allSakura1Files, "SKC", scenarioFiles);

	//Parse the TBL files
	vector<SakuraTextFile> sakuraTextFiles;
	FindAllSakuraText(scenarioFiles, sakuraTextFiles);

	vector<FileNameContainer> translatedTextFiles;
	FindAllFilesWithinDirectory(translatedTextDirectory, translatedTextFiles);

	//Create map of table file name to the file (0100TBL => 0100TBL file)
	map<string, const SakuraTextFile*> sakuraTableFileMap;
	for (const SakuraTextFile& sakuraFile : sakuraTextFiles)
	{
		sakuraTableFileMap[sakuraFile.mFileNameInfo.mNoExtension] = &sakuraFile;
	}

	//Seperate all files into their own directories
	map<string, vector<FileNameContainer>> directoryMap;
	for (const FileNameContainer& imageFileNameInfo : imageFiles)
	{
		if (imageFileNameInfo.mPathOnly.find_first_of("SK") != string::npos)
		{
			const size_t lastOf = imageFileNameInfo.mPathOnly.find_last_of('\\');
			const string leaf = imageFileNameInfo.mPathOnly.substr(lastOf + 1);
			if (lastOf != string::npos && leaf.size() > 0)
			{
				directoryMap[leaf].push_back(imageFileNameInfo);
			}
		}
	}

	const string htmlHeader1("<html>\n<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}");
	const string htmlHeader2("#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;}</style>");
	const string htmlHeader = htmlHeader1 + htmlHeader2;

	//Create html files for each directory
	static char buffer[2048];
	const string outputDirectory = dialogImageDirectory + string("..\\..\\Translation\\");
	if (!CreateDirectoryHelper(outputDirectory))
	{
		printf("Unable to create translation directory %s.  Error: (%d)\n", outputDirectory.c_str(), GetLastError());
		return false;
	}

	//Sort bmp files from smallest to greatest (001.bmp -> 999.bmp)
	for (map<string, vector<FileNameContainer>>::iterator iter = directoryMap.begin(); iter != directoryMap.end(); ++iter)
	{
		std::sort(iter->second.begin(), iter->second.end());
	}

	//Create html files
	for (map<string, vector<FileNameContainer>>::const_iterator directoryMapIter = directoryMap.begin(); directoryMapIter != directoryMap.end(); ++directoryMapIter)
	{
		printf("Creating html file for %s\n", directoryMapIter->first.c_str());

		//Find sakura file for this dialog
		const string tblFileName = directoryMapIter->first;
		map<string, const SakuraTextFile*>::const_iterator sakuraFileIter = sakuraTableFileMap.find(tblFileName);
		if (sakuraFileIter == sakuraTableFileMap.end())
		{
			printf("Unable to find corresponding sakura file for: %s\n", tblFileName.c_str());
			continue;
		}

		if (directoryMapIter->second.size() != sakuraFileIter->second->mStringInfoArray.size())
		{
			printf("Unable to create html file for %s.  StringInfo to dialog file count mismatch.\n", tblFileName.c_str());
			continue;
		}

		//Store all crcs based on FileNameContainer*
		const size_t numImageFiles = directoryMapIter->second.size();
		map<const FileNameContainer*, unsigned long> crcMap;
		for (size_t fileIndex = 0; fileIndex < numImageFiles; ++fileIndex)
		{
			const FileNameContainer* pFileContainer = &directoryMapIter->second[fileIndex];
			FileData fileData;
			if (!fileData.InitializeFileData(*pFileContainer))
			{
				printf("Unable to open file for crc generation");
				break;
			}

			crcMap[pFileContainer] = fileData.GetCRC();
		}

		//Find all duplicates within this file. That is, duplicates that are internal to only this file.
		printf("Finding Duplicates for %s \n", directoryMapIter->first.c_str());
		int numberOfDuplicatesFound = 0;
		map<string, vector<string>> duplicatesMap; //FileName, Vector<Duplicate FileNames>
		for (size_t fileIndex = 0; fileIndex < numImageFiles; ++fileIndex)
		{
			const FileNameContainer& firstImageName = directoryMapIter->second[fileIndex];

			if (crcMap.find(&firstImageName) == crcMap.end())
			{
				printf("Crc data not found.  Unable to generate web files.\n");
				return false;
			}

			const unsigned long imageCrc = crcMap[&firstImageName];

			//See if there is a duplicate in any file
			map<unsigned long, vector<string>>::iterator dupCrcMapIter = dupCrcMap.find(imageCrc);
			if (dupCrcMapIter != dupCrcMap.end())
			{
				const size_t numCrcMatchesForFile = dupCrcMapIter->second.size();
				if (numCrcMatchesForFile == 0)
				{
					printf("No crc matches found for file.  Something went wrong.\n");
					return false;
				}
				numberOfDuplicatesFound++;
			}

			//Make sure we haven't already found a match for this file
			bool bExistingDupFound = false;
			for (map<string, vector<string>>::const_iterator mapIter = duplicatesMap.begin(); mapIter != duplicatesMap.end(); ++mapIter)
			{
				const size_t numDups = mapIter->second.size();
				for (size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
				{
					if (mapIter->second[dupIndex] == firstImageName.mNoExtension)
					{
						bExistingDupFound = true;
						break;
					}
				}

				if (bExistingDupFound)
				{
					break;
				}
			}

			if (bExistingDupFound)
			{
				continue;
			}

			//Check all other files
			for (size_t secondFileIndex = fileIndex + 1; secondFileIndex < numImageFiles; ++secondFileIndex)
			{
				const FileNameContainer* pSecondFile = &directoryMapIter->second[secondFileIndex];
				if (crcMap.find(pSecondFile) == crcMap.end())
				{
					printf("Crc data not found.  Unable to generate web files.\n");
					return false;
				}

				if (crcMap[pSecondFile] == imageCrc)
				{
					duplicatesMap[firstImageName.mNoExtension].push_back(directoryMapIter->second[secondFileIndex].mNoExtension);
				}
			}
		}

		const string htmlFileName = outputDirectory + directoryMapIter->first + string(".php");

		TextFileWriter htmlFile;
		if (!htmlFile.OpenFileForWrite(htmlFileName))
		{
			printf("Unable to create an html file: %s", htmlFileName.c_str());
			continue;
		}

		//Common header stuff
		htmlFile.WriteString(htmlHeader);

		if (!bForRelease)
		{
			fprintf(htmlFile.GetFileHandle(), "\n<div id=\"FileName\" style=\"display: none;\">%s</div>\n", tblFileName.c_str());
			fprintf(htmlFile.GetFileHandle(), "\n<div id=\"LastImageIndex\" style=\"display: none;\">%zi</div>\n", numImageFiles);
			fprintf(htmlFile.GetFileHandle(), "\n<div id=\"NumberOfDuplicates\" style=\"display: none;\">%i</div>\n", numberOfDuplicatesFound);

			htmlFile.WriteString("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">\n\n");

			//Load Data on startup
			htmlFile.WriteString("\t$( window ).on( \"load\", function()\n {\n\t\tOnStartup();\n});\n\n");
			htmlFile.WriteString("</script>\n\n");

			//Begin functions
			htmlFile.WriteString("<script type=\"text/javascript\">\n");

			//SaveDuplicateData function
			htmlFile.WriteString("function SaveDuplicateData(inDialogImageName, inEnglish, inDivID, inCRC)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
			htmlFile.WriteString("     	$.ajax({\n");
			htmlFile.WriteString("          type: \"POST\",\n");
			htmlFile.WriteString("          url: \"UpdateTranslationTest.php\",\n");
			htmlFile.WriteString("          data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:inEnglish, inDivId:inDivID, inCrc:inCRC },\n");
			htmlFile.WriteString("          success: function(result)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               var trId = \"tr_\" + inDivID;\n");
			htmlFile.WriteString("               if( inEnglish != \"Untranslated\" && inEnglish != \"<div>Untranslated</div>\")\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
			htmlFile.WriteString("                    {\n");
			htmlFile.WriteString("                         document.getElementById(trId).bgColor = \"#e3fec8\";\n");
			htmlFile.WriteString("                    }\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("               else\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    document.getElementById(trId).bgColor = \"#fefec8\";\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("          }\n");
			htmlFile.WriteString("     });\n");
			htmlFile.WriteString("}\n\n");

			//SaveData function
			htmlFile.WriteString("function SaveData(inDialogImageName, inDivID, inCRC)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     var translatedText = document.getElementById(inDivID).value;\n");
			htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
			htmlFile.WriteString("     $.ajax({\n");
			htmlFile.WriteString("          type: \"POST\",\n");
			htmlFile.WriteString("          url: \"UpdateTranslationTest.php\",\n");
			htmlFile.WriteString("          data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:translatedText, inDivId:inDivID, inCrc:inCRC },\n");
			htmlFile.WriteString("          success: function(result)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("                var trId = \"tr_\" + inDivID;\n");
			htmlFile.WriteString("                if( translatedText != \"Untranslated\" && translatedText != \"<div>Untranslated</div>\")\n");
			htmlFile.WriteString("                {\n");
			htmlFile.WriteString("                     if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
			htmlFile.WriteString("                     {\n");
			htmlFile.WriteString("                          document.getElementById(trId).bgColor = \"#e3fec8\";\n");
			htmlFile.WriteString("                     }\n");
			htmlFile.WriteString("                }\n");
			htmlFile.WriteString("                else\n");
			htmlFile.WriteString("                {\n");
			htmlFile.WriteString("                     document.getElementById(trId).bgColor = \"#fefec8\";\n");
			htmlFile.WriteString("                }\n");
			htmlFile.WriteString("         }\n");
			htmlFile.WriteString("    });\n");
			htmlFile.WriteString("}\n");

			//SaveEdits function
			htmlFile.WriteString("function SaveEdits(inDialogImageName, inDivID, inCRC)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     SaveData(inDialogImageName, inDivID, inCRC);\n\n");
			htmlFile.WriteString("     var translatedText = document.getElementById(inDivID).value;\n");

			//Print out array containing all available duplicate
			if (duplicatesMap.size())
			{
				htmlFile.WriteString("     var allDups = [\n");
			}
			else
			{
				htmlFile.WriteString("     var allDups = [];\n");
			}

			bool firstDupPrinted = false;
			for (map<string, vector<string>>::const_iterator mapIter = duplicatesMap.begin(); mapIter != duplicatesMap.end(); ++mapIter)
			{
				if (firstDupPrinted)
				{
					fprintf(htmlFile.GetFileHandle(), ",\n");
				}

				fprintf(htmlFile.GetFileHandle(), "                    \"edit_%s\"", mapIter->first.c_str());

				//Print dups of this entry (contained in mapIter->first)
				const size_t numDups = mapIter->second.size();
				for (size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
				{
					fprintf(htmlFile.GetFileHandle(), ", \"edit_%s\"", mapIter->second[dupIndex].c_str());
				}

				firstDupPrinted = true;

			}
			if (duplicatesMap.size())
			{
				htmlFile.WriteString("];\n\n");
			}

			for (map<string, vector<string>>::const_iterator mapIter = duplicatesMap.begin(); mapIter != duplicatesMap.end(); ++mapIter)
			{
				fprintf(htmlFile.GetFileHandle(), "     var edit_%s_duplicates = [", mapIter->first.c_str());

				const size_t numDups = mapIter->second.size();
				for (size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
				{
					fprintf(htmlFile.GetFileHandle(), "\"%s\"", mapIter->second[dupIndex].c_str());
					if (dupIndex + 1 < numDups)
					{
						fprintf(htmlFile.GetFileHandle(), ",");
					}
				}
				fprintf(htmlFile.GetFileHandle(), "];\n");

				//Now print vars for all of the dups
				for (size_t dupIndex = 0; dupIndex < numDups; ++dupIndex)
				{
					fprintf(htmlFile.GetFileHandle(), "     var edit_%s_duplicates = [\"%s\"", mapIter->second[dupIndex].c_str(), mapIter->first.c_str());

					if (numDups > 1)
					{
						fprintf(htmlFile.GetFileHandle(), ",");
					}

					size_t numDupsPrinted = 0;
					for (size_t dupIndex2 = 0; dupIndex2 < numDups; ++dupIndex2)
					{
						if (dupIndex == dupIndex2)
						{
							continue;
						}

						fprintf(htmlFile.GetFileHandle(), "\"%s\"", mapIter->second[dupIndex2].c_str());
						if (numDupsPrinted + 1 < numDups - 1)
						{
							fprintf(htmlFile.GetFileHandle(), ",");
						}

						++numDupsPrinted;
					}
					fprintf(htmlFile.GetFileHandle(), "];\n");
				}
			}

			htmlFile.WriteString("     var dynName = inDivID + \"_duplicates\";\n\n");
			htmlFile.WriteString("     if( allDups.includes(inDivID) )\n");
			htmlFile.WriteString("     {\n");
			htmlFile.WriteString("          var dupArray = eval(dynName);\n");
			htmlFile.WriteString("          for(var i = 0; i < dupArray.length; i++)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               var lookupName = \"#edit_\" + dupArray[i];\n");
			htmlFile.WriteString("               $(lookupName).html(translatedText)\n");
			htmlFile.WriteString("               var dupImageName = dupArray[i] + \".bmp\";\n");
			htmlFile.WriteString("               var dupDivID     = \"edit_\" + dupArray[i];\n\n");
			htmlFile.WriteString("               SaveData(dupImageName, dupDivID, 0)\n");
			htmlFile.WriteString("          }\n");
			htmlFile.WriteString("     }\n");
			htmlFile.WriteString("}\n");

			//Export data function
			htmlFile.WriteString("function ExportData()\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     $(\"textarea\").each ( function ()\n");
			htmlFile.WriteString("     {\n");
			htmlFile.WriteString("          var thisText = $(this).text();\n");
			htmlFile.WriteString("          thisText = thisText.replace(/<br>/g, '&ltbr&gt');\n");
			htmlFile.WriteString("          thisText = thisText.replace(/<sp>/g, '&ltsp&gt');\n");
			htmlFile.WriteString("          document.write(thisText + \"<br>\");\n");
			htmlFile.WriteString("     });\n");
			htmlFile.WriteString("}\n");

			//UpdateLoadingBar function
			htmlFile.WriteString("function UpdateLoadingBar(inLoadPercentage)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     var elem            = document.getElementById(\"myBar\");\n");
			htmlFile.WriteString("     var loadPercentElem = document.getElementById(\"LoadPercent\");\n");
			htmlFile.WriteString("     inLoadPercentage    = Math.floor( inLoadPercentage*100 + 0.5);\n");
			htmlFile.WriteString("     elem.style.width = inLoadPercentage + '%';\n");
			htmlFile.WriteString("     loadPercentElem.innerHTML = (inLoadPercentage).toString() + \"%\";\n");
			htmlFile.WriteString("}\n");

			//AttemptToLoadDuplicateData function
			htmlFile.WriteString("function AttemptToLoadDuplicateData(inDivID, inCRC, inTrID, inPercentComplete)\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("     $.ajax({\n");
			htmlFile.WriteString("     type: \"POST\",\n");
			htmlFile.WriteString("     url: \"GetTranslationFromCRC.php\",\n");
			htmlFile.WriteString("     data: { inCrc: inCRC},\n");
			htmlFile.WriteString("     success: function(result)\n");
			htmlFile.WriteString("     {\n");
			htmlFile.WriteString("          UpdateLoadingBar(inPercentComplete);\n\n");
			htmlFile.WriteString("          var json = $.parseJSON(result);\n");
			htmlFile.WriteString("          var i;\n");
			htmlFile.WriteString("          for (i = 0; i < json.length; i++)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               var jsonEntry = json[i];\n");
			htmlFile.WriteString("               var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
			htmlFile.WriteString("               if( english != \"Untranslated\" )\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    if( document.getElementById(inTrID).bgColor != \"#fec8c8\" )\n");
			htmlFile.WriteString("                    {\n");
			htmlFile.WriteString("                         document.getElementById(inTrID).bgColor = \"#e3fec8\";\n");
			htmlFile.WriteString("                    }\n");
			htmlFile.WriteString("                    $(inDivID).html(english);\n");
			htmlFile.WriteString("                    var divID = inDivID.replace(\"#\", \"\");\n");
			htmlFile.WriteString("                    var imageName = divID.replace(\"edit_\", \"\") + \".bmp\";\n");
			htmlFile.WriteString("                    SaveDuplicateData(imageName, english, divID, inCRC);\n");
			htmlFile.WriteString("                    return;\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("          }\n");
			htmlFile.WriteString("     },\n");
			htmlFile.WriteString("     error: function()\n");
			htmlFile.WriteString("     {\n");
			htmlFile.WriteString("     }\n");
			htmlFile.WriteString("   });\n");
			htmlFile.WriteString("}\n");

			//LoadData function
			htmlFile.WriteString("function LoadData(){\n");
			htmlFile.WriteString("     var fileName = document.getElementById(\"FileName\").innerHTML;\n");
			htmlFile.WriteString("     $.ajax({\n");
			htmlFile.WriteString("          type: \"POST\",\n");
			htmlFile.WriteString("          url: \"GetTranslationData.php\",\n");
			htmlFile.WriteString("          data: { inTBLFileName: fileName},\n");
			htmlFile.WriteString("          success: function(result)\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               var numDupsPendingLoad = document.getElementById(\"NumberOfDuplicates\").innerHTML;\n");
			htmlFile.WriteString("               var json = $.parseJSON(result);\n");
			htmlFile.WriteString("               var i;\n");
			htmlFile.WriteString("               for (i = 0; i < json.length; i++)\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    var jsonEntry = json[i];\n");
			htmlFile.WriteString("                    var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
			htmlFile.WriteString("                    var divId     = \"#\" + jsonEntry.DivId;\n");
			htmlFile.WriteString("                    var trId      = \"tr_\" + jsonEntry.DivId;\n");
			htmlFile.WriteString("                    if( english != \"Untranslated\" && english != \"<div>Untranslated</div>\")\n");
			htmlFile.WriteString("                    {\n");
			htmlFile.WriteString("                         if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
			htmlFile.WriteString("                         {\n");
			htmlFile.WriteString("                              document.getElementById(trId).bgColor = \"#e3fec8\";\n");
			htmlFile.WriteString("                         }  \n");
			htmlFile.WriteString("                         $(divId).html(english);\n\n");
			htmlFile.WriteString("                         var idNum     = jsonEntry.DivId.replace(\"edit_\", \"\");\n");
			htmlFile.WriteString("                         var dupId     = \"dup_\" + idNum;\n");
			htmlFile.WriteString("                         var dupValue  = document.getElementById(dupId).innerHTML;\n");
			htmlFile.WriteString("                         if( dupValue == \"true\" )\n");
			htmlFile.WriteString("                              numDupsPendingLoad--;\n");
			htmlFile.WriteString("                    }\n");
			htmlFile.WriteString("               }\n\n");
			htmlFile.WriteString("               ////Load duplicates\n");
			htmlFile.WriteString("               if( numDupsPendingLoad <= 0 )\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    UpdateLoadingBar(1);\n\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("               else\n");
			htmlFile.WriteString("               {\n");
			htmlFile.WriteString("                    var lastImageIndex  = document.getElementById(\"LastImageIndex\").innerHTML;\n");
			htmlFile.WriteString("                    var numDupProcessed = 0;\n");
			htmlFile.WriteString("                    for(i = 1; i < lastImageIndex; ++i)\n");
			htmlFile.WriteString("                    {\n");
			htmlFile.WriteString("                         var dupId    = \"dup_\" + i;\n");
			htmlFile.WriteString("                         var dupValue = document.getElementById(dupId).innerHTML;\n");
			htmlFile.WriteString("                         if( dupValue == \"false\" )\n");
			htmlFile.WriteString("                         {\n");
			htmlFile.WriteString("                              continue;\n");
			htmlFile.WriteString("                         }\n");
			htmlFile.WriteString("                         var divId    = \"#edit_\" + i;\n");
			htmlFile.WriteString("                         var trId     = \"tr_edit_\" + i;\n");
			htmlFile.WriteString("                         var crcId    = \"crc_\" + i;\n");
			htmlFile.WriteString("                         var crcValue = document.getElementById(crcId).innerHTML;\n");
			htmlFile.WriteString("                         var translatedText = document.getElementById(\"edit_\" + i).value;\n");
			htmlFile.WriteString("                         crcValue     = parseInt(crcValue, 16)\n");
			htmlFile.WriteString("                         if( translatedText == \"Untranslated\" )\n");
			htmlFile.WriteString("                         {\n");
			htmlFile.WriteString("                              var percentComplete = (numDupProcessed+1)/numDupsPendingLoad;\n");
			htmlFile.WriteString("                              numDupProcessed = numDupProcessed + 1;\n");
			htmlFile.WriteString("                              AttemptToLoadDuplicateData(divId, crcValue, trId, percentComplete);\n");
			htmlFile.WriteString("                         }\n");
			htmlFile.WriteString("                    }\n");
			htmlFile.WriteString("               }\n");
			htmlFile.WriteString("          },\n");
			htmlFile.WriteString("          error: function()\n");
			htmlFile.WriteString("          {\n");
			htmlFile.WriteString("               alert('Unable to load data');\n");
			htmlFile.WriteString("          }\n");
			htmlFile.WriteString("     });\n");
			htmlFile.WriteString("}\n");


			//FixOnChangeEditableElements - A function that saves the data whenever input happens
			htmlFile.WriteString("function FixOnChangeEditableElements()\n{\n");
			htmlFile.WriteString("\tvar tags = document.querySelectorAll('[contenteditable=true][onChange]');//(requires FF 3.1+, Safari 3.1+, IE8+)\n");
			htmlFile.WriteString("\tfor (var i=tags.length-1; i>=0; i--) if (typeof(tags[i].onblur)!='function')\n{\n");
			htmlFile.WriteString("\t\ttags[i].onfocus = function()\n{\n");
			htmlFile.WriteString("\t\tthis.data_orig=this.innerHTML;\n};\n");
			htmlFile.WriteString("\t\ttags[i].onblur = function()\n{\n");
			htmlFile.WriteString("\t\tif( this.innerHTML != this.data_orig)\n");
			htmlFile.WriteString("\t\t\tthis.onchange();\n");
			htmlFile.WriteString("\tdelete this.data_orig;\n};\n}\n}\n");

			//Startup function
			htmlFile.WriteString("function OnStartup()\n{\n");
			htmlFile.WriteString("\tFixOnChangeEditableElements();\n}\n");

			//End scripts
			htmlFile.WriteString("</script>\n\n");
			htmlFile.WriteString("</head>\n\n");

			//Call startup function
			htmlFile.WriteString("<body>\n");

			htmlFile.WriteString("<?php include 'GetUserPermissions.php';\n");
			htmlFile.WriteString("\t$bPermissionFound = false;\n");
			htmlFile.WriteString("\tforeach ($allowedFiles as $value)\n{");
			fprintf(htmlFile.GetFileHandle(), "\t\tif( $value == \"%s\" )\n\t\t{\n", tblFileName.c_str());
			htmlFile.WriteString("\t\t\t$bPermissionFound = true;\n");
			htmlFile.WriteString("\t\t\tbreak;\n\t\t}\n\t}");
			htmlFile.WriteString("if( $bPermissionFound || $masterUnlock )\n{\n?>");

			fprintf(htmlFile.GetFileHandle(), "<article><header align=\"center\"><h1>Dialog For %s</h1></header></article>\n", directoryMapIter->first.c_str());

			htmlFile.WriteString("<br>\n");
			htmlFile.WriteString("<b>Instructions:</b><br>\n");
			htmlFile.WriteString("-Please let me know on Discord once the file is complete.<br>\n");
			htmlFile.WriteString("-This page is best displayed using Chrome.  Otherwise some of the table borders are missing for some reason.<br>\n");
			htmlFile.WriteString("-Skip any row that is grayed out.<br>\n");
			htmlFile.WriteString("-Your changes are automatically saved.<br>\n");
			htmlFile.WriteString("-Press the Load Data button when you come back to the page to load your changes.<br>\n");
			htmlFile.WriteString("-Please wait for the Load Bar to complete.  It's a bit slow, but as more of the file is translated, it will speed up.  If it gets stuck in the 90's, that's fine, consider it done. I'll fix this bug soon.<br><br>\n");

			htmlFile.WriteString("<b>Style:</b><br>\n");
			htmlFile.WriteString("-Use only a single space after a period.<br>\n");
			htmlFile.WriteString("-You do not need to worry about line breaks.  Just translate the text as one line.  The text insertion tool will automatically add newlines as needed.<br>\n");
			htmlFile.WriteString("-There is a 120 character limit for each dialog entry.<br><br>\n\n");

			htmlFile.WriteString("<b>LIPS Events</b><br>\n");
			htmlFile.WriteString("-Pink rows are LIPS events where the user has to pick which line to say.<br><br>\n");
		
			htmlFile.WriteString("<b>Naming Conventions:</n><br>\n");
			htmlFile.WriteString("<a href=\"https://docs.google.com/spreadsheets/d/1imZZn_SfbmMxBpEnyj8_oZ1BEnMN0q0Ck1XYQgKggm4/edit?usp=sharing\" target=\"_blank\">Click here to view the naming conventions for Characters, Locations, and Terms</a> <br>\n");

			htmlFile.WriteString("<?php\n");
			htmlFile.WriteString("$currUser = $_SERVER['PHP_AUTH_USER'];\n");
			htmlFile.WriteString("if( $currUser == \"rahmed\" )\n");
			htmlFile.WriteString("{\n");
			htmlFile.WriteString("echo \"<input align=\\\"center\\\" type=\\\"button\\\" value=\\\"Export Data\\\" onclick=\\\"ExportData()\\\"/>\";\n");
			htmlFile.WriteString("}\n");
			htmlFile.WriteString("?>\n\n");

			//Load Data button
			htmlFile.WriteString("<table align=\"center\">\n");
			htmlFile.WriteString("     <tr>\n");
			htmlFile.WriteString("          <td>\n");
			htmlFile.WriteString("               <input align=\"center\" type=\"button\" value=\"Load Data\" onclick=\"LoadData()\"/>\n");
			htmlFile.WriteString("          </td>\n");
			htmlFile.WriteString("     </tr>\n");
			htmlFile.WriteString("     <tr>\n");
			htmlFile.WriteString("          <td>\n");
			htmlFile.WriteString("               Duplicate Load Progress\n");
			htmlFile.WriteString("          </td>\n");
			htmlFile.WriteString("          <td width=\"400\">\n");
			htmlFile.WriteString("               <div id=\"myProgress\">\n");
			htmlFile.WriteString("               <div id=\"myBar\"></div>\n");
			htmlFile.WriteString("          </div>\n");
			htmlFile.WriteString("          </td>\n");
			htmlFile.WriteString("          <td>\n");
			htmlFile.WriteString("               <div id=\"LoadPercent\">0</div>\n");
			htmlFile.WriteString("          </td>\n");
			htmlFile.WriteString("     </tr>\n");
			htmlFile.WriteString("</table><br>\n\n");

		}//if( !bForRelease )

		//Write table
		htmlFile.WriteString("<table>\n");
		htmlFile.WriteString("\t<tr bgcolor=\"#c8c8fe\">\n");
		htmlFile.WriteString("\t<th>#</th>\n");
		htmlFile.WriteString("\t<th>Speaker</th>\n");
		htmlFile.WriteString("\t<th>Japanese</th>\n");
		htmlFile.WriteString("\t<th>English</th>\n");
		htmlFile.WriteString("\t<th>Appearance Order</th>\n");
		htmlFile.WriteString("\t<th>CRC</th>\n");
		htmlFile.WriteString("\t<th>Has a Duplicate</th>\n");
		htmlFile.WriteString("\t</tr>\n");

		//****Find Translated Text File****
		//Search for the corresponding translated file name
		const FileNameContainer* pMatchingTranslatedFileName = nullptr;
		for (const FileNameContainer& translatedFileName : translatedTextFiles)
		{
			if (translatedFileName.mNoExtension.find(directoryMapIter->first) != string::npos)
			{
				pMatchingTranslatedFileName = &translatedFileName;
				break;
			}
		}

		//Open translated text file
		bool bCanUseTranslatedFile = pMatchingTranslatedFileName ? true : false;
		TextFileData translatedFile((pMatchingTranslatedFileName ? *pMatchingTranslatedFileName : ""));
		if (!translatedFile.InitializeTextFile(true, false))
		{
			//	printf("Unable to open the translation file %s.\n", pMatchingTranslatedFileName->mFullPath.c_str());
			bCanUseTranslatedFile = false;
		}

		//Make sure we have the correct amount of lines
		if (bCanUseTranslatedFile && directoryMapIter->second.size() < translatedFile.mLines.size())
		{
			//	printf("Unable to use tranlsted file: %s because the translation line count is incorrect.\n", pMatchingTranslatedFileName->mNoExtension.c_str());
			bCanUseTranslatedFile = false;
		}
		//****Done Finding Translated Text File****

		//Create a map that gives us an index into the dialog entries when given a number
		const SakuraTextFile* pSakuraFile = sakuraFileIter->second;
		map<size_t, size_t> dialogOrder_SeqIdToImageNum;
		map<size_t, size_t> dialogOrder_SeqIdToLips;
		const size_t numLinesOfDialog = pSakuraFile->mLines.size();
		const size_t numSequenceEntries = pSakuraFile->mSequenceEntries.size();
		for (size_t dialogEntry = 0; dialogEntry < numSequenceEntries; ++dialogEntry)
		{
			const uint16 textIndex = pSakuraFile->mSequenceEntries[dialogEntry].mTextIndex;
			dialogOrder_SeqIdToImageNum[textIndex] = dialogEntry;

			if (pSakuraFile->mSequenceEntries[dialogEntry].mbIsLips)
			{
				dialogOrder_SeqIdToLips[textIndex] = textIndex;
			}
		}

		//Create entries for all images
		int imageNumber = 0;
		for (const FileNameContainer& fileNameInfo : directoryMapIter->second)
		{
			const unsigned long crc = crcMap[&directoryMapIter->second[imageNumber]];
			const bool bIsDuplicate = dupCrcMap.find(crc) != dupCrcMap.end();
			const char* bgColor = bForRelease ? "e3fec8" : "fefec8";
			const bool bHasDialogEntry = dialogOrder_SeqIdToImageNum.find(imageNumber + 1) != dialogOrder_SeqIdToImageNum.end(); //Image ids in the game are 1 based, not 0 based
			const bool bIsLipsEntry = bHasDialogEntry ? dialogOrder_SeqIdToLips.find(imageNumber + 1) != dialogOrder_SeqIdToLips.end() : false;// pOrder ? dialogOrderIter->second.idAndLips.find(id)->second : false;

			if (bIsLipsEntry)
			{
				bgColor = "fec8c8"; //LIPS event are highlighted in pink
			}
			else if (!bHasDialogEntry)
			{
				bgColor = "B9B9B9"; //Gray out rows that don't need translations
			}

			const char* pVarSuffix = fileNameInfo.mNoExtension.c_str();
			fprintf(htmlFile.GetFileHandle(), "<tr id=\"tr_edit_%i\" bgcolor=\"#%s\">\n", imageNumber + 1, bgColor);
			snprintf(buffer, 2048, "<td align=\"center\" width=\"20\">%i</td>", imageNumber + 1);
			htmlFile.WriteString(string(buffer));

			string faceImageId = pSakuraFile->GetFaceImageId(imageNumber + 1);
			if (faceImageId.size())
			{
				snprintf(buffer, 2048, "<td width=\"48\"><img src=\"..\\ExtractedData\\FaceWin\\%s.png\"></td>", faceImageId.c_str());
			}
			else
			{
				snprintf(buffer, 2048, "<td width=\"48\"><img src=\"..\\ExtractedData\\Faces\\UnknownFace.png\"></td>");
			}
			htmlFile.WriteString(string(buffer));

			snprintf(buffer, 2048, "<td width=\"240\"><img src=\"..\\ExtractedData\\SakuraWars2\\Disc%i\\Cache0\\Text\\%s\\%s\"></td>", discNumber, pSakuraFile->mFileNameInfo.mNoExtension.c_str(), fileNameInfo.mFileName.c_str());
			htmlFile.WriteString(string(buffer));

			//snprintf(buffer, 2048, "<td width=480><div id=\"edit_%s\" contenteditable=\"true\" onChange=\"SaveEdits('%i.bmp', 'edit_%i')\">Untranslated</div></td>", pVarSuffix, num + 1, num + 1);
			const char* pEnglishText = bCanUseTranslatedFile ? translatedFile.mLines[imageNumber].mFullLine.c_str() : "Untranslated";
			snprintf(buffer, 2048, "<td width=\"480\"><textarea id=\"edit_%s\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i', '%lu')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">%s</textarea></td>", pVarSuffix, imageNumber + 1, imageNumber + 1, crc, pEnglishText);
			htmlFile.WriteString(string(buffer));

			if (bHasDialogEntry)
			{
				htmlFile.WriteString("<td align=\"center\" width=\"120\">");

				snprintf(buffer, 2048, "Order: %i", imageNumber + 1);
				htmlFile.WriteString(string(buffer));

				htmlFile.WriteString("</td>");
			}
			else
			{
				snprintf(buffer, 2048, "<td align=\"center\" width=\"120\">Order: -1</td>");
				htmlFile.WriteString(string(buffer));
			}

			snprintf(buffer, 2048, "<td id=\"crc_%i\" align=\"center\" width=\"20\">%08x</td>", imageNumber + 1, crc);
			htmlFile.WriteString(string(buffer));

			snprintf(buffer, 2048, "<td id=\"dup_%i\" align=\"center\" width=\"20\">%s</td>", imageNumber + 1, bIsDuplicate ? "true" : "false");
			htmlFile.WriteString(string(buffer));

			htmlFile.WriteString("</tr>\n");

			++imageNumber;
		}
		htmlFile.WriteString("</table><br>\n");

		htmlFile.WriteString("<?php\n}\n?>");

		//End file
		htmlFile.WriteString("</body>\n");
		htmlFile.WriteString("</html>\n");
	}

	return true;
}
