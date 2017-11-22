/*********************************************************************************
*
*  Copyright (C) 2014 Hisilicon Technologies Co., Ltd.  All rights reserved.
*
*  This program is confidential and proprietary to Hisilicon Technologies Co., Ltd.
*  (Hisilicon), and may not be copied, reproduced, modified, disclosed to
*  others, published or used, in whole or in part, without the express prior
*  written permission of Hisilicon.
*
***********************************************************************************
  File Name                 :   adec_intf.c
  Version                   :   Initial Draft
  Author                    :   Hisilicon multimedia software group
  Created                   :   2006/01/23
  Last Modified             :
  Description               :
  Function List         :   So Much ....
  History               :
  1.Date                :   2006/01/23
    Author              :   f47391
    Modification        :   Created file

******************************************************************************/
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/delay.h>

#include <asm/uaccess.h>


#include "hi_drv_mmz.h"
#include "hi_drv_stat.h"
#include "hi_drv_sys.h"
#include "drv_dev_ext.h"
#include "hi_drv_proc.h"
#include "hi_error_mpi.h"
#include "drv_adec_ext.h"
#include "hi_drv_adec.h"
#include "hi_kernel_adapt.h"
#include "drv_file_ext.h"


#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

typedef struct hiADEC_KADDR_S
{
    HI_BOOL            bUsed;
    ADEC_PROC_ITEM_S*  psAdecKernelAddr;
    MMZ_BUFFER_S       AdecProcMmz;
    HI_CHAR            szProcMmzName[32];
} ADEC_KADDR_S;

typedef struct tagAdec_REGISTER_PARAM_S
{
    DRV_PROC_READ_FN  pfnReadProc;
    DRV_PROC_WRITE_FN pfnWriteProc;
} ADEC_REGISTER_PARAM_S;

static ADEC_KADDR_S g_sAdecKAddrArray[ADEC_INSTANCE_MAXNUM];
static UMAP_DEVICE_S g_sAdecDevice;
HI_DECLARE_MUTEX(g_AdecMutex);

#ifndef HI_ADVCA_FUNCTION_RELEASE
static ADEC_REGISTER_PARAM_S s_stProcParam;
static HI_S32 ADEC_RegProc(HI_U32 u32AdecNum, ADEC_REGISTER_PARAM_S* pstParam);

static HI_S32 ADECConvertSampleRate(HI_UNF_SAMPLE_RATE_E enSampleRate)
{
    return (HI_S32)enSampleRate;
}
#endif

/*****************************************************************************
 Prototype    : ADECResetVKaddrArray
 Description  : Reset
 Input        : None
 Output       : None
 Return Value :
 Calls        :
 Called By    :
  History        :
  1.Date         : 2006/1/18
    Author       : vicent feng
    Modification : Created function
*****************************************************************************/
static HI_VOID ADECResetVKaddrArray(HI_VOID)
{
    HI_U32 i;

    for (i = 0; i < ADEC_INSTANCE_MAXNUM; i++)
    {
        g_sAdecKAddrArray[i].bUsed = HI_FALSE;
        g_sAdecKAddrArray[i].psAdecKernelAddr = NULL;
    }

    return;
}

