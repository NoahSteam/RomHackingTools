/*****************************************************************************/
/* compress_rtns.c - CMP-Compatible Decompression Routines.                  */
/*                   CMP is really just a form of RLE compression.           */
/*****************************************************************************/


///////////////////////////////////////////////////////////////////////////////
// CMP Data Compression Format                                               //
///////////////////////////////////////////////////////////////////////////////
// Header Format - Variable Length (32-bits or 64-bits) //                   //
//////////////////////////////////////////////////////////                   //
// Word 0 [16-bits]                                                          //
//   Compression Type                                                        //
//   Value:  0000_YY00 0000_Z000                                             //
//           where: YY = 00 (8-bit RLE), 01 (16-bit RLE), 10 (32-bit RLE)    //
//                  Z determines format of the rest of the header            //
//                                                                           //
// If Z = 0:                                                                 //
// Word 1 [16-bits]                                                          //
//   Decompression Size in Bytes.                                            //
// End of Header                                                             //
//                                                                           //
// If Z = 1:                                                                 //
// Word 1 [16-bits]                                                          //
//   16-bit padding.  Required for alignment of the 32-bit size to follow    //
// Words 2,3 [Combine for 32-bit LW]                                         //
//   Decompression Size in Bytes.                                            //
// End of Header                                                             //
///////////////////////////////////////////////////////////////////////////////
// Payload Format //                                                         //
////////////////////                                                         //
// Two types of data are encountered: compressed and direct copy.            //
// Compression occurs using X-bit data types (X = 8, 16, or 32).             //
//                                                                           //
// ---------------------------------------------------------------------     //
// Compressed copy:  | (Length-2) # of X-bit matches   | X-bit pattern |     //
// ---------------------------------------------------------------------     //
// Note that compression requires at least 2 in-order matches.               //
// Length and pattern occupy X-bits                                          //
//                                                                           //
// ------------------------------------------------------------------------  //
// Direct copy:      | Neg length in #X-bit units | Data to directly copy |  //
// ------------------------------------------------------------------------  //
///////////////////////////////////////////////////////////////////////////////

/* Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decompress_rtns.h"

/* Globals */
static FILE* infile = NULL;	/* File Ptr to input file */
static int G_bufsize = 0;	/* Decompression Buffer Size */

/*****************************************************************************/
/* analyzeHeader - Analyzes the CMP compression header.                      */
/*                 Call prior to decompression.                              */
/*                 Determines size of decompressed data, compression type,   */
/*                 and updates offset to start of the data payload.          */
/* Returns: 0 on success, -1 on failure.                                     */
/*****************************************************************************/
int analyzeHeader(const char* inputFname, int* fileOffset, int* dataSizeBytes, int* cmprType){

	int rval, ZformatFlag, spareTest;
	short* pHdr;
	char hdr[8];
	FILE* ifile = NULL;

	/* Open the input file */
	fopen_s(&ifile, inputFname, "rb");
	if (ifile == NULL){
		printf("Aborting, Error opening input file %s!\n", inputFname);
		return -1;
	}
	pHdr = (short*)hdr;
	memset(pHdr, 0, 8);  /* Zero the header buffer */

	/* Seek to the file offset in question */
	fseek(ifile, *fileOffset, SEEK_SET);

	/* Read Header 16-bit Word 0 */
	rval = fread(pHdr, 2, 1, ifile);
	if (rval != 1){
		printf("Aborting, Error reading word 0\n");
		return -1;
	}
	swap16(pHdr);
	*cmprType = (((*pHdr) >> 10) & 0x3);
	ZformatFlag = (((*pHdr) >> 3) & 0x1);
	spareTest = (*pHdr) & 0xF3F7;
	pHdr += 1;

	/* Determine if the compression type is valid */
	if (*cmprType == 0x3){
		printf("Aborting, Invalid compression type 0x3 specified!\n");
		fclose(ifile);
		return -1;
	}

	/* Determine if the spare bits are zero, Warn if not. */
	if (spareTest != 0)
	{
		printf("WARNING, Spare bits not set to zero.  Likely invalid format!\n");
		return -1;
	}

	/* Parse size of decompressed data using ZformatFlag */
	rval = fread(pHdr, 2, 1, ifile);
	if (rval != 1){
		printf("Aborting, Error reading word 1\n");
		fclose(ifile);
		return -1;
	}
	if (ZformatFlag == 0){
		swap16(pHdr);
		*dataSizeBytes = *pHdr;

		/* Update File offset start for decompression */
		*fileOffset += 4;
	}
	else{
		/* Word 1 was a spare, read words 2/3 for size of data */
		pHdr += 1;
		rval = fread(dataSizeBytes, 4, 1, ifile);
		if (rval != 1){
			printf("Aborting, Error reading words 2/3\n");
			fclose(ifile);
			return -1;
		}
		swap32(dataSizeBytes);

		/* Update File offset start for decompression */
		*fileOffset += 8;
	}
	fclose(ifile);

	if (*dataSizeBytes == 0){
		printf("Error, decompression size is 0!\n");
		return -1;
	}

	return 0;
}




