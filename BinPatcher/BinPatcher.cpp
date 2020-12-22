/*BinPatcher - Patch a .bin file with the header of a saturn game

(c) Copyright 2018 Rizwan Ahmed

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*******************************************************************************/

#include <stdio.h>
#include <windows.h>
#include <vector>
#include <string>
#include "..\Utils\Utils.h"

using std::string;

void main()
{
	/*
	char buffer[MAX_PATH];
	DWORD dwRet = GetCurrentDirectory(MAX_PATH, buffer);
	if( !dwRet )
	{
		printf("Cannot patch file.  Error: (%d)\n", GetLastError());
		return;
	}

	const string currDir(buffer); 
	
	string patchName = currDir + string("\\") + string("IP.bin");
	FileData patchData;
	if( !patchData.InitializeFileData(patchName.c_str(), patchName.c_str()) )
	{
		printf("Unable to open binary file %s.  Error: (%d)\n", patchName.c_str(), GetLastError());
		return;
	}
	
	string binName = currDir + string("\\") + string("1.bin");
	FileData originalData;
	if( !originalData.InitializeFileData(binName.c_str(), binName.c_str()) )
	{
		printf("Unable to open binary file %s.  Error: (%d)\n", binName.c_str(), GetLastError());
		return;
	}

	//Out file
	FILE* pBinaryFile = fopen(binName.c_str(), "wb");
	if( !pBinaryFile )
	{
		printf("Unable to open file: %s.\n", binName.c_str());
		return;
	}

	fwrite(patchData.GetData(), patchData.GetDataSize(), 1, pBinaryFile);
	fwrite(originalData.GetData() + patchData.GetDataSize(), originalData.GetDataSize() - patchData.GetDataSize(), 1, pBinaryFile);

	fclose(pBinaryFile);
	*/
}
