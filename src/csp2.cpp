/*******************************************************************************
 * Include Files
 *******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csp2.h"                  // function proptotypes for DLL
#include "crccalc.h"               // CRC function prototypes
#include <sys/timeb.h>
#include <iostream>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/poll.h>
using namespace std;

//#define _DEBUG 1

#ifdef _DEBUG
#include <time.h>              // time stamps for debug files
#endif

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

/*******************************************************************************
 * Defines
 *******************************************************************************/
/** Macro **/
#define LONG_2_UCHAR(x)             ((unsigned char)x)
/** local definitions **/

#define STX                         ((char)0x02)    // <stx> character
#define CS1504_PWR_UP_TIME          400             // delay from pwr down to up                       
#define POLL_INTERVAL               500             // 500 ms (.5 second) interval
// Command byte values...
#define INTERROGATE                 ((char)0x01)
#define CLEAR_BAR_CODES             ((char)0x02)
#define DOWNLOAD_PARAMETERS         ((char)0x03)
#define SET_DEFAULTS                ((char)0x04)
#define POWER_DOWN                  ((char)0x05)
#define RESERVED_CODE_6             ((char)0x06)
#define UPLOAD                      ((char)0x07)
#define UPLOAD_PARAMETERS           ((char)0x08)
#define SET_TIME                    ((char)0x09)
#define GET_TIME                    ((char)0x0a)

#define Param_ASCII_Mode            ((char)0x4f)
#define Param_Store_RTC             ((char)0x23)

// CSP device status value...
#define NO_ERROR_ENCOUNTERED        ((long)6)

#define MAXTIME                     ((long)  5)     // 50 millisecond ticks, ~ 1 second
#define MAXSIZE                     ((long)16384)   // maximum size of retrieved barcodes
#define RX_QUE_SIZE                 ((long)1024)    // communications RX buffer setting
#define TX_QUE_SIZE                 ((long) 512)    // communications Tx buffer setting
#define MAX_XMIT_SIZE               ((long) 60)

#define STOP_POLLING_THREAD_RETRY   ((int)11)

/*******************************************************************************
 * Public Variables
 *******************************************************************************/
/* default settings */
int nDelayCount = MAXTIME; // used for interrogate timeouts
long nCspActivePort = -1; // no comm port selected
long nCspDeviceStatus = -1; // no current Device status
long nCspProtocolVersion = -1; // no protocol version available
long nCspSystemStatus = -1; // no system status available

long nCspStoredBarcodes = 0; // number of stored barcode strings
long nCspRetryCount = 5; // default number of interrogation retries

int nCspASCII = -1; // CS1504 ASCII mode status
int nCspRTC = -1; // CS1504 RTC Storage Status

/*******************************************************************************
 * Local Variables
 *******************************************************************************/
/** private data **/
#ifdef _DEBUG
static char szDebugTime[10]; // Time stamp string
#endif

/** CallBack Thread **/
int dwThreadId;
int dwThrdParam = 1;

pthread_t hThread /*= NULL*/;
pthread_t hCallBackDoneEvent/* = NULL*/;

void* csp2PollCallBack = NULL;

bool Polling = false;
bool PollingEnabled = false;

//struct _timeb PwrDwnTime;

/*******************************************************************************
 * Public Function Prototypes
 *******************************************************************************/
//DCB      dcb,OriginalDcb;              // device control blocks for Comm ports
int hCom; // handle for comm port
int dwError; // errors from Win comm API
bool fSuccess; // API call results

//HMODULE hModuleThis;                   // handle to get version info

/*static long nCspVolume; // Device volume setting*/
static char szCspSwVersion[9] = { 0 }; // Device software version
static char szCspDeviceId[9]; // Device ID
static char szCspBarData[MAXSIZE]; // uploaded barcode storage
static char aByteBuffer[MAXSIZE]; // temp storage of CSP device response
BYTEWORD CRCtest; // temp var for CRC checks
static long nDeviceIdLen = 0;

/*******************************************************************************
 * Local Functions Prototypes
 *******************************************************************************/
/** private functions **/
static long csp2Getc(void);
static char csp2LrcCheck(char aLrcBytes[], long nMaxLength);
static void csp2InitParms(void);
static long csp2SendCommand(char *aCommand, long nMaxLength);
static long GetCTS(void);
static long GetParam(int nParam, char szString[], long nMaxLength);

#define TRACE printf

/*
 Packed Time Stamp Structure Reference
 years    :6; // 3.2 - 3.7
 months   :4; // 2.7 - 3.1
 days     :5; // 2.2 - 2.6
 hours    :5; // 1.4 - 2.1
 minutes  :6; // 0.6 - 1.3
 seconds  :6; // 0.0 - 0.5
 */
#define YEAR_MASK	   0x0000003f
#define MONTH_MASK	0x0000000f
#define DAY_MASK	   0x0000001f
#define HOUR_MASK	   0x0000001f
#define MINUTE_MASK	0x0000003f
#define SECOND_MASK	0x0000003f

#define RTC_Get_Years(l)	((unsigned long) l & YEAR_MASK)
#define RTC_Get_Months(l)	(((unsigned long) l >> 6) & MONTH_MASK)
#define RTC_Get_Days(l)		(((unsigned long) l >> 10) & DAY_MASK)
#define RTC_Get_Hours(l)	(((unsigned long) l >> 15) & HOUR_MASK)
#define RTC_Get_Minutes(l)	(((unsigned long) l >> 20) & MINUTE_MASK)
#define RTC_Get_Seconds(l)	(((unsigned long) l >> 26) & SECOND_MASK)

struct CodeTypeStruct {
	unsigned char CodeId;
	char CodeType[40];
};

const CodeTypeStruct CodeTypes[] = { { 0x16, "Bookland" }, { 0x02, "Codabar" },
		{ 0x0c, "Code 11" }, { 0x20, "Code 32" }, { 0x03, "Code 128" }, { 0x01,
				"Code 39" }, { 0x13, "Code 39 Full ASCII" },
		{ 0x07, "Code 93" }, { 0x1d, "Composite" }, { 0x17, "Coupon" }, { 0x04,
				"D25" }, { 0x1b, "Data Matrix" }, { 0x0f, "EAN-128" }, { 0x0b,
				"EAN-13" }, { 0x4b, "EAN-13+2" }, { 0x8b, "EAN-13+5" }, { 0x0a,
				"EAN-8" }, { 0x4a, "EAN-8+2" }, { 0x8a, "EAN-8+5" }, { 0x05,
				"IATA" }, { 0x19, "ISBT-128" },
		{ 0x21, "ISBT-128 Concatenated" }, { 0x06, "ITF" },
		{ 0x28, "Macro PDF" }, { 0x0E, "MSI" }, { 0x11, "PDF-417" }, { 0x26,
				"Postbar (Canada)" }, { 0x1e, "Postnet (US)" }, { 0x23,
				"Postal (Australia)" }, { 0x22, "Postal (Japan)" }, { 0x27,
				"Postal (UK)" }, { 0x1c, "QR Code" }, { 0x31, "RSS Limited" },
		{ 0x30, "RSS-14" }, { 0x32, "RSS Expanded" }, { 0x24, "Signature" }, {
				0x15, "Trioptic Code 39" }, { 0x08, "UPCA" },
		{ 0x48, "UPCA+2" }, { 0x88, "UPCA+5" }, { 0x09, "UPCE" }, { 0x49,
				"UPCE+2" }, { 0x89, "UPCE+5" }, { 0x10, "UPCE1" }, { 0x50,
				"UPCE1+2" }, { 0x90, "UPCE1+5" }

};

unsigned char aInterrogateCmd[] = { INTERROGATE, // Interrogate Command
		STX, // opcode
		0x00, // NULL terminate the message
		0x9f, // CRC HH
		0xde // CRC LL
		};

unsigned char aClearBarCodesCmd[] = { CLEAR_BAR_CODES, // Clear Bar Code Command
		STX, // opcode
		0x00, // NULL terminate the message
		0x9f, // CRC HH
		0x2e // CRC LL
		};

unsigned char aSetDefaultsCmd[] = { SET_DEFAULTS, // Set Defaults Command
		STX, // opcode
		0x01, 0x01, 0x00, // NULL terminate the message
		0xd7, // CRC HH
		0x7b // CRC LL
		};

unsigned char aSetTimeCmd[] = { SET_TIME, // Set Time Command
		STX, // opcode
		0x00, // NULL terminate the message
		0x5d, // CRC HH
		0xaf // CRC LL
		};

unsigned char aGetTimeCmd[] = { GET_TIME, // Get Time Command
		STX, // opcode
		0x00, // NULL terminate the message
		0x5d, // CRC HH
		0xaf // CRC LL
		};

unsigned char aPowerDownCmd[] = { POWER_DOWN, // Power down Command
		STX, // opcode
		0x00, // NULL terminate the message
		0x5e, // CRC HH
		0x9f // CRC LL
		};

unsigned char aUploadCmd[] = { UPLOAD, // Upload Command
		STX, // opcode
		0x00, // NULL terminate the message
		0x9e, // CRC HH
		0x3e // CRC LL
		};

void hexdump(void *ptr, int buflen) {
	unsigned char *buf = (unsigned char*) ptr;
	int i, j;
	for (i = 0; i < buflen; i += 16) {
		printf("%06x: ", i);
		for (j = 0; j < 16; j++)
			if (i + j < buflen)
				printf("%02x ", buf[i + j]);
			else
				printf("   ");
		printf(" ");
		for (j = 0; j < 16; j++)
			if (i + j < buflen)
				printf("%c", isprint(buf[i + j]) ? buf[i + j] : '.');
		printf("\n");
	}
}

