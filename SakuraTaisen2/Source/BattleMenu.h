#pragma once

#pragma pack(push, 1)
struct BattleMenuImageInfo
{
	uint16 offsetDiv8;
	uint16 reserved1;
	uint8  widthDiv8;
	uint8  height;
	uint16 reserved2;
};
#pragma pack(pop)

void ExtractGameOverMenu(const string& rootSakuraDirectory, bool bBmp, const string& outDirectory)
{
	const string gameOverDir = outDirectory + "GameOver\\";
	CreateDirectoryHelper(gameOverDir);

	const char* filesToExtract[2] = { "GOVERKV1", "GOVERTV1" };
	for (int fileIndex = 0; fileIndex < 2; ++fileIndex)
	{
		const string dataFilePath = rootSakuraDirectory + string("SAKURA2\\") + string(filesToExtract[fileIndex]) + string(".BIN");
		FileNameContainer dataFileNameInfo(dataFilePath.c_str());

		FileData fileData;
		if (!fileData.InitializeFileData(dataFileNameInfo))
		{
			return;
		}

		struct ImageInfo
		{
			int width;
			int height;
		};

		const int numImages = 3;
		const ImageInfo images[numImages] =
		{
			320, 240,
			176, 80,
			176, 64
		};

		const string bmpExt = bBmp ? string(".bmp") : (".png");
		uint32 offset = 0x10000;
		for (int imageIndex = 0; imageIndex < numImages; ++imageIndex)
		{
			const string imageName = gameOverDir + std::string(filesToExtract[fileIndex]) + std::to_string(imageIndex) + bmpExt;
			Extract16BitImageFromData(imageName, fileData.GetData() + offset, images[imageIndex].width, images[imageIndex].height);

			offset += images[imageIndex].width * images[imageIndex].height * 2;
		}
	}
}

void ExtractBattleMenu(const string& rootSakuraDirectory, bool bBmp, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	ExtractGameOverMenu(rootSakuraDirectory, bBmp, outDirectory);

	const string sakuraFilePath = rootSakuraDirectory + string("SAKURA2\\ETCDATA.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return;
	}

	const string bmpExt = bBmp ? string(".bmp") : (".png");

	const char* filesToExtract[2] = { "GOVERKV1", "GOVERTV1" };

	//GOVERTV1
	for(int fileIndex = 0; fileIndex < 2; ++fileIndex)
	{
		const string dataFilePath = rootSakuraDirectory + string("SAKURA2\\") + string(filesToExtract[fileIndex]) + string(".BIN");
		FileNameContainer dataFileNameInfo(dataFilePath.c_str());

		FileData fileData;
		if (!fileData.InitializeFileData(dataFileNameInfo))
		{
			return;
		}

		const int paletteSize = 32;

		PaletteData paletteData1;
		if (!paletteData1.CreateFrom15BitData(fileData.GetData() + 0x41E00, paletteSize))
		{
			printf("Unable to create palette 1.\n");
			return;
		}

		PaletteData paletteData2;
		if (!paletteData2.CreateFrom15BitData(fileData.GetData() + 0x41E20, paletteSize))
		{
			printf("Unable to create palette 2.\n");
			return;
		}

		const string finalOutputDirectory = outDirectory + string(filesToExtract[fileIndex]) + string("\\");
		CreateDirectoryHelper(finalOutputDirectory);

		const int numEntries = 170;
		BattleMenuImageInfo lookupTable[numEntries];
		const unsigned int offsetToData = 0x000000A0;
		memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

		for (int i = 0; i < numEntries; ++i)
		{
			lookupTable[i].offsetDiv8 = SwapByteOrder(lookupTable[i].offsetDiv8) * 8;
			lookupTable[i].widthDiv8 *= 8;

			const string outFileName = finalOutputDirectory + std::to_string(i) + bmpExt;

			PaletteData& palette = i < 78 && !(i >= 33 && i <= 46) ? paletteData1 : paletteData2;
			BitmapWriter outBmp;
			outBmp.CreateBitmap(outFileName, lookupTable[i].widthDiv8, -lookupTable[i].height, 4, fileData.GetData() + lookupTable[i].offsetDiv8, (lookupTable[i].widthDiv8 * lookupTable[i].height) / 2, palette.GetData(), palette.GetSize(), bBmp);
		}
	}
}

