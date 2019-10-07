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
	printf("usage: YabuseMednafenSaveConverter [command]\n");
	printf("Commands:\n");
	printf("ExtractRawText rootSakuraTaisenDirectory outDirectory\n");
}

int main(int argc, char *argv[])
{
	if( argc < 2 )
	{
		return 0;
	}

	if( argc == 2 )
	{
		const string inFileName(argv[1]);
		FileNameContainer fileName(inFileName.c_str());

		if( fileName.mExtention == "bkr" )
		{

		}
		else if( fileName.mExtention == "bin" )
		{

		}
	}

	PrintHelp();
	return false;
}
