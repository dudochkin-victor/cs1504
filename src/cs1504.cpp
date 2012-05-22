/*******************************************************************************
 *
 *  Filename:      TestConsole.cppp
 *
 *  Copyright(c) Symbol Technologies Inc., 2001
 *
 *  Description:     Defines the entry point for the console application.
 *
 *  Author:          Chris Brock.
 *
 *  Creation Date:   ?/?/??
 *
 *  Derived From:    New File
 *
 *  Edit History:
 *   $Log:   U:/keyfob/archives/winfob/examples/msvc/TestConsole.cpV  $
 *
 *    Rev 1.4   Feb 08 2002 11:50:08   pangr
 * Added psuedocode and changed code format
 *
 *    Rev 1.3   Jan 29 2002 14:34:34   pangr
 * Removed Reference to debug.h
 *
 *    Rev 1.2   Jan 29 2002 13:53:14   pangr
 * Header format change. No code change.
 *
 *    Rev 1.1   Jan 25 2002 15:30:50   pangr
 * Initial release.  Added command line paramenter to select the COM port.
 * If none is specified, then COM1 is the default.
 *
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "csp2.h"

#include <unistd.h>
#include <errno.h>

#define COMPORT_ARG		"p"
#define HELP_ARG		"h"

const char *dev = "/dev/ttyUSB0";

static const char *comports[] =
{
	"COM1", "COM2", "COM3", "COM4", "COM5", "COM6","COM7", "COM8", "COM9",
	"COM10","COM11", "COM12", "COM13", "COM14",	"COM15", "COM16"
};

// Displays Help
static void display_help(const char *exename) {

	printf("Usage:\n%s [<%s port>| %s]\nwhere port is COM port COM1..COM16\n"
		"Note: COMX is case sensitive!\n"
		,exename, COMPORT_ARG, HELP_ARG);
}

int SetDTR(int on);
int main(int argc, char* argv[])
{
	int    nRetStatus,AsciiMode,RTC;
	int	PacketLength,BarcodesRead;
	char	Packet[64],aBuffer[256],TimeStamp[32];
	int		i,k;
	long	nComPort = 0;		/* the default comport is 1 */

//	/* Check to see if we have any command line arguments. */
//	if (argc > 1 )
//    {
//    	/* Arguments parsing */
//		for (i=1; i<argc; i++)
//        {
//    		if (!strcmp(COMPORT_ARG, argv[i]))
//            {
//				/* Advance index */
//				i++;
//				/* Check to see what, if any com port was specified. */
//				for (k=0; k < sizeof(comports)/sizeof(*comports); k++)
//                {
//					if (!strcmp(comports[k], argv[i]))
//                    {
//						/* Argument found */
//						nComPort = k;	/* since offset of 1 */
//						break;
//					}
//				}
//				/* Check to see if we didn't find a match */
//				if (k >= sizeof(comports)/sizeof(*comports))
//                {
//					display_help(argv[0]);
//					return 1;
//				}
//			}
//			/* Add additional arguments below...*/
//			else
//            {
//				/* Error in arguments */
//				display_help(argv[0]);
//				return 1;
//			}
//		}
//	}

	/* Enable debug logging */
	csp2SetDebugMode(PARAM_ON);
	int fd = open(dev, O_RDWR /*| O_NOCTTY | O_FSYNC | O_NDELAY*//* | O_NONBLOCK*/);
		if (fd < 0) {
			perror(dev);
			return -1;
		} else
			fcntl(fd, F_SETFL, 0);

	if ((nRetStatus = csp2Init(fd)) != STATUS_OK)
	{
		/* error condition, alert the user of the problem */
		printf( "Comm port not initialized.\nError Status:%d\n",
			nRetStatus);
		return 1;
	}

	nRetStatus = csp2ReadData(); /* Read barcodes */
	if (nRetStatus<0)
	{
		/* error condition, alert the user of the problem */
		printf( "Error reading barcode info.\nError Status: %d",
			nRetStatus);
	}

	else
	{
		/* save number of barcodes read into DLL */
		BarcodesRead = nRetStatus;
		printf("Read %d packets\n", BarcodesRead);

		/* Get Device Id */
		nRetStatus = csp2GetDeviceId(aBuffer, 8);
		printf("Device ID ");
		for (k=0;k<nRetStatus;k++)
			printf(" %02X",(unsigned char) aBuffer[k]);

		/* Get Device Software Revision */
		nRetStatus = csp2GetSwVersion(aBuffer, 9);
		printf("\nDevice SW Version: %s\n", aBuffer);

		/* Get packet type (ASCII/Binary) */
		AsciiMode = csp2GetASCIIMode();
		printf("ASCII Mode = %d",AsciiMode);

		/* Get TimeStamp setting (on/off) */
		RTC=csp2GetRTCMode();
		printf(", RTC Mode = %d\n",RTC);

		for (i=0;i<BarcodesRead;i++)
		{
			PacketLength = csp2GetPacket(Packet,i,63); /* Read packets */
			printf("\nBarcode %d\n", i+1);
			/* print out packet (hex) */
			for (k=0;k<PacketLength;k++) printf(" %02X",(unsigned char) Packet[k]);
			printf("\n");

			if ((PacketLength>0)&& (AsciiMode==PARAM_ON)) /* normal packet processing */
			{
				/* convert ascii mode code type to string */
				nRetStatus = csp2GetCodeType((long)Packet[1],aBuffer,30);
				printf("CodeConversion returns: %d\n",nRetStatus);
				strcat(aBuffer," ");

				if (RTC==PARAM_ON) /* convert timestamp if necessary */
				{
					/* convert binary timestamp to string */
					csp2TimeStamp2Str((unsigned char *) &Packet[PacketLength-4],
									  TimeStamp,30);
					/* append barcode (less timestamp) to codetype */
					/* offset: timestamp=4, len=1, codetype = 1 */
					strncat(aBuffer,Packet+2,PacketLength-6);

					/* append timestamp */
					strcat(aBuffer,TimeStamp);
				}

				/* if no RTC just append barcode to codetype (no timestamp) */
				else strncat(aBuffer,Packet+2,PacketLength-2);

				printf("%s\n",aBuffer);
			}
			else
			{
				/* Process Binary Packet Mode */
			}

		} /* end of read packets loop */
	}
	printf("\nPress return to exit");
	getchar(); /* wait for keyboard to exit */
	return 0;
}


