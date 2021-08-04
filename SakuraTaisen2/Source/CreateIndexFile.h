#pragma once

void CreateIndexFile(const string& inMesFileDirectory, const string& inSKFileDirectory, const string& outFilePath)
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
	htmlFile.WriteStringWithNewLine("			<th>");
	htmlFile.WriteStringWithNewLine("				Claim");
	htmlFile.WriteStringWithNewLine("			</th>");

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
		FindAllSakuraText(skFiles, sakuraTextFiles);

		for (const SakuraTextFile& skFile : sakuraTextFiles)
		{
			const char* pFileName = skFile.mFileNameInfo.mNoExtension.c_str();

			htmlFile.Printf("			<?php\n");
			htmlFile.Printf("				$searchFile = \"%s\";\n", pFileName);
			htmlFile.Printf("				include 'GetFileOwners.php';\n");
			htmlFile.Printf("				include 'GetFileProgress.php';\n");
			htmlFile.Printf("			?>\n");
			htmlFile.Printf("\n");
			htmlFile.Printf("				<tr bgcolor = <?php echo \"$progressColor\" ?> ><td align = \"center\"><a href = \"%s.php\">%s</a></td><td align = \"center\"> <?php echo $fileOwners; ?> </td><td align = \"center\"><?php echo \"(\".$linesTranslated.\" of %i)\" ?></td><td><button onclick = \"TakeOwnershipOfFile('%s')\">Take Ownership</button></td></tr>\n", pFileName, pFileName, (int)skFile.mLines.size(), pFileName);
		}

		for (const SysFileExtractor& sysFile : sysFiles)
		{
			if (sysFile.mLines.size() == 0)
			{
				continue;
			}

			const char* pFileName = sysFile.mFileNameInfo.mNoExtension.c_str();

			htmlFile.Printf("			<?php\n");
			htmlFile.Printf("				$searchFile = \"%s\";\n", pFileName);
			htmlFile.Printf("				include 'GetFileOwners.php';\n");
			htmlFile.Printf("				include 'GetFileProgress.php';\n");
			htmlFile.Printf("			?>\n");
			htmlFile.Printf("\n");
			htmlFile.Printf("				<tr bgcolor = <?php echo \"$progressColor\" ?> ><td align = \"center\"><a href = \"%s.php\">%s</a></td><td align = \"center\"> <?php echo $fileOwners; ?> </td><td align = \"center\"><?php echo \"(\".$linesTranslated.\" of %i)\" ?></td><td><button onclick = \"TakeOwnershipOfFile('%s')\">Take Ownership</button></td></tr>\n", pFileName, pFileName, (int)sysFile.mLines.size(), pFileName);
		}
	}

	//Infoname
	{
		const char* pFileName = "InfoName";
		const int numFiles = 267;
		htmlFile.Printf("			<?php\n");
		htmlFile.Printf("				$searchFile = \"%s\";\n", pFileName);
		htmlFile.Printf("				include 'GetFileOwners.php';\n");
		htmlFile.Printf("				include 'GetFileProgress.php';\n");
		htmlFile.Printf("			?>\n");
		htmlFile.Printf("\n");
		htmlFile.Printf("				<tr bgcolor = <?php echo \"$progressColor\" ?> ><td align = \"center\"><a href = \"%s.php\">%s</a></td><td align = \"center\"> <?php echo $fileOwners; ?> </td><td align = \"center\"><?php echo \"(\".$linesTranslated.\" of %i)\" ?></td><td><button onclick = \"TakeOwnershipOfFile('%s')\">Take Ownership</button></td></tr>\n", pFileName, pFileName, numFiles, pFileName);
	}

	//GoverKV
	{
		const char* pFileName = "GoverKV";
		const int numFiles = 53;
		htmlFile.Printf("			<?php\n");
		htmlFile.Printf("				$searchFile = \"%s\";\n", pFileName);
		htmlFile.Printf("				include 'GetFileOwners.php';\n");
		htmlFile.Printf("				include 'GetFileProgress.php';\n");
		htmlFile.Printf("			?>\n");
		htmlFile.Printf("\n");
		htmlFile.Printf("				<tr bgcolor = <?php echo \"$progressColor\" ?> ><td align = \"center\"><a href = \"%s.php\">%s</a></td><td align = \"center\"> <?php echo $fileOwners; ?> </td><td align = \"center\"><?php echo \"(\".$linesTranslated.\" of %i)\" ?></td><td><button onclick = \"TakeOwnershipOfFile('%s')\">Take Ownership</button></td></tr>\n", pFileName, pFileName, numFiles, pFileName);
	}

	//MNameCG1
	{
		const char* pFileName = "MNameCG1";
		int numFiles = 49;
		htmlFile.Printf("			<?php\n");
		htmlFile.Printf("				$searchFile = \"%s\";\n", pFileName);
		htmlFile.Printf("				include 'GetFileOwners.php';\n");
		htmlFile.Printf("				include 'GetFileProgress.php';\n");
		htmlFile.Printf("			?>\n");
		htmlFile.Printf("\n");
		htmlFile.Printf("				<tr bgcolor = <?php echo \"$progressColor\" ?> ><td align = \"center\"><a href = \"%s.php\">%s</a></td><td align = \"center\"> <?php echo $fileOwners; ?> </td><td align = \"center\"><?php echo \"(\".$linesTranslated.\" of %i)\" ?></td><td><button onclick = \"TakeOwnershipOfFile('%s')\">Take Ownership</button></td></tr>\n", pFileName, pFileName, numFiles, pFileName);
	}

	//MNameCG2
	{
		const char* pFileName = "MNameCG2";
		const int numFiles = 7;
		htmlFile.Printf("			<?php\n");
		htmlFile.Printf("				$searchFile = \"%s\";\n", pFileName);
		htmlFile.Printf("				include 'GetFileOwners.php';\n");
		htmlFile.Printf("				include 'GetFileProgress.php';\n");
		htmlFile.Printf("			?>\n");
		htmlFile.Printf("\n");
		htmlFile.Printf("				<tr bgcolor = <?php echo \"$progressColor\" ?> ><td align = \"center\"><a href = \"%s.php\">%s</a></td><td align = \"center\"> <?php echo $fileOwners; ?> </td><td align = \"center\"><?php echo \"(\".$linesTranslated.\" of %i)\" ?></td><td><button onclick = \"TakeOwnershipOfFile('%s')\">Take Ownership</button></td></tr>\n", pFileName, pFileName, numFiles, pFileName);
	}

	//MNameCG3
	{
		const char* pFileName = "MNameCG3";
		const int numFiles = 15;
		htmlFile.Printf("			<?php\n");
		htmlFile.Printf("				$searchFile = \"%s\";\n", pFileName);
		htmlFile.Printf("				include 'GetFileOwners.php';\n");
		htmlFile.Printf("				include 'GetFileProgress.php';\n");
		htmlFile.Printf("			?>\n");
		htmlFile.Printf("\n");
		htmlFile.Printf("				<tr bgcolor = <?php echo \"$progressColor\" ?> ><td align = \"center\"><a href = \"%s.php\">%s</a></td><td align = \"center\"> <?php echo $fileOwners; ?> </td><td align = \"center\"><?php echo \"(\".$linesTranslated.\" of %i)\" ?></td><td><button onclick = \"TakeOwnershipOfFile('%s')\">Take Ownership</button></td></tr>\n", pFileName, pFileName, numFiles, pFileName);
	}

	//PBOOK_FL
	{
		const char* pFileName = "PBOOK_FL";
		const int numFiles = 98;
		htmlFile.Printf("			<?php\n");
		htmlFile.Printf("				$searchFile = \"%s\";\n", pFileName);
		htmlFile.Printf("				include 'GetFileOwners.php';\n");
		htmlFile.Printf("				include 'GetFileProgress.php';\n");
		htmlFile.Printf("			?>\n");
		htmlFile.Printf("\n");
		htmlFile.Printf("				<tr bgcolor = <?php echo \"$progressColor\" ?> ><td align = \"center\"><a href = \"%s.php\">%s</a></td><td align = \"center\"> <?php echo $fileOwners; ?> </td><td align = \"center\"><?php echo \"(\".$linesTranslated.\" of %i)\" ?></td><td><button onclick = \"TakeOwnershipOfFile('%s')\">Take Ownership</button></td></tr>\n", pFileName, pFileName, numFiles, pFileName);
	}


	htmlFile.WriteStringWithNewLine("		</table>");
	htmlFile.WriteStringWithNewLine("	</body>");
	htmlFile.WriteStringWithNewLine("</html>");
}