bool PatchBattleGameOverScreens(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	struct FilesToPatch
	{
		const char* pName;
	};
	const FilesToPatch filesToPatch[] =
	{
		"GOVERKV1",
		"GOVERTV1",
	};

	for(int fileIndex = 0; fileIndex < 2; ++fileIndex)
	{
		const string translatedImagePath1 = inTranslatedDataDirectory + string("GOVER\\") + string(filesToPatch[fileIndex].pName) + "1.bmp";
		const string translatedImagePath2 = inTranslatedDataDirectory + string("GOVER\\") + string(filesToPatch[fileIndex].pName) + "2.bmp";

		RGBBmpToSaturnConverter patchedImageData1;
		if (!patchedImageData1.ConvertBmpToSakuraFormat(translatedImagePath1))
		{
			printf("PatchBattleMenu: Couldn't convert image: %s.\n", translatedImagePath1.c_str());
			return false;
		}

		if (patchedImageData1.GetImageWidth() != 176 && patchedImageData1.GetImageHeight() != 80)
		{
			printf("PatchBattleMenu: Image needs to be 176x80: %s.\n", translatedImagePath1.c_str());
			return false;
		}

		RGBBmpToSaturnConverter patchedImageData2;
		if (!patchedImageData2.ConvertBmpToSakuraFormat(translatedImagePath2))
		{
			printf("PatchBattleMenu: Couldn't convert image: %s.\n", translatedImagePath2.c_str());
			return false;
		}

		if (patchedImageData2.GetImageWidth() != 176 && patchedImageData2.GetImageHeight() != 64)
		{
			printf("PatchBattleMenu: Image needs to be 176x64: %s.\n", translatedImagePath2.c_str());
			return false;
		}

		for (int i = 0; i < 2; ++i)
		{
			const string sakuraFilePath = inPatchedSakuraDirectory + string("SAKURA2\\") + filesToPatch[i].pName + string(".bin");
			FileReadWriter sakuraFile;
			if (!sakuraFile.OpenFile(sakuraFilePath))
			{
				return false;
			}

			sakuraFile.WriteData(0x35800, patchedImageData1.GetImageData(), patchedImageData1.GetImageDataSize(), false);
			sakuraFile.WriteData(0x3c600, patchedImageData2.GetImageData(), patchedImageData2.GetImageDataSize(), false);
		}
	}

	return true;
}

bool PatchBattleNumberLocation(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	const string sakuraFilePath = inPatchedSakuraDirectory + string("SAKURA2\\ETCDATA.BIN");
	FileReadWriter etcFile;
	if(!etcFile.OpenFile(sakuraFilePath))
	{
		return false;
	}

	const int numEntries = 10;
	int fileOffset = 0x715; //0x21cf15 in memory
	const char xOffset = 0x9;

	for(int i = 0; i < numEntries; ++i)
	{
		etcFile.WriteData(fileOffset, &xOffset, sizeof(xOffset), false);
		fileOffset += 8;
	}

	return true;
}

struct FilesToPatch
{
	const char* pName;
	int offset;
};

const FilesToPatch GBattleMenuFilesDisc1[] =
{
	"GOVERKV1", 0,
	"GOVERTV1", 0,
	"M00VDP1", 0x17e40,
	"M01VDP1", 0x17e40,
	"M02VDP1", 0x17e40,
	"M03VDP1", 0x17e40,
	"M04VDP1", 0x17e40,
	"M05VDP1", 0x17e40,
	"M26VDP1", 0x17e40,
	"M27VDP1", 0x17e40,
	"M29VDP1", 0x17e40,
	"M31VDP1", 0x17e40,
	"M33VDP1", 0x17e40,
	"M34VDP1", 0x17e40
};

const FilesToPatch GBattleMenuFilesDisc2[] =
{
	"GOVERKV1", 0x00000000,
	"GOVERTV1", 0x00000000,
	"M07VDP1", 0x00017e40,
	"M08VDP1", 0x00017e40,
	"M09VDP1", 0x00017e40,
	"M10VDP1", 0x00017e40,
	"M11VDP1", 0x00017e40,
	"M12VDP1", 0x00017e40,
	"M13VDP1", 0x00017e40,
	"M15VDP1", 0x00017e40,
	"M16VDP1", 0x00017e40,
	"M17VDP1", 0x00017e40,
	"M18VDP1", 0x00017e40,
	"M36VDP1", 0x00017e40,
	"M37VDP1", 0x00017e40,
	"M49VDP1", 0x00017e40,
	"M50VDP1", 0x00017e40,
	"M51VDP1", 0x00017e40,
	"M52VDP1", 0x00017e40,
	"M53VDP1", 0x00017e40,
	"M54VDP1", 0x00017e40,
	"M91VDP1", 0x00017e40,
	"M92VDP1", 0x0000fa40,
};

