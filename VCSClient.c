
#include <stdlib.h>
#include <string.h>
#include <svc.h>
#include <svctxo.h>
#include <applidl.h>
#include <acldev.h>
#include <aclstr.h>
#include <message.h>
#include <printer.h>
#include "logsys.h"
#include "varrec.h"
#include "eeslapi.h"
//Include file for Device Manager
#include "devman.h"
//Application specific Include files
//#include "..\include\VCS.h"
//#include "..\include\BIOPAY.h"
#include "..\include\VCSClient.h"
#include "..\include\Appevent.h"
#include <vcsInterFace.h>


//void getTime(char *systime);




/*logical name of comm server application*/
#define COMM_SVR "COMMSVR"
/*logical name of client application*/
#define APP_NAME "VCS"
/*maximum no of events the event queue can hold (default 30)*/
#define MAX_QEVENTS 30
/*maximum no of bytes to be transferred to comm server*/
#define COMMSVR_EVENT_DATA_SIZE	500


//extern char  chFlagWaitForENQ;  // flag used to wait 4 ENQ from host

/*variables used while communicating with comm server*/

/*global buffer for storing the sender name*/
char senderName[10];
/*global buffer for storing request string*/
char vcsReqBuff[1000];
/*global buffer for storing read event data*/
unsigned char vcsReadEventBuf[500];

unsigned char vcsReadAllPipeData[2000];

unsigned char vcsInPipeData[2000];

/*global buffer for storing write event data*/
unsigned char vcsWriteEventBuf[1000];
/*global buffer for storing event data size*/
int eventDataSize;
/*session handle*/
int hSession=-1;

extern char  strResponse[2000];
extern int   iLenResponse;
int RevEnqFlag = 0;
extern int flagInitSession;
/*LRC Defined */
char		lrc=0;

/*task data*/
EESL_IMM_DATA vcstaskData;

int StatusIDs[MAX_STATUS_FIELDS];
extern char LogicalName[EESL_APP_LOGICAL_NAME_SIZE];    // To store the logical name of this Appl


//extern char reqBuff[1024];

//char respBuff[5000];


//void initString(void);
void InitIPSXString(void);


short vcsWriteEventToServer(int vcsEvent,short max_retry)
{
    unsigned int shErr = 1;
    short retry = 0;

  	while(1)
	{
		/*write the event to comm server*/
		shErr = EESL_send_event(COMM_SVR,vcsEvent,(unsigned char *)vcsWriteEventBuf, (unsigned int)eventDataSize);
        if (!shErr)
			break;
		retry++;
        if (retry >= max_retry)
			break;
		SVC_WAIT(200);
	}

	return shErr;
}

short vcsReadEventFromServer(short wait_time, short max_retry)
{
	unsigned int shErr;
	long evt;
	int timerID;
    short retry = 0;

	while(1)
	{
		/*set the timer to wait for event*/
    timerID = set_timer((long)(wait_time * 1000), EVT_TIMER);
		/*wait for event to occur*/
    evt = wait_event();
    shErr = EESL_send_event(APP_NAME, 11001, NULL, 0);
		retry++;

        if (retry >= max_retry)
			return VCS_ERROR;

		/*if the event is timer event*/
    if (evt & EVT_TIMER)
		{
            short q = 0;
            shErr = EESL_send_event(APP_NAME, 11001, NULL, 0);
            q = EESL_queue_count();
            LOG_PRINTF(("Queue %d", q));
			LOG_PRINTF(("wait time for read event expired"));

            if (retry >= max_retry)
				return VCS_ERROR;
       else if (q == 0)
				continue;
		}

		/*clear the wait timer*/
		clr_timer(timerID);

		/*check the status of the event queue*/
    if (!EESL_queue_count())
		{
			LOG_PRINTF(("Queue empty"));
			SVC_WAIT(10);
			continue;
		}

		/*read the event if the queue is not empty*/
    memset((char *)vcsReadEventBuf, 0, sizeof(vcsReadEventBuf));
    shErr = EESL_send_event(APP_NAME, 11001, NULL, 0);
		shErr = EESL_read_cust_evt((unsigned char*)vcsReadEventBuf, sizeof(vcsReadEventBuf),(unsigned int*)&eventDataSize, senderName);
		LOG_PRINTF(("read event = %d from %s, data: %s", shErr, senderName, vcsReadEventBuf));

    if (strcmp(senderName, COMM_SVR))
		{
				LOG_PRINTF(("received event is not from comm server"));
				continue;
		}
     
    
     if (shErr != 0)
     {
	     	LOG_PRINTF(("shErr received Going to break=%d",shErr));
				break;
		 }
//	SVC_WAIT(1000);
		//SVC_WAIT(10);
	}

	/*return the event read from comm server*/
	return shErr;
}