/* Register HIAO Dev                                                         */
static HI_S32 ADECRegisterDevice(struct file_operations* drvFops, PM_BASEOPS_S* drvops)
{
    ADECResetVKaddrArray();

    /*register adec Dev*/
    snprintf(g_sAdecDevice.devfs_name, sizeof(g_sAdecDevice.devfs_name), "%s", UMAP_DEVNAME_ADEC);
    g_sAdecDevice.fops  = drvFops;
    g_sAdecDevice.minor = UMAP_MIN_MINOR_ADEC;
    g_sAdecDevice.owner  = THIS_MODULE;
    g_sAdecDevice.drvops = drvops;
    if (HI_DRV_DEV_Register(&g_sAdecDevice) < 0)
    {
        //HI_ERR_ADEC("adec device register failed!\n");
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

/* Unregister adec Dev                                                       */
static HI_VOID ADECUnregisterDevice(HI_VOID)
{
    HI_DRV_DEV_UnRegister(&g_sAdecDevice);

    return;
}

static HI_S32 ADEC_DRV_Create(unsigned long arg, struct file* filp)
{
    HI_U32 i;
    HI_S32 Ret;
    ADEC_KADDR_S*  psKAddrElem;

    for (i = 0;  i < ADEC_INSTANCE_MAXNUM;  i++)
    {
        if (g_sAdecKAddrArray[i].bUsed == HI_FALSE)
        {
            break;
        }
    }

    if (i >= ADEC_INSTANCE_MAXNUM)
    {
        HI_ERR_ADEC("Drv source is not enough to support so much adec\n");
        return -1;
    }

    g_sAdecKAddrArray[i].bUsed = HI_TRUE;
    g_sAdecKAddrArray[i].psAdecKernelAddr = NULL;
    snprintf(g_sAdecKAddrArray[i].szProcMmzName, sizeof(g_sAdecKAddrArray[i].szProcMmzName), "%s%02d", "ADEC_Proc", i);
    filp->private_data = ((void*)(&g_sAdecKAddrArray[i]));


#ifndef HI_ADVCA_FUNCTION_RELEASE
    ADEC_RegProc(i, &s_stProcParam);
#endif


    psKAddrElem = (ADEC_KADDR_S*) filp->private_data;

    if (!psKAddrElem)
    {
        HI_ERR_ADEC(KERN_ERR "psKAddrElem == NULL");
        return -1;
    }

    Ret = HI_DRV_MMZ_AllocAndMap(psKAddrElem->szProcMmzName, MMZ_OTHERS, sizeof(ADEC_PROC_ITEM_S), 0, &psKAddrElem->AdecProcMmz);
    if (HI_SUCCESS != Ret)
    {
        HI_ERR_ADEC("HI_DRV_MMZ_AllocAndMap failed\n");
        return -1;
    }
    if (!psKAddrElem->psAdecKernelAddr)
    {
        psKAddrElem->psAdecKernelAddr = (ADEC_PROC_ITEM_S*)psKAddrElem->AdecProcMmz.u32StartVirAddr;
        psKAddrElem->psAdecKernelAddr->enPcmCtrlState = ADEC_CMD_CTRL_STOP;
        psKAddrElem->psAdecKernelAddr->u32SavePcmCnt = 0;
        psKAddrElem->psAdecKernelAddr->enEsCtrlState = ADEC_CMD_CTRL_STOP;
        psKAddrElem->psAdecKernelAddr->u32SaveEsCnt = 0;
    }
    if (copy_to_user((void*)arg, &psKAddrElem->AdecProcMmz.u32StartPhyAddr, sizeof(HI_U32)))
    {
        HI_ERR_ADEC("copy_to_user failed\n");
        return -1;
    }
    HI_INFO_ADEC("ADEC_DRV_Create %d\n", i);


    return HI_SUCCESS;
}

/*****************************************************************************
 Prototype    : ADEC_DRV_Open
 Description  : Open
 Input        : None
 Output       : None
 Return Value :
 Calls        :
 Called By    :
  History        :
  1.Date         : 2006/1/18
    Author       : vicent feng
    Modification : Created function
*****************************************************************************/
static HI_S32 ADEC_DRV_Open(struct inode* inode, struct file* filp)
{
    return 0;
}


static HI_S32 ADEC_DRV_Destroy(struct file* filp)
{
    ADEC_KADDR_S*  psKAddrElem;
    HI_U32 u32ChanId;
    HI_CHAR aszBuf[16];


    psKAddrElem = (ADEC_KADDR_S*) filp->private_data;

    if (!psKAddrElem)
    {
        HI_ERR_ADEC("psKAddrElem is NULL\n");
        return -1;
    }

    if (psKAddrElem->bUsed != HI_TRUE)
    {
        HI_ERR_ADEC("psKAddrElem is Not Used\n");
        return -1;
    }
#ifndef HI_ADVCA_FUNCTION_RELEASE
    u32ChanId = (psKAddrElem->szProcMmzName[9] - '0') * 10 + (psKAddrElem->szProcMmzName[10] - '0');
    snprintf(aszBuf, sizeof(aszBuf), "adec%02d", u32ChanId);
    HI_DRV_PROC_RemoveModule(aszBuf);
#endif
    if (psKAddrElem->psAdecKernelAddr)
    {
        HI_DRV_MMZ_UnmapAndRelease(&psKAddrElem->AdecProcMmz);
    }
    psKAddrElem->psAdecKernelAddr = NULL;
    psKAddrElem->bUsed = HI_FALSE;

    HI_INFO_ADEC("ADEC_DRV_Destroy Finish\n");
    return 0;
}

static HI_S32 ADEC_DRV_Close(struct inode* inode, struct file* filp)
{
    ADEC_KADDR_S*  psKAddrElem;
    HI_S32 Ret;
    HI_U32 u32ChanId;
    HI_CHAR aszBuf[16];

    Ret = down_interruptible(&g_AdecMutex);
    if (Ret != HI_SUCCESS)
    {
        HI_ERR_ADEC("down_interruptible Failed\n");
        return HI_FAILURE;
    }
    psKAddrElem = (ADEC_KADDR_S*) filp->private_data;

    if (!psKAddrElem)
    {
        up(&g_AdecMutex);
        HI_WARN_ADEC("psKAddrElem is NULL\n");
        return -1;
    }

    if (psKAddrElem->bUsed != HI_TRUE)
    {
        up(&g_AdecMutex);
        HI_WARN_ADEC("psKAddrElem is Not Used\n");
        return -1;
    }
#ifndef HI_ADVCA_FUNCTION_RELEASE
    u32ChanId = (psKAddrElem->szProcMmzName[9] - '0') * 10 + (psKAddrElem->szProcMmzName[10] - '0');
    snprintf(aszBuf, sizeof(aszBuf), "adec%02d", u32ChanId);
    HI_DRV_PROC_RemoveModule(aszBuf);
#endif
    if (psKAddrElem->psAdecKernelAddr)
    {
        HI_DRV_MMZ_UnmapAndRelease(&psKAddrElem->AdecProcMmz);
    }
    psKAddrElem->psAdecKernelAddr = NULL;
    psKAddrElem->bUsed = HI_FALSE;
    psKAddrElem = HI_NULL;
    up(&g_AdecMutex);

    HI_INFO_ADEC("ADEC_DRV_Close Finish\n");
    return 0;
}

/*****************************************************************************
 Prototype    : ADEC_DRV_Release
 Description  : Release
 Input        : None
 Output       : None
 Return Value :
 Calls        :
 Called By    :
  History        :
  1.Date         : 2006/1/18
    Author       : vicent feng
    Modification : Created function
*****************************************************************************/
static HI_S32 ADEC_DRV_Release(struct inode* inode, struct file* filp)
{
    HI_S32 Ret;

    Ret = down_interruptible(&g_AdecMutex);
    if (Ret != HI_SUCCESS)
    {
        HI_ERR_ADEC("down_interruptible Failed\n");
        return HI_FAILURE;
    }
    HI_WARN_ADEC("ADEC_DRV_Release\n");

    ADEC_DRV_Close(inode, filp);

    up(&g_AdecMutex);
    return 0;

}

/*****************************************************************************
 Prototype    : ADEC_DRV_Ioctl
 Description  : Ioctl
 Input        : None
 Output       : None
 Return Value :
 Calls        :
 Called By    :
  History        :
  1.Date         : 2006/1/18
    Author       : vicent feng
    Modification : Created function
*****************************************************************************/

static long ADEC_DRV_Ioctl(struct file* filp,
                           HI_U32 cmd, unsigned long arg)
{
    HI_S32 Ret;

    Ret = down_interruptible(&g_AdecMutex);
    if (Ret != HI_SUCCESS)
    {
        HI_ERR_ADEC("down_interruptible Failed\n");
        return HI_FAILURE;
    }

    switch (cmd)
    {
        case DRV_ADEC_PROC_INIT:
        {
            Ret = ADEC_DRV_Create(arg, filp);
            if (Ret != HI_SUCCESS)
            {
                up(&g_AdecMutex);
                HI_ERR_ADEC("ADEC_DRV_Create Failed\n");
                return -1;
            }
            up(&g_AdecMutex);

            break;
        }
        case DRV_ADEC_PROC_EXIT:
        {
            Ret = ADEC_DRV_Destroy(filp);
            if (Ret != HI_SUCCESS)
            {
                HI_ERR_ADEC("ADEC_DRV_Destroy Failed\n");
                up(&g_AdecMutex);
            }
            up(&g_AdecMutex);
            break;
        }
        default:
        {
            up(&g_AdecMutex);
            return -1;
        }
    }

    up(&g_AdecMutex);
    return 0;
}

static struct file_operations ADEC_DRV_Fops =
{
    .owner          = THIS_MODULE,
    .open           = ADEC_DRV_Open,
    .unlocked_ioctl = ADEC_DRV_Ioctl,
    .release        = ADEC_DRV_Release,
};
#ifndef HI_ADVCA_FUNCTION_RELEASE

/*****************************************************************************
 Prototype    : ADEC_DRV_Proc
 Description  : Proc
 Input        : None
 Output       : None
 Return Value :
 Calls        :
 Called By    :
  History        :
  1.Date         : 2006/1/18
    Author       : vicent feng
    Modification : Created function
*****************************************************************************/
static HI_S32 ADEC_DRV_Proc(struct seq_file* p, HI_U32  u32ChNum)
{
    HI_U32 u32BufPercent, u32FramePercent, u32FrameFullNum, u32DataSize;
    ADEC_PROC_ITEM_S* tmp = NULL;
    DRV_PROC_ITEM_S*  pProcItem;

    pProcItem = p->private;

    PROC_PRINT(p, "\n--------------------------- ADEC[%02d] State --------------------------\n\n", u32ChNum);

    if (g_sAdecKAddrArray[u32ChNum].bUsed == HI_FALSE)
    {
        PROC_PRINT(p, "  ADEC[%02d] not open\n", u32ChNum);
        return HI_SUCCESS;
    }

    if (g_sAdecKAddrArray[u32ChNum].psAdecKernelAddr == NULL)
    {
        PROC_PRINT(p, "  ADEC[%02d] PROC not INIT\n", u32ChNum);
        return HI_SUCCESS;
    }

    tmp = (ADEC_PROC_ITEM_S*)g_sAdecKAddrArray[u32ChNum].psAdecKernelAddr;
    if (tmp->u32BufSize)
    {
        if (tmp->u32BufWrite >= tmp->s32BufRead)
        {
            u32DataSize    = (tmp->u32BufWrite - tmp->s32BufRead);
        }
        else
        {
            u32DataSize    = (tmp->u32BufSize - tmp->s32BufRead) + tmp->u32FrameWrite;
        }
        u32BufPercent = u32DataSize * 100 / tmp->u32BufSize;
    }
    else
    {
        u32BufPercent = 0;
        u32DataSize = 0;
    }

    if (tmp->u32FrameSize)
    {
        if (tmp->u32FrameWrite >= tmp->u32FrameRead)
        {
            u32FramePercent = (tmp->u32FrameWrite - tmp->u32FrameRead) * 100 / tmp->u32FrameSize;
            u32FrameFullNum = tmp->u32FrameWrite - tmp->u32FrameRead;
        }
        else
        {
            u32FramePercent = ((tmp->u32FrameSize
                                - tmp->u32FrameRead) + tmp->u32FrameWrite) * 100 / tmp->u32FrameSize;
            u32FrameFullNum = tmp->u32FrameSize - tmp->u32FrameRead + tmp->u32FrameWrite;
        }
    }
    else
    {
        u32FramePercent = 0;
        u32FrameFullNum = 0;
    }

    PROC_PRINT(p,
               "WorkState                              :%s\n"
               "CodecID                                :0x%x\n"
               "DecoderName                            :%s\n"
               "Description                            :%s\n"
               "*DecodeThreadID                        :%d\n"
               "Volume                                 :%d\n"
               "SampleRate                             :%d\n"
               "BitWidth                               :%d\n"
               "Channels                               :%d\n"
               "*PcmSamplesPerFrame                    :%d\n"
               "*BitsBytePerFrame                      :0x%x\n"
               "StreamFormat                           :%s\n\n"
               "*TryDecodetimes                        :%u\n"
               "FrameNum(Total/Error)                  :%d/%d\n"
               "FrameUnsupportNum                      :%d\n"
               "StreamCorruptNum                       :%d\n"
               "StreamBuf(Total/Use/Percent)(Bytes)    :%u/%d/%u%%\n"
               "StreamBuf(readPos/writePos)            :0x%x/0x%x\n"
               "OutFrameBuf(Total/Use/Percent)         :%u/%u/%u%%\n"
               "GetBuffer(Try/OK)                      :%u/%u\n"
               "PutBuffer(Try/OK)                      :%u/%u\n"
               "SendStream(Try/OK)                     :%u/%u\n"
               "ReceiveFrame(Try/OK)                   :%u/%u\n"
               "PtsLostNum                             :%d\n"
               "*DecodeThreadExecTimeOutCnt            :%u\n"
               "*DecodeThreadScheTimeOutCnt            :%u\n"
               "*DecodeThreadSleepTimeMS               :%d\n\n",
               (tmp->bAdecWorkEnable == HI_TRUE) ? "start" : "stop",
               tmp->u32CodecID,
               tmp->szCodecType,
               tmp->szCodecDescription,
               tmp->u32ThreadId, //verify
               tmp->u32Volume,
               (ADECConvertSampleRate(tmp->enSampleRate)),
               tmp->enBitWidth,
               tmp->u32OutChannels,
               tmp->u32PcmSamplesPerFrame,
               tmp->u32BitsOutBytesPerFrame,
               (tmp->enFmt == HI_TRUE) ? "packet" : "non-packet",
               tmp->u32DbgTryDecodeCount,
               tmp->u32FramnNm,
               (HI_S32)tmp->u32ErrFrameNum,
               tmp->u32CodecUnsupportNum,
               tmp->u32StreamCorruptNum,
               tmp->u32BufSize,
               u32DataSize,
               u32BufPercent,
               tmp->s32BufRead,
               tmp->u32BufWrite,
               tmp->u32FrameSize,
               u32FrameFullNum,
               u32FramePercent,
               tmp->u32DbgGetBufCount_Try,
               tmp->u32DbgGetBufCount,
               tmp->u32DbgPutBufCount_Try,
               tmp->u32DbgPutBufCount,
               tmp->u32DbgSendStraemCount_Try,
               tmp->u32DbgSendStraemCount,
               tmp->u32DbgReceiveFrameCount_Try,
               tmp->u32DbgReceiveFrameCount,
               tmp->u32PtsLost,
               tmp->ThreadExeTimeOutCnt,
               tmp->ThreadScheTimeOutCnt,
               tmp->u32AdecSystemSleepTime);

    return HI_SUCCESS;
}
#endif
/*****************************************************************************
 Prototype    : ADEC_DRV_Suspend
 Description  : Suspend
 Input        : None
 Output       : None
 Return Value :
 Calls        :
 Called By    :
  History        :
  1.Date         : 2010/5/15
    Author       : wei deng
    Modification : Created function
*****************************************************************************/
static int adec_pm_suspend(PM_BASEDEV_S* pdev, pm_message_t state)
{
#ifndef HI_ADVCA_FUNCTION_RELEASE
    HI_PRINT("ADEC suspend OK\n");
#endif
    return 0;
}


/*****************************************************************************
 Prototype    : ADEC_DRV_Resume
 Description  : Resume
 Input        : None
 Output       : None
 Return Value :
 Calls        :
 Called By    :
  History        :
  1.Date         : 2010/5/15
    Author       : wei deng
    Modification : Created function
*****************************************************************************/
static int adec_pm_resume(PM_BASEDEV_S* pdev)
{
#ifndef HI_ADVCA_FUNCTION_RELEASE
    HI_PRINT("ADEC resume OK\n");
#endif
    return 0;
}

static PM_BASEOPS_S adec_drvops =
{
    .probe        = NULL,
    .remove       = NULL,
    .shutdown     = NULL,
    .prepare      = NULL,
    .complete     = NULL,
    .suspend      = adec_pm_suspend,
    .suspend_late = NULL,
    .resume_early = NULL,
    .resume       = adec_pm_resume,
};
#ifndef HI_ADVCA_FUNCTION_RELEASE


#define ADEC_DEBUG_SHOW_HELP(u32ChNum) \
    do                                                         \
    {                                                          \
        HI_DRV_PROC_EchoHelper("\nfunction: save es data before audio decode\n"); \
        HI_DRV_PROC_EchoHelper("commad:   echo save_es start|stop > /proc/msp/adec%02d\n", u32ChNum); \
        HI_DRV_PROC_EchoHelper("example:  echo save_es start > /proc/msp/adec%02d\n", u32ChNum); \
        HI_DRV_PROC_EchoHelper("\nfunction: save pcm data after audio decode \n"); \
        HI_DRV_PROC_EchoHelper("commad:   echo save_pcm start|stop > /proc/msp/adec%02d\n", u32ChNum); \
        HI_DRV_PROC_EchoHelper("example:  echo save_pcm start > /proc/msp/adec%02d\n\n", u32ChNum); \
    } while (0)

#define ADEC_STRING_SKIP_BLANK(str)            \
    while (str[0] == ' ')      \
    {                                  \
        (str)++;                    \
    }

#define ADEC_STRING_SKIP_NON_BLANK(str) \
    while (str[0] != ' ')      \
    {                                  \
        (str)++;                    \
    }

static HI_S32 ADEC_DRV_ReadProc( struct seq_file* p, HI_VOID* v )
{
    HI_U32 u32ChNum;
    DRV_PROC_ITEM_S* pstProcItem;

    pstProcItem = p->private;

    if (HI_NULL == pstProcItem)
    {
        HI_ERR_ADEC("the proc item pointer of adec  is NULL\n");
        return HI_FAILURE;
    }

    u32ChNum = (pstProcItem->entry_name[4] - '0') * 10 + (pstProcItem->entry_name[5] - '0');
    if (u32ChNum >= ADEC_INSTANCE_MAXNUM)
    {
        PROC_PRINT(p, "Invalid Adec ID:%d.\n", u32ChNum);
        return HI_FAILURE;
    }

    ADEC_DRV_Proc( p, u32ChNum );

    return HI_SUCCESS;
}


#define  ADEC_PATH_NAME_MAXLEN  256
#define  ADEC_FILE_NAME_MAXLEN  256

static HI_S32 ADEC_DRV_WriteProc(struct file* file, const char __user* buf, size_t count, loff_t* ppos)
{
    HI_U32 u32ChNum;
    ADEC_CMD_SAVE_E enSaveCmd;
    HI_CHAR szBuf[48];
    HI_CHAR* pcBuf = szBuf;
    HI_CHAR* pcStartCmd = "start";
    HI_CHAR* pcStopCmd = "stop";
    HI_CHAR* pcSavePcmCmd = "save_pcm";
    HI_CHAR* pcSaveEsCmd = "save_es";
    HI_CHAR* pcHelpCmd = "help";
    struct seq_file* p = file->private_data;
    ADEC_PROC_ITEM_S* pstAdecProc;
    DRV_PROC_ITEM_S* pstProcItem = p->private;

    if ( copy_from_user( szBuf, buf, count ) )
    {
        HI_ERR_ADEC( "copy from user failed\n" );
        return HI_FAILURE;
    }

    u32ChNum = (pstProcItem->entry_name[4] - '0') * 10 + (pstProcItem->entry_name[5] - '0');

    if ( u32ChNum >= ADEC_INSTANCE_MAXNUM )
    {
        HI_ERR_ADEC( "Invalid Adec ID:%02d.\n", u32ChNum );
        goto SAVE_CMD_FAULT;
    }
    if ( g_sAdecKAddrArray[u32ChNum].bUsed == HI_FALSE )
    {
        HI_ERR_ADEC( "  ADEC[%02d] not open\n", u32ChNum );
        return HI_FAILURE;
    }

    if ( g_sAdecKAddrArray[u32ChNum].psAdecKernelAddr == NULL )
    {
        HI_ERR_ADEC( "  ADEC[%02d] PROC not INIT\n", u32ChNum );
        return HI_FAILURE;
    }

    pstAdecProc = ( ADEC_PROC_ITEM_S* )g_sAdecKAddrArray[u32ChNum].psAdecKernelAddr;
    ADEC_STRING_SKIP_BLANK( pcBuf );

    if ( strstr( pcBuf, pcSavePcmCmd ) )
    {
        enSaveCmd = ADEC_CMD_PROC_SAVE_PCM;
        pcBuf += strlen( pcSavePcmCmd );
    }
    else if ( strstr( pcBuf, pcSaveEsCmd ) )
    {
        enSaveCmd = ADEC_CMD_PROC_SAVE_ES;
        pcBuf += strlen( pcSaveEsCmd );
    }
    else if ( strstr( pcBuf, pcHelpCmd ) )
    {
        ADEC_DEBUG_SHOW_HELP(u32ChNum);
        return count;
    }
    else
    {
        goto SAVE_CMD_FAULT;
    }

    ADEC_STRING_SKIP_BLANK( pcBuf );

    if ( strstr( pcBuf, pcStartCmd ) )
    {
        if ( HI_SUCCESS != HI_DRV_FILE_GetStorePath( pstAdecProc->filePath, ADEC_PATH_NAME_MAXLEN ) )
        {
            HI_ERR_ADEC( "get store path failed\n" );
            return HI_FAILURE;
        }

        if ( enSaveCmd == ADEC_CMD_PROC_SAVE_PCM )
        {
            snprintf( pstAdecProc->filePath, sizeof( pstAdecProc->filePath ), "%s/adec%d_%d.pcm", pstAdecProc->filePath, u32ChNum, pstAdecProc->u32SavePcmCnt );
            if (pstAdecProc->enPcmCtrlState != ADEC_CMD_CTRL_START)
            {
                pstAdecProc->u32SavePcmCnt++;
                pstAdecProc->enPcmCtrlState = ADEC_CMD_CTRL_START;
            }
        }
        else if ( enSaveCmd == ADEC_CMD_PROC_SAVE_ES )
        {
            snprintf( pstAdecProc->filePath, sizeof( pstAdecProc->filePath ), "%s/adec%d_%d.es", pstAdecProc->filePath, u32ChNum, pstAdecProc->u32SaveEsCnt );
            if (pstAdecProc->enEsCtrlState != ADEC_CMD_CTRL_START)
            {
                pstAdecProc->u32SaveEsCnt++;
                pstAdecProc->enEsCtrlState =  ADEC_CMD_CTRL_START;
            }
        }
    }
    else if ( strstr( pcBuf, pcStopCmd ) )
    {
        if ( enSaveCmd == ADEC_CMD_PROC_SAVE_PCM )
        {
            pstAdecProc->enPcmCtrlState  = ADEC_CMD_CTRL_STOP;
        }
        else if ( enSaveCmd == ADEC_CMD_PROC_SAVE_ES )
        {
            pstAdecProc->enEsCtrlState  = ADEC_CMD_CTRL_STOP;
        }
    }
    else
    {
        goto SAVE_CMD_FAULT;
    }

    return count;

SAVE_CMD_FAULT:
    HI_ERR_ADEC( "proc cmd is fault\n" );
    ADEC_DEBUG_SHOW_HELP(u32ChNum);
    return HI_FAILURE;
}

#ifndef HI_ADVCA_FUNCTION_RELEASE

static ADEC_REGISTER_PARAM_S s_stProcParam =
{
    .pfnReadProc  = ADEC_DRV_ReadProc,
    .pfnWriteProc = ADEC_DRV_WriteProc,
};
#endif

static HI_S32 ADEC_RegProc(HI_U32 u32AdecNum, ADEC_REGISTER_PARAM_S* pstParam)
{
    HI_CHAR aszBuf[16];
    DRV_PROC_ITEM_S*  pProcItem;

    /* Check parameters */
    if (HI_NULL == pstParam)
    {
        HI_ERR_ADEC("the parameter pointer of adec register is NULL\n");
        return HI_FAILURE;
    }

    /* Create proc */
    snprintf(aszBuf, sizeof(aszBuf), "adec%02d", u32AdecNum);
    pProcItem = HI_DRV_PROC_AddModule(aszBuf, HI_NULL, HI_NULL);
    if (!pProcItem)
    {
        HI_FATAL_ADEC("Create Adec proc entry fail!\n");
        return HI_FAILURE;
    }

    /* Set functions */
    pProcItem->read  = pstParam->pfnReadProc;
    pProcItem->write = pstParam->pfnWriteProc;

    HI_INFO_ADEC("Create Adec proc entry for OK!\n");
    return HI_SUCCESS;
}

static HI_S32 ADEC_DRV_RegisterProc(ADEC_REGISTER_PARAM_S* pstParam)
{

    /* Check parameters */
    if (HI_NULL == pstParam)
    {
        HI_ERR_ADEC("the parameter pointer of adec register is NULL\n");
        return HI_FAILURE;
    }
    /* Create proc when use*/

    return HI_SUCCESS;
}
#endif
/*****************************************************************************
 Prototype    : ADEC_DRV_ModInit
 Description  : ModelInit
 Input        : None
 Output       : None
 Return Value :
 Calls        :
 Called By    :
  History        :
  1.Date         : 2006/1/18
    Author       : vicent feng
    Modification : Created function
*****************************************************************************/
HI_S32 ADEC_DRV_ModInit(HI_VOID)
{
    /*register ADEC device*/
    HI_S32 ret;

#ifndef HI_MCE_SUPPORT
    ret = HI_DRV_ADEC_Init();
    if (HI_SUCCESS != ret)
    {
        HI_ERR_ADEC("ADEC_DRV_Init Fali \n");
        return ret;
    }
#endif

    ADECRegisterDevice(&ADEC_DRV_Fops, &adec_drvops);
#ifndef HI_ADVCA_FUNCTION_RELEASE
    ret = ADEC_DRV_RegisterProc(&s_stProcParam);
    if (HI_SUCCESS != ret)
    {
        HI_FATAL_ADEC("Reg proc fail!\n");
        return HI_FAILURE;
    }


#ifdef MODULE
    HI_PRINT("Load hi_adec.ko success.  \t(%s)\n", VERSION_STRING);
#endif
#endif

    return HI_SUCCESS;
}

HI_VOID  ADEC_DRV_ModExit(HI_VOID)
{
    /*unregister ADEC device*/
    ADECUnregisterDevice();

#ifndef HI_MCE_SUPPORT
    HI_DRV_ADEC_DeInit();
#endif

    return;
}

#ifdef MODULE
module_init(ADEC_DRV_ModInit);
module_exit(ADEC_DRV_ModExit);
#endif

MODULE_AUTHOR("HISILICON");
MODULE_LICENSE("GPL");


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */