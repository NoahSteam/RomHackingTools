#pragma once
//Size can't be changed when patching
//80 images
//64 * 15 4bpp
//48 * 15 1f8
//128 * 15 @0x360
//40x46 8bpp @1840 (character portrait)
//40x32 8bpp @1f80(character eyes)
//40x46 8bpp @2740 (portrait 2
//40x32 8bpp @2e80 (eyes 2)

struct InfoNameImageInfo
{
	int width;
	int height;
	int offset;
	int paletteOffset;
};

const int NumImagesInSet = 7;
InfoNameImageInfo InfoNameImageSet[7] =
{
	{64, 16, 0, 0},
	{48, 15, 0x1E0, 0},
	{128, 15, 0x360, 0},
	{40, 46, 0x1840, 0x1700},// (character portrait)
	{40, 32, 0x1f80, 0x1700},//(character eyes)
	{40, 46, 0x2740, 0x2600},// (portrait 2
	{40, 32, 0x2e80, 0x2600}// (eyes 2)
};

void ExtractInfoName(const string& rootSakuraDirectory, const string& paletteFileName, bool bBmp, const string& outDirectory)
{
	CreateDirectoryHelper(outDirectory);

	const string sakuraFilePath = rootSakuraDirectory + string("SAKURA2\\INFONAME.BIN");
	FileNameContainer sakuraFileNameInfo(sakuraFilePath.c_str());
	FileData sakuraFileData;
	if (!sakuraFileData.InitializeFileData(sakuraFileNameInfo))
	{
		return;
	}

	FileNameContainer paletteFileNameInfo(paletteFileName.c_str());
	FileData paletteFileData;
	if (!paletteFileData.InitializeFileData(paletteFileNameInfo))
	{
		return;
	}

	const string bmpExt = bBmp ? string(".bmp") : (".png");

	{
		PaletteData palette;
		if (!palette.CreateFrom15BitData(paletteFileData.GetData(), 32))
		{
			printf("Unable to create palette 1.\n");
			return;
		}

		const int numEntries = 129;
		int baseOffset = 0;
		for (int i = 0; i < numEntries; ++i)
		{
			const string setName = string("Char") + std::to_string(i) + string("_");

			for(int setIndex = 0; setIndex < 3; ++setIndex)
			{
				const int offset         = InfoNameImageSet[setIndex].offset + baseOffset;
				const int width          = InfoNameImageSet[setIndex].width;
				const int height         = InfoNameImageSet[setIndex].height;
				const int paletteOffset  = InfoNameImageSet[setIndex].paletteOffset + baseOffset;
				const int paletteSize    = InfoNameImageSet[setIndex].paletteOffset == 0 ? palette.GetSize() : 512;
				const int divisor        = paletteSize == 64 ? 2 : 1;
				const int bpp            = paletteSize == 64 ? 4 : 8;

				PaletteData palette2;
				if (paletteSize == 512 && !palette2.CreateFrom15BitData(sakuraFileData.GetData() + paletteOffset, 512))
				{
					printf("Unable to create palette 1.\n");
					return;
				}
				
				PaletteData& paletteToUse = paletteSize == 512 ? palette2 : palette;

				const string outFileName = outDirectory + setName + std::to_string(setIndex) + bmpExt;

				BitmapWriter outBmp;
				outBmp.CreateBitmap(outFileName, width, -height, bpp, sakuraFileData.GetData() + offset, (width * height) / divisor, paletteToUse.GetData(), paletteToUse.GetSize() , bBmp);

				printf("Extracted %s at %08x\n", outFileName.c_str(), offset);
			}

			baseOffset += 0x4000;
		}
	}
}

bool PatchInfoName(const string& inPatchedSakuraDirectory, const string& inTranslatedDataDirectory)
{
	const string sakuraFilePath = inPatchedSakuraDirectory + string("SAKURA2\\INFONAME.BIN");
	FileReadWriter infoNameFile;
	if (!infoNameFile.OpenFile(sakuraFilePath))
	{
		return false;
	}

	const string translatedDirectory = inTranslatedDataDirectory + "\\InfoName\\Translated\\";

	const string bmpExt(".bmp");
	const int numEntries = 129;
	int baseOffset = 0;
	for (int i = 0; i < numEntries; ++i)
	{
		const string setName = string("Char") + std::to_string(i) + string("_");

		for (int setIndex = 0; setIndex < 3; ++setIndex)
		{
			const int offset = InfoNameImageSet[setIndex].offset + baseOffset;
			const int width = InfoNameImageSet[setIndex].width;
			const int height = InfoNameImageSet[setIndex].height;
			
			const string translatedFileName = translatedDirectory + setName + std::to_string(setIndex) + bmpExt;

			BitmapReader translatedFile;
			if(!translatedFile.ReadBitmap(translatedFileName, false))
			{
				continue;
			}

			if(translatedFile.GetBitCount() != 4)
			{
				printf("%s needs to be 4bpp\n", translatedFileName.c_str());
				continue;
			}

			if(translatedFile.mBitmapData.mInfoHeader.mImageWidth != width || abs(translatedFile.mBitmapData.mInfoHeader.mImageHeight) != height)
			{
				printf("Dimensions for %s should be %ix%i, but are %ix%i\n", translatedFileName.c_str(), width, height, translatedFile.mBitmapData.mInfoHeader.mImageWidth, abs(translatedFile.mBitmapData.mInfoHeader.mImageHeight));
				continue;
			}

			infoNameFile.WriteData(offset, translatedFile.mBitmapData.mColorData.mpRGBA, translatedFile.mBitmapData.mColorData.mSizeInBytes);
		}

		baseOffset += 0x4000;
	}

	return true;
}

bool CreateNameInfoNameSpreadsheet(const string& imageDirectory)
{
	TextFileWriter htmlFile;
	const string htmlFileName = imageDirectory + string("..\\Translation\\InfoName.php");
	if (!htmlFile.OpenFileForWrite(htmlFileName))
	{
		printf("Unable to create an html file: %s", htmlFileName.c_str());
		return false;
	}

	htmlFile.WriteString("<html>\n");
	htmlFile.WriteString("	<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;}</style>\n");
	htmlFile.WriteString("	<div id=\"FileName\" style=\"display: none;\">InfoName</div>\n");
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
	htmlFile.WriteString("		if( $value == \"InfoName\" )\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$bPermissionFound = true;\n");
	htmlFile.WriteString("			break;\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	if( $bPermissionFound || $masterUnlock )\n\t{\n?>");
	htmlFile.WriteString("		<article><header align=\"center\"><h1>File: InfoName</h1></header></article>\n");
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
		fprintf(htmlFile.GetFileHandle(), "					<td align=\"center\" width=\"20\">%s</td><td width=\"88\"><img src=\"..\\ExtractedData\\SakuraWars2\\Disc1\\InfoName\\%s.png\"></td><td width=\"480\"><textarea id=\"edit_%i\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>\n", pImageNumber, pImageNumber, i, i, i);
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
