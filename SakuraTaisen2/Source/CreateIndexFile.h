#pragma once

void CreateIndexFile(const string& inMesFileDirectory, const string& inSKFileDirectory, const string& inMiniGameDirectory, const string& outFilePath)
{
	TextFileWriter htmlFile;
	if (!htmlFile.OpenFileForWrite(outFilePath))
	{
		printf("Unable to create an html file: %s", outFilePath.c_str());
		return;
	}

	htmlFile.WriteStringWithNewLine("<html>");
	htmlFile.WriteStringWithNewLine("	<head>");
	htmlFile.WriteStringWithNewLine("		<style>");
	htmlFile.WriteStringWithNewLine("			table, th, td");
	htmlFile.WriteStringWithNewLine("			{");
	htmlFile.WriteStringWithNewLine("				border: 1px solid black;");
	htmlFile.WriteStringWithNewLine("				border-collapse: collapse;");
	htmlFile.WriteStringWithNewLine("			}");
	htmlFile.WriteStringWithNewLine("			body");
	htmlFile.WriteStringWithNewLine("			{");
	htmlFile.WriteStringWithNewLine("				background-image: url('SakuraWars2.jpg');");
	htmlFile.WriteStringWithNewLine("				background-repeat: no-repeat;");
	htmlFile.WriteStringWithNewLine("				background-attachment: fixed;");
	htmlFile.WriteStringWithNewLine("				background-size: cover;");
	htmlFile.WriteStringWithNewLine("			}");
	htmlFile.WriteStringWithNewLine("		</style>");
	htmlFile.WriteStringWithNewLine("	</head>");
	htmlFile.WriteStringWithNewLine("	<script src = \"https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js\">");
	htmlFile.WriteStringWithNewLine("		$(window).on(\"load\", function()");
	htmlFile.WriteStringWithNewLine("		{");
	htmlFile.WriteStringWithNewLine("			OnStartup();");
	htmlFile.WriteStringWithNewLine("		});");
	htmlFile.WriteStringWithNewLine("		</script>");
	htmlFile.WriteStringWithNewLine("");
	htmlFile.WriteStringWithNewLine("		<script type = \"text/javascript\">");
	htmlFile.WriteStringWithNewLine("		function TakeOwnershipOfFile(inFileName)");
	htmlFile.WriteStringWithNewLine("		{");
	htmlFile.WriteStringWithNewLine("			$.ajax({");
	htmlFile.WriteStringWithNewLine("			type: \"POST\",");
	htmlFile.WriteStringWithNewLine("				  url : \"TakeOwnership.php\",");
	htmlFile.WriteStringWithNewLine("					data : { inTBLFileName: inFileName},");
	htmlFile.WriteStringWithNewLine("					success : function(result)");
	htmlFile.WriteStringWithNewLine("			{");
	htmlFile.WriteStringWithNewLine("				location.reload();");
	htmlFile.WriteStringWithNewLine("			},");
	htmlFile.WriteStringWithNewLine("			error : function()");
	htmlFile.WriteStringWithNewLine("			{");
	htmlFile.WriteStringWithNewLine("				alert('Unable to load data');");
	htmlFile.WriteStringWithNewLine("			}");
	htmlFile.WriteStringWithNewLine("			});");
	htmlFile.WriteStringWithNewLine("		}");
	htmlFile.WriteStringWithNewLine("	</script>");
	htmlFile.WriteStringWithNewLine("	<body>");
	htmlFile.WriteStringWithNewLine("		<table>");
	htmlFile.WriteStringWithNewLine("			<th>");
	htmlFile.WriteStringWithNewLine("				File");
	htmlFile.WriteStringWithNewLine("			</th>");
	htmlFile.WriteStringWithNewLine("			<th>");
	htmlFile.WriteStringWithNewLine("				Translator");
	htmlFile.WriteStringWithNewLine("			</th>");
	htmlFile.WriteStringWithNewLine("			<th>");
	htmlFile.WriteStringWithNewLine("				Progress");
	htmlFile.WriteStringWithNewLine("			</th>");

	auto AddRowForFile = [&htmlFile](const char* pFileName, const int numLines)
	{
		htmlFile.Printf("			<?php\n");
		htmlFile.Printf("				$searchFile = \"%s\";\n", pFileName);
		htmlFile.Printf("				$numLines = \"%i\";\n", numLines);
		htmlFile.Printf("				include 'GetFileOwners.php';\n");
		htmlFile.Printf("				include 'GetFileProgress.php';\n");
		htmlFile.Printf("			?>\n");
		htmlFile.Printf("\n");
		htmlFile.Printf("				<tr bgcolor = <?php echo \"$progressColor\" ?> ><td align = \"center\"><a href = \"%s.php\">%s</a></td><td align = \"center\"> <?php echo $fileOwners; ?> </td><td align = \"center\"><?php echo \"(\".$linesTranslated.\" of %i)\" ?></td></tr>\n", pFileName, pFileName, numLines);
	
	};

	//Write entries
	{
		vector<FileNameContainer> skFiles;
		FindAllFilesWithinDirectory(inSKFileDirectory, skFiles);

		vector<FileNameContainer> sysFileNames;
		FindAllFilesWithinDirectory(inMesFileDirectory, sysFileNames);

		//Parse the TBL files
		vector<SysFileExtractor> sysFiles;
		ParseAllSysFiles(sysFileNames, sysFiles);

		vector<SakuraTextFile> sakuraTextFiles;
		FindAllSakuraText(skFiles, sakuraTextFiles, false);

		for (const SakuraTextFile& skFile : sakuraTextFiles)
		{
			const char* pFileName = skFile.mFileNameInfo.mNoExtension.c_str();
			const int numLines = (int)skFile.mLines.size();
			if( numLines > 0 )
			{
				AddRowForFile(pFileName, numLines);
			}
		}

		for (const SysFileExtractor& sysFile : sysFiles)
		{
			if (sysFile.mLines.size() == 0)
			{
				continue;
			}

			const char* pFileName = sysFile.mFileNameInfo.mNoExtension.c_str();
			const int numLines = (int)sysFile.mLines.size();

			AddRowForFile(pFileName, numLines);
		}
	}

	//Infoname
	{
		AddRowForFile("InfoName", 267);
	}

	//GoverKV
	{
		AddRowForFile("GoverKV", 53);
	}

	//MNameCG1
	{
		AddRowForFile("MNameCG1", 49);
	}

	//MNameCG2
	{
		AddRowForFile("MNameCG2", 7);
	}

	//MNameCG3
	{
		AddRowForFile("MNameCG3", 15);
	}

	//PBOOK_FL
	{
		AddRowForFile("PBOOK_FL", 98);
	}

	//PBOOK_FL
	{
		AddRowForFile("PBOOK_BT", 62);
	}

	//Minigames
	{
		vector<string> miniGameDirectories;
		FindAllDirectoriesWithinDirectory(inMiniGameDirectory, miniGameDirectories);

		for (const string& miniGameDirectory : miniGameDirectories)
		{
			FileNameContainer miniGameDirectoryName((inMiniGameDirectory + miniGameDirectory).c_str());

			//Get number of files within each minigame
			vector<FileNameContainer> miniGameImages;
			FindAllFilesWithinDirectory(miniGameDirectoryName.mFullPath, miniGameImages);

			const char* pFileName = miniGameDirectoryName.mNoExtension.c_str();
			const int numFiles = (int)miniGameImages.size();

			AddRowForFile(pFileName, numFiles);
		}
	}

	htmlFile.WriteStringWithNewLine("		</table>");
	htmlFile.WriteStringWithNewLine("	</body>");
	htmlFile.WriteStringWithNewLine("</html>");
}