/*********************************************************************                      
 * Synopsis:       DWORD WINAPI CallBackThreadFunc(  LPVOID lpParameter )
 *
 * Description:    This is the function associated with the polling thread.
 *             It monitors the incoming handshake and trips the callback
 *             if it sees a CS1504
 *
 * Parameters:     lpParameter is passed from the spawning thread, and is
 *             currently unused.
 *
 * Return Value:   STATUS_OK regardless.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

int CallBackThreadFunc(void * lpParameter) {
	//   TRACE("CallBack Thread Started.\n");
	//
	//    if (csp2PollCallBack != NULL)
	//    {
	//       TRACE("Calling Callback\n");
	//       csp2PollCallBack();
	//       TRACE("Callback done\n");
	//    }
	//
	//    // Kick the PollingThreadFunc wait on my exit.
	//    if (hCallBackDoneEvent != NULL)
	//    {
	////       SetEvent(hCallBackDoneEvent);
	//    }

	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       DWORD WINAPI PollingThreadFunc(  LPVOID lpParameter )
 *
 * Description:    This is the function associated with the polling thread.
 *             It monitors the incoming handshake and trips the callback
 *             if it sees a CS1504
 *
 * Parameters:     lpParameter is passed from the spawning thread, and is
 *             currently unused.
 *
 * Return Value:   STATUS_OK regardless.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

int PollingThreadFunc(void * lpParameter) {
	//    int dwRC;
	//    int dwCallBackThreadId;
	////    HANDLE hCallBackThread;
	//
	//    // Create the event the
	//    hCallBackDoneEvent = CreateEvent(NULL,  // default Security Attributes
	//        false,                              // No manual reset
	//        false,                              // initial state is false
	//        (LPCSTR)"CallBackDone");            // the name
	//
	//
	//    if (NULL == hCallBackDoneEvent)
	//    {
	//        TRACE("Error: hCallBackDoneEvent no created!\n");
	//        return GENERAL_ERROR;
	//    }
	//    TRACE("Event Created... Polling Thread Active...\n");
	//
	//    while (Polling)
	//    {
	//
	//        // Check to see if polling is enabled.
	//        if (PollingEnabled)
	//        {
	//            if (GetCTS()==HS_SET)
	//            {
	//
	//                TRACE("Creating Callback Thread\n");
	//                // Spawn Thread to invoke callback
	//                hCallBackThread = CreateThread(
	//                    NULL,                        // no security attributes
	//                    //0,                           // use default stack size
	//                    100000,                           // use default stack size
	//                    CallBackThreadFunc,          // thread function
	//                    NULL,                        // argument to thread function
	//                    0,                           // use default creation flags
	//                    &dwCallBackThreadId);                // returns the thread identifier
	//
	//
	//                if (NULL != hCallBackThread)
	//                {
	//
	//                    TRACE("Waiting for Callback to finish\n");
	//                    // Wait for thread to exit.
	//                    dwRC = WaitForSingleObjectEx(hCallBackDoneEvent, INFINITE, false);
	//
	//                    if (0 > dwRC)
	//                    {
	//                        TRACE("Error.. Waiting for Single Object %d \n", GetLastError());
	//                    }
	//                    else
	//                    {
	//                        TRACE("Wait completed for callback\n");
	//                    }
	//                }
	//                else
	//                {
	//                    // Thread not created.
	//                    Sleep(50);
	//                }
	//            }
	//        }
	//        Sleep(POLL_INTERVAL);
	//    }
	//    // Close the event handle.
	//    CloseHandle(hCallBackDoneEvent);
	//
	//    TRACE("Polling Thread Stopped...\n");
	return (STATUS_OK);

}

long csp2EnablePolling(void) {
	// for now
	if (!Polling)
		return GENERAL_ERROR;

	PollingEnabled = false;
	return STATUS_OK;
}

long csp2DisablePolling(void) {
	// for now
	if (!Polling)
		return GENERAL_ERROR;

	PollingEnabled = true;
	return STATUS_OK;
}

// Stop polling the 1504
/*********************************************************************                      
 * Synopsis:       long csp2StopPolling(void)
 *
 * Description:    Cancels the seperate polling thread.
 *
 * Parameters:     None.
 *
 * Return Value:   STATUS_OK if ok.
 *             GENERAL_ERROR we had to unnaturally terminate the thread
 *             Positive value is the error if the thread did not return
 *             a status.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2StopPolling(void) {
	//   int i;
	//   int exitcode;
	//
	//   TRACE("Stopping Polling Thread...\n");
	//
	//   if (Polling == false)
	//      return GENERAL_ERROR;
	//
	//   // If we are waiting for the callback to complete... stop waiting
	//   if ( hCallBackDoneEvent != NULL)
	//      SetEvent(hCallBackDoneEvent);
	//
	//   Polling=false;
	//
	//   // NULL the callback.
	//   csp2PollCallBack = NULL;
	//
	//   for (i=0;i<STOP_POLLING_THREAD_RETRY;i++)
	//   {
	//      if (GetExitCodeThread(hThread,&exitcode))
	//      {
	//         if (exitcode==STILL_ACTIVE)
	//         {
	////            Sleep(50);
	//         }
	//         else
	//         {
	//            TRACE("Thread exited OK......\n");
	////            CloseHandle(hThread);
	//            return(STATUS_OK);
	//         }
	//      }
	//      else
	//      {
	//         // something bad happened
	//         TRACE("Bad Exit code for Thread...\n");
	////         exitcode=GetLastError();
	////         TerminateThread(hThread,NULL);
	////         CloseHandle(hThread);
	//         return(exitcode);
	//      }
	//   }
	//
	//   TRACE("We had to kill the thread...\n");
	//   // Thread still running, but should have ended. Terminate it.
	////   TerminateThread(hThread,NULL);
	////   CloseHandle(hThread);
	return (GENERAL_ERROR);
}

/*********************************************************************                      
 * Synopsis:       long csp2StartPolling
 *              (FARPROC csp2CallBack)
 *
 * Description:    This function spawns a new thread to poll for a
 *             CS1504. The callback address allows the thread to
 *                 inform the caller that a device has been detected.
 *
 * Parameters:     FARPROC csp2CallBack is the address of the callers
 *             callback function
 *
 * Return Value:   STATUS_OK if ok.
 *             GENERAL_ERROR if already polling
 *             BAD_PARAM if invalid callback provided
 *             SETUP_ERROR if problem occurred spawning the thread
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

// Set up to poll the 1504
long csp2StartPolling(void * csp2CallBack) {
	long retval;

	retval = STATUS_OK;
	//   if (Polling)
	//      return(GENERAL_ERROR); // already polling
	//
	//   if (NULL == csp2CallBack)
	//      return(BAD_PARAM);
	//
	//   // Assign the callback.
	//   csp2PollCallBack = csp2CallBack;
	//   Polling = true;
	//   PollingEnabled = true;
	//
	//   TRACE("Creating Thread to Start Polling for cs1504\n");
	//
	//   // Create a thread to poll for CS1504
	//   hThread = CreateThread(
	//        NULL,                        // no security attributes
	//        0,                           // use default stack size
	//        PollingThreadFunc,           // thread function
	//        &dwThrdParam,                // argument to thread function
	//        0,                           // use default creation flags
	//        &dwThreadId);                // returns the thread identifier
	//   // Check the return value for success.
	//
	//   if (hThread == NULL)
	//   {
	//      retval = SETUP_ERROR;
	//      Polling = false;
	//      PollingEnabled = false;
	//      CloseHandle( hThread );
	//   }
	return (retval);
}

/*********************************************************************                      
 * Synopsis:       static long csp2ReadBytes(char *aBuffer, int nBytes)
 *
 * Description:    This function reads nBytes from the CS1504 using the
 *                 csp2Getc function.
 *
 * Parameters:     aBuffer(out) - buffer where data is copied to
 *                 nBytes(in) - Number of bytes to copy
 *
 * Return Value:   number of characters read.
 *                 COMMUNICATIONS_ERROR if no character was read
 *                 BAD_PARAM if aBuffer was NULL.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

static long csp2ReadBytes(char *aBuffer, int nBytes) {
	long i;
	long nValue;

	// Check to see if the buffer is valid.
	if (aBuffer == NULL)
		return SETUP_ERROR;

	for (i = 0; i < nBytes; i++) {
		if ((nValue = csp2Getc()) < 0)
			return COMMUNICATIONS_ERROR;

		aBuffer[i] = LONG_2_UCHAR(nValue);
	}

	return i;
}

/*********************************************************************                      
 * Synopsis:       static long csp2Getc(void)
 *
 * Description:    This function retrieves a character from the
 *                 active serial port. If the serial port does not
 *                 have a character within the MAXTIME period, the
 *                 function returns with a COMMUNICATIONS_ERROR.
 *
 * Parameters:     None
 *
 * Return Value:   character read (zero or positive value)
 *                 COMMUNICATIONS_ERROR if no character was read
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:          Return is type long!!!.
 *                 Use LONG_2_UCHAR to get byte
 *                 value if the greater than equal to zero.
 **********************************************************************/
// 0x06
// 0x02
// 0x31
// 0x00
// 0x00 0x02 0x00 0x00 0x00 0x0E 0x1A 0x76
// 0x4E 0x42 0x52 0x49 0x4B 0x41 0x41 0x45
// 0x00
// 0x62 0x21
static long csp2Getc(void) {
	long i;
	unsigned char chRead;
	int BytesRead;
	int bRC;

	// a port is selected, can we retrieve any data from it?
	for (i = 0; i < nDelayCount; i++) {
		// try to read a character...
		BytesRead = 0;
		// Attempt to read 1 byte from the stream.
		bRC = read(hCom, &chRead, 1);

		// Check if ReadFile was successfull
		if (bRC > 0) {
#ifdef _DEBUG
			TRACE("R[0x%02X] ", (unsigned char) chRead);
#endif
			return ((long) chRead);
		} else if (bRC == 0) {
			return -2;
		} else {
			// Error
			return COMMUNICATIONS_ERROR;
		}

		// let other tasks run while we wait for a character...
		usleep(50000);
	}
	//   }
	TRACE("\n[device time out]\n");

	// if we make it this far, no character was read. So...
	return COMMUNICATIONS_ERROR;
}