short vcsInitSession()
{
    short status=-1;
	short retry=0;
	unsigned int vcsErr;
	unsigned int vcsNative;


	LOG_PRINTF(("Before InitSession"));
	//getTime(timeBuf);

	if (EESL_check_app_present(COMM_SVR, &vcstaskData) != EESL_APP_REGISTERED)
	{
		return VCS_NOT_PRESENT;
	}
	else
	{
		/*Initialize Flexi record for communication*/
		vVarInitRecord(vcsWriteEventBuf, sizeof(vcsWriteEventBuf), 0);
		ushInitStandardFlexi(vcsWriteEventBuf);

		/*Send Initialize request to the comm server*/
		eventDataSize=0;
		memset(vcsWriteEventBuf,0,sizeof(vcsWriteEventBuf));

		/*send message to comm server*/
		status = vcsWriteEventToServer(VCS_EVT_INIT_REQ,20);

		/*If the request to comm server failed*/
		if(status!=0)
			return VCS_ERROR;

		LOG_PRINTF(("comm_server Init status : %d",status));

		/*read message from comm server*/
		while(1)
		{
			status = vcsReadEventFromServer(10,5);
			if(status==VCS_EVT_INIT_RESP)
				break;
			else
				retry++;

			/*maximum number of retries to read the response from comm server*/
			if(retry>25)
				break;
		}

		LOG_PRINTF(("comm_server Init, read event from comm server : %d",status));

		/*Initialize read event flexi record to parse the values*/
		ushInitStandardFlexi(vcsReadEventBuf);

		/*Extract session handle*/
		shVarGetUnsignedInt(VCS_FLD_SESS_HANDLE, (unsigned int *) &hSession);
		LOG_PRINTF (("comm_server Init Session Handle : %d", hSession));

		/*Extract Generic error*/
		shVarGetUnsignedInt(VCS_FLD_SESS_ERROR, (unsigned int *) &vcsErr);
		LOG_PRINTF (("comm_server Init Generic Error :%d", vcsErr));

		/*Extract Native error*/
		shVarGetUnsignedInt(VCS_FLD_SESS_NATIVE, (unsigned int *) &vcsNative);
		LOG_PRINTF (("comm_server Init Native Error	:%d", vcsNative));

		if(vcsErr != 0)
		{
			LOG_PRINTF(("comm_server Init request was unsuccessful: %d",vcsErr));
			return vcsErr;
		}
	}

	LOG_PRINTF(("After InitSession"));
	//getTime(timeBuf);

	return VCS_SUCCESS;
}


short vcsWriteConnectReq(unsigned short ssl,
					   unsigned short client_auth,
					   unsigned char * host_ip,
					   unsigned short port,
					   unsigned short app_group)
{
    short status=-1;
	//char timeBuf[25];

	LOG_PRINTF(("Before writing connect request"));

	/*Initialize Flexi record for communication*/
	vVarInitRecord(vcsWriteEventBuf, sizeof(vcsWriteEventBuf), 0);

	ushInitStandardFlexi(vcsWriteEventBuf);

	/*Fill the Flexi record for communication*/
	shVarAddUnsignedInt(VCS_FLD_CONN_HOSTSSL,ssl);
	shVarAddUnsignedInt(VCS_FLD_CONN_CLNTAUTH,client_auth);
	shVarAddData(VCS_FLD_CONN_URL,host_ip,strlen((char *)host_ip));
	shVarAddUnsignedInt(VCS_FLD_CONN_PORT,port);
	shVarAddUnsignedInt(VCS_FLD_CONN_APPGROUP,app_group);
	shVarAddUnsignedInt(VCS_FLD_SESS_HANDLE,hSession);
	shGetRecordLength(vcsWriteEventBuf,(int *)&(eventDataSize));


	LOG_PRINTF(("Writing connection request to %s, port %d, ssl %d, client authentication %d, session handle %d",host_ip,port,ssl,client_auth,hSession));
	/*Send Connect request to the comm server*/
	status = vcsWriteEventToServer(VCS_EVT_CONN_REQ,30);

	LOG_PRINTF(("comm_server Connect status : %d",status));

	//EESL_send_event(APP_NAME,11001,NULL,0);

	LOG_PRINTF(("After writing connect request"));
	//getTime(timeBuf);
	return status;
}


