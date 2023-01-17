#pragma once

#pragma pack(push, 1)
struct MNameCGHeader
{
	uint16 offsetToImage;// * 8 to get to actual offset
	uint16 reserved1; //*2
	uint8  width; //*8 to get actual height
	uint8  height;
	uint16 reserved;
};
#pragma pack(pop)

void ExtractNameCG(const string& rootSakuraDirectory, const string& paletteFileName, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	const string sakuraFilePath = rootSakuraDirectory + string("SAKURA.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());

	FileNameContainer paletteFileNameInfo(paletteFileName.c_str());
	FileData paletteFile;
	if (!paletteFile.InitializeFileData(paletteFileNameInfo))
	{
		return;
	}

	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return;
	}

	PaletteData paletteData;
	if (!paletteData.CreateFrom15BitData(paletteFile.GetData(), paletteFile.GetDataSize()))
	{
		printf("Unable to create palette.\n");
		return;
	}

	const string bmpExt(".bmp");

	//CG File 1
	{
		const string cgFilePath = rootSakuraDirectory + string("SAKURA1\\M_NAME1.CG");
		FileNameContainer cgFileNameInfo(cgFilePath.c_str());

		FileData cgFileData;
		if (!cgFileData.InitializeFileData(cgFileNameInfo))
		{
			return;
		}

		const string cg1OutputDirectory = outDirectory + std::to_string(1) + string("\\");
		CreateDirectoryHelper(cg1OutputDirectory);

		const int numEntries = 49;
		MNameCGHeader lookupTable[numEntries];
		const unsigned int offsetToData = 0x00021C08;
		memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

		for (int i = 0; i < numEntries; ++i)
		{
			lookupTable[i].offsetToImage = SwapByteOrder(lookupTable[i].offsetToImage) * 8;
			lookupTable[i].width *= 8;

			const string outFileName = cg1OutputDirectory + std::to_string(i) + bmpExt;

			BitmapWriter outBmp;
			outBmp.CreateBitmap(outFileName, lookupTable[i].width, -lookupTable[i].height, 4, cgFileData.GetData() + lookupTable[i].offsetToImage, (lookupTable[i].width * lookupTable[i].height) / 2, paletteData.GetData(), paletteData.GetSize(), true);
		}
	}

	//CG File 2
	{
		const string cgFilePath = rootSakuraDirectory + string("SAKURA1\\M_NAME2.CG");
		FileNameContainer cgFileNameInfo(cgFilePath.c_str());

		FileData cgFileData;
		if (!cgFileData.InitializeFileData(cgFileNameInfo))
		{
			return;
		}

		const string cg1OutputDirectory = outDirectory + std::to_string(2) + string("\\");
		CreateDirectoryHelper(cg1OutputDirectory);

		const int numEntries = 7;
		MNameCGHeader lookupTable[numEntries];
		const unsigned int offsetToData = 0x000222EC;
		memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

		for (int i = 0; i < numEntries; ++i)
		{
			lookupTable[i].offsetToImage = SwapByteOrder(lookupTable[i].offsetToImage) * 8;
			lookupTable[i].width *= 8;

			const string outFileName = cg1OutputDirectory + std::to_string(i) + bmpExt;

			BitmapWriter outBmp;
			outBmp.CreateBitmap(outFileName, lookupTable[i].width, -lookupTable[i].height, 4, cgFileData.GetData() + lookupTable[i].offsetToImage, (lookupTable[i].width * lookupTable[i].height) / 2, paletteData.GetData(), paletteData.GetSize(), true);
		}
	}

	//CG File 3
	{
		const string cgFilePath = rootSakuraDirectory + string("SAKURA1\\M_NAME3.CG");
		FileNameContainer cgFileNameInfo(cgFilePath.c_str());

		FileData cgFileData;
		if (!cgFileData.InitializeFileData(cgFileNameInfo))
		{
			return;
		}

		const string cg1OutputDirectory = outDirectory + std::to_string(3) + string("\\");
		CreateDirectoryHelper(cg1OutputDirectory);

		const int numEntries = 15;
		MNameCGHeader lookupTable[numEntries];
		const unsigned int offsetToData = 0x000223F0;
		memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(lookupTable));

		for (int i = 0; i < numEntries; ++i)
		{
			lookupTable[i].offsetToImage = SwapByteOrder(lookupTable[i].offsetToImage) * 8;
			lookupTable[i].width *= 8;

			const string outFileName = cg1OutputDirectory + std::to_string(i) + bmpExt;

			BitmapWriter outBmp;
			outBmp.CreateBitmap(outFileName, lookupTable[i].width, -lookupTable[i].height, 4, cgFileData.GetData() + lookupTable[i].offsetToImage, (lookupTable[i].width * lookupTable[i].height) / 2, paletteData.GetData(), paletteData.GetSize(), true);
		}
	}

	printf("Success\n");
}

