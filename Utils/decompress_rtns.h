/*****************************************************************************/
/* compress_rtns.h - Sega CMP Compatible Decompression Routines.             */
/*****************************************************************************/
#ifndef COMPRESS_RTNS_H
#define COMPRESS_RTNS_H

//Defines
#define BYTE_CMP_TYPE  0  //1-byte RLE Pattern Compression
#define SHORT_CMP_TYPE 1  //2-byte RLE Pattern Compression
#define LONG_CMP_TYPE  2  //4-byte RLE Pattern Compression

#define swap16(a)   *a = ((*a >> 8) & 0x00FF) | \
	                     ((*a << 8) & 0xFF00)

#define swap32(a)  *a = ((*a >> 24) & 0x000000FF) | \
	                    ((*a >> 8)  & 0x0000FF00) | \
	                    ((*a << 8)  & 0x00FF0000) | \
					    ((*a << 24) & 0xFF000000)

//Max signed datatype values
#define MAX_S_BYTE		127
#define MAX_S_SHORT		32767
#define MAX_S_LONG		2147483647
#define MIN_S_BYTE		-128    /* Can have uncompressed runs up to 128 */
#define MIN_S_SHORT		-32768
#define MIN_S_LONG		(-2147483647 - 1)

//Fctn Prototypes
int analyzeHeader(const char* inputFname, int* fileOffset, int* dataSizeBytes, int* cmprType);

int cmp_decompress(const char* inputFname, const char* outputFname, int fileOffset, int sizeBytes, int cmprType);

int decmpr_8bit(char* obuf, int decomprSizeBytes);
int decmpr_16bit(char* obuf, int decomprSizeBytes);
int decmpr_32bit(char* obuf, int decomprSizeBytes);

#endif
