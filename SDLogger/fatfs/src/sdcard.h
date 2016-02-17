/**************************************************************************//**
 * @file     sd.h
 * @brief    Header file for sd.c
 * @version  1.0
 * @date     18. Nov. 2010
 *
 * @note
 * Copyright (C) 2010 NXP Semiconductors(NXP), ChaN. 
 * All rights reserved.
 *
 ******************************************************************************/

#ifndef __SD_H
#define __SD_H

#include "stdbool.h"
#include "LPC17xx.h"			/* LPC17xx Definitions */

//#define USE_FIFO - We have errors here....

#ifndef NULL
 #ifdef __cplusplus              // EC++
  #define NULL          0
 #else
  #define NULL          ((void *) 0)
 #endif
#endif

/* Command definitions in SPI bus mode */
#define GO_IDLE_STATE           0
#define SEND_OP_COND            1
#define SWITCH_FUNC             6
#define SEND_IF_COND            8
#define SEND_CSD                9
#define SEND_CID                10
#define STOP_TRANSMISSION       12
#define SEND_STATUS             13
#define SET_BLOCKLEN            16
#define READ_SINGLE_BLOCK       17
#define READ_MULTIPLE_BLOCK     18
#define WRITE_SINGLE_BLOCK      24
#define WRITE_MULTIPLE_BLOCK    25
#define APP_CMD                 55
#define READ_OCR                58
#define CRC_ON_OFF              59

/* Application specific commands supported by SD.
All these commands shall be preceded with APP_CMD (CMD55). */
#define SD_STATUS               13
#define SD_SEND_OP_COND         41

/* R1 response bit flag definition */
#define R1_NO_ERROR         0x00
#define R1_IN_IDLE_STATE    0x01
#define R1_ERASE_RESET      0x02
#define R1_ILLEGAL_CMD      0x04
#define R1_COM_CRC_ERROR    0x08
#define R1_ERASE_SEQ_ERROR  0x10
#define R1_ADDRESS_ERROR    0x20
#define R1_PARA_ERROR       0x40
#define R1_MASK             0x7F

/* The sector size is fixed to 512bytes in most applications. */
#define SECTOR_SIZE 		512

/* Memory card type definitions */
#define CARDTYPE_UNKNOWN        0
#define CARDTYPE_MMC            1   /* MMC */
#define CARDTYPE_SDV1           2   /* V1.x Standard Capacity SD card */
#define CARDTYPE_SDV2_SC        3   /* V2.0 or later Standard Capacity SD card */
#define CARDTYPE_SDV2_HC        4   /* V2.0 or later High/eXtended Capacity SD card */

/* SD/MMC card configuration */
typedef struct tagCARDCONFIG
{
    uint32_t sectorsize;    /* size (in byte) of each sector, fixed to 512bytes */
    uint32_t sectorcnt;     /* total sector number */  
    uint32_t blocksize;     /* erase block size in unit of sector */     
	uint8_t  ocr[4];		/* OCR */
	uint8_t  cid[16];		/* CID */
	uint8_t  csd[16];		/* CSD */
    uint8_t  status[64];    /* Status */
} CARDCONFIG;

DSTATUS MMC_disk_status(void);
DSTATUS MMC_disk_initialize(void);
DRESULT MMC_disk_read(BYTE *buff, DWORD sector, UINT count);
DRESULT MMC_disk_write(const BYTE *buff, DWORD sector, UINT count);
DRESULT MMC_disk_ioctl (BYTE cmd, void *buff);	/* Always add in diskio.c */

/* Public functions */
bool SD_Init (void);
bool SD_ReadSector (uint32_t sect, uint8_t *buf, uint32_t cnt);
bool SD_WriteSector (uint32_t sect, const uint8_t *buf, uint32_t cnt);
bool SD_ReadConfiguration (void);
uint8_t SD_SendCommand (uint8_t cmd, uint32_t arg, uint8_t *buf, uint32_t len);
uint8_t SD_SendACommand (uint8_t cmd, uint32_t arg, uint8_t *buf, uint32_t len);
bool SD_RecvDataBlock (uint8_t *buf, uint32_t len);
bool SD_SendDataBlock (const uint8_t *buf, uint8_t tkn, uint32_t len);
bool SD_WaitForReady (void);
// void disk_timerproc (void); - Use if RTC on.

#endif // __SD_H

/* --------------------------------- End Of File ------------------------------ */
