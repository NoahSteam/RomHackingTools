#include <stdio.h>
#include <windows.h>
#include <vector>
#include <string>
#include <list>
#include <map>
#include <algorithm>
#include "..\Utils\Utils.h"
#include <assert.h>

using std::vector;
using std::string;
using std::list;
using std::map;

void PrintHelp()
{
	printf("Drag and drop your save file onto the program.  Alternatively, use commandline:\nUsage: YabuseMednafenSaveConverter inFileName\n");
}

int main(int argc, char *argv[])
{
	if( argc < 2 )
	{
		PrintHelp();
		return 0;
	}

	if( argc == 2 )
	{
		const string inFileName(argv[1]);
		FileNameContainer fileName(inFileName.c_str());
		char buffer[MAX_PATH];
		GetCurrentDirectory(MAX_PATH, buffer);

		if( fileName.mExtention == ".bkr" )
		{
			const string outFileName = string(buffer) + string("\\bkram.bin");

			MednafinToYabause convertor;
			convertor.ConvertData(inFileName, outFileName);
		}
		else if( fileName.mExtention == ".bin" )
		{
			const string outFileName = string(buffer) + string("\\mednafen.bkr");

			YabauseToMednafin convertor;
			convertor.ConvertData(inFileName, outFileName);
		}
	}

	PrintHelp();

	return 1;
}
