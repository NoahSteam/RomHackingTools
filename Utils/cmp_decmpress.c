/*****************************************************************************/
/* cmp_decmpress.c - Program to perform decompression compatible with        */
/*                   Sega's CMP routines (Used by Sega Saturn)               */
/*                                                                           */
/* v1.0  1/07/19   Initial Version                                           */
/*****************************************************************************/


/* Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decompress_rtns.h"

/* Defines */
#define MIN_ARGS  3
#define PROG_VERSION    "1.0"

/* Prototypes */
void printUsage();




/*****************************************************************************/
/* main - Checks input arguments, calls compression routine, puts header on  */
/*        compressed data and outputs to a file.                             */
/*****************************************************************************/
int main(int argc, char** argv){

	static char inputFname[300];
	static char outputFname[300];
	int cmprType, x, rval;
	int fileOffset, dataSizeBytes;

	/* Init */
	cmprType = 0;
	fileOffset = dataSizeBytes = 0;

	printf("cmp_decmpress v%s\n",PROG_VERSION);

        /* Check # of input arguments */
	if(argc < MIN_ARGS){
		printf("Error in number of input arguments\n");
		printUsage();
		return -1;
	}


	/***********************************************************/
	/* Check for help, ignore everything else if help is found */
	/***********************************************************/
	for(x = 1; x < argc; x++){
		if(strcmp(argv[x],"-h") == 0){
			printUsage();
			return 0;
		}
	}

	/**********************************/
	/* Parse Optional Input Arguments */
	/**********************************/
	for(x = 1; x < argc; x++){

		/* File Offset */
		if(strcmp(argv[x],"-f") == 0){
			if(argc > (x+1)){
				x++;
				fileOffset = atoi(argv[x]);
			}
		}

		/* End of optional arguments */
		else
			break;
	}

	/********************************/
	/* Parse Input/Output Filenames */
	/********************************/
	x = argc - 2;
	strcpy(inputFname,  argv[x++]);
	strcpy(outputFname, argv[x]);

        /*****************************************************/
        /* Analyze the header to determine compression type, */
        /* decompressed file size, & validity                */
        /*****************************************************/
        if(analyzeHeader(inputFname, &fileOffset, &dataSizeBytes, &cmprType) < 0){
            printf("Aborting, Error during header analysis!\n");
            return -1;
        }

	/*****************************/
	/* Perform the Decompression */
	/*****************************/
	rval = cmp_decompress(inputFname, outputFname, fileOffset, 
                              dataSizeBytes, cmprType);
	if(rval < 0){
		printf("Error encountered during decompression.\n");
		return -1;
	}

	/* Success */
	printf("Decompression Completed Sucessfully!\n");

	return 0;
}




/*****************************************************************************/
/* printUsage - Displays command line parameters.                            */
/*****************************************************************************/
void printUsage(){

	printf("cmp_decmpress [options] inputFile outputFile\n");
	printf("    Available options:\n");
	printf("      -f offset Byte offset in input file to begin compression.\n");
	printf("      -h        Help, Prints this message.\n");
	return;
}
