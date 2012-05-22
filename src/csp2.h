/*******************************************************************************
*                      
*  Filename:      K:\keyfob\tony\winfob\Csp2.h
*
*  Copyright(c) Symbol Technologies Inc., 2001
*  
*  Description:     This file provides the user API interface
*                   function prototypes for Symbol's CS1504 Consumer Scanning 
*                   Products as Dynamic Link Library. When 
*                   compiled into a DLL, the user can access 
*                   all of the functions available for the 
*                   Symbol CS1504.
*
*  Author:          Tony Russo
*
*  Creation Date:   4/04/2001
*
*  Derived From:    New File
*
*  Edit History:
*   $Log:   U:/keyfob/winfob/include/CSP2.H_V  $
* 
*    Rev 1.1   Jan 29 2002 16:29:54   pangr
* Incorporated CWB changes. 
* Added #defines for DATA_AVAILABLE and
* DATA_NOT_AVAILABLE.  Added prototypes for
* csp2EnablePolling and csp2DisablePolling
* 
*    Rev 1.0   05 Apr 2001 09:24:16   RUSSOA
* Initial revision.
*
*
*	Added Polling headers and helper functions 4/6/01 CWB
*******************************************************************************/
                    
/*******************************************************************************
* Include Files
*******************************************************************************/

/*******************************************************************************
* Defines
*******************************************************************************/ 
#ifdef DLL_IMPORT_EXPORT
    #undef DLL_IMPORT_EXPORT
#endif

#define DLL_IMPORT_EXPORT

/*#ifdef DLL_SOURCE_CODE
    #define DLL_IMPORT_EXPORT __declspec(dllexport) __stdcall
#else
    #define DLL_IMPORT_EXPORT __declspec(dllimport) __stdcall
#endif*/

#ifdef __cplusplus
    #define NoMangle extern "C"
#else
    #define NoMangle
#endif

#define HS_SET                      ((int) 1)
#define HS_CLEAR                    ((int) 0)

// Returned status values...
#define STATUS_OK                   ((long) 0)
#define COMMUNICATIONS_ERROR        ((long)-1)  
#define BAD_PARAM                   ((long)-2)
#define SETUP_ERROR                 ((long)-3)
#define INVALID_COMMAND_NUMBER      ((long)-4)  
#define COMMAND_LRC_ERROR           ((long)-7)  
#define RECEIVED_CHARACTER_ERROR    ((long)-8)  
#define GENERAL_ERROR               ((long)-9)  
#define FILE_NOT_FOUND              ((long) 2)
#define ACCESS_DENIED               ((long) 5)

// Parameter values...
#define PARAM_OFF                   ((long) 0) 
#define PARAM_ON                    ((long) 1) 

#define DATA_AVAILABLE              ((long) 1)
#define NO_DATA_AVAILABLE           ((long) 0)

#define DETERMINE_SIZE              ((long) 0)

#define TRACE printf

/*******************************************************************
 *              Communications setup section...                    *
 *******************************************************************/

#ifndef COM1
    #define COM1                    ((long) 0)
    #define COM2                    ((long) 1)
    #define COM3                    ((long) 2)
    #define COM4                    ((long) 3)
    #define COM5                    ((long) 4)
    #define COM6                    ((long) 5)
    #define COM7                    ((long) 6)
    #define COM8                    ((long) 7)
    #define COM9                    ((long) 8)
    #define COM10                   ((long) 9)
    #define COM11                   ((long)10)
    #define COM12                   ((long)11)
    #define COM13                   ((long)12)
    #define COM14                   ((long)13)
    #define COM15                   ((long)14)
    #define COM16                   ((long)15)
#endif

/*******************************************************************************
* Public Variables
*******************************************************************************/ 

/*******************************************************************************
* Local Variables
*******************************************************************************/ 

/*******************************************************************************
* Public Function Prototypes 
*******************************************************************************/ 
// Communications
int csp2Init(int fd);
long csp2Restore(void);
long csp2WakeUp(void);
long csp2DataAvailable(void);

// Basic Functions
long csp2ReadData(void);
long csp2ClearData(void);
long csp2PowerDown(void);
long csp2GetTime(unsigned char aTimeBuf[]);
long csp2SetTime(unsigned char aTimeBuf[]);
long csp2SetDefaults(void);

// CSP Data Get
long csp2GetPacket(char szBarData[], long nBarcodeNumber, long nMaxLength);
long csp2GetDeviceId(char szDeviceId[8], long nMaxLength);
long csp2GetProtocol(void);
long csp2GetSystemStatus(void);
long csp2GetSwVersion(char szSwVersion[9], long nMaxLength);
long csp2GetASCIIMode(void);
long csp2GetRTCMode(void);

// DLL Configuration
long csp2SetRetryCount(long nRetryCount);
long csp2GetRetryCount(void);

// Miscellaneous
long csp2TimeStamp2Str(unsigned char *Stamp, char *value, long nMaxLength);
long csp2GetCodeType(unsigned long CodeID, char *CodeType, long nMaxLength);

// Advanced functions
long csp2ReadRawData(char aBuffer[], long nMaxLength);
long csp2SetParam(long nParam, char szString[], long nMaxLength);
long csp2GetParam(long nParam, char szString[], long nMaxLength);
long csp2Interrogate(void);
long csp2GetCTS(void);
long csp2SetDTR(long nOnOff);
long csp2SetDebugMode(long nOnOff);
long csp2StartPolling(/*FARPROC*/ void* csp2CallBack);
long csp2StopPolling(void);
long csp2EnablePolling(void);
long csp2DisablePolling(void);

/*******************************************************************************
* Local Functions Prototypes
*******************************************************************************/ 

/*******************************************************************
 *                       End of file                               *
 *******************************************************************/