/*****************************************************************************/
/* cmp_decompress - Top Level Decompression routine.                         */
/* Returns: 0 on success, -1 on failure.                                     */
/*****************************************************************************/
int cmp_decompress(const char* inputFname, const char* outputFname, int fileOffset,	int sizeBytes, int cmprType)
{
	FILE* ofile = NULL;
	char* obuffer = NULL;
//	unsigned int fsize = 0;
	int rval = 0;

	/* Open the input file for reading */
	fopen_s(&infile, inputFname, "rb");
	if (infile == NULL){
		printf("Error opening file %s\n", inputFname);
		return -1;
	}

	/* Allocate memory for the decompression */
	/* (Add some padding since you are allowed to overrun the final size, then truncate it) */
	G_bufsize = sizeBytes * 2;
	obuffer = (char*)malloc(G_bufsize);
	if (obuffer == NULL){
		printf("Error allocing memory for decompression data\n");
		return -1;
	}

	/* Jump to starting offset of the input file data payload */
	fseek(infile, fileOffset, SEEK_SET);

	/* Decompress Based on Selected Pattern Length: 8/16/32-bit */
	switch (cmprType){

	case BYTE_CMP_TYPE:
	{
		printf("8-bit Decompression\n");
		if (decmpr_8bit(obuffer, sizeBytes) < 0){
			printf("8-bit decompression failed.\n");
			rval = -1;
		}
	}
		break;

	case SHORT_CMP_TYPE:
	{
		printf("16-bit Decompression\n");
		if (decmpr_16bit(obuffer, sizeBytes) < 0){
			printf("16-bit decompression failed.\n");
			rval = -1;
		}
	}
		break;

	case LONG_CMP_TYPE:
	{
		printf("32-bit Decompression\n");
		if (decmpr_32bit(obuffer, sizeBytes) < 0){
			printf("32-bit decompression failed.\n");
			rval = -1;
		}
	}
		break;

	default:
		printf("Error, incorrect compression type specified.\n");
		fclose(infile);
		free(obuffer);
		return -1;
		break;
	}


	/**************************************************/
	/* Write the Decompressed data to the output file */
	/**************************************************/
	if (rval >= 0){
		ofile = NULL;
		fopen_s(&ofile, outputFname, "wb");
		if (ofile == NULL){
			printf("Error opening output file for writing.\n");
			return -1;
		}
		fwrite(obuffer, sizeBytes, 1, ofile);
		fclose(ofile);
	}

	/******************/
	/* Free Resources */
	/******************/
	if (obuffer != NULL)
		free(obuffer);
	fclose(infile);

	return rval;
}




/*****************************************************************************/
/* decmpr_8bit - 8-bit CMP Decompression routine.                            */
/* Inputs: ptr to output stream, size of output stream                       */
/* Returns: 0 on success, -1 on failure.                                     */
/*****************************************************************************/
int decmpr_8bit(char* obuf, int decomprSizeBytes){

	char unit;
	int rval, copySize, x;
	int lastFlag = 0;
	int unitSize = 1;
	int remainingBytes = decomprSizeBytes;

	memset(obuf, 0, decomprSizeBytes);

	/* While output space exists, continue to attempt decompression */
	while (remainingBytes > 0){

		/* EOF */
		if (feof(infile)){
			printf("Error, premature end of file.\n");
			return -1;
		}

		/* Read 1 unit of data */
		rval = fread(&unit, unitSize, 1, infile);
		if (rval != 1){
			printf("Aborting, Error reading unit of data\n");
			return -1;
		}

		/* If negative, direct copy the next #*-1 units */
		/* If positive, copy the next item #+2 times    */
		if (unit < 0){
			copySize = unit * -1;
			if (copySize*unitSize > remainingBytes){
				copySize = (remainingBytes / unitSize) + 1;
				lastFlag = 1;  //Prevent abort due to final read
			}
			rval = fread(obuf, unitSize, copySize, infile);
			if ((rval != copySize) && (lastFlag != 1)){
				printf("Aborting, Error reading string of data\n");
				return -1;
			}
			obuf += (copySize*unitSize);
			remainingBytes -= copySize*unitSize;
		}
		else{
			copySize = (int)unit + 2;
			if (copySize*unitSize > remainingBytes){
				copySize = (remainingBytes / unitSize) + 1;
			}
			rval = fread(&unit, unitSize, 1, infile);
			if (rval != 1){
				printf("Aborting, Error reading string of data\n");
				return -1;
			}
			for (x = 0; x < copySize; x++){
				memcpy(obuf++, &unit, unitSize);
			}
			remainingBytes -= copySize*unitSize;
		}
	}


	return 0;
}