short vcsReadConnectResp()
{
    short status=-1;
	short retry=0;
	unsigned int vcsErr;
	unsigned int vcsNative;
	//char timeBuf[25];

	LOG_PRINTF(("Before read connect response"));
	//getTime(timeBuf);
	/*read message from comm server*/
	while(1)
	{
		//status = vcsReadEventFromServer(10,25);
		status = vcsReadEventFromServer(10,10);
		if(status==VCS_EVT_CONN_RESP)
			break;
		else
			retry++;

		/*maximum number of retries to read the response from comm server*/

		if(retry>10)
			break;
	}

	LOG_PRINTF(("comm_server Connect, read event from comm server : %d",status));

	/*Initialize read event flexi record to parse the values*/
	ushInitStandardFlexi(vcsReadEventBuf);

	/*Extract Generic error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_ERROR, (unsigned int *) &vcsErr);
	LOG_PRINTF (("comm_server Connect Generic Error :%d", vcsErr));

	/*Extract Native error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_NATIVE, (unsigned int *) &vcsNative);
	LOG_PRINTF (("comm_server Connect Native Error	:%d", vcsNative));

	if(vcsErr != 0)
	{
		LOG_PRINTF(("comm_server Connect request was unsuccessful: %d",vcsErr));
		return vcsErr;
	}

	LOG_PRINTF(("After read connect response"));
	//getTime(timeBuf);
	return VCS_SUCCESS;
}



short vcsSendRequestToServer(const char *szReqBuff)
{
    short status=-1;
	short retry=0;
	unsigned int vcsErr;
	unsigned int vcsNative;
	unsigned int len=0;
	int cntBytes;
	int flgBytes=0;
	char timeBuf[25];

//    LOG_PRINTF(("Before Sending request"));
	//getTime(timeBuf);
	/*Initialize Flexi record for communication*/
	vVarInitRecord(vcsWriteEventBuf, sizeof(vcsWriteEventBuf), 0);
	ushInitStandardFlexi(vcsWriteEventBuf);

    len = strlen(szReqBuff);

	/*Add fields to response request packet*/
	shVarAddUnsignedInt(VCS_FLD_SEND_BUFSIZE, len);
	shVarAddUnsignedInt(VCS_FLD_SESS_HANDLE, hSession);
	shGetRecordLength(vcsWriteEventBuf,(int *)&(eventDataSize));

	/*send message to comm server*/
	status = vcsWriteEventToServer(VCS_EVT_SEND_REQ,30);
//    LOG_PRINTF(("comm_server Send request status : %d",status));

	/*If the request to comm server failed*/
    if (status != 0)
	{
    //   LOG_PRINTF(("Request to comm server failed"));
		return VCS_ERROR;
	}

	/*send raw data to comm server*/

    cntBytes = len;

//    LOG_PRINTF(("Send request to server : %s",szReqBuff));

	while(1)
	{
		if(cntBytes<500)
		{
//			strncpy((char *)vcsWriteEventBuf,(char *)&szReqBuff[500],cntBytes);
			if(flgBytes)
				memcpy(vcsWriteEventBuf,szReqBuff+500,cntBytes);
			else
				memcpy(vcsWriteEventBuf,szReqBuff,cntBytes);
			vcsWriteEventBuf[cntBytes+1]='\0';
			eventDataSize=cntBytes;
			status = vcsWriteEventToServer(VCS_EVT_DATA_RAW,30);
        //   LOG_PRINTF(("sent status : %d bytes : %d ",status,eventDataSize));
			break;
		}
		else
		{
			flgBytes=1;
			memcpy(vcsWriteEventBuf,szReqBuff,500);
			eventDataSize=500;
			status = vcsWriteEventToServer(VCS_EVT_DATA_RAW,30);
         //   LOG_PRINTF(("sent status %d bytes %d str : %s",status,eventDataSize,vcsWriteEventBuf));
		}
		cntBytes-=500;
	}


	/*If the request to comm server failed*/
	if(status!=0)
	{
        LOG_PRINTF(("Raw data Request to comm server failed"));
		return VCS_ERROR;
	}

   //LOG_PRINTF(("comm_server Send raw data status : %d",status));

	/*read message from comm server*/
	while(1)
	{
        status = vcsReadEventFromServer(30, 25);
        if (status == VCS_EVT_SEND_RESP)
			break;
		else
			retry++;

		/*maximum number of retries to read the response from comm server*/
		if(retry>10)
			break;
	}

//    LOG_PRINTF(("comm_server Send server, read event from comm server : %d",status));

	/*Initialize read event flexi record to parse the values*/
	ushInitStandardFlexi(vcsReadEventBuf);

	/*Extract Generic error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_ERROR, (unsigned int *) &vcsErr);
//    LOG_PRINTF (("comm_server Send Generic Error :%d", vcsErr));

	/*Extract Native error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_NATIVE, (unsigned int *) &vcsNative);
//    LOG_PRINTF (("comm_server Send Native Error :%d", vcsNative));

    if (vcsErr != 0)
	{
//        LOG_PRINTF(("comm_server Send request was unsuccessful: %d",vcsErr));
		return vcsErr;
	}

//    LOG_PRINTF(("After Sending request"));
	//getTime(timeBuf);
	return VCS_SUCCESS;
}

/*short vcsReceiveRespFromServer(int max_length, int time_out)
{
    short status = -1;
    short retry = 0;
	unsigned int vcsErr;
	unsigned int vcsNative;
    unsigned int len = 0;
    unsigned int resSize = 0;

    static short hFile = -1;

    EESL_send_event(LogicalName, 11001, NULL, 0);//--added -hp

	vVarInitRecord(vcsWriteEventBuf, sizeof(vcsWriteEventBuf), 0);
	ushInitStandardFlexi(vcsWriteEventBuf);


    shVarAddUnsignedInt(VCS_FLD_RECV_BUFSIZE, max_length);
    shVarAddUnsignedInt(VCS_FLD_RECV_TIMEOUT, time_out);
	shVarAddUnsignedInt(VCS_FLD_SESS_HANDLE, hSession);
    shGetRecordLength(vcsWriteEventBuf, (int *)&(eventDataSize));


    status = vcsWriteEventToServer(VCS_EVT_RECV_REQ, 30);
//    LOG_PRINTF(("comm_server Receive response status : %d", status));

    if (status != 0)
		return VCS_ERROR;


	while(1)
	{
//		EESL_send_event(APP_NAME,11001,NULL,0);
//        LOG_PRINTF(("waiting for VCS_EVT_RECV_RESP"));
		//status = vcsReadEventFromServer(2,25);
        status = vcsReadEventFromServer(10, 10);
        if (status == VCS_EVT_RECV_RESP)
			break;
		else
			retry++;

        if (retry > 10)
			break;
	}

//    LOG_PRINTF(("comm_server receive response, read event from comm server : %d", status));

	ushInitStandardFlexi(vcsReadEventBuf);


	shVarGetUnsignedInt(VCS_FLD_RECV_BUFSIZE, (unsigned int *) &resSize);
//    LOG_PRINTF (("comm_server Receive Buffer Size :%d", resSize));


	shVarGetUnsignedInt(VCS_FLD_SESS_ERROR, (unsigned int *) &vcsErr);
//    LOG_PRINTF (("comm_server Receive Generic Error :%d", vcsErr));


	shVarGetUnsignedInt(VCS_FLD_SESS_NATIVE, (unsigned int *) &vcsNative);
//    LOG_PRINTF (("comm_server Receive Native Error  :%d", vcsNative));

    if (vcsNative)
	{
		return vcsNative;
	}

    if (resSize <= 0)
	{
        LOG_PRINTF(("Received empty packet"));
		return 0;
		//return VCS_ERROR;
	}

    if (vcsErr != 0)
	{
		LOG_PRINTF(("comm_server Receive response was unsuccessful: %d",vcsErr));
		return vcsErr;
	}

	while(1)
	{
//        LOG_PRINTF(("waiting for raw data"));
        status = vcsReadEventFromServer(10, 5);
        if (status == VCS_EVT_DATA_RAW)
			break;
		else
			retry++;

        if (retry > 3)
			break;
	}

    memset(strResponse, 0, sizeof(strResponse));
    memcpy(strResponse, (char *)vcsReadEventBuf, resSize);
	iLenResponse = resSize;

    vcsReadEventBuf[resSize] = '\0';

	return VCS_SUCCESS;
}*/

short vcsReceiveRespFromServer(int max_length, int time_out)
{
    int inIndex = 0;
    short status = -1;
    short retry = 0;
	unsigned int vcsErr;
	unsigned int vcsNative;
    unsigned int len = 0;
    unsigned int resSize = 0;

    static short hFile = -1;

//    EESL_send_event(LogicalName, 11001, NULL, 0);//--added -hp

	vVarInitRecord(vcsWriteEventBuf, sizeof(vcsWriteEventBuf), 0);
	ushInitStandardFlexi(vcsWriteEventBuf);

    shVarAddUnsignedInt(VCS_FLD_RECV_BUFSIZE, 1);
    shVarAddUnsignedInt(VCS_FLD_RECV_TIMEOUT, (time_out * 1000));
	shVarAddUnsignedInt(VCS_FLD_SESS_HANDLE, hSession);
    shGetRecordLength(vcsWriteEventBuf, (int *)&(eventDataSize));

    status = vcsWriteEventToServer(VCS_EVT_RECV_REQ, 30);
//    LOG_PRINTF(("comm_server Receive response status : %d", status));

    if (status != 0)
		return VCS_ERROR;

	while(1)
	{
       LOG_PRINTF(("***************waiting for VCS_EVT_RECV_RESP***************************&&&&*************"));
        status = vcsReadEventFromServer(10,10);   //10,10
        if (status == VCS_EVT_RECV_RESP)
			break;
		else
			retry++;

        if (retry > 5)   //10
			break;
	}

    LOG_PRINTF(("comm_server receive response, read event from comm server: %d", status));

	ushInitStandardFlexi(vcsReadEventBuf);

	shVarGetUnsignedInt(VCS_FLD_RECV_BUFSIZE, (unsigned int *) &resSize);
//    LOG_PRINTF(("comm_server Receive Buffer Size :%d", resSize));

	shVarGetUnsignedInt(VCS_FLD_SESS_ERROR, (unsigned int *) &vcsErr);
//    LOG_PRINTF(("comm_server Receive Generic Error :%d", vcsErr));

	shVarGetUnsignedInt(VCS_FLD_SESS_NATIVE, (unsigned int *) &vcsNative);
//    LOG_PRINTF(("comm_server Receive Native Error  :%d", vcsNative));

    if (vcsNative)
		return vcsNative;

    if (resSize <= 0)
	{
        LOG_PRINTF(("Received empty packet"));
		return 0;
		//return VCS_ERROR;
	}

  if (vcsErr != 0)
	{
       LOG_PRINTF(("comm_server Receive response was unsuccessful: %d", vcsErr));
		return vcsErr;
	}

	while(1)
	{
//        LOG_PRINTF(("waiting for raw data"));
        status = vcsReadEventFromServer(10, 5);    //10,5
        if (status == VCS_EVT_DATA_RAW)
			break;
		else
			retry++;

      if (retry > 3)
			break;
	}
//LOG_PRINTF(("Received New Added %s", vcsReadEventBuf));
    memset(strResponse, 0, sizeof(strResponse));
    strResponse[inIndex++] = vcsReadEventBuf[0];
    vcsReadEventBuf[1] = '\0';

    if (max_length > 1)
    {
        vVarInitRecord(vcsWriteEventBuf, sizeof(vcsWriteEventBuf), 0);
        ushInitStandardFlexi(vcsWriteEventBuf);

        shVarAddUnsignedInt(VCS_FLD_RECV_BUFSIZE, max_length);
        shVarAddUnsignedInt(VCS_FLD_RECV_TIMEOUT, 0);
        shVarAddUnsignedInt(VCS_FLD_SESS_HANDLE, hSession);
        shGetRecordLength(vcsWriteEventBuf, (int *)&(eventDataSize));

        status = vcsWriteEventToServer(VCS_EVT_RECV_REQ, 30);
//        LOG_PRINTF(("comm_server Receive response status: %d", status));

        if (status != 0)
            return VCS_ERROR;

        while(1)
        {
//            LOG_PRINTF(("waiting for VCS_EVT_RECV_RESP"));
            status = vcsReadEventFromServer(10, 10);   //10,10
            if (status == VCS_EVT_RECV_RESP)
                break;
            else
                retry++;

            if (retry > 10)
                break;
        }

//        LOG_PRINTF(("comm_server receive response, read event from comm server: %d", status));

        ushInitStandardFlexi(vcsReadEventBuf);

        shVarGetUnsignedInt(VCS_FLD_RECV_BUFSIZE, (unsigned int *) &resSize);
//        LOG_PRINTF(("comm_server Receive Buffer Size :%d", resSize));

        shVarGetUnsignedInt(VCS_FLD_SESS_ERROR, (unsigned int *) &vcsErr);
//        LOG_PRINTF(("comm_server Receive Generic Error :%d", vcsErr));

        shVarGetUnsignedInt(VCS_FLD_SESS_NATIVE, (unsigned int *) &vcsNative);
//        LOG_PRINTF(("comm_server Receive Native Error  :%d", vcsNative));

        if (vcsNative)
            return vcsNative;

        if (resSize <= 0)
        {
//            LOG_PRINTF(("Received empty packet"));
            return 0;
		//return VCS_ERROR;
        }

        if (vcsErr != 0)
        {
//            LOG_PRINTF(("comm_server Receive response was unsuccessful: %d", vcsErr));
            return vcsErr;
        }

        while(1)
        {
            LOG_PRINTF(("waiting for raw data"));
            status = vcsReadEventFromServer(3, 3); //10,5
            if (status == VCS_EVT_DATA_RAW)
                break;
            else
                retry++;

            if (retry > 3)
                break;
        }

  			//LOG_PRINTF(("Received New Added 2 %s", vcsReadEventBuf));
        iLenResponse = resSize + inIndex;
        memcpy(&strResponse[inIndex], (char *)vcsReadEventBuf, resSize);
        memcpy((char *)vcsReadEventBuf, strResponse, iLenResponse);

        vcsReadEventBuf[iLenResponse] = '\0';
  	}

	return VCS_SUCCESS;
}

short vcsFlushEvents()
{
    short evt = 1;
    short evtCnt = 0;
	while(evt)
	{
		/*read event from the queue*/
		evt = EESL_read_cust_evt((unsigned char*)vcsReadEventBuf, sizeof(vcsReadEventBuf),(unsigned int *)&eventDataSize, senderName);
		evtCnt++;
		/*if the read events are > maximum events the queue can hold then break*/
        if (evtCnt > MAX_QEVENTS)
			break;
	}
	return evtCnt;
}


short vcsDeInitSession(void)
{
    short status = -1;
    short retry = 0;
	unsigned int vcsErr;
	unsigned int vcsNative;
	//char timeBuf[25];

	LOG_PRINTF(("Before Deinitializing"));
	//getTime(timeBuf);

	/*Initialize Flexi record for communication*/
	vVarInitRecord(vcsWriteEventBuf, sizeof(vcsWriteEventBuf), 0);
	ushInitStandardFlexi(vcsWriteEventBuf);

	/*Add fields to response request packet*/
	shVarAddUnsignedInt(VCS_FLD_SESS_HANDLE, hSession);
    shGetRecordLength(vcsWriteEventBuf, (int *)&(eventDataSize));

	/*send message to comm server*/
    status = vcsWriteEventToServer(VCS_EVT_DEINIT_REQ, 30);
    LOG_PRINTF(("comm_server Receive response status : %d", status));

	/*If the request to comm server failed*/
    if (status != 0)
		return VCS_ERROR;

	/*read message from comm server*/
	while(1)
	{
		LOG_PRINTF(("waiting for VCS_EVT_DISC_RESP"));
		status = vcsReadEventFromServer(10,25);
        if (status == VCS_EVT_DEINIT_RESP)
			break;
		else
			retry++;

		/*maximum number of retries to read the response from comm server*/
        if (retry > 10)
			break;
	}

	LOG_PRINTF(("comm_server deinit response, read event from comm server : %d",status));

	/*Initialize read event flexi record to parse the values*/
	ushInitStandardFlexi(vcsReadEventBuf);

	/*Extract Generic error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_ERROR, (unsigned int *) &vcsErr);
	LOG_PRINTF (("comm_server Receive Generic Error :%d", vcsErr));

	/*Extract Native error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_NATIVE, (unsigned int *) &vcsNative);
	LOG_PRINTF (("comm_server Receive Native Error	:%d", vcsNative));

	if(vcsErr != 0)
	{
		LOG_PRINTF(("comm_server deinit response was unsuccessful: %d",vcsErr));
		return vcsErr;
	}
	LOG_PRINTF(("After Deinitializing"));
	//getTime(timeBuf);
	return VCS_SUCCESS;
}

short vcsDisconnect(void)
{
    short status = -1;
    short retry = 0;
	unsigned int vcsErr;
	unsigned int vcsNative;
	//char timeBuf[25];


	LOG_PRINTF(("Before Disconnecting"));

	if(flagInitSession != VCS_SUCCESS)
	{
		return VCS_SUCCESS;
	}

	//getTime(timeBuf);
    EESL_send_event(LogicalName,11001,NULL,0);//--added -hp
	vcsFlushEvents();
	LOG_PRINTF(("Calling Disconnect"));
	/*Initialize Flexi record for communication*/
	vVarInitRecord(vcsWriteEventBuf, sizeof(vcsWriteEventBuf), 0);
	ushInitStandardFlexi(vcsWriteEventBuf);

	/*Add fields to response request packet*/
	shVarAddUnsignedInt(VCS_FLD_SESS_HANDLE, hSession);
    shGetRecordLength(vcsWriteEventBuf, (int *)&(eventDataSize));

	/*send message to comm server*/
    status = vcsWriteEventToServer(VCS_EVT_DISC_REQ, 30);
    LOG_PRINTF(("comm_server Receive response status: %d", status));

	/*If the request to comm server failed*/
    if(status != 0)
		return VCS_ERROR;

	/*read message from comm server*/
	while(1)
	{
		LOG_PRINTF(("waiting for VCS_EVT_DISC_RESP"));
        status = vcsReadEventFromServer(10, 25);
		LOG_PRINTF(("status = %d",status));
        if (status == VCS_EVT_DISC_RESP)
			break;
		else
			retry++;

		/*maximum number of retries to read the response from comm server*/
        if (retry > 10)
			break;
	}

	LOG_PRINTF(("comm_server disconnect response, read event from comm server : %d",status));

	/*Initialize read event flexi record to parse the values*/
	ushInitStandardFlexi(vcsReadEventBuf);

	/*Extract Generic error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_ERROR, (unsigned int *) &vcsErr);
	LOG_PRINTF (("comm_server Receive Generic Error :%d", vcsErr));

	/*Extract Native error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_NATIVE, (unsigned int *) &vcsNative);
	LOG_PRINTF (("comm_server Receive Native Error	:%d", vcsNative));

	if(vcsErr != 0)
	{
		LOG_PRINTF(("comm_server Disconnect response was unsuccessful: %d",vcsErr));
		return vcsErr;
	}

	LOG_PRINTF(("After Disconnectin"));
	//getTime(timeBuf);
	return VCS_SUCCESS;
}

short vcsGetStatus(void)
{
    short status = -1;
    short retry = 0;
	unsigned int vcsErr;
	unsigned int vcsNative;
	unsigned int vcsConnStatus;
	unsigned int vcsSignal;
	//char timeBuf[25];

	LOG_PRINTF(("Before Getstatus"));
	//getTime(timeBuf);
	/*Initialize Flexi record for communication*/
	vVarInitRecord(vcsWriteEventBuf, sizeof(vcsWriteEventBuf), 0);
	ushInitStandardFlexi(vcsWriteEventBuf);

	/*Add fields to response request packet*/
    memset(&StatusIDs, 0, sizeof(StatusIDs));
    StatusIDs[0] = VCS_FLD_CONN_STATUS;
    StatusIDs[1] = VCS_FLD_STATUS_SIGNAL_STRENGTH_PERCNT;
    shVarAddData(VCS_FLD_STATUS_IDS, (unsigned char*) &StatusIDs, sizeof(StatusIDs));
	shVarAddUnsignedInt(VCS_FLD_SESS_HANDLE, hSession);
	shGetRecordLength(vcsWriteEventBuf,(int *)&(eventDataSize));

	/*send message to comm server*/
	status = vcsWriteEventToServer(VCS_EVT_STATUS_REQ,30);
	//SVC_WAIT(10);
	LOG_PRINTF(("comm_server status request status : %d",status));

	/*If the request to comm server failed*/
	if(status!=0)
		return VCS_ERROR;

	/*read message from comm server*/
	while(1)
	{
		LOG_PRINTF(("waiting for VCS_EVT_STATUS_RESP"));
        status = vcsReadEventFromServer(10, 5);
		//SVC_WAIT(10);
        if (status == VCS_EVT_STATUS_RESP)
			break;
		else
			retry++;

		/*maximum number of retries to read the response from comm server*/
        if (retry > 20)
			break;
	}

    LOG_PRINTF(("comm_server status response, read event from comm server: %d", status));

	/*Initialize read event flexi record to parse the values*/
	ushInitStandardFlexi(vcsReadEventBuf);

	/*Extract Generic error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_ERROR, (unsigned int *) &vcsErr);
	LOG_PRINTF (("comm_server Receive Generic Error :%d", vcsErr));

	/*Extract Native error*/
	shVarGetUnsignedInt(VCS_FLD_SESS_NATIVE, (unsigned int *) &vcsNative);
	LOG_PRINTF (("comm_server Receive Native Error	:%d", vcsNative));

	/*Extract */
	shVarGetUnsignedInt(VCS_FLD_CONN_STATUS, (unsigned int *) &vcsConnStatus);
	LOG_PRINTF (("comm_server Receive conn status :%d", vcsConnStatus));

	/*Extract Native error*/
	shVarGetUnsignedInt(VCS_FLD_STATUS_SIGNAL_STRENGTH_PERCNT, (unsigned int *) &vcsSignal);
	LOG_PRINTF (("comm_server Receive signal	:%d", vcsSignal));

	if(vcsErr != 0)
	{
		LOG_PRINTF(("comm_server status response was unsuccessful: %d",vcsErr));
		return vcsErr;
	}
	LOG_PRINTF(("After Getstatus"));
	//getTime(timeBuf);
	return VCS_SUCCESS;
}

//************************* Added by PD for as per Updated VISA1 Flow *****************
short vcsSendReceive(const char *szReq, char *szRes, short rcvSize, short rcvTimeout)
{
		char szlen[4];
    int status = -1;
    int cnt = 0;
    int cntZero = 0;
    int flgRcvZero = 0;
		char strError[25];
    short retVal = -1;
    char strETX[] = {0x03, 0x00};
    short hfile2 = -1;
    int len = 0, i = 0;
		char szIPType[3];
    short ipType = 0;

	LOG_PRINTF(("In Send receive"));
	
	
  memset(szIPType, 0, sizeof(szIPType));
   memset(vcsReadEventBuf, 0, sizeof(vcsReadEventBuf));
	readDLDParam("#IP_TYPE", szIPType);
  ipType = atoi(szIPType);

	ShowLText(8,"Receiving..");
    //send request
    
    
    
   status =SaifvcsSend(szReq);
     
   // status = vcsSendRequestToServer(szReq);
	  
	  if (status != VCS_SUCCESS)
		{
	     //  LOG_PRINTF(("send status: %d = failed. returning", status));
			//return status;
				return -3;
		}
		
   // status = vcsReceiveRespFromServer(2000, rcvTimeout);
   
   
   
   
   
   
 //  status=1;
  // while(status>0)
  // {
   			status=SaifvcsReceive(szRes,rcvSize,rcvTimeout);
   			
   			LOG_PRINTF(("szRes*********************=%s", szRes));
   		  LOG_PRINTF(("Recv Status=%d", status));
   		  
//   		 
   			memset(szRes,0,sizeof(szRes));
   			memset(vcsReadEventBuf,0,sizeof(vcsReadEventBuf));
   			if(status>0)
   			{
   				status=SaifvcsReceive(szRes,rcvSize,1);
    			LOG_PRINTF(("szRes*********************=%s", szRes));
   				LOG_PRINTF(("Recv Status=%d", status));
   			}
//  			memset(szRes,0,sizeof(szRes));
//   			memset(vcsReadEventBuf,0,sizeof(vcsReadEventBuf));
//   			
//   			if(status>0)
//   			{
//   				
//   				status=SaifvcsReceive(szRes,rcvSize,2);
//   				
//   						LOG_PRINTF(("szRes*********************=%s", szRes));
//   				LOG_PRINTF(("Recv Status=%d", status));
//   			}		
//   		  LOG_PRINTF(("szRes*********************=%s", szRes));

    //}
   
//   ShowLText(8,"Receiving...");
//    if (status == VCS_SUCCESS)
//		{
//		
//			  LOG_PRINTF(("Received First Buffer%s", vcsReadEventBuf));
//			  LOG_PRINTF(("Process 1"));
//     	  memset(vcsReadAllPipeData, 0, sizeof(vcsReadAllPipeData));
//        LOG_PRINTF(("Process 2"));
//       // status = EESL_send_event(APP_NAME, 11001, NULL, 0);
//				LOG_PRINTF(("Process 3"));
//				
//				
//			
//				while(status>0)
//				{
//			      memset(vcsInPipeData, 0, sizeof(vcsInPipeData));
//						status = EESL_read_cust_evt((unsigned char*)vcsInPipeData, sizeof(vcsInPipeData),(unsigned int*)&eventDataSize, senderName);
//    			  LOG_PRINTF(("EESL Status is %d=",status));
//    			  
//    			  
//    				if(status>0)
//    				{
//    					  LOG_PRINTF(("********************In pipe Data****************************: %s",vcsInPipeData));
//		    				LOG_PRINTF(("Inside Pipe Loop"));
//				    		memset(szlen, 0, sizeof(szlen));
//				  			LOG_PRINTF(("Process 1, get InPipeData Length"));
//				  			sprintf(szlen, "%d", strlen(vcsInPipeData));
//								LOG_PRINTF(("Process 2, Concat length in VCSReadAllPipeData Buffer"));
//								strcat(vcsReadAllPipeData, szlen);
//								LOG_PRINTF(("Process 3, Concatenating InpipeData to ReadAllpipeData Buffer"));
//							  strcat(vcsReadAllPipeData, vcsInPipeData);
//
//							  LOG_PRINTF(("********************Concatenated Initial Pipe Data****************************: %s",vcsReadAllPipeData));
//						 		LOG_PRINTF(("Done Done"));
//						}
//						else
//						{
//							break;
//						}
//    	  }	
//    		
//    		LOG_PRINTF(("Process Pipe Data received"));
//    		LOG_PRINTF(("********************Final Pipe Data****************************: %s",vcsReadAllPipeData));
//    		
//    		LOG_PRINTF(("Process 4"));
//    		memset(szlen, 0, sizeof(szlen));
//  			LOG_PRINTF(("Process 5"));
//  			sprintf(szlen, "%d", strlen(vcsReadAllPipeData));
//				LOG_PRINTF(("Process 6"));
//				strcat(vcsReadEventBuf, szlen);
//			
//				
//				
//			//memcpy(vcsReadEventBuf, vcsReadPipeData, strlen(vcsReadPipeData)+1);
//				LOG_PRINTF(("Process 8"));
//			  strcat(vcsReadEventBuf, vcsReadAllPipeData);
//			  LOG_PRINTF(("Process 9"));
//				memset(szRes, 0, sizeof(szRes));
//        strcpy(szRes, (char *)vcsReadEventBuf);
//      
//     	  LOG_PRINTF(("********************Concated Final Response****************************: %s",szRes));
//     	  
//        status = etherDisconnect(rcvTimeout);
//        LOG_PRINTF(("etherDisconnect return status %d", status));
//				//break;
//				LOG_PRINTF(("communication complete. returning 1"));
//				return 1;
//		}
//		else if (status != VCS_SUCCESS)
//		{
//        retVal = CheckErr(status, strError);
//        window(1, 8, 21, 8);
//				clrscr();
//                write_at(strError, strlen(strError), 1, 8);
//				SVC_WAIT(3000);
//				//break;
//				return -1;
//		}
//
//   LOG_PRINTF(("Received %s", vcsReadEventBuf));
//    LOG_PRINTF(("attempt %d", cnt));

		return VCS_SUCCESS;
}

short CheckErr(int er, char *err_resp)
{
	switch(er)
	{
		case 0:
			return VCS_SUCCESS;

		case VCS_ERR_MISSING_DATA:
			strcpy(err_resp,"Data missing");
			return(VCS_ERROR);

		case VCS_ERR_NO_SESSHAND:
			strcpy(err_resp,"No session handle");
			return(VCS_ERROR);

		case VCS_ERR_DUP_SESSION:
			strcpy(err_resp,"Duplicate session");
			return(VCS_ERROR);

		case VCS_ERR_NO_MEMORY:
			strcpy(err_resp,"No memory");
			return(VCS_ERROR);

		case VCS_ERR_INVALID_SESS:
			strcpy(err_resp,"Invalid session");
			return(VCS_ERROR);

		case VCS_ERR_NO_HOSTCTX:
			strcpy(err_resp,"No host connection");
			return(VCS_ERROR);

		case VCS_ERR_NO_CONN_PORT:
			strcpy(err_resp,"No port connection");
			return(VCS_ERROR);

		case VCS_ERR_NO_CONN_URL:
			strcpy(err_resp,"No url connection");
			return(VCS_ERROR);

		case VCS_ERR_CONN_FAILED:
			strcpy(err_resp,"Connection failed");
			return(VCS_ERROR);

		case VCS_ERR_NO_SENDBUF_SIZE:
			strcpy(err_resp,"No send buffer size");
			return(VCS_ERROR);

		case VCS_ERR_LARGE_SEND:
			strcpy(err_resp,"Large data send");
			return(VCS_ERROR);

		case VCS_ERR_DATA_SEND:
			strcpy(err_resp,"Data sending error");
			return(VCS_SUCCESS);

		case VCS_ERR_NO_RECVBUF_SIZE:
			strcpy(err_resp,"No receive buffer size");
			return(VCS_ERROR);

		case VCS_ERR_LARGE_RECV:
			strcpy(err_resp,"Large data receievd");
			return(VCS_ERROR);

		case VCS_ERR_DATA_RECV:
			strcpy(err_resp,"data receiving error");
			return(VCS_ERROR);

		case VCS_ERR_IO_INIT_FAILED:
			strcpy(err_resp,"IO initialisation failed");
			return(VCS_ERROR);

		default:
            strcpy(err_resp, "Transaction failed");
			return(VCS_ERROR);
	}
}

int waitEnq(short rcvTimeout)
{
    short retry = 0, retVal = -1;
    int status = -1;
	char strError[25];
    //char ENQ[2] = {0x05, 0x00};
    //char NAK[2] = {0x15, 0x00};

//    LOG_PRINTF(("Receive Timeout is %d",rcvTimeout));
	while(1)
	{
//        LOG_PRINTF(("waiting for ENQ"));
        status = vcsReceiveRespFromServer(1, rcvTimeout);
//        LOG_PRINTF(("Wait ENQ vcsReadEventBuf %s", vcsReadEventBuf));
//        LOG_PRINTF(("waitEnq: Status of waiting for vcsReceiveRespFromServer is %d", status));
        if (status == 1)
		{
			//if(strstr(vcsReadEventBuf,ENQ))
            if ((unsigned int)vcsReadEventBuf[0] == 0x05)
			{
//                LOG_PRINTF(("RECEIVED ENQ FROM SERVER"));
				RevEnqFlag = 1;
				return 1;
			}

			//if(strstr(vcsReadEventBuf,NAK))
            if ((unsigned int)vcsReadEventBuf[0]==0x15)
			{
//                LOG_PRINTF(("RECEIVED NAK FROM SERVER"));
				return -1;
			}

		}
		else
		{
			retVal=CheckErr(status,strError);
			LOG_PRINTF(("waitEnq: CheckErr returned %s",strError));
			retry++;
            LOG_PRINTF(("retry no: %d", retry));
		}

		//maximum number of retries to read the response from comm server
		if(retry>10)
		{
			LOG_PRINTF(("NOT RECEIVED ENQ FROM SERVER"));
			return -1;
		}
	}
}

int waitAck(short rcvTimeout)
{
    short retry = 0, retVal = -1;
    int status = -1;
	char strError[25];
    char ACK[2] = {0x06, 0x00};
    char NAK[2] = {0x15, 0x00};

//    LOG_PRINTF(("Receive Timeout is %d", rcvTimeout));
	while(1)
	{
//        LOG_PRINTF(("waiting for ACK %ld", read_ticks()));
        status = vcsReceiveRespFromServer(1, rcvTimeout);
//        status = vcsReceiveRespFromServer(255, rcvTimeout);
//        LOG_PRINTF(("Wait ACK vcsReadEventBuf %d %ld %d", iLenResponse, read_ticks(), vcsReadEventBuf[0]));
//        LOG_PRINTF(("vcsSendReceive: Status of waiting for vcsReceiveRespFromServer is %d %d", status, vcsReadEventBuf[0]));
        if (status == 1)
		{
            //if (strstr(vcsReadEventBuf, ACK))
            if ((unsigned int)vcsReadEventBuf[0] == 0x06)
			{
//                LOG_PRINTF(("RECEIVED ACK FROM SERVER"));
				break;
			}

            //if(strstr(vcsReadEventBuf, NAK))
            if ((unsigned int)vcsReadEventBuf[0] == 0x15)
			{
//                LOG_PRINTF(("RECEIVED NAK FROM SERVER"));
				return -1;
			}
		}
		else
		{
            retVal = CheckErr(status, strError);
            LOG_PRINTF(("waitAck: CheckErr returned %s", strError));
			retry++;
            LOG_PRINTF(("retry no: %d", retry));
		}

		//maximum number of retries to read the response from comm server
        if (retry > 10)
		{
			LOG_PRINTF(("NOT RECEIVED ACK FROM SERVER"));
			return -1;
		}
	}
	return 1;
}

int sendAck(void)
{
	int status=-1;
	char strAck[4];

	LOG_PRINTF(("inside sendAck"));

	sprintf(strAck,"%c",0x06);

	status=vcsSendRequestToServer(strAck);
	return status;
}

int etherDisconnect(short rcvTimeout)
{
	short retry=0,retVal=-1;
	int status=-1;
	char strError[25];
	//char EOT[2]={0x04,0x00};
	//char NAK[2]={0x15,0x00};
	//char RS[2]={0x1E,0x00};
	short recvFlag=0;

	memset(strError,0,sizeof(strError));

	LOG_PRINTF(("inside etherDisconnect"));

	LOG_PRINTF(("calling vcsDisconnect"));
	retVal=vcsDisconnect();
	LOG_PRINTF(("vcsDisconnect Return Value:%d",retVal));

	return 1;
}

int sendNAK(void)
{
	int status=-1;
	char strNAK[4];

	LOG_PRINTF(("inside sendNAK"));

	sprintf(strNAK,"%c",0x15);

	status=vcsSendRequestToServer(strNAK);
	return status;
}
//FF-02Mar2014
short SaifvcsSend(const char *szReq)
{
	int status = -1;

	LOG_PRINTF(("SaifvcsSend"));

    //send request
    status = vcsSendRequestToServer(szReq);
    if (status != VCS_SUCCESS)
	{
		return VCS_ERROR;
	}

  LOG_PRINTF(("send status: %d", status));

	return VCS_SUCCESS;
}

//FF-02Mar2014
short SaifvcsReceive(char *szRes, short rcvSize, short rcvTimeout)
{
  int status = -1;
  int cnt = 0;
  int cntZero = 0;
  int flgRcvZero = 0;
	char strError[25];
  short retVal = -1;

	LOG_PRINTF(("SaifvcsReceive"));

  for(;cnt < 20 || flgRcvZero != 1;)
	{
    status = vcsReceiveRespFromServer(1000, rcvTimeout);
    LOG_PRINTF(("vcsReceiveRespFromServer status[%d] - cnt[%d]", status, cnt));
    if (status == 0)
		{
      if (flgRcvZero == 1)//zero bytes received in given rcvTimeout 
				break;
			else
			{
				cntZero++;
        if (cntZero > 2)//if two times scan unsuccessful, return 
					break;
				continue;
			}
		}
		else
		{
      if (status == VCS_SUCCESS)
			{
				strcpy(szRes, (char *)vcsReadEventBuf);//copying received data 
				LOG_PRINTF(("Bytes Received [%d]", strlen(vcsReadEventBuf)));
        flgRcvZero = 1;
				return 1;
			}
      else if (status != VCS_SUCCESS)
			{
       	retVal = CheckErr(status, strError);
        window(1, 8, 21, 8);
				clrscr();
        write_at(strError, strlen(strError), 1, 8);
				//SVC_WAIT(3000);
				SVC_WAIT(100);//100 msec delay 
				return -1;
			}
		}

    //LOG_PRINTF(("Received %s", vcsReadEventBuf));

		SVC_WAIT(1);
		cnt++;
    //LOG_PRINTF(("attempting %d", cnt));
	}

	return VCS_ERROR;
}