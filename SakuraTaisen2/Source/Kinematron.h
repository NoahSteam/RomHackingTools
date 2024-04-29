#pragma once

//Kinemtron images are encoded using some strange algorithm

struct KinematronImageTableEntry
{
	uint16 width;
	uint16 height;
	uint32 unknown1;
	uint32 offset;
	uint32 unknown2;

	void SwapEndianness()
	{
		width = SwapByteOrder(width);
		height = SwapByteOrder(height);
		offset = SwapByteOrder(offset);
	}
};

void DecompressKinemtronData(uint32* pEncodedData, int param_2)
{
	uint32 uVar1;
	int iVar2;
	int iVar3 = 0;
	uint32 key;
	int iVar5;
	uint32 secondValue;
	uint32 firstValue;

	iVar2 = 0xAC53AC53;//DAT_060333c0; r8
	uVar1 = 0xAAAA5555;//DAT_060333bc; r9

	char* pStart = (char*)pEncodedData;

	/* Top of COMMFILE 060c0000 in memory */
	firstValue = SwapByteOrder(*pEncodedData); //uVar7 = r6
	secondValue = SwapByteOrder(pEncodedData[1]); //param_1 = r7
	key = firstValue;
	while (true)
	{
		iVar5 = param_2; //r4 = r10
		if (iVar5 < 0)
		{
			iVar5 += 0x00ff0009;//PTR_DAT_060333b8._0_2_; r1 = 0x00ff0009
		}

		//iVar3 = r0
		//iVar3 = iVar5 >> 8;//(*(code*)PTR_FUN_060333c4)(); shar r4, 8 times
		iVar5 = iVar5 >> 8;

		if (iVar5 + 1 <= iVar3)
		{
			break;
		}

		iVar5 = 0;
		do
		{
			const uint32 encodedValue = SwapByteOrder(*pEncodedData);
			*pEncodedData = SwapByteOrder(encodedValue ^ key);
			key = (key ^ 0xAAAA5555) + 0xAC53AC53;
			++pEncodedData;
		} while (++iVar5 < 0x40);

		key = (firstValue ^ 0x13579BDF) + secondValue; //uVar7 = r6
		secondValue = firstValue;
		firstValue = key;
		iVar3 += 1;
		//	param_2 = uVar4;
	}
	return;
}

void TestRadio()
{
	FileNameContainer name("a:\\SakuraWars2\\Disc1_Original\\SAKURA1\\COMMFILE.ALL");
	//	FileNameContainer name("a:\\SakuraWars2\\COMMFILE2.ALL");
	FileData commFile;
	if (!commFile.InitializeFileData(name))
	{
		return;
	}

	const int dataSize = commFile.GetDataSize() - 0x7800;
	char* pData = new char[dataSize];
	char* pDataStart = pData;
	memcpy_s(pData, dataSize, commFile.GetData() + 0x7800, commFile.GetDataSize() - 0x7800);
	//	memcpy_s(pData, dataSize, commFile.GetData(), commFile.GetDataSize());
	DecompressKinemtronData((uint32*)pData, 0xd74c);//0x7594);

	FileWriter outData;
	if (outData.OpenFileForWrite("a:\\SakuraWars2\\radioOut.bin"))
	{
		outData.WriteData(pDataStart, dataSize);
	}

	delete[] pData;
}

void EncodeRadio()
{
	uint32 c1 = 0xAAAA5555;
	uint32 c2 = 0xAC53AC53;
	uint32 k = 0x0b17;

	uint32 uVar4 = (k ^ 0xAAAA5555) + 0xAC53AC53;
	uint32 a = 0x26;
	uint32 b = a ^ uVar4;
	b += 0;

	uint32 uVar5 = (uVar4 ^ 0xAAAA5555) + 0xAC53AC53;
	uint32 c = 0xa841e45b;
	uint32 d = c ^ uVar5;
	d += 0;

	uint32 given = 0x56fe0ab3;
	uint32 calculatedKey = (0x0b17 ^ 0xAAAA5555) + 0xac53ac53;
	uint32 encoded = given ^ calculatedKey;
	encoded += 0;

	uint32 given2 = 0x00e9e848;
	uint32 calculatedKey2 = (calculatedKey ^ 0xAAAA5555) + 0xac53ac53;
	uint32 encoded2 = given2 ^ calculatedKey2;
	encoded2 += 0;

	uint32 given3 = 0x8b14961b;
	uint32 calculatedKey3 = (calculatedKey2 ^ 0xAAAA5555) + 0xac53ac53;
	uint32 encoded3 = given3 ^ calculatedKey3;
	encoded3 += 0;

	FileNameContainer decodedFileName("a:\\SakuraWars2\\radioOut.bin");
	FileData decodedFile;
	if (!decodedFile.InitializeFileData(decodedFileName))
	{
		return;
	}

	FileWriter outFile;
	if (!outFile.OpenFileForWrite("a:\\SakuraWars2\\Encoded.bin"))
	{
		return;
	}

	uint32* pDecodedStream = (uint32*)(decodedFile.GetData());
	uint32 prevKey = 0xb17;

	uint32 firstEncodedValueInBlock = SwapByteOrder(pDecodedStream[0]) ^ prevKey;
	outFile.WriteData(&prevKey, sizeof(uint32), true);

	bool bCalcSecond = true;
	uint32 decodedIndex = 1;
	uint32 secondValue = 0;
	const uint32 numEntriesInData = decodedFile.GetDataSize() >> 2;
	while (decodedIndex < numEntriesInData)
	{
		const uint32 numEntriesInBlock = 0x40;
		uint32 entryIndex = 1;
		while (entryIndex < numEntriesInBlock)
		{
			const uint32 given = SwapByteOrder(pDecodedStream[decodedIndex]);
			const uint32 key = (prevKey ^ 0xAAAA5555) + 0xac53ac53;
			const uint32 encoded = given ^ key;
			prevKey = key;

			if (bCalcSecond)
			{
				secondValue = encoded;
				bCalcSecond = false;
			}

			outFile.WriteData(&encoded, sizeof(uint32), true);
			++decodedIndex;
			++entryIndex;
		}

		prevKey = (firstEncodedValueInBlock ^ 0x13579BDF) + secondValue;
		secondValue = firstEncodedValueInBlock;
		firstEncodedValueInBlock = prevKey;

		uint32 nextVal = SwapByteOrder(pDecodedStream[decodedIndex++]);
		uint32 nextEncoded = nextVal ^ prevKey;
		outFile.WriteData(&nextEncoded, sizeof(uint32), true);
	}
}