const FilesToPatch GBattleMenuFilesDisc3[] =
{
	"GOVERKV1.BIN", 0x00000000,
	"GOVERTV1.BIN", 0x00000000,
	"M19VDP1.BIN",  0x00017e40,
	"M20VDP1.BIN",  0x00017e40,
	"M21VDP1.BIN",  0x00017e40,
	"M22VDP1.BIN",  0x00017e40,
	"M23VDP1.BIN",  0x00017e40,
	"M24VDP1.BIN",  0x00017e40,
	"M25VDP1.BIN",  0x00017e40,
	"M45VDP1.BIN",  0x00017e40,
	"M46VDP1.BIN",  0x00017e40,
	"M47VDP1.BIN",  0x00017e40,
};

bool PatchBattleMenu(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory, int inDiscNumber)
{
	printf("Patching Battle Menu\n");

	const string sakuraFilePath = inPatchedSakuraDirectory + string("SAKURA2\\ETCDATA.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return false;
	}

	int numFiles = 0;
	const FilesToPatch* filesToPatch = nullptr;	
	switch(inDiscNumber)
	{
		case 1:
		{
			filesToPatch = GBattleMenuFilesDisc1;
			numFiles = sizeof(GBattleMenuFilesDisc1) / sizeof(FilesToPatch);
		}break;

		case 2:
		{
			filesToPatch = GBattleMenuFilesDisc2;
			numFiles = sizeof(GBattleMenuFilesDisc2) / sizeof(FilesToPatch);
		}break;

		case 3:
		{
			filesToPatch = GBattleMenuFilesDisc3;
			numFiles = sizeof(GBattleMenuFilesDisc3) / sizeof(FilesToPatch);
		}break;

		default:
		{
			printf("Invalid disc number %i\n", inDiscNumber);
			return false;	
		}
	};

	const string patchedImageDirectory = inTranslatedDataDirectory + Seperators + "GOVER\\";
	for (int fileIndex = 0; fileIndex < numFiles; ++fileIndex)
	{
		const string dataFilePath = inPatchedSakuraDirectory + string("SAKURA2\\") + string(filesToPatch[fileIndex].pName) + string(".BIN");
		
		FileReadWriter fileData;
		if (!fileData.OpenFile(dataFilePath))
		{
			return false;
		}

		const string bmpExt(".bmp");
		const int numEntries = 170;
		BattleMenuImageInfo lookupTable[numEntries];
		const unsigned int offsetToData = 0x000000A0;
		memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

		for (int i = 0; i < numEntries; ++i)
		{
			lookupTable[i].offsetDiv8 = SwapByteOrder(lookupTable[i].offsetDiv8) * 8;
			lookupTable[i].widthDiv8 *= 8;

			const string translatedImagePath = patchedImageDirectory + std::to_string(i) + bmpExt;
			if (!DoesFileExist(translatedImagePath))
			{
				continue;
			}

			BmpToSaturnConverter patchedImageData;
			if (!patchedImageData.ConvertBmpToSakuraFormat(translatedImagePath, false))
			{
				printf("PatchBattleMenu: Couldn't convert image: %s.\n", translatedImagePath.c_str());
				return false;
			}

			if(lookupTable[i].widthDiv8 != patchedImageData.GetImageWidth())
			{
				printf("PatchBattleMenu: Expected width of %i, got %i for %s.\n", lookupTable[i].widthDiv8, patchedImageData.GetImageWidth(), translatedImagePath.c_str());
				return false;
			}

			if (lookupTable[i].height != patchedImageData.GetImageHeight())
			{
				printf("PatchBattleMenu: Expected height of %i, got %i for %s.\n", lookupTable[i].height, patchedImageData.GetImageHeight(), translatedImagePath.c_str());
				return false;
			}

			if(!fileData.WriteData(filesToPatch[fileIndex].offset + lookupTable[i].offsetDiv8, patchedImageData.GetImageData(), patchedImageData.GetImageDataSize(), false))
			{
				printf("PatchBattleMenu: Unable to write to file: %s.\n", translatedImagePath.c_str());
				return false;
			}
		}
	}

	if(!PatchBattleGameOverScreens(inPatchedSakuraDirectory, inTranslatedDataDirectory))
	{
		return false;
	}

	return PatchBattleNumberLocation(inPatchedSakuraDirectory, inTranslatedDataDirectory);
}