/*********************************************************************                      
 * Synopsis:       static char csp2LrcCheck(char aLrcBytes[], long nMaxLength)
 *
 * Description:    THIS ROUTINE IS KEPT FOR POTENTIAL FUTURE USE WITH CS2000 DEVICES
 *                 Computes the Longitudinal Redundancy Check on a
 *                 string of specified length.
 *
 * Parameters:     aLrcBytes[] contains the bytes used for computing the LRC
 *                 nMaxLength determines how many bytes of aLrcBytes[] are used
 *                 to perform LRC checking.
 *
 * Return Value:    The calculated LRC is returned.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

static char csp2LrcCheck(char aLrcBytes[], long nMaxLength) {
	long i;
	unsigned char chLrc = 0; // initialize LRC value

	// compute the LRC for the entire string...
	for (i = 0; i < nMaxLength; i++)
		chLrc ^= aLrcBytes[i];

	return (chLrc);
}

/*********************************************************************                      
 * Synopsis:        static void csp2InitParms(void)
 *
 * Description:     Invalidates all values used by this DLL. Used
 *                  to indicate that previously stored data is no
 *                  longer valid, possibly because of a new comm
 *                  port setting.
 *
 * Parameters:      None
 *
 * Return Value:    None
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

static void csp2InitParms(void) {
	nCspActivePort = -1; // no comm port selected
	nCspDeviceStatus = -1; // no current Device status
	nCspProtocolVersion = -1; // no protocol version available
	nCspSystemStatus = -1; // no system status available

	nCspASCII = -1; // CS1504 ASCII mode status unavailable
	nCspRTC = -1; // CS1504 RTC Storage Status unavailable

	nCspStoredBarcodes = 0; // number of stored barcode strings
}

/*********************************************************************                      
 * Synopsis:        static long csp2SendCommand (char *aCommand, long nMaxLength)
 *
 * Description:     This function handles the basic overhead associated
 *                  with sending the CSP device a command. Examples of
 *                  commands for the CSP device are:
 *                      Interrogation
 *                      Read Barcodes
 *                      Clear Barcodes
 *                      Power Down
 *                      Get parameter
 *                      Set parameter
 *
 *                  aByteBuffer[0] contains the first data byte read
 *                  from the CSP device.
 *
 * Parameters:      aCommand[] is an array of command bytes to send
 *                  to the CSP device.
 *                  nMaxLength specifies how many bytes in aCommand[]
 *                  are to be sent to the CSP device.
 *
 * Return Value:    STATUS_OK
 *                  COMMUNICATIONS_ERROR
 *                  INVALID_COMMAND_NUMBER
 *                  COMMAND_LRC_ERROR
 *                  RECEIVED_CHARACTER_ERROR
 *                  GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

static long csp2SendCommand(char *aCommand, long nMaxLength) {
	//   DWORD nBytesWritten;
	//
	//   // make sure we have a valid port selected...
	//   if (nCspActivePort < COM1)
	//      return( COMMUNICATIONS_ERROR );
	//
	// delay before sending message
	usleep(200000);
	//
	//   // clear the receive queue...
	//   fSuccess = PurgeComm(hCom,PURGE_RXCLEAR);
	//   if (!fSuccess)
	//      return(COMMUNICATIONS_ERROR);
	//
	//   // send the command string...
	//   fSuccess = WriteFile(hCom,aCommand,nMaxLength,&nBytesWritten,NULL);
	//   if (!fSuccess)
	//      return(COMMUNICATIONS_ERROR);
	//
	/*int s = */write(hCom, aCommand, nMaxLength);
	//printf("SEND: %d \n", s);
	//hexdump(aCommand, nMaxLength);
	TRACE("\n    STATUS:           ");
	// Get status return
	if ((nCspDeviceStatus = csp2Getc()) < 0)
		return COMMUNICATIONS_ERROR;

	aByteBuffer[0] = LONG_2_UCHAR(nCspDeviceStatus);

	if (nCspDeviceStatus != NO_ERROR_ENCOUNTERED)
		return (nCspDeviceStatus);
	else
		return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long GetCTS(void)
 *
 * Description:    Get status of the CTS line
 *
 * Parameters:     None
 *
 * Return Value:   <0 for error, 0 for clear, 1 for set

 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

static long GetCTS(void) {
	//   DWORD ModemStat;
	//
	//   if (hCom == INVALID_HANDLE_VALUE)
	//      return(COMMUNICATIONS_ERROR);
	//
	//   fSuccess = GetCommModemStatus(hCom,&ModemStat);
	//   if (!fSuccess)
	//      return(COMMUNICATIONS_ERROR);
	//   if (ModemStat & MS_CTS_ON)
	//      return(HS_SET);
	//   else
	return (HS_CLEAR);

}

/*********************************************************************                      
 * Synopsis:       long GetDTR(void)
 *
 * Description:    Get status of DTR output line
 *
 * Parameters:     None
 *
 * Return Value:   <0 for error, 0 for clear, 1 for set
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long GetDTR() {
	int serial;
	int retval = ioctl(hCom, TIOCMGET, &serial);
	if (retval == -1)
		return COMMUNICATIONS_ERROR;
	if (serial & TIOCM_DTR) {
		return HS_CLEAR;
	} else {
		return HS_SET;
	}
}

/*********************************************************************                      
 * Synopsis:       long SetDTR(long nOnOff)
 *
 * Description:    Set the DTR Output line
 *
 * Parameters:
 *
 * Return Value:   0 for success, <0 for error
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

int SetDTR(int on) {
	int controlbits = TIOCM_DTR;
	cout << "SET " << on << endl;
	if (on)
		return (ioctl(hCom, TIOCMBIC, &controlbits));
	else
		return (ioctl(hCom, TIOCMBIS, &controlbits));

}

/*********************************************************************                      
 * Synopsis:       long csp2WakeUp(void)
 *
 * Description:    If DTR is set, clears it momentarily, then sets it again.
 *                 Otherwise just set it.
 *
 * Parameters:
 *
 * Return Value:
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2WakeUp(void) {
	int retval/*, k, i*/;
	//	struct _timeb now;

	// See if DTR is active
	retval = GetDTR();
	cout << "RETVAL " << retval << endl;
	if (retval < 0)
		return (COMMUNICATIONS_ERROR);
	if (retval > 0) {
		// DTR is currently set, so clear it
		//		retval = SetDTR(HS_CLEAR);
		//		if (retval < 0)
		//			return (COMMUNICATIONS_ERROR);

		// delay a little before setting DTR
		usleep(175000);
	}

	//	_ftime(&now);
	//	if ((now.time - PwrDwnTime.time) < 2) {
	//		k = now.millitm;
	//		if (now.time != PwrDwnTime.time)
	//			k += 1000;
	//		i = CS1504_PWR_UP_TIME - (k - PwrDwnTime.millitm);
	//
	//			TRACE("\n\nWakeUp Timing Info: %d.%d %d.%d %d.\n", now.time,
	//					now.millitm, PwrDwnTime.time, PwrDwnTime.millitm, i);
	//
	//		if (i > 0)
	//			Sleep(i);
	//	}
	//	retval = SetDTR(HS_SET);
	//	if (retval < 0)
	//		return (COMMUNICATIONS_ERROR);
	//	else
	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long csp2GetCTS(void)
 *
 * Description:
 *
 * Parameters:
 *
 * Return Value:   <0 : Error.  0 : CTS is clear.  >0 : CTS is set.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetCTS(void) {
	return (GetCTS());
}