void ExtractKinematronImages(const std::string& inSakuraDir, const std::string& inOutputDirectory, bool bInBmp)
{
	CreateDirectoryHelper(inOutputDirectory);

	const std::string imgExt = bInBmp ? ".bmp" : ".png";

	const std::string filePath = inSakuraDir + "COMMFILE.ALL";
	FileData commFile;
	if (!commFile.InitializeFileData(filePath))
	{
		return;
	}

	//Decode the data
	const int dataSize = commFile.GetDataSize() - 0x7800;
	char* pDecodedData = new char[dataSize];
	memcpy_s(pDecodedData, dataSize, commFile.GetData() + 0x7800, commFile.GetDataSize() - 0x7800);
	DecompressKinemtronData((uint32*)pDecodedData, 0xd74c);//0x7594);

	const char* pPalette = pDecodedData + 0x290;

	const int offsetToImageTable = 0x1120;
	const int numImages          = 0x2c;
	const int offsetToImages     = offsetToImageTable + sizeof(KinematronImageTableEntry)*numImages;
	KinematronImageTableEntry images[numImages];
	memcpy_s(images, sizeof(images), pDecodedData + offsetToImageTable, sizeof(images));

	for (int i = 0; i < numImages; ++i)
	{
		images[i].SwapEndianness();
	}

	for(int i = 0; i < numImages; ++i)
	{
		const string bitmapFileName = inOutputDirectory + std::to_string(i) + imgExt;
		const uint16 width          = images[i].width;
		const uint16 height         = images[i].height;
		const int imgSize           = (width*height) >> 1;

		ExtractImageFromData(pDecodedData + offsetToImages + images[i].offset, imgSize, bitmapFileName, pPalette, 32, true, width, height, 1, 256, 0, true, bInBmp);
	}

	delete[] pDecodedData;
}

bool CreateNameKinematronSpreadsheet(const string& imageDirectory)
{
	TextFileWriter htmlFile;
	const string htmlFileName = imageDirectory + string("..\\..\\Translation\\Kinematron.php");
	if (!htmlFile.OpenFileForWrite(htmlFileName))
	{
		printf("Unable to create an html file: %s", htmlFileName.c_str());
		return false;
	}

	htmlFile.WriteString("<html>\n");
	htmlFile.WriteString("	<head><style>textarea {width: 100%;top: 0; left: 0; right: 0; bottom: 0; position: absolute; resize: none;-webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;} table {border-collapse: collapse;} table, th, td { position: relative; border: 1px solid black;}#myProgress {width: 100%;	background-color: #ddd;} #myBar {width: 1%;height: 30px; background-color: #4CAF50;} .rotate90{ -webkit - transform: rotate(270deg); -moz - transform: rotate(270deg); -o - transform: rotate(270deg); -ms - transform: rotate(270deg); transform: rotate(270deg); }</style>\n");
	htmlFile.WriteString("	<div id=\"FileName\" style=\"display: none;\">Kinematron</div>\n");
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
	htmlFile.WriteString("		if( $value == \"PBOOK_EC\" )\n");
	htmlFile.WriteString("		{\n");
	htmlFile.WriteString("			$bPermissionFound = true;\n");
	htmlFile.WriteString("			break;\n");
	htmlFile.WriteString("		}\n");
	htmlFile.WriteString("	}\n");
	htmlFile.WriteString("	if( $bPermissionFound || $masterUnlock )\n\t{\n?>");
	htmlFile.WriteString("		<article><header align=\"center\"><h1>File: PBOKinematronOK_EC</h1></header></article>\n");
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

	std::sort(fileNames.begin(), fileNames.end(), [](const FileNameContainer& a, const FileNameContainer& b)
		{
			return std::atoi(a.mNoExtension.c_str()) < std::atoi(b.mNoExtension.c_str());
		});

	int i = 0;
	for (const FileNameContainer& fileName : fileNames)
	{
		if(!((i >= 12 && i <= 19) || (i >= 28 && i<= 31) || (i >= 33 && i <= 36) || (i == 40 || i == 43)))
		{
			++i;
			continue;
		}

		const char* pImageNumber = fileName.mNoExtension.c_str();

		fprintf(htmlFile.GetFileHandle(), "				<tr id=\"tr_edit_%i\" bgcolor=\"#fefec8\">\n", i);
		fprintf(htmlFile.GetFileHandle(), "					<td align=\"center\" width=\"20\">%s</td><td width=\"128\" height=\"16\" align=\"center\"><img src=\"..\\ExtractedData\\SakuraWars2\\Disc1\\Kinematron\\%s.png\" ></td><td width=\"480\"><textarea id=\"edit_%i\" contenteditable=true onchange=\"SaveEdits('%i.bmp', 'edit_%i')\" style=\"border: none; width: 100%%; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box;\">Untranslated</textarea></td>\n", pImageNumber, pImageNumber, i, i, i);
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