/*****************************************************************************/
/* decmpr_16bit - 16-bit CMP Decompression routine.                          */
/* Inputs: ptr to output stream, size of output stream                       */
/* Returns: 0 on success, -1 on failure.                                     */
/*****************************************************************************/
int decmpr_16bit(char* obuf, int decomprSizeBytes){

	short unit;
	int rval, copySize, x;
	int unitSize = 2;
	int lastFlag = 0;
	int remainingBytes = decomprSizeBytes;

	/* While output space exists, continue to attempt decompression */
	while (remainingBytes > 0){

		/* EOF */
		if (feof(infile)){
			printf("Error, premature end of file.\n");
			return -1;
		}

		/* Read 1 unit of data */
		rval = fread(&unit, unitSize, 1, infile);
		if (rval != 1){
			printf("Aborting, Error reading unit of data\n");
			return -1;
		}
		swap16(&unit);

		/* If negative, direct copy the next #*-1 units */
		/* If positive, copy the next item #+2 times    */
		if (unit < 0){
			copySize = unit * -1;
			if (copySize*unitSize > remainingBytes){
				copySize = (remainingBytes / unitSize) + 1;
				lastFlag = 1;  //Prevent abort due to final read
			}
			rval = fread(obuf, unitSize, copySize, infile);
			if ((rval != copySize) && (lastFlag != 1)){
				printf("Aborting, Error reading string of data\n");
				return -1;
			}
			obuf += (copySize*unitSize);
			remainingBytes -= (copySize*unitSize);
		}
		else{
			copySize = (int)unit + 2;
			if (copySize*unitSize > remainingBytes){
				copySize = (remainingBytes / unitSize) + 1;
			}
			rval = fread(&unit, unitSize, 1, infile);
			if (rval != 1){
				printf("Aborting, Error reading string of data\n");
				return -1;
			}
			swap16(&unit);
			for (x = 0; x < copySize; x++){
				memcpy(obuf, &unit, unitSize);
				obuf += unitSize;
			}
			remainingBytes -= (copySize*unitSize);
		}
	}

	return 0;
}




/*****************************************************************************/
/* decmpr_32bit - 32-bit CMP Decompression routine.                          */
/* Inputs: ptr to output stream, size of output stream                       */
/* Returns: 0 on success, -1 on failure.                                     */
/*****************************************************************************/
int decmpr_32bit(char* obuf, int decomprSizeBytes){

	int unit, x;
	int rval, copySize;
	int unitSize = 4;
	int lastFlag = 0;
	int remainingBytes = decomprSizeBytes;

	/* While output space exists, continue to attempt decompression */
	while (remainingBytes > 0){

		/* EOF */
		if (feof(infile)){
			printf("Error, premature end of file.\n");
			return -1;
		}

		/* Read 1 unit of data */
		rval = fread(&unit, unitSize, 1, infile);
		if (rval !=	1){
			printf("Aborting, Error reading unit of data\n");
			return -1;
		}
		swap32(&unit);

		/* If negative, direct copy the next #*-1 units */
		/* If positive, copy the next item #+2 times    */
		if (unit < 0){
			copySize = unit * -1;
			if (copySize*unitSize > remainingBytes){
				copySize = (remainingBytes / unitSize) + 1;
				lastFlag = 1;  //Prevent abort due to final read
			}
			rval = fread(obuf, unitSize, copySize, infile);
			if ((rval != copySize) && (lastFlag != 1)){
				printf("Aborting, Error reading string of data\n");
				return -1;
			}
			obuf += (copySize*unitSize);
			remainingBytes -= (copySize*unitSize);
		}
		else{
			copySize = (int)unit + 2;
			if (copySize*unitSize > remainingBytes){
				copySize = (remainingBytes / unitSize) + 1;
			}
			rval = fread(&unit, unitSize, 1, infile);
			if (rval != 1){
				printf("Aborting, Error reading string of data\n");
				return -1;
			}
			swap32(&unit);
			for (x = 0; x < copySize; x++){
				memcpy(obuf, &unit, unitSize);
				obuf += unitSize;
			}
			remainingBytes -= (copySize*unitSize);
		}
	}

	return 0;
}