/*********************************************************************                      
 * Synopsis:       long csp2SetDTR(long nOnOff)
 *
 * Description:
 *
 * Parameters:
 *
 * Return Value:
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2SetDTR(long nOnOff) {
	int retval;

	if (nOnOff)
		retval = SetDTR(HS_SET);
	else
		retval = SetDTR(HS_CLEAR);
	return ((long) retval);

}

/*********************************************************************                      
 * Synopsis:
 *
 * Description:    This routine assumes that the DTR line is CLEAR. If it is not,
 *                 the device may have already timed out and gone to sleep.
 *
 * Parameters:
 *
 * Return Value:  0 for no data available, >0 for data available.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2DataAvailable(void) {
	int retval;

	//	// make sure we have a valid port selected...
	//	if (nCspActivePort < COM1)
	//		return (COMMUNICATIONS_ERROR);

	// See if DTR is active
	retval = GetDTR();
	if (retval < 0)
		return (COMMUNICATIONS_ERROR);
	if (retval == 0) {
		retval = SetDTR(HS_SET);
		if (retval < 0)
			return (COMMUNICATIONS_ERROR);
		// wait a little before checking line.
		usleep(175000);
	}

	// Get the data ready line (CTS) status
	// retval=SioCTS(nCspActivePort);
	retval = GetCTS();

	return (retval);

}

/*********************************************************************
 * Synopsis:        long csp2Init(long nComPort)
 *
 * Description:    This function tries to open the communications port
 *                 specified by nComPort. f it is successful, the
 *                 port is configured for CSP device compatibility.
 *
 *                 This provides a single call to initialize the CSP
 *                 commnications and all of the software structures
 *                 for the CSP interface. The intialization consists of:
 *                    1. Initialize all memory structures, queues, and pointers
 *                    2. Establish serial communications with the specified port
 *
 * Parameters:     nComPort specifies a communications port such as
 *                 COM1, Com2, etc.
 *
 * Return Value:   STATUS_OK if the specified nComPort is now active
 *                 BAD_PARAM if nComPort is invalid
 *                 COMMUNICATIONS_ERROR if nCommPort could not be activated
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

struct termios oldtio;

int csp2Init(int serial_fd) {
	int bits;
	struct termios options;
	//	const char Ports[16][10] = { "COM1", "COM2", "COM3", "COM4", "COM5",
	//			"COM6", "COM7", "COM8", "COM9", "\\\\.\\COM10", "\\\\.\\COM11",
	//			"\\\\.\\COM12", "\\\\.\\COM13", "\\\\.\\COM14", "\\\\.\\COM15",
	//			"\\\\.\\COM16" };
	//
	//	COMMTIMEOUTS TimeOuts;
	//
	TRACE("Starting cs1504 communction\n");
	hCom = serial_fd;
	//
	//	// is the user requesting a valid port?
	//	if ((nComPort >= COM1) && (nComPort <= COM16)) {
	//		// close any previously opened ports...
	//		if (nCspActivePort >= COM1)
	//			csp2Restore();//CloseHandle(hCom);// should restore here
	//
	//		memset(&PwrDwnTime, 0, sizeof(PwrDwnTime));
	//		hCom = CreateFile(Ports[nComPort], GENERIC_READ | GENERIC_WRITE, 0, // comm devices must be opened w/exclusive-access
	//				NULL, // no security attributes
	//				OPEN_EXISTING, // comm devices must use OPEN_EXISTING
	//				0, // not overlapped I/O
	//				NULL // hTemplate must be NULL for comm devices
	//				);
	//
	//		if (hCom == INVALID_HANDLE_VALUE)
	//			return (COMMUNICATIONS_ERROR);
	//
	//		// Set buffer sizes
	//
	//		fSuccess = SetupComm(hCom, RX_QUE_SIZE, TX_QUE_SIZE);
	//		if (!fSuccess) {
	//			// Handle the error.
	//			CloseHandle( hCom);
	//			hCom = INVALID_HANDLE_VALUE;
	//			return (COMMUNICATIONS_ERROR);
	//		}
	//
	//		// Omit the call to SetupComm to use the default queue sizes.
	//		// Get the current configuration.
	//
	//		// Save original DCB
	//		/*
	//		 fSuccess = GetCommState(hCom, &OriginalDcb);
	//		 if (!fSuccess)
	//		 {
	//		 // Handle the error.
	//		 CloseHandle(hCom);
	//		 hCom = INVALID_HANDLE_VALUE;
	//		 return(COMMUNICATIONS_ERROR);
	//		 }
	//		 */
	//		// ok, now set up for our purposes
	//		fSuccess = GetCommState(hCom, &dcb);
	//
	//		if (!fSuccess) {
	//			// Handle the error.
	//			CloseHandle( hCom);
	//			hCom = INVALID_HANDLE_VALUE;
	//			return (COMMUNICATIONS_ERROR);
	//		}
	//
	//		memcpy(&OriginalDcb, &dcb, sizeof(dcb)); // save original dcb
	//		// Fill in the DCB: baud=9600, 8 data bits, no parity, 1 stop bit.
	//
	//		dcb.BaudRate = 9600;
	//		dcb.ByteSize = 8;
	//		dcb.Parity = ODDPARITY;
	//		dcb.StopBits = ONESTOPBIT;
	//		dcb.fDtrControl = DTR_CONTROL_ENABLE;
	//		dcb.fOutxCtsFlow = false;
	//		dcb.fDsrSensitivity = false;

	fcntl(hCom, F_SETOWN, getpid());

	tcgetattr(hCom, &oldtio); /* save current port settings */
	//	tcgetattr(hCom, &options);
	options.c_lflag &= ~(ICANON | ECHO);
	options.c_cflag &= ~(CRTSCTS | CSTOPB | CSIZE | HUPCL);
	options.c_cflag |= (PARENB | PARODD);
	options.c_cflag |= (CS8 | CLOCAL | CREAD);
	options.c_iflag |= (IGNBRK | IGNPAR | INPCK | IXANY);
	options.c_iflag &= ~(IXON | IXOFF | BRKINT | IGNCR | ICRNL | INLCR | ISTRIP
			| IUCLC | PARMRK);
	options.c_oflag |= (OPOST);
	options.c_oflag &= ~(OCRNL | OFILL | OLCUC | ONLCR | ONLRET | ONOCR);
	options.c_cc[VMIN] = 0;
	options.c_cc[VTIME] = 10;

	cfsetispeed(&options, B9600);
	cfsetospeed(&options, B9600);

	tcflush(hCom, TCIOFLUSH);
	tcsetattr(hCom, TCSANOW, &options);

	bits = TIOCM_DTR;
	ioctl(hCom, TIOCMBIC, &bits);
	bits = TIOCM_RTS;
	ioctl(hCom, TIOCMBIS, &bits);

	tcflush(hCom, TCIOFLUSH);

	int iFlags = TIOCM_DTR;
	ioctl(hCom, TIOCMBIS, &iFlags);
	int status;

	ioctl(hCom, TIOCMGET, &status);

	//		fSuccess = SetCommState(hCom, &dcb);
	//
	//		if (!fSuccess) {
	//			// Handle the error.
	//			CloseHandle( hCom);
	//			hCom = INVALID_HANDLE_VALUE;
	//			return (COMMUNICATIONS_ERROR);
	//		}
	//
	//		/* !!!  A note on Comm TimeOuts  !!!
	//		 A value of MAXDWORD for ReadIntervalTimeout, combined with zero
	//		 values for both the ReadTotalTimeoutConstant and
	//		 ReadTotalTimeoutMultiplier members, specifies that the read
	//		 operation is to return immediately with the characters that have
	//		 already been received, even if no characters have been received.
	//		 */
	//		fSuccess = GetCommTimeouts(hCom, &TimeOuts);
	//
	//		if (!fSuccess) {
	//			// Handle the error.
	//			CloseHandle( hCom);
	//			hCom = INVALID_HANDLE_VALUE;
	//			return (COMMUNICATIONS_ERROR);
	//		}
	//
	//		TimeOuts.ReadIntervalTimeout = MAXDWORD;
	//		TimeOuts.ReadTotalTimeoutConstant = NULL;
	//		TimeOuts.ReadTotalTimeoutMultiplier = NULL;
	//
	//		fSuccess = SetCommTimeouts(hCom, &TimeOuts);
	//
	//		if (!fSuccess) {
	//			// Handle the error.
	//			CloseHandle( hCom);
	//			hCom = INVALID_HANDLE_VALUE;
	//			return (COMMUNICATIONS_ERROR);
	//		}
	//
	//		nCspActivePort = nComPort;
	//
	return (STATUS_OK);
	//	}

	return (BAD_PARAM);
}