bool PatchMNameCG(const std::string& inPatchedSakuraDirectory, const std::string& inTranslatedDataDirectory)
{
	printf("Patching PatchMNameCG\n");

	const string sakuraFilePath = inPatchedSakuraDirectory + string("SAKURA.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());

	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return false;
	}

	struct CGFilesToPatch
	{
		const char* pTranslatedDirectory;
		const char* pCGFileName;
		int numEntries;
		unsigned int offsetToData;
	};

	const int MaxEntries = 49;
	const int numCGFiles = 3;
	CGFilesToPatch cgFilesToPatch[numCGFiles] = 
	{
		{"1\\", "SAKURA1\\M_NAME1.CG", 49, 0x00021C08},
		{"2\\", "SAKURA1\\M_NAME2.CG", 7, 0x000222EC},
		{"3\\", "SAKURA1\\M_NAME3.CG", 15, 0x000223F0},
	};

	//CG Files
	for(int cgIndex = 0; cgIndex < numCGFiles; ++cgIndex)
	{
		const string cgFilePath = inPatchedSakuraDirectory + cgFilesToPatch[cgIndex].pCGFileName;
		FileReadWriter cgFileData;
		if (!cgFileData.OpenFile(cgFilePath))
		{
			return false;
		}

		const std::string translatedDirectory = inTranslatedDataDirectory + std::string("MNameCG\\Translated\\") + cgFilesToPatch[cgIndex].pTranslatedDirectory;

		const std::string bmpExt(".bmp");
		MNameCGHeader lookupTable[MaxEntries];
		const unsigned int offsetToData = cgFilesToPatch[cgIndex].offsetToData;
		memcpy_s(lookupTable, sizeof(lookupTable), sakuraFileData.GetData() + offsetToData, sizeof(MNameCGHeader)*cgFilesToPatch[cgIndex].numEntries);

		for (int i = 0; i < cgFilesToPatch[cgIndex].numEntries; ++i)
		{
			lookupTable[i].offsetToImage = SwapByteOrder(lookupTable[i].offsetToImage) * 8;
			lookupTable[i].width *= 8;

			const string imageName = translatedDirectory + std::to_string(i) + bmpExt;
			BmpToSaturnConverter patchedImage;
			if( !patchedImage.ConvertBmpToSakuraFormat(imageName, false) )
			{
				if( patchedImage.GetImageHeight() != lookupTable[i].height || patchedImage.GetImageWidth() != lookupTable[i].width )
				{
					printf("Image dimensions don't match original: %s\n", imageName.c_str());
				}
				return false;
			}
			
			cgFileData.WriteData(lookupTable[i].offsetToImage, patchedImage.GetImageData(), patchedImage.GetImageDataSize());
		}
	}

	printf("Succeeded patching CG files\n");
	return true;
}

bool CreateNameCG1Spreadsheet(const string& imageDirectory)
{
	TextFileWriter htmlFile;
	const string htmlFileName = imageDirectory + string("..\\..\\Translation\\MNameCG3.php");
	if (!htmlFile.OpenFileForWrite(htmlFileName))
	{
		printf("Unable to create an html file: %s", htmlFileName.c_str());
		return false;
	}

	htmlFile.WriteString("<html>\n");
	htmlFile.WriteString("	<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;}</style>\n");
	htmlFile.WriteString("	<div id=\"FileName\" style=\"display: none;\">MNameCG3</div>\n");
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
	htmlFile.WriteString("		if( $value == \"MNameCG3\" )\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$bPermissionFound = true;\n");
	htmlFile.WriteString("			break;\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	if( $bPermissionFound || $masterUnlock )\n\t{\n?>");
	htmlFile.WriteString("		<article><header align=\"center\"><h1>File: MNameCG3</h1></header></article>\n");
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

	const int firstImage = 0;
	const int lastImage = 14;
	for (int i = firstImage; i <= lastImage; ++i)
	{
		fprintf(htmlFile.GetFileHandle(), "				<tr id=\"tr_edit_%i\" bgcolor=\"#fefec8\">\n", i);
		fprintf(htmlFile.GetFileHandle(), "					<td align=\"center\" width=\"20\">%i</td><td width=\"88\"><img src=\"..\\ExtractedData\\SakuraWars2\\Disc1\\MNameCG\\3\\%i.png\"></td><td width=\"480\"><textarea id=\"edit_%i\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>\n", i, i, i, i, i);
		fprintf(htmlFile.GetFileHandle(), "				</tr>\n");
	}

	htmlFile.WriteString("			</table><br>\n");
	htmlFile.WriteString("			<?php\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	?></body>\n");
	htmlFile.WriteString("	</html>\n");

	return true;
}