bool CreateNameGOVERSpreadsheet(const string& imageDirectory)
{
	TextFileWriter htmlFile;
	const string htmlFileName = imageDirectory + string("..\\..\\Translation\\GoverKV.php");
	if (!htmlFile.OpenFileForWrite(htmlFileName))
	{
		printf("Unable to create an html file: %s", htmlFileName.c_str());
		return false;
	}

	htmlFile.WriteString("<html>\n");
	htmlFile.WriteString("	<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;}</style>\n");
	htmlFile.WriteString("	<div id=\"FileName\" style=\"display: none;\">GoverKV</div>\n");
	htmlFile.WriteString("	<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">\n");
	htmlFile.WriteString("		$( window ).on( \"load\", function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			OnStartup();\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	</script>\n\n");
	htmlFile.WriteString("	<script type=\"text/javascript\">\n");
	htmlFile.WriteString("		function SaveData(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var translatedText = document.getElementById(inDivID).value;\n");
	htmlFile.WriteString("			var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("			$.ajax({\n");
	htmlFile.WriteString("				type: \"POST\",\n");
	htmlFile.WriteString("				url: \"UpdateTranslationTest.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName, inImageName:inDialogImageName, inTranslation:translatedText, inDivId:inDivID, inCrc:0 },\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					var trId = \"tr_\" + inDivID;\n");
	htmlFile.WriteString("					if( translatedText != \"Untranslated\" && translatedText != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("							document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("				else\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					document.getElementById(trId).bgColor = \"#fefec8\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("}\n");
	htmlFile.WriteString("		function SaveEdits(inDialogImageName, inDivID)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			SaveData(inDialogImageName, inDivID);\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		function ExportData()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$(\"textarea\").each ( function ()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var thisText = $(this).text();\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<br>/g, '&ltbr&gt');\n");
	htmlFile.WriteString("				thisText = thisText.replace(/<sp>/g, '&ltsp&gt');\n");
	htmlFile.WriteString("				document.write(thisText + \"<br>\");\n");
	htmlFile.WriteString("			});\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	function LoadData()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var fileName = document.getElementById(\"FileName\").innerHTML;\n");
	htmlFile.WriteString("		$.ajax({\n");
	htmlFile.WriteString("		type: \"POST\",\n");
	htmlFile.WriteString("			  url: \"GetTranslationData.php\",\n");
	htmlFile.WriteString("				data: { inTBLFileName: fileName},\n");
	htmlFile.WriteString("				success: function(result)\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			var json = $.parseJSON(result);\n");
	htmlFile.WriteString("			var i;\n");
	htmlFile.WriteString("			for (i = 0; i < json.length; i++)\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				var jsonEntry = json[i];\n");
	htmlFile.WriteString("				var english   = jsonEntry.English.replace(/\\\\/g, \'\');\n");
	htmlFile.WriteString("				var divId     = \"#\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					var trId      = \"tr_\" + jsonEntry.DivId;\n");
	htmlFile.WriteString("					if( english != \"Untranslated\" && english != \"<div>Untranslated</div>\")\n");
	htmlFile.WriteString("					{\n");
	htmlFile.WriteString("						if( document.getElementById(trId).bgColor != \"#fec8c8\" )\n");
	htmlFile.WriteString("						{\n");
	htmlFile.WriteString("						       document.getElementById(trId).bgColor = \"#e3fec8\";\n");
	htmlFile.WriteString("						}\n");
	htmlFile.WriteString("						$(divId).html(english);\n");
	htmlFile.WriteString("					}\n");
	htmlFile.WriteString("			}\n");
	htmlFile.WriteString("		},\n");
	htmlFile.WriteString("		error: function()\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			alert('Unable to load data');\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("		});\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function FixOnChangeEditableElements()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		var tags = document.querySelectorAll('[contenteditable=true][onChange]');\n");
	htmlFile.WriteString("		for (var i=tags.length-1; i>=0; i--) if (typeof(tags[i].onblur)!='function')\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			tags[i].onfocus = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				this.data_orig=this.innerHTML;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("			tags[i].onblur = function()\n");
	htmlFile.WriteString("			{\n");
	htmlFile.WriteString("				if( this.innerHTML != this.data_orig)\n");
	htmlFile.WriteString("					this.onchange();\n");
	htmlFile.WriteString("				delete this.data_orig;\n");
	htmlFile.WriteString("			};\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	function OnStartup()\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		FixOnChangeEditableElements();\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	</script>\n");
	htmlFile.WriteString("	</head>\n");
	htmlFile.WriteString("	\n");
	htmlFile.WriteString("	<body>\n");
	htmlFile.WriteString("	<?php include 'GetUserPermissions.php';\n");
	htmlFile.WriteString("	$bPermissionFound = false;\n");
	htmlFile.WriteString("	foreach ($allowedFiles as $value)\n");
	htmlFile.WriteString("	{\n");
	htmlFile.WriteString("		if( $value == \"GoverKV\" )\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$bPermissionFound = true;\n");
	htmlFile.WriteString("			break;\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	if( $bPermissionFound || $masterUnlock )\n\t{\n?>");
	htmlFile.WriteString("		<article><header align=\"center\"><h1>File: GoverKV</h1></header></article>\n");
	htmlFile.WriteString("			<br>\n");
	htmlFile.WriteString("			<b>Instructions:</b><br>\n");
	htmlFile.WriteString("			-This page is best displayed using Chrome.  Otherwise some of the table borders are missing for some reason.<br>\n");
	htmlFile.WriteString("			-Your changes are automatically saved.<br>\n");
	htmlFile.WriteString("			-Press the Load Data button when you come back to the page to load your changes.<br>\n");
	htmlFile.WriteString("			<b>Naming Conventions:</n><br>\n");
	htmlFile.WriteString("			<a href=\"https://docs.google.com/spreadsheets/d/1imZZn_SfbmMxBpEnyj8_oZ1BEnMN0q0Ck1XYQgKggm4/edit?usp=sharing\" target=\"_blank\">Click here to view the naming conventions for Characters, Locations, and Terms</a> <br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("				$currUser = $_SERVER['PHP_AUTH_USER'];\n");
	htmlFile.WriteString("				if( $currUser == \"rahmed\" )\n");
	htmlFile.WriteString("				{\n");
	htmlFile.WriteString("					echo \"<input align=\\\"center\\\" type=\\\"button\\\" value=\\\"Export Data\\\" onclick=\\\"ExportData()\\\"/>\";\n");
	htmlFile.WriteString("				}\n");
	htmlFile.WriteString("			?>\n\n");
	htmlFile.WriteString("			<table align=\"center\">\n");
	htmlFile.WriteString("				<tr>\n");
	htmlFile.WriteString("					<td>\n");
	htmlFile.WriteString("						<input align=\"center\" type=\"button\" value=\"Load Data\" onclick=\"LoadData()\"/>\n");
	htmlFile.WriteString("					</td>\n");
	htmlFile.WriteString("				</tr>\n");
	htmlFile.WriteString("			</table><br>\n\n");
	htmlFile.WriteString("			<table>\n");
	htmlFile.WriteString("				<tr bgcolor=\"#c8c8fe\">\n");
	htmlFile.WriteString("					<th>#</th>\n");
	htmlFile.WriteString("					<th>Japanese</th>\n");
	htmlFile.WriteString("					<th>English</th>\n");
	htmlFile.WriteString("				</tr>\n");

	vector<FileNameContainer> fileNames;
	FindAllFilesWithinDirectory(imageDirectory, fileNames);

	int i = 0;
	for (const FileNameContainer& fileName : fileNames)
	{
		const char* pImageNumber = fileName.mNoExtension.c_str();

		fprintf(htmlFile.GetFileHandle(), "				<tr id=\"tr_edit_%i\" bgcolor=\"#fefec8\">\n", i);
		fprintf(htmlFile.GetFileHandle(), "					<td align=\"center\" width=\"20\">%s</td><td width=\"88\"><img src=\"..\\ExtractedData\\SakuraWars2\\Disc1\\GoverKV\\%s.png\"></td><td width=\"480\"><textarea id=\"edit_%i\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>\n", pImageNumber, pImageNumber, i, i, i);
		fprintf(htmlFile.GetFileHandle(), "				</tr>\n");
	
		++i;
	}

	htmlFile.WriteString("			</table><br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	?></body>\n");
	htmlFile.WriteString("	</html>\n");

	return true;

}