/*********************************************************************                      
 * Synopsis:        long csp2Restore()
 *
 * Description:    This function attempts to close open connections
 *                 to the serial port specified by nCspActivePort.
 *
 * Parameters:     None
 *
 * Return Value:   STATUS_OK if the port was closed
 *                 COMMUNICATIONS_ERROR if the connection was never
 *                 made or cannot be closed
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2Restore(void) {
	//	long nRetStatus = COMMUNICATIONS_ERROR;
	//	fSuccess = NULL;
	//	// are we attempting to close a valid port?
	//	if (nCspActivePort >= COM1) {
	//
	//		// Stop any polling
	//		(void) csp2StopPolling();
	//
	//		TRACE("\nClosing ComPort %d.\n", nCspActivePort + 1);
	//		// close any previously opened ports...
	//		if (hCom != INVALID_HANDLE_VALUE) {
	//			fSuccess = SetCommState(hCom, &OriginalDcb);
	//		}
	//		fSuccess = CloseHandle(hCom);
	//		hCom = INVALID_HANDLE_VALUE;
	//	}
	//
	//	// initialize the dll interface...
	//	csp2InitParms();
	//
	//	// return status...
	//	if (!fSuccess)
	//		return (COMMUNICATIONS_ERROR);
	//	else
	return (STATUS_OK);
}

/*********************************************************************
 * Synopsis:        long csp2ReadData()
 *
 * Description:    This function reads the following information stored
 *                  in the CSP device:
 *                      All Barcodes
 *                      Device ID
 *                      Signature
 *
 *                  The information is stored within the DLL. In order
 *                  to retreive the information, the user must call the
 *                  corresponding functions listed below:
 *                      csp2GetPacket()
 *                      csp2GetDeviceId()
 *                      csp2GetSignature()
 *
 * Parameters:     None
 *
 * Return Value:   Number of stored barcode strings if no error, otherwise:
 *                 COMMUNICATIONS_ERROR
 *                 INVALID_COMMAND_NUMBER
 *                 COMMAND_LRC_ERROR
 *                 RECEIVED_CHARACTER_ERROR
 *                 GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2ReadData(void) {
	long nRetStatus;
	char ParamValue[2];

	csp2SetDebugMode(1); // was 0!

	TRACE("\nIn csp2ReadData\n");
	// wake up the device
	nRetStatus = csp2WakeUp();
	// if response not available, reply with error
	if (nRetStatus < 0) {
		TRACE("failed wakeup!\n");
		return (nRetStatus);
	}

	// Interrogate the device
	nRetStatus = csp2Interrogate();
	// if response not available, reply with error
	if (nRetStatus < 0) {
		TRACE("\nfailed interrogate!\n");
		return (nRetStatus);
	}
	// Get Ascii Mode setting
	nRetStatus = GetParam(Param_ASCII_Mode, ParamValue, 1);
	if (nRetStatus < 0) {
		TRACE("failed GetParam1!\n");
		return (nRetStatus);
	}
	nCspASCII = (int) ParamValue[0];

	// Get TimeStamp Setting
	nRetStatus = GetParam(Param_Store_RTC, ParamValue, 1);
	if (nRetStatus < 0) {
		TRACE("failed GetParam2!\n");
		return (nRetStatus);
	}
	nCspRTC = (long) ParamValue[0];

	TRACE("\nAscii Mode: %d, RTC Mode %d.\n", nCspASCII, nCspRTC);

	// read the data from the CSP device
	nRetStatus = csp2ReadRawData(aByteBuffer, DETERMINE_SIZE);

	// if response not available, reply with error
	if (nRetStatus < 0) {
		TRACE("failed ReadRawData\n");
		return (nRetStatus);
	}
	// shut down the device
	if ((nRetStatus = csp2PowerDown()) < 0) {
		TRACE("failed shutdown\n");
		return nRetStatus;
	}
	return (nCspStoredBarcodes);
}

/*********************************************************************
 * Synopsis:        long csp2ClearData()
 *
 * Description:     Clear all of the bar codes stored in the CSP device.
 *
 * Parameters:
 *
 * Return Value:    STATUS_OK
 *                  COMMUNICATIONS_ERROR
 *                  INVALID_COMMAND_NUMBER
 *                  COMMAND_LRC_ERROR
 *                  RECEIVED_CHARACTER_ERROR
 *                  GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2ClearData(void) {
	long i;
	long nRetStatus;

	// wake up the device
	nRetStatus = csp2WakeUp();
	// if response not available, reply with error
	if (nRetStatus < 0)
		return (nRetStatus);

	// Interrogate the device
	nRetStatus = csp2Interrogate();
	// if response not available, reply with error
	if (nRetStatus < 0)
		return (nRetStatus);

	// send the command to the CSP device...
#ifdef _DEBUG
	TRACE("\n\nClear Barcodes: [Time: %s]\n", szDebugTime);
#endif
	nRetStatus = csp2SendCommand((char *) aClearBarCodesCmd,
			sizeof(aClearBarCodesCmd));

	if (nRetStatus != STATUS_OK)
		return (nRetStatus);

	// Response looks good, get the entire message...
	i = 1;

	// get the STX character...
	TRACE("\n    STX:              ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;
	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// get the NULL character...
	TRACE("\n    NULL:             ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;
	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// verify the CRC...
	TRACE("\n    CRC:              ");
	if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
		return nRetStatus;

	if (VerifyCRC((unsigned char *) aByteBuffer, i) != true)
		return (COMMAND_LRC_ERROR);

	if ((nRetStatus = csp2PowerDown()) < 0)
		return nRetStatus;

	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long csp2SetDefaults(void)
 *
 * Description:    Sets defaults on the device
 *
 * Parameters:     None
 *
 * Return Value:   STATUS_OK
 *                 COMMUNICATIONS_ERROR
 *                 INVALID_COMMAND_NUMBER
 *                 COMMAND_LRC_ERROR
 *                 RECEIVED_CHARACTER_ERROR
 *                 GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2SetDefaults(void) {
	long i;
	long nRetStatus;
	long nCSLength;

	// wake up the device
	nRetStatus = csp2WakeUp();
	// if response not available, reply with error
	if (nRetStatus < 0)
		return (nRetStatus);

	// Interrogate the device
	nRetStatus = csp2Interrogate();
	// if response not available, reply with error
	if (nRetStatus < 0)
		return (nRetStatus);

	// send the command to the CSP device...
#ifdef _DEBUG
	TRACE("\n\nSet Defaults: [Time: %s]\n", szDebugTime);
#endif
	nRetStatus = csp2SendCommand((char *) aSetDefaultsCmd,
			sizeof(aSetDefaultsCmd));

	if (nRetStatus != STATUS_OK)
		return (nRetStatus);

	// Response looks good, get the entire message...
	i = 1;

	// get the STX character...
	TRACE("\n    STX:              ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;

	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// get counted string response

	nCSLength = nRetStatus = csp2Getc();

	// Look for the NULL to terminate while
	while (nCSLength > 0) {
		// Get and store the counted string length
		aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

		// Read nCSLength bytes from the stream
		nRetStatus = csp2ReadBytes(&aByteBuffer[i], nCSLength);

		// Check to see if there was an error.
		if (nRetStatus < 0)
			return nRetStatus;

		// Adjust index.
		i += nRetStatus;

		// Get the next length;
		nCSLength = nRetStatus = csp2Getc();
	}

	// Check to see if there is an error
	if (nRetStatus < 0)
		return nRetStatus;

	// OK.. copy the last length byte of zero.
	aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

	// verify the CRC...
	TRACE("\n    CRC:              ");
	if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
		return nRetStatus;

	if (VerifyCRC((unsigned char *) aByteBuffer, i) != true)
		return (COMMAND_LRC_ERROR);

	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long csp2SetTime(unsigned char aTimeBuf[])
 *
 * Description:    Sets the current time on the device
 *
 * Parameters:     aTimeBuf[] is the array where the 6 bytes of time data are taken.
 *
 * Return Value:   STATUS_OK
 *                 COMMUNICATIONS_ERROR
 *                 INVALID_COMMAND_NUMBER
 *                 COMMAND_LRC_ERROR
 *                 RECEIVED_CHARACTER_ERROR
 *                 GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2SetTime(unsigned char aTimeBuf[]) {
	long nRetStatus;
	long i;
	long nCSLength;

	if (aTimeBuf == NULL)
		return (SETUP_ERROR);

	// wake up the device
	nRetStatus = csp2WakeUp();
	// if response not available, reply with error
	if (nRetStatus < 0)
		return (nRetStatus);

	// see if the CSP device is connected...
	if ((nRetStatus = csp2Interrogate()) != STATUS_OK)
		return (nRetStatus);

	// build the download packet...
	i = 0;
	aByteBuffer[i++] = SET_TIME;
	aByteBuffer[i++] = STX;

	// set the length of the counted string...
	aByteBuffer[i++] = 0x06;

	// copy the string into the buffer...
	memcpy(&aByteBuffer[i], aTimeBuf, 6);
	i += 6;

	// null terminate all strings, assumed
	aByteBuffer[i++] = 0x00;

	// perform CRC Check on the string...

	CRCtest.w = ComputeCRC16((unsigned char *) aByteBuffer, i);
	aByteBuffer[i++] = CRCtest.b.hi;
	aByteBuffer[i++] = CRCtest.b.lo;

#ifdef _DEBUG
	time_t t;
	struct tm *tmp;
	t = time(NULL);
	tmp = localtime(&t);
	strftime(szDebugTime, 10, "%T", tmp); // Get time stamp string

	TRACE("\n\nSet Time(SystemTime) %s\n", szDebugTime);
	for (int k = 0; k < 6; k++)
	TRACE("0x%X:", aTimeBuf[k]);

	TRACE("\n");
#endif
	nRetStatus = csp2SendCommand(aByteBuffer, i);

	if (nRetStatus != STATUS_OK)
		return (nRetStatus);

	// Response looks good, get the entire message...
	i = 1;

	// get the STX character...
	TRACE("\n    STX:              ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;

	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// read in the counted string until a NULL occurs...
	// first byte "j" is the length of the counted string...
	TRACE("\n    Counted String:   ");

	nCSLength = nRetStatus = csp2Getc();

	// Look for the NULL to terminate while
	while (nCSLength > 0) {
		// Store the counted string length
		aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

		// Read nCSLength bytes from the stream
		// Including the time character...
		// read the time value into aByteBuffer[] upto nCSLength characters...
		nRetStatus = csp2ReadBytes(&aByteBuffer[i], nCSLength);

		// Check to see if there was an error.
		if (nRetStatus < 0)
			return nRetStatus;

		// Adjust index.
		i += nRetStatus;
#ifdef _DEBUG
		TRACE("\n                      ");
#endif
		nCSLength = nRetStatus = csp2Getc();
	}

	// Check to see if failed the read..
	if (nRetStatus < 0)
		return nRetStatus;

	// OK.. we didn't fail the read. Store the last zero length;
	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// Read the CRC Bytes.
	// verify the CRC...
	TRACE("\n    CRC:              ");
	if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
		return nRetStatus;
	if (VerifyCRC((unsigned char *) aByteBuffer, i) != true)
		return (COMMAND_LRC_ERROR);

	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long csp2GetTime(unsigned char aTimeBuf[])
 *
 * Description:    Get the current time from the device
 *
 * Parameters:     aTimeBuf[] is the array where data will be copied after it
 *                 is read out of the Device. THIS ARRAY IS ASSUMED TO BE AT
 *                 LEAST 6 BYTES IN LENGTH.
 *
 * Return Value:   STATUS_OK
 *                 COMMUNICATIONS_ERROR
 *                 INVALID_COMMAND_NUMBER
 *                 COMMAND_LRC_ERROR
 *                 RECEIVED_CHARACTER_ERROR
 *                 GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetTime(unsigned char aTimeBuf[]) {
	long i;
	long nRetStatus;

	if (aTimeBuf == NULL)
		return (SETUP_ERROR);

	// wake up the device
	nRetStatus = csp2WakeUp();

	// if response not available, reply with error
	if (nRetStatus < 0)
		return (nRetStatus);

	// Interrogate the device
	nRetStatus = csp2Interrogate();
	// if response not available, reply with error

	if (nRetStatus < 0)
		return (nRetStatus);

	// send the command to the CSP device...

#ifdef _DEBUG
	TRACE("\n\nGetTime: [Time: %s]\n", szDebugTime);
#endif
	nRetStatus = csp2SendCommand((char *) aGetTimeCmd, sizeof(aGetTimeCmd));

	if (nRetStatus != STATUS_OK)
		return (nRetStatus);

	// Response looks good, get the entire message...
	i = 1;

	// get the STX character...
	TRACE("\n    STX:              ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;

	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// get the count (6) character...
	TRACE("\n    0x06:             ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;

	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// Not sure ???
	aTimeBuf[0] = 'a';

	nRetStatus = csp2ReadBytes((char*) &aTimeBuf[0], 6);

	if (nRetStatus < 0)
		return nRetStatus;

	memcpy(&aByteBuffer[i], aTimeBuf, 6);
	i += 6;

	TRACE("\n    NULL:             ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;

	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// verify the CRC...
	TRACE("\n    CRC:              ");
	if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
		return nRetStatus;
	if (VerifyCRC((unsigned char *) aByteBuffer, i) != true)
		return (COMMAND_LRC_ERROR);

	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long csp2PowerDown()
 *
 * Description:    Direct the Device to end communication and enter
 *                 STOP mode until the next trigger action. This function
 *                 returns on or before the Device issues an audible signal
 *                 confirming the power down command.
 *
 * Parameters:
 *
 * Return Value:  STATUS_OK
 *                COMMUNICATIONS_ERROR
 *                INVALID_COMMAND_NUMBER
 *                COMMAND_LRC_ERROR
 *                RECEIVED_CHARACTER_ERROR
 *                GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2PowerDown(void) {
	long i;
	long nRetStatus;

	// Interrogate the device
	nRetStatus = csp2Interrogate();
	// if response not available, reply with error
	if (nRetStatus < 0)
		return (nRetStatus);

	// send the command to the CSP device...

#ifdef _DEBUG
	TRACE("\n\nPower Down: [Time: %s]\n", szDebugTime);
#endif
	nRetStatus = csp2SendCommand((char *) aPowerDownCmd, sizeof(aPowerDownCmd));

	if (nRetStatus != STATUS_OK)
		return (nRetStatus);

	// Response looks good, get the entire message...
	i = 1;
	// get the STX character...
	TRACE("\n    STX:              ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;

	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// get the NULL character...
	TRACE("\n    NULL:             ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;

	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// verify the CRC...
	TRACE("\n    CRC:              ");
	if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
		return nRetStatus;

	if (VerifyCRC((unsigned char *) aByteBuffer, i) != true)
		return (COMMAND_LRC_ERROR);

	// It worked, note the time
	//	_ftime(&PwrDwnTime);

	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long csp2GetPacket(char szBarData[],
 *                                                      long nBarcodeNumber,
 *                                                      long nMaxLength)
 *
 * Description:
 *
 * Parameters:     szBarData[] is a character array the user has allocated
 *                 to hold the barcode data. The string will be null terminated.
 *                 nBarcodeNumber indicates which barcode stored in
 *                 the szCspBcString[] will be returned.
 *                 nMaxLength is the length of the allocated space including the
 *                 null terminator. If nMaxLength is set to DETERMINE_SIZE, the
 *                 function will return the length of the barcode without
 *                 copying any data.
 *
 * Return Value:   > 0  length of the barcode including the NULL terminator
 *                 BAD_PARAM if the requested barcode does not exist
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetPacket(char szBarData[], long nBarcodeNumber, long nMaxLength) {
	long i;
	long nBarFound;
	long nBarLength;

	// make sure the requested barcode is valid...
	if (nBarcodeNumber < 0)
		return (SETUP_ERROR);

	if (nBarcodeNumber >= nCspStoredBarcodes)
		return (SETUP_ERROR);

	if (szBarData == NULL)
		return (SETUP_ERROR);

	// OK, go find the barcode...
	i = 0;
	nBarFound = 0;
	while ((nBarFound < nBarcodeNumber) && (i < MAXSIZE)) {
		// Add next string length (+1) to current index to index next counted string
		i += szCspBarData[i] + 1;

		// bump count to the next barcode...
		nBarFound++;
	}

	// OK, we've got the barcode, so process it...
	nBarLength = szCspBarData[i] + 1;

	// did the user only request the string's length?
	if (nMaxLength > DETERMINE_SIZE) {
		// get the maximum number of characters to copy...
		nMaxLength = min(nBarLength, nMaxLength);

		// copy the barcode...
		memcpy(szBarData, &szCspBarData[i], nMaxLength);
	}

	return (nBarLength);
}

/*********************************************************************                      
 * Synopsis:       long csp2GetDeviceId(char szDeviceId[9],
 *                                                        long nMaxLength)
 *
 * Description:    This function retrieves the Device ID array from the CSP
 *                 device data stored in the DLL. This is a binary array.
 *
 * Parameters:     szDeviceId[] holds the returned data.
 *                 nMaxLength is the length of the allocated space.
 *
 * Return Value:   The length of the Device ID array. This is not a string!
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetDeviceId(char szDeviceId[8], long nMaxLength) {
	//	// did the user only request the string's length?
	//	if (szDeviceId == NULL)
	//		return (SETUP_ERROR);
	//	if (nMaxLength <= DETERMINE_SIZE)
	//		return (SETUP_ERROR);
	//
	//	if ((nMaxLength > DETERMINE_SIZE) && (nCspActivePort >= COM1)) {
	//		// get the maximum number of characters to copy...
	//		nMaxLength = min(sizeof(szCspDeviceId), nMaxLength);
	//
	//		// copy the Device ID...
	//		memcpy(szDeviceId, szCspDeviceId, nMaxLength);
	//		nDeviceIdLen = nMaxLength;
	//		return (nMaxLength);
	//	}

	return (0);
}

/*********************************************************************                      
 * Synopsis:       long csp2GetRTCMode()
 *
 * Description:    Returns an integer corresponding to the Store_RTC
 *                 parameter from the CSP device.
 *
 * Parameters:     None
 *
 * Return Value:   >= Current RTC storage information
 *                 < 0, RTC storage mode not available
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetRTCMode(void) {
	return (nCspRTC);
}

/*********************************************************************
 * Synopsis:       long csp2GetASCIIMode()
 *
 * Description:    Returns an integer corresponding to the ASCII_MODE
 *                 parameter from the CSP device.
 *
 * Parameters:     None
 *
 * Return Value:   >= Current ASCII_MODE information
 *                 < 0, ASCII_MODE not available
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetASCIIMode(void) {
	return (nCspASCII);
}

/*********************************************************************
 * Synopsis:       long csp2GetProtocol()
 *
 * Description:    Returns an integer corresponding to the Protocol
 *                 byte from the CSP device.
 *
 * Parameters:     None
 *
 * Return Value:   >= Current Protocol version information
 *                 < 0, Protocol version not available
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetProtocol() {
	return (nCspProtocolVersion);
}

/*********************************************************************
 * Synopsis:       long csp2GetSystemStatus()
 *
 * Description:    Returns an integer corresponding to the System
 *                 Status byte from the CSP device.
 *
 * Parameters:     None
 *
 * Return Value:   >= Current System Status information
 *                 < 0, System Status not available
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetSystemStatus(void) {
	return (nCspSystemStatus);
}

/*********************************************************************
 * Synopsis:       long csp2GetSwVersion(char szSwVersion[9],
 *                                                         long nMaxLength)
 *
 * Description:    This function retrieves the Software Version string from the CSP
 *                 device data stored in the DLL. The string is null terminated.
 *
 * Parameters:     szSwVersion[] holds the returned data.
 *                 nMaxLength is the length of the allocated space including
 *                 the null terminator.
 *
 * Return Value:   Length of Software Version string including null terminator.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetSwVersion(char szSwVersion[9], long nMaxLength) {
	//	if (szSwVersion == NULL) {
	//		TRACE("NULL buffer!\n");
	//		return (SETUP_ERROR);
	//	}
	//	// did the user an illegal string length?
	//	if (nMaxLength <= DETERMINE_SIZE) {
	//		return (SETUP_ERROR);
	//	}
	//	// get the maximum number of characters to copy...
	//	nMaxLength = min(sizeof(szCspSwVersion), nMaxLength);
	//
	//	// copy the Software Version...
	//	memcpy(szSwVersion, szCspSwVersion, nMaxLength);
	//
	//	// and null terminate the string...
	//	szSwVersion[nMaxLength - 1] = 0;

	return (nMaxLength);
}

/*********************************************************************
 * Synopsis:       long csp2SetRetryCount(long nRetryCount)
 *
 * Description:    This function sets the communications retry count. The
 *                 default setting is 5. In the event of a communications
 *                 failure, the DLL will attempt the device interrogation
 *                 nRetryCount more times before returning a communications
 *                 error. This allows the user time to activate the CSP
 *                 device and connect it to the system in a reasonable time
 *                 period.
 *
 * Parameters:     nRetryCount specifies the number of interrogation retries.
 *                 The valid range for nRetryCount is [0..9]
 *
 * Return Value:   STATUS_OK
 *                 BAD_PARAM if nRetryCount not in range of valid values.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2SetRetryCount(long nRetryCount) {
	// make sure the retry count is within the valid range...
	if ((nRetryCount < 0) || (nRetryCount > 9))
		return (SETUP_ERROR);

	// change the retry count as indicated
	nCspRetryCount = nRetryCount;

	return (STATUS_OK);
}

/*********************************************************************
 * Synopsis:       long csp2GetRetryCount()
 *
 * Description:    This function returns the current value of nRetryCount.
 *
 * Parameters:     None
 *
 * Return Value:   The value of nRetryCount
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetRetryCount(void) {
	return (nCspRetryCount);
}

/*********************************************************************
 * Synopsis:        long csp2SetDebugMode(long nOnOff)
 *
 * Description:     This function turns the debug mode of the DLL on or off.
 *
 *                  When the debug mode is on, all commands and reponses are written
 *                  to a debug file named "debug.txt". Each entry in the file
 *                  will have a time stamp preceeding it.
 *
 *                  Note: this function function only writes data to the
 *                  output file in the debug version of the DLL.
 *
 * Parameters:      nOnOff is one of the following values:
 *                  PARAM_ON
 *                  PARAM_OFF
 *
 * Return Value:    STATUS_OK
 *                  FILE_NOT_FOUND
 *                  other: see WinError.h
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2SetDebugMode(long nOnOff) {
	//	if (nOnOff == PARAM_OFF) {
	//		TRACE_DISABLE;
	//	} else {
	//		// Use the default output file
	//		TRACE_ENABLE( NULL);
	//		TRACE("Consumer Scanning Products Debug File\n"
	//			"Initial Comm timeout = %d ms\n", (MAXTIME * 50));
	//	}
	//	// everything is OK
	return (STATUS_OK);
}

/*********************************************************************
 * Synopsis:       long csp2GetCommInfo(long nComPort)
 *
 * Description:    This function stub is retained for compatability with CSP
 *                 programs only.
 *
 * Parameters:     nCommPort is in the range [COM1..COM16]
 *
 * Return Value:
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetCommInfo(long nComPort) {
	return (0);
}

/*********************************************************************
 * Synopsis:       long csp2ReadRawData(char aBuffer[],
 *                                                        long nMaxLength )
 *
 * Description:    This function performs an "Upload" command on a CSP device.
 *                 The raw data read from the CSP device is placed into
 *                 aBuffer[] for up to nMaxLength bytes.  If the amount of
 *                 data that is read from the CSP device exceeds nMaxLength,
 *                 the buffer will be truncated at nMaxLength bytes.
 *
 *                 This function will retry the interrogation upto
 *                 nCspRetryCount times before aborting the operation.
 *
 *                 Protocol Change effective 4/1/98: Signature field may
 *                 or may not be present: it can be disabled. This code now
 *                 takes that in to account.
 *
 *                 This function asks the Device if the signature is enabled
 *                 before it starts an upload. So long as the comunications
 *                 works, it performs the upload.
 *
 *                 If the signature is enabled, and if the last counted
 *                 string is 8 characters long, it is assumed to be the
 *                 signature string.
 *
 *                 If it is not eight characters long, or the signature
 *                 is disabled, then the Signature field is returned as
 *                 'Disabled'.
 *
 * Parameters:     aBuffer[] array where data will be copied after it
 *                 is read out of the Device.
 *                 nMaxLength is the length of the allocated array.
 *                 If nMaxLength is set to DETERMINE_SIZE, the function will
 *                 return the length of the barcode without copying any data.
 *
 * Return Value:   Number of bytes read if no error, otherwise:
 *                 COMMUNICATIONS_ERROR
 *                 INVALID_COMMAND_NUMBER
 *                 COMMAND_LRC_ERROR
 *                 RECEIVED_CHARACTER_ERROR
 *                 GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2ReadRawData(char aBuffer[], long nMaxLength) {
	long i;
	long nRetStatus;
	long nCSLength;
	long nBarcodes;
	char *pStartBarCodeData;
	long nCopyLength;

	// send the command to the CSP device...

#ifdef _DEBUG
	TRACE("\n\nRead Data: [Time: %s]\n", szDebugTime);
#endif 
	if (aBuffer == NULL)
		return (SETUP_ERROR);

	if (nMaxLength < 0)
		return (SETUP_ERROR);

	nRetStatus = csp2SendCommand((char *) aUploadCmd, sizeof(aUploadCmd));

	if (nRetStatus != STATUS_OK)
		return (nRetStatus);

	// Response looks good, get the entire message...
	i = 1;

	// get the STX character...
	TRACE("\n    STX:             :");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;

	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// get the Device id characters...
	TRACE("\n    Device ID:       :");
	if ((nRetStatus = csp2ReadBytes(szCspDeviceId, 8)) < 0)
		return nRetStatus;
	memcpy(&aByteBuffer[i], szCspDeviceId, 8);
	i += 8;

	// read in the counted strings until a NULL occurs...
	nBarcodes = nCspStoredBarcodes = 0;

	TRACE("\n    Counted String:  :");
	nCSLength = nRetStatus = csp2Getc();

	// Set the Start of Barcode Data
	pStartBarCodeData = &aByteBuffer[i];

	// Continue while the length off one the counted string elements
	// is above zero.. values less then 0 are errors. ff
	while (nCSLength > 0) {
		// Store the length of the counted string.
		aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

		// Read nCSLenth bytes...
		if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], nCSLength)) < 0)
			break;

		// Read OK... Adjust the index..
		i += nCSLength;

		// Increment the number of barcodes stored...
		nBarcodes++;
		TRACE("\n                      ");

		// Read the next length
		nCSLength = nRetStatus = csp2Getc();
	}

	// Check to see if there was an error reading
	if (nRetStatus < 0)
		return nRetStatus;

	aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

	// Get the CRC
	TRACE("\n    CRC:             :");
	if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
		return nRetStatus;

	if (VerifyCRC((unsigned char *) aByteBuffer, i) != true)
		return (COMMAND_LRC_ERROR);

	// Now we know that the Downloaded data is OK
	nCSLength = (long) ((&aByteBuffer[i]) - pStartBarCodeData);

	if (nCSLength <= (int) sizeof(szCspBarData))
		memcpy(szCspBarData, pStartBarCodeData, nCSLength);

	nCspStoredBarcodes = nBarcodes;
	// should we copy the data to the user's array?
	if (nMaxLength) {
		nCopyLength = min(i + 1, nMaxLength);
		memcpy(aBuffer, aByteBuffer, nCopyLength);
	}
	return (nCopyLength); // Return byte count
}

/*********************************************************************
 * Synopsis:       long  csp2SetMultiParam( char *szString,
 *                                                            long nMaxLength )
 *
 * Description:    The csp2SetMultiParam function permits the user to write
 *                 multiple parameters at one time rather than individual
 *                 downloads.
 *
 * Parameters:     *string points to a buffer of multiple parameter strings.
 *                 Each parameter and value must be present in the counted
 *                 string format within the buffer. The trailing CRC will
 *                 be added by the function call.
 *                 length specifies how many characters in string[] make
 *                 up the new parameter setting.
 *
 * Return Value:   STATUS_OK
 *                 COMMUNICATIONS_ERROR
 *                 INVALID_COMMAND_NUMBER
 *                 COMMAND_LRC_ERROR
 *                 RECEIVED_CHARACTER_ERROR
 *                 GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2SetMultiParam(char szString[], long nMaxLength) {
	long nRetStatus;
	long i = 0;
	long k;
	long nCSLength;

	if (nMaxLength > MAX_XMIT_SIZE)
		return (GENERAL_ERROR);

	if (szString == NULL)
		return (GENERAL_ERROR);
	// see if the CSP device is connected...
	if ((nRetStatus = csp2Interrogate()) != STATUS_OK)
		return (nRetStatus);

	// start the download packet...

	aByteBuffer[i++] = DOWNLOAD_PARAMETERS;
	aByteBuffer[i++] = STX;

	// copy the string into the buffer...
	for (k = 0; k < nMaxLength; k++)
		aByteBuffer[i++] = szString[k];

	// null terminate all strings, assumed
	aByteBuffer[i++] = 0x00;

	// perform CRC Check on the string...

	CRCtest.w = ComputeCRC16((unsigned char *) aByteBuffer, i);
	aByteBuffer[i++] = CRCtest.b.hi;
	aByteBuffer[i++] = CRCtest.b.lo;

	// send the command to the CSP device...

#ifdef _DEBUG  
	TRACE("\n\nSet Parameter: %s [Time: %s]\n", aByteBuffer, szDebugTime);
#endif 
	nRetStatus = csp2SendCommand(aByteBuffer, i);

	if (nRetStatus != STATUS_OK)
		return (nRetStatus);

	// Response looks good, get the entire message...
	i = 1;

	// get the STX character...
	TRACE("\n    STX:              ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;
	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// read in the counted string until a NULL occurs...
	// first byte "j" is the length of the counted string...
	TRACE("\n    Counted String:   ");

	nCSLength = nRetStatus = csp2Getc();

	while (nCSLength > 0) {
		// Get and store the length of the counted string.
		aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

		// read the parameter value into aByteBuffer[]
		if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], nCSLength)) < 0)
			break;

		TRACE("\n                      ");
		// Adjust the index of i..
		i += nRetStatus;

		// Read the next length character.
		nCSLength = nRetStatus = csp2Getc();
	}

	// Check to see if there was an error
	if (nRetStatus < 0)
		return nRetStatus;

	// OK.. copy the last length
	aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

	// verify the CRC...
	TRACE("\n    CRC:              ");
	if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
		return nRetStatus;
	if (VerifyCRC((unsigned char *) aByteBuffer, i) != true)
		return (COMMAND_LRC_ERROR);

	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long  csp2SetParam( long nParam,
 *                                                       char *szString,
 *                                                       long nMaxLength )
 *
 * Description:    The csp2SetParam function permits the user to write
 *                 individual Device parameters one at a time rather than
 *                 batching all of the parameters for one download.
 *
 * Parameters:     param_number is the CSP device parameter to change
 *                *string points to the new parameter setting
 *                length specifies how many characters in string[] make
 *                up the new parameter setting.
 *
 * Return Value:  STATUS_OK
 *                COMMUNICATIONS_ERROR
 *                INVALID_COMMAND_NUMBER
 *                COMMAND_LRC_ERROR
 *                RECEIVED_CHARACTER_ERROR
 *                GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2SetParam(long nParam, char szString[], long nMaxLength) {
	long nRetStatus;
	long i = 0;
	long k;
	long nCSLength;

	if (nMaxLength < 0)
		return (SETUP_ERROR);

	if (szString == NULL)
		return (SETUP_ERROR);

	// see if the CSP device is connected...
	if ((nRetStatus = csp2Interrogate()) != STATUS_OK)
		return (nRetStatus);

	// start the download packet...
	aByteBuffer[i++] = DOWNLOAD_PARAMETERS;
	aByteBuffer[i++] = STX;

	// set the length of the counted string...
	aByteBuffer[i++] = nMaxLength + 1;

	// specify the parameter number...
	aByteBuffer[i++] = (char) nParam;

	// copy the string into the buffer...
	for (k = 0; k < nMaxLength; k++)
		aByteBuffer[i++] = szString[k];

	// null terminate all strings, assumed
	aByteBuffer[i++] = 0x00;

	// perform CRC Check on the string...

	CRCtest.w = ComputeCRC16((unsigned char *) aByteBuffer, i);
	aByteBuffer[i++] = CRCtest.b.hi;
	aByteBuffer[i++] = CRCtest.b.lo;

	// send the command to the CSP device...

#ifdef _DEBUG
	TRACE("\n\nSet Parameter: %s [Time: %s]\n", aByteBuffer, szDebugTime);
#endif 
	nRetStatus = csp2SendCommand(aByteBuffer, i);

	if (nRetStatus != STATUS_OK)
		return (nRetStatus);

	// Response looks good, get the entire message...
	i = 1;

	// get the STX character...
	TRACE("\n    STX:              ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;
	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// read in the counted string until a NULL occurs...
	// first byte "j" is the length of the counted string...
	TRACE("\n    Counted String:   ");

	nCSLength = nRetStatus = csp2Getc();

	// do until the length is 0
	while (nCSLength > 0) {
		// Get and store the length
		aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

		// Read the string portion with parameter value.
		if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], nCSLength)) < 0)
			break;

		// Read OK.. adjust the index.
		i += nRetStatus;

		// Read the next length
		nCSLength = nRetStatus = csp2Getc();
	}

	// Check for a read error
	if (nRetStatus < 0)
		return nRetStatus;

	// OK.. Store the last nCSLength;
	aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

	// verify the CRC...
	TRACE("\n    CRC:              ");
	if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
		return nRetStatus;

	if (VerifyCRC((unsigned char *) aByteBuffer, i) != true)
		return (COMMAND_LRC_ERROR);

	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long  csp2GetParam( long nParam,
 *                                                       char *szString,
 *                                                       long nMaxLength )
 *
 * Description:    The csp2GetParam function permits the user to read
 *                 individual Device parameters one at a time.
 *
 * Parameters:     nParam is the CSP device parameter to retrieve
 *                 *szString stores the retrieved parameter value
 *                 nMaxLength specifies the maximum number of characters that
 *                 can be stored in *string.
 *
 * Return Value:   STATUS_OK
 *                 COMMUNICATIONS_ERROR
 *                 INVALID_COMMAND_NUMBER
 *                 COMMAND_LRC_ERROR
 *                 RECEIVED_CHARACTER_ERROR
 *                 GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2GetParam(long nParam, char szString[], long nMaxLength) {
	long nRetStatus;

	// see if the CSP device is connected...
	if ((nRetStatus = csp2Interrogate()) != STATUS_OK)
		return (nRetStatus);
	if (szString == NULL)
		return (GENERAL_ERROR);
	if (nMaxLength < 0)
		return (SETUP_ERROR);
	return (GetParam(nParam, szString, nMaxLength));
}

/*********************************************************************                      
 * Synopsis:       long  GetParam( long nParam,
 *                                                   char *szString,
 *                                                   long nMaxLength )
 *
 * Description:    The GetParam is the underlying functionality for the
 *                 csp2GetParam function. It permits the user to read
 *                 individual Device parameters one at a time.
 *
 * Parameters:     nParam is the CSP device parameter to retrieve
 *                 *szString stores the retrieved parameter value
 *                 nMaxLength specifies the maximum number of characters that
 *                 can be stored in *string.
 *
 * Return Value:   STATUS_OK
 *                 COMMUNICATIONS_ERROR
 *                 INVALID_COMMAND_NUMBER
 *                 COMMAND_LRC_ERROR
 *                 RECEIVED_CHARACTER_ERROR
 *                 GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

char aGetParametersCmd[] = { UPLOAD_PARAMETERS, STX,

0x01, // Read one parameter
		0x00, // TBD parameter

		0x00, // NULL terminate the message
		0x00, // CRC HH
		0x00 // CRC LL
		};

#define GPC_PARM_START  ((long) 3)
#define GPC_CRC         (sizeof(aGetParametersCmd) - 2)
#define GPC_SIZE        (sizeof(aGetParametersCmd) - 1)

static long GetParam(int nParam, char szString[], long nMaxLength) {
	long i;
	long nRetStatus;
	long nCSLength;

	// fill in the command string...
	aGetParametersCmd[GPC_PARM_START] = (char) nParam;
	CRCtest.w = ComputeCRC16((unsigned char *) aGetParametersCmd, GPC_CRC);
	aGetParametersCmd[GPC_CRC] = CRCtest.b.hi;
	aGetParametersCmd[GPC_CRC + 1] = CRCtest.b.lo;

	// send the command to the CSP device...

#if _DEBUG
	time_t t;
	struct tm *tmp;
	t = time(NULL);
	tmp = localtime(&t);
	strftime(szDebugTime, 10, "%T", tmp); // Get time stamp string
	TRACE("\n\nGet Parameter %d: [Time: %s]\n", nParam, szDebugTime);
#endif 
	nRetStatus = csp2SendCommand(aGetParametersCmd, sizeof(aGetParametersCmd));

	if (nRetStatus != STATUS_OK)
		return (nRetStatus);

	// Response looks good, get the entire message...
	i = 1;

	// get the STX character...
	TRACE("\n    STX:              ");
	if ((nRetStatus = csp2Getc()) < 0)
		return nRetStatus;
	aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

	// read in the counted string until a NULL occurs...
	// first byte "j" is the length of the counted string...
	TRACE("\n    Counted String:   ");

	nCSLength = nRetStatus = csp2Getc();

	while (nCSLength > 0) {
		// Get and store the length;
		aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

		// Read the data.. parameter value + data
		if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], nCSLength)) < 0)
			break;

		// Read OK..
		// Copy the new extracted data the user buffer  Dont copy the paramenter value.
		// Since this should not be returned to the user.
		memcpy(szString, &aByteBuffer[i + 1], min(nRetStatus - 1, nMaxLength));

		// Adjust the index;
		i += nRetStatus;
		TRACE("\n                      ");

		// Get next length;
		nCSLength = nRetStatus = csp2Getc();
	}
	// Check for a read error
	if (nRetStatus < 0)
		return nRetStatus;

	// Add the last length for the counted string.
	aByteBuffer[i++] = LONG_2_UCHAR(nCSLength);

	// verify the CRC...
	TRACE("\n    CRC:              ");
	if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
		return nRetStatus;

	if (VerifyCRC((unsigned char *) aByteBuffer, i) != true) {

		TRACE("\n    CRCFailure:");

#ifdef _DEBUG     
		for (int j = 0; j < (i + 2); j++) {
			TRACE(" %02X", (unsigned char) aByteBuffer[j]);
		}
#endif 
		return (COMMAND_LRC_ERROR);
	}
	return (STATUS_OK);
}

/*********************************************************************                      
 * Synopsis:       long  csp2Interrogate(void)
 *
 * Description:    Request a response from the CSP device indicating that it is
 *                 operating and containing version information. Valid response
 *                 information is saved in local storage.
 *
 *                 The interrograte function will automatically retry the
 *                 interrogation operation up to nCspRetryCount times before
 *                 aborting.
 *
 * Parameters:     None
 *
 * Return Value:   STATUS_OK
 *                 COMMUNICATIONS_ERROR
 *                 INVALID_COMMAND_NUMBER
 *                 COMMAND_LRC_ERROR
 *                 RECEIVED_CHARACTER_ERROR
 *                 GENERAL_ERROR
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

long csp2Interrogate(void) {

	long i;
	long nRetStatus;
	long nCount = nCspRetryCount;

	TRACE("Starting to Interrogate the device\n");
	// interrogate the CSP device up to nCspRetryCount+1 times...
	do {

#ifdef _DEBUG
		time_t t;
		struct tm *tmp;
		t = time(NULL);
		tmp = localtime(&t);
		strftime(szDebugTime, 10, "%T", tmp); // Get time stamp string

		TRACE("\nInterrogate: [Time: %s %d.%d]\n", szDebugTime, tmp->tm_sec,
				(int) t);
#endif
		//
		// invalidate the old interrogation status...
		nCspDeviceStatus = -1; // no current Device status
		nCspProtocolVersion = -1; // no protocol version available
		nCspSystemStatus = -1; // no system status available
		szCspDeviceId[0] = 0; // no user Id available
		szCspSwVersion[0] = 0; // no software version available
		nDeviceIdLen = 0; // Don't know Id len so set it to 0

		// send the command to the CSP device...
		nRetStatus = csp2SendCommand((char *) aInterrogateCmd,
				sizeof(aInterrogateCmd));

		// if the Device reported an error, try again from the top...
		if (nRetStatus != STATUS_OK) {
			nDelayCount = nDelayCount + 2; // add 100 ms to delay time

			TRACE("\nComm Delay now %d ms.\n", (nDelayCount * 50));
			continue;
		}

		// Response looks good, get the entire message...
		i = 1;
		// get the STX character...
		TRACE("\n    STX              :");
		if ((nRetStatus = csp2Getc()) < 0) {
			continue; // RESEND IF NO CONNECTED
			//return nRetStatus;
		}
		aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

		// get the protocol version character...
		TRACE("\n    Protocol Version :");
		if ((nCspProtocolVersion = nRetStatus = csp2Getc()) < 0)
			return nRetStatus;
		aByteBuffer[i++] = LONG_2_UCHAR(nCspProtocolVersion);

		// get the system status character...
		TRACE("\n    System Status    :");
		if ((nCspSystemStatus = nRetStatus = csp2Getc()) < 0)
			return nRetStatus;
		aByteBuffer[i++] = LONG_2_UCHAR(nCspSystemStatus);

		// get the user id characters...
		TRACE("\n    Device ID        :");
		if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 8)) < 0)
			return nRetStatus;

		// Copy the szCspDeviceID
		memcpy(szCspDeviceId, &aByteBuffer[i],
				min(nRetStatus, (int)sizeof(szCspDeviceId)));
		// Adjust index;
		i += nRetStatus;

		// get the software version characters...
		TRACE("\n    Software Version :");
		if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 8)) < 0)
			return nRetStatus;

		// Copy the szCspDeviceID
		memcpy(szCspSwVersion, &aByteBuffer[i],
				min(nRetStatus, (int)sizeof(szCspSwVersion)));
		// Adjust index;
		i += nRetStatus;

		// get the NULL character...
		TRACE("\n    NULL             :");
		if ((nRetStatus = csp2Getc()) < 0)
			return nRetStatus;
		aByteBuffer[i++] = LONG_2_UCHAR(nRetStatus);

		// verify the CRC...
		TRACE("\n    CRC              :");
		if ((nRetStatus = csp2ReadBytes(&aByteBuffer[i], 2)) < 0)
			return nRetStatus;

		if (VerifyCRC((unsigned char *) aByteBuffer, i) != true) {
			TRACE("NOT VERIFIED");
			nRetStatus = COMMAND_LRC_ERROR;
			continue;
		}

		// CSP device was interrogated successfully...
		return (STATUS_OK);

	} while (nCount--);

	return (nRetStatus); // interrogation failed
}

/*********************************************************************
 * Synopsis:      NoMangle long DLL_IMPORT_EXPORT csp2TimeStamp2Str(
 *					unsigned char *Stamp, char *value)
 *
 * Description:   Csp2TimeStamp2Str is used to convert the CS1504 packed
 *				 time array to an ascii string. IT IS THE CALLER'S
 *				 RESPOSIBILITY TO PROVIDE ADEQUATE SPACE. (21 BYTES)
 *
 *
 * Parameters:    Stamp[] is the 4 byte time stamp array, value is a
 *				 pointer to a string with at least 21 bytes of allocated
 *				 storage.
 *
 * Return Value:  An ascii timestamp is returned, HH:MM:SS AM MM/DD/YYYY
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

// Note - time value return is at least 20 characters
long csp2TimeStamp2Str(unsigned char *Stamp, char *value, long nMaxLength) {

	char s[25], AMPM[3];
	unsigned int hours, minutes, seconds, months, days, years;
	long status;
	union {
		unsigned long l;
		unsigned char c[4];
	} TimeConvert;

	// make sure parameters are valid
	if (Stamp == NULL)
		return (SETUP_ERROR);

	if (value == NULL)
		return (SETUP_ERROR);

	if (nMaxLength < 0)
		return (SETUP_ERROR);

	TimeConvert.c[0] = Stamp[3]; // copy the timestamp into the union
	TimeConvert.c[1] = Stamp[2]; // note that this is compiler sensitive
	TimeConvert.c[2] = Stamp[1];
	TimeConvert.c[3] = Stamp[0];

	hours = RTC_Get_Hours(TimeConvert.l);
	minutes = RTC_Get_Minutes(TimeConvert.l);
	seconds = RTC_Get_Seconds(TimeConvert.l);
	months = RTC_Get_Months(TimeConvert.l);
	days = RTC_Get_Days(TimeConvert.l);
	years = RTC_Get_Years(TimeConvert.l);

	strcpy(AMPM, "AM");
	if (hours > 11) {
		hours -= 12;
		AMPM[0] = 'P';
	}
	if (hours == 0)
		hours = 12;

	// Check to see if there is an error.
	// if the value is 63 (the 6 bit second field is set to all 1s)
	// then it is most likely that the time is suspect.
	if (seconds != 63) {
		// Time is OK
		sprintf(s, " %2d:%02d:%02d %s %2d/%0d/%02d", hours, minutes, seconds,
				AMPM, months, days, years);
		status = STATUS_OK;
	} else {
		// Time is suspect.
		sprintf(s, "%2d:%02d:?? %s %2d/%0d/%02d", hours, minutes, AMPM, months,
				days, years);
		status = BAD_PARAM;
	}

	strncpy(value, s, nMaxLength);
	return (status);
}

/*********************************************************************
 * Synopsis:      NoMangle long DLL_IMPORT_EXPORT csp2GetCodeType(
 *					unsigned char CodeID, char *CodeType, long nMaxLength)
 *
 *
 * Description:   csp2GetCodeType is used to convert the CS1504 CodeId
 *				 to an ascii string.
 *
 *
 * Parameters:    CodeID is the id returned in an ascii mode barcode counted
 *				string.CodeType is a pointer to a string to recieve the
 *				ascii equivalent. nMaxLength is the max size of the returned
 *				string
 *
 * Return Value:  An ascii Code Type is returned.
 *
 * Inputs:
 *
 * Outputs:
 *
 * External Calls:
 *
 * Notes:
 **********************************************************************/

// Allow 40 characters for codetype string
long csp2GetCodeType(unsigned long CodeID, char *CodeType, long nMaxLength) {
	unsigned char i;

	if (CodeType == NULL)
		return (SETUP_ERROR);
	if (nMaxLength < 0)
		return (SETUP_ERROR);

	for (i = 0; i < (sizeof(CodeTypes) / sizeof(CodeTypeStruct)); i++)
		if (CodeTypes[i].CodeId == CodeID)
			break;

	if (i < (sizeof(CodeTypes) / sizeof(CodeTypeStruct))) {
		strncpy(CodeType, CodeTypes[i].CodeType, nMaxLength);
		return (STATUS_OK);
	}
	strncpy(CodeType, "Unknown", nMaxLength);
	return (GENERAL_ERROR);
}
