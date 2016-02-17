/**************************************************************************//**
 * @file     sd.c
 * @brief    Drivers for SD/MMC/SDHC
 * @version  1.0
 * @date     18. Nov. 2010
 *
 * @note
 * Copyright (C) 2010 NXP Semiconductors(NXP), ChaN. 
 * All rights reserved.
 *
 * Disk operation interface (disk_xxxx()): ChaN
 * SD driver (SD_xxxx()): NXP
 *   Card initilization flow and some code snippets are inspired from ChaN.
 *
 ******************************************************************************/

#include "stdbool.h"

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_ssp.h"

#include "diskio.h"
#include "sdcard.h"

#include "SDLogger.h"

static bool SendDatatoSDCard(uint8_t *data, uint8_t size);
static bool ReceiveDatafromSDCard(uint8_t *data, uint8_t size);
static void SSELSelect(void);
static void SSELUnselect(void);

/* Local variables */
static volatile DSTATUS status = STA_NOINIT;	/* Disk status */
static volatile WORD Timer1, Timer2;			/* 100Hz decrement timer stopped at zero (disk_timerproc()) */

BYTE CardType;
CARDCONFIG CardConfig;

DSTATUS MMC_disk_initialize(void)
{
	SysTick_Config(SystemCoreClock/100);	/* Generate interrupt each 10 ms */

	if (SD_Init() && SD_ReadConfiguration()) status &= ~STA_NOINIT;

	return status;
}

DRESULT MMC_disk_ioctl (BYTE cmd, void *buff)
{
	DRESULT res;
	BYTE n, *ptr = buff;

	if (status & STA_NOINIT)
	{
		return RES_NOTRDY;
	}

	res = RES_ERROR;

	switch (cmd)
	{
		case CTRL_SYNC :		/* Make sure that no pending write process */
			SSELSelect();
			if (SD_WaitForReady() == true) res = RES_OK;
		break;
		case GET_SECTOR_COUNT:	/* Get number of sectors on the disk (DWORD) */
			*(DWORD*)buff = CardConfig.sectorcnt;
			res = RES_OK;
		break;
		case GET_SECTOR_SIZE :	/* Get R/W sector size (WORD) */
			*(WORD*)buff = CardConfig.sectorsize;	//512;
			res = RES_OK;
		break;
		case GET_BLOCK_SIZE :	/* Get erase block size in unit of sector (DWORD) */
			*(DWORD*)buff = CardConfig.blocksize;
			res = RES_OK;
		break;
		case MMC_GET_TYPE :		/* Get card type flags (1 byte) */
			*ptr = CardType;
			res = RES_OK;
		break;
		case MMC_GET_CSD :		/* Receive CSD as a data block (16 bytes) */
			for (n = 0;n < 16; n++) *(ptr+n) = CardConfig.csd[n];
			res = RES_OK;
		break;
		case MMC_GET_CID :		/* Receive CID as a data block (16 bytes) */
			for (n = 0; n < 16; n++) *(ptr+n) = CardConfig.cid[n];
			res = RES_OK;
		break;
		case MMC_GET_OCR :		/* Receive OCR as an R3 resp (4 bytes) */
			for (n = 0; n < 4; n++) *(ptr+n) = CardConfig.ocr[n];
			res = RES_OK;
		break;
		case MMC_GET_SDSTAT :	/* Receive SD status as a data block (64 bytes) */
			for (n = 0; n < 64; n++) *(ptr+n) = CardConfig.status[n];
			res = RES_OK;
		break;
		default:
			res = RES_PARERR;
		break;
	}
	SSELUnselect();
	return res;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT MMC_disk_read(BYTE *buff, DWORD sector, UINT count)
{
	if (status & STA_NOINIT)
	{
		return RES_NOTRDY;
	}

	if (SD_ReadSector (sector, buff, count) == true)
	{
		return RES_OK;
	}

	return RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/
DSTATUS MMC_disk_status(void)
{
	return status;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
DRESULT MMC_disk_write(const BYTE *buff, DWORD sector, UINT count)
{
	if (status & STA_NOINIT) return RES_NOTRDY;

	if (SD_WriteSector(sector, buff, count) == true)
	{
		return RES_OK;
	}
	return RES_ERROR;
}

/**
  * @brief  Initializes the memory card.
  *
  * @param  None
  * @retval true: Init OK.
  *         false: Init failed.
  *
  * Note: Refer to the init flow at http://elm-chan.org/docs/mmc/sdinit.png
  */
bool SD_Init (void)
{
	PINSEL_CFG_Type PinCfg;
	SSP_CFG_Type SSP_ConfigStruct;
    uint8_t i, r1, buf[4];

    /* Init SPI interface */
    /*
	 * Initialize SSP0 pin connect
	 * P0.15 - SCK;
	 * P0.16 - SSEL //dummy
	 * P0.17 - MISO
	 * P0.18 - MOSI
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = PINSEL_PINMODE_NORMAL;
	PinCfg.Pinmode = PINSEL_PINMODE_PULLUP;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 15;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 18;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 17;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;							/* We need to use this because SSEL is made outside. */
	PinCfg.Pinnum = 16;
	PINSEL_ConfigPin(&PinCfg);

	GPIO_SetDir(SSELPORTNUM, (1 << SSELPIN), 1);

	SSP_ConfigStructInit(&SSP_ConfigStruct);	/* Initialize SSP configuration structure to default. */
	SSP_ConfigStruct.ClockRate = 400000;		/* Changed velocity to 400KHz. */
	SSP_Init(SDSSP, &SSP_ConfigStruct);			/* Initialize SSP peripheral with parameter given in structure above. */
	SSP_Cmd(SDSSP, ENABLE);						/* Enable SSP peripheral. */

    /* Set card type to unknown */
    CardType = CARDTYPE_UNKNOWN;

    /* Before reset, Send at least 74 clocks at low frequency (between 100kHz and 400kHz) with CS high and DI (MISO) high. */
    SSELUnselect();
    buf[0] = 0xff;
    for (i = 0; i < 10; i++) SendDatatoSDCard(&buf[0], 1);

    /* Send CMD0 with CS low to enter SPI mode and reset the card.
    The card will enter SPI mode if CS is low during the reception of CMD0. 
    Since the CMD0 (and CMD8) must be sent as a native command, the CRC field
    must have a valid value. */
    if (SD_SendCommand (GO_IDLE_STATE, 0, NULL, 0) != R1_IN_IDLE_STATE) // CMD0
    {
        goto init_end;
    }

    /* Now the card enters IDLE state. */

    /* Card type identification Start ... */

    /* Check the card type, needs around 1000ms */    
    r1 = SD_SendCommand (SEND_IF_COND, 0x1AA, buf, 4);  // CMD8
    if (r1 & 0x80) goto init_end;

    Timer1 = 100; // 1000ms
    if (r1 == R1_IN_IDLE_STATE)
    { 	/* It's V2.0 or later SD card */
        if (buf[2] != 0x01 || buf[3] != 0xAA) goto init_end;

        /* The card is SD V2 and can work at voltage range of 2.7 to 3.6V */
        do
        {
            r1 = SD_SendACommand (SD_SEND_OP_COND, 0x40000000, NULL, 0);  // ACMD41
            if      (r1 == 0x00) break;
            else if (r1 > 0x01)  goto init_end;            
        } while (Timer1);

        if (Timer1 && SD_SendCommand (READ_OCR, 0, buf, 4) == R1_NO_ERROR)  // CMD58
        {
            CardType = (buf[0] & 0x40) ? CARDTYPE_SDV2_HC : CARDTYPE_SDV2_SC;
        }
    }
    else
    { 	/* It's Ver1.x SD card or MMC card */
        /* Check if it is SD card */
        if (SD_SendCommand (APP_CMD, 0, NULL, 0) & R1_ILLEGAL_CMD)
        {   
            CardType = CARDTYPE_MMC; 
            while (Timer1 && SD_SendCommand (SEND_OP_COND, 0, NULL, 0));
        }  
        else 
        {   
            CardType = CARDTYPE_SDV1; 
            while (Timer1 && SD_SendACommand (SD_SEND_OP_COND, 0, NULL, 0));
        }

        if (Timer1 == 0) CardType = CARDTYPE_UNKNOWN;
    }

    /* For SDHC or SDXC, block length is fixed to 512 bytes, for others,
    the block length is set to 512 manually. */
    if (CardType == CARDTYPE_MMC || CardType == CARDTYPE_SDV1 || CardType == CARDTYPE_SDV2_SC )
    {
        if (SD_SendCommand (SET_BLOCKLEN, SECTOR_SIZE, NULL, 0) != R1_NO_ERROR) CardType = CARDTYPE_UNKNOWN;
    }

init_end:              
   SSELUnselect();

    if (CardType == CARDTYPE_UNKNOWN)
    {
        return (false);
    }
    else     /* Init OK. use high speed during data transaction stage. */
    {
    	SSP_Cmd(SDSSP, DISABLE);
    	SSP_ConfigStruct.ClockRate = 2000000;		/* Changed velocity to 20MHz. */
		SSP_Init(SDSSP, &SSP_ConfigStruct);			/* Initialize SSP peripheral with parameter given in structure above. */
		SSP_Cmd(SDSSP, ENABLE);
        return (true);
    }
}

/**
  * @brief  Wait for the card is ready. 
  *
  * @param  None
  * @retval true: Card is ready for read commands.
  *         false: Card is not ready
  */
bool SD_WaitForReady (void)
{
	uint8_t data = 0;

    Timer2 = 50;    // 500ms
    ReceiveDatafromSDCard(&data, 1);
    do
    {
    	ReceiveDatafromSDCard(&data, 1);
        if (data == 0xFF) return true;
    } while (Timer2);

    return false;
}

/**
  * @brief  Send a command and receive a response with specified format. 
  *
  * @param  cmd: Specifies the command index.
  * @param  arg: Specifies the argument.
  * @param  buf: Pointer to byte array to store the response content.
  * @param  len: Specifies the byte number to be received after R1 response.
  * @retval Value below 0x80 is the normal R1 response (0x0 means no error) 
  *         Value above 0x80 is the additional returned status code.
  *             0x81: Card is not ready
  *             0x82: command response time out error
  */
uint8_t SD_SendCommand (uint8_t cmd, uint32_t arg, uint8_t *buf, uint32_t len) 
{
    uint8_t r1, i;
    uint8_t crc_stop;

    /* The CS signal must be kept low during a transaction */
    SSELSelect();

    /* Wait until the card is ready to read (DI signal is High) */
    if (SD_WaitForReady() == false) return 0x81;

    /* Prepare CRC7 + stop bit. For cmd GO_IDLE_STATE and SEND_IF_COND, 
    the CRC7 should be valid, otherwise, the CRC7 will be ignored. */
    if      (cmd == GO_IDLE_STATE)  crc_stop = 0x95; /* valid CRC7 + stop bit */
    else if (cmd == SEND_IF_COND)   crc_stop = 0x87; /* valid CRC7 + stop bit */
    else                            crc_stop = 0x01; /* dummy CRC7 + Stop bit */

    /* Send 6-byte command with CRC. */
    cmd |= 0x40;
    SendDatatoSDCard(&cmd, 1);
    r1 = (arg >> 24);
    SendDatatoSDCard(&r1, 1);
    r1 = (arg >> 16);
    SendDatatoSDCard(&r1, 1);
    r1 = (arg >> 8);
    SendDatatoSDCard(&r1, 1);
    //
    SendDatatoSDCard(&arg, 1);
    SendDatatoSDCard(&crc_stop, 1); /* Valid or dummy CRC plus stop bit */
   
    /* The command response time (Ncr) is 0 to 8 bytes for SDC, 
    1 to 8 bytes for MMC. */
    for (i = 8; i; i--)
    {
        ReceiveDatafromSDCard(&r1, 1);
        if (r1 != 0xFF) break;   /* received valid response */      
    }
    if (i == 0)  return (0x82); /* command response time out error */

    /* Read remaining bytes after R1 response */
    if (buf && len)
    {
    	do
        {
        	ReceiveDatafromSDCard(buf, 1);
        	buf++;
        } while (--len);
    }
    return (r1);
}

/**
  * @brief  Send an application specific command for SD card 
  *         and receive a response with specified format. 
  *
  * @param  cmd: Specifies the command index.
  * @param  arg: Specifies the argument.
  * @param  buf: Pointer to byte array to store the response content.
  * @param  len: Specifies the byte number to be received after R1 response.
  * @retval Value below 0x80 is the normal R1 response(0x0 means no error)
  *         Value above 0x80 is the additional returned status code.
  *             0x81: Card is not ready
  *             0x82: command response time out error
  *
  * Note: All the application specific commands should be precdeded with APP_CMD
  */
uint8_t SD_SendACommand (uint8_t cmd, uint32_t arg, uint8_t *buf, uint32_t len)
{
    uint8_t r1;

    /* Send APP_CMD (CMD55) first */
	r1 = SD_SendCommand(APP_CMD, 0, NULL, 0);
	if (r1 > 1) return r1;    
    
    return (SD_SendCommand (cmd, arg, buf, len));
}

/**
  * @brief  Read single or multiple sector(s) from memory card.
  *
  * @param  sect: Specifies the starting sector index to read
  * @param  buf:  Pointer to byte array to store the data
  * @param  cnt:  Specifies the count of sectors to read
  * @retval true or false.
  */
bool SD_ReadSector (uint32_t sect, uint8_t *buf, uint32_t cnt)
{
    bool flag;

    /* Convert sector-based address to byte-based address for non SDHC */
    if (CardType != CARDTYPE_SDV2_HC) sect <<= 9;

    flag = false;

    if (cnt > 1) /* Read multiple block */
    {
		if (SD_SendCommand(READ_MULTIPLE_BLOCK, sect, NULL, 0) == R1_NO_ERROR) 
        {            
			do
			{
				if (SD_RecvDataBlock(buf, SECTOR_SIZE) == false) break;
				buf += SECTOR_SIZE;
			} while (--cnt);

			/* Stop transmission */
            SD_SendCommand(STOP_TRANSMISSION, 0, NULL, 0);				

            /* Wait for the card is ready */
            if (SD_WaitForReady() && cnt==0) flag = true;
        }
    }
    else   /* Read single block */
    {        
        if (SD_SendCommand(READ_SINGLE_BLOCK, sect, NULL, 0) == R1_NO_ERROR)
        {
        	if(SD_RecvDataBlock(buf, SECTOR_SIZE) == true)
			{
				flag = true;
			}
        }
    }

    /* De-select the card */
    SSELUnselect();

    return (flag);
}

/**
  * @brief  Write single or multiple sectors to SD/MMC. 
  *
  * @param  sect: Specifies the starting sector index to write
  * @param  buf: Pointer to the data array to be written
  * @param  cnt: Specifies the number sectors to be written
  * @retval true or false
  */
bool SD_WriteSector (uint32_t sect, const uint8_t *buf, uint32_t cnt)
{
    bool flag = false;
    uint8_t cmd = 0xFD;

    /* Convert sector-based address to byte-based address for non SDHC */
    if (CardType != CARDTYPE_SDV2_HC) sect <<= 9; 

    if (cnt > 1)  /* write multiple block */
    { 
        if (SD_SendCommand (WRITE_MULTIPLE_BLOCK, sect, NULL, 0) == R1_NO_ERROR)
        {
            do
            {
                if (SD_SendDataBlock(buf, 0xFC, SECTOR_SIZE) == false)  break;
                buf += SECTOR_SIZE;
            } while (--cnt);

            /* Send Stop Transmission Token. */
            SendDatatoSDCard(&cmd, 1);
        
            /* Wait for complete */
            if (SD_WaitForReady() && cnt==0) flag = true;
        }
    }
    else  /* write single block */
    {
        if ((SD_SendCommand (WRITE_SINGLE_BLOCK, sect, NULL, 0) == R1_NO_ERROR) && (SD_SendDataBlock (buf, 0xFE, SECTOR_SIZE) == true))
        {
            flag = true;
        }
    }

    /* De-select the card */
    SSELUnselect();

    return (flag);
}

/**
  * @brief  Read card configuration and fill structure CardConfig.
  *
  * @param  None
  * @retval true or false.
  */
bool SD_ReadConfiguration ()
{
    uint8_t buf[16];
    uint32_t i, c_size, c_size_mult, read_bl_len;
    bool retv;
  
    retv = false;

    /* Read OCR */
    if (SD_SendCommand(READ_OCR, 0, CardConfig.ocr, 4) != R1_NO_ERROR) goto end;

    /* Read CID */
    if ((SD_SendCommand(SEND_CID, 0, NULL, 0) != R1_NO_ERROR) || SD_RecvDataBlock (CardConfig.cid, 16)==false) goto end;

    /* Read CSD */
    if ((SD_SendCommand(SEND_CSD, 0, NULL, 0) != R1_NO_ERROR) || SD_RecvDataBlock (CardConfig.csd, 16)==false) goto end;

    /* sector size */
    CardConfig.sectorsize = 512;
    
    /* sector count */
    if (((CardConfig.csd[0] >> 6) & 0x3) == 0x1) /* CSD V2.0 (for High/eXtended Capacity) */
    {
        /* Read C_SIZE */
        c_size =  (((uint32_t)CardConfig.csd[7]<<16) + ((uint32_t)CardConfig.csd[8]<<8) + CardConfig.csd[9]) & 0x3FFFFF;
        /* Calculate sector count */
       CardConfig.sectorcnt = (c_size + 1) * 1024;

    }
    else   /* CSD V1.0 (for Standard Capacity) */
    {
        /* C_SIZE */
        c_size = (((uint32_t)(CardConfig.csd[6]&0x3)<<10) + ((uint32_t)CardConfig.csd[7]<<2) + (CardConfig.csd[8]>>6)) & 0xFFF;
        /* C_SIZE_MUTE */
        c_size_mult = ((CardConfig.csd[9]&0x3)<<1) + ((CardConfig.csd[10]&0x80)>>7);
        /* READ_BL_LEN */
        read_bl_len = CardConfig.csd[5] & 0xF;
        CardConfig.sectorcnt = (c_size+1) << (read_bl_len + c_size_mult - 7);        
    }

    /* Get erase block size in unit of sector */
    switch (CardType)
    {
        case CARDTYPE_SDV2_SC:
        case CARDTYPE_SDV2_HC:
            if ((SD_SendACommand (SD_STATUS, 0, buf, 1) !=  R1_NO_ERROR) || SD_RecvDataBlock(buf, 16) == false) goto end;      /* Read partial block */
            for (i=64-16;i;i--) ReceiveDatafromSDCard(NULL, 1); /* Purge trailing data */
            CardConfig.blocksize = 16UL << (buf[10] >> 4); /* Calculate block size based on AU size */
            break;
        case CARDTYPE_MMC:
            CardConfig.blocksize = ((uint16_t)((CardConfig.csd[10] & 124) >> 2) + 1) * (((CardConfig.csd[10] & 3) << 3) + ((CardConfig.csd[11] & 224) >> 5) + 1);
            break;
        case CARDTYPE_SDV1:
            CardConfig.blocksize = (((CardConfig.csd[10] & 63) << 1) + ((uint16_t)(CardConfig.csd[11] & 128) >> 7) + 1) << ((CardConfig.csd[13] >> 6) - 1);
            break;
        default:
            goto end;                
    }

    retv = true;
end:
    SSELUnselect();

    return retv;
}

/**
  * @brief  Receive a data block with specified length from SD/MMC. 
  *
  * @param  buf: Pointer to the data array to store the received data
  * @param  len: Specifies the length (in byte) to be received.
  *              The value should be a multiple of 4.
  * @retval true or false
  */
bool SD_RecvDataBlock (uint8_t *buf, uint32_t len)
{
    uint8_t datatoken;

    /* Read data token (0xFE) */
	Timer1 = 10;   /* Data Read Timeout: 100ms */
	do
	{
		ReceiveDatafromSDCard(&datatoken, 1);
        if (datatoken == 0xFE) break;
	} while (Timer1);

	if(datatoken != 0xFE) return (false);	/* data read timeout */

    /* Read data block */
#ifdef USE_FIFO
	ReceiveDatafromSDCard(buf, len);
#else
    uint32_t i = 0;

    for (i = 0; i < len; i++)
    {
    	ReceiveDatafromSDCard(&buf[i], 1);
    }
#endif

    datatoken = 0xff;
    /* 2 bytes CRC will be discarded. */
    ReceiveDatafromSDCard(&datatoken, 1);
    ReceiveDatafromSDCard(&datatoken, 1);
    return (true);
}

/**
  * @brief  Send a data block with specified length to SD/MMC. 
  *
  * @param  buf: Pointer to the data array to store the received data
  * @param  tkn: Specifies the token to send before the data block
  * @param  len: Specifies the length (in byte) to send.
  *              The value should be 512 for memory card.
  * @retval true or false
  */
bool SD_SendDataBlock (const uint8_t *buf, uint8_t tkn, uint32_t len)
{
    uint8_t recv = 0xff;
    
    /* Send Start Block Token */
    SendDatatoSDCard(&tkn, 1);

    /* Send data block */
#ifdef USE_FIFO
    SendDatatoSDCard(buf, len);
#else
    uint32_t i = 0;

    for (i = 0; i < len; i++) 
    {
    	SendDatatoSDCard(&buf[i], 1);
    }
#endif

    /* Send 2 bytes dummy CRC */
    SendDatatoSDCard(&recv, 1);
    SendDatatoSDCard(&recv, 1);

    /* Read data response to check if the data block has been accepted. */
    ReceiveDatafromSDCard(&recv, 1);
    if ((recv & 0x0F) != 0x05)
    {
        return (false); /* write error */
    }

    /* Wait for write complete. */
    Timer1 = 20;  // 200ms
    do
    {
    	ReceiveDatafromSDCard(&recv, 1);
        if (recv == 0xFF) break;  
    } while (Timer1);

    if (recv == 0xFF) return true;       /* write complete */
    else              return (false);    /* write time out */

}

/*-----------------------------------------------------------------------*/
/* Platform dependent functions				                             */
/*-----------------------------------------------------------------------*/
/* This function must be called from timer interrupt routine in period
/  of 10 ms to generate card control timing.
*/
void disk_timerproc (void)
{
    WORD n;

	n = Timer1;						/* 100Hz decrement timer stopped at 0 */
	if (n) Timer1 = --n;
	n = Timer2;
	if (n) Timer2 = --n;
}

/* SysTick Interrupt Handler (10ms)    */
void SysTick_Handler(void)
{
   disk_timerproc(); 				/* Disk timer function (100Hz) */
}

static bool SendDatatoSDCard(uint8_t *data, uint8_t size)
{
	SSP_DATA_SETUP_Type Transfer;

	Transfer.tx_data = data;
	Transfer.rx_data = NULL;
	Transfer.length = size;

	if(SSP_ReadWrite (SDSSP, &Transfer, SSP_TRANSFER_POLLING) == 0)
	{
		return(true);
	}
	return(false);
}

static bool ReceiveDatafromSDCard(uint8_t *data, uint8_t size)
{
	SSP_DATA_SETUP_Type Transfer;

	Transfer.tx_data = NULL;
	Transfer.rx_data = data;
	Transfer.length = size;

	if(SSP_ReadWrite (SDSSP, &Transfer, SSP_TRANSFER_POLLING) == 0)
	{
		return(true);
	}
	return(false);
}

static void SSELSelect(void)
{
	GPIO_ClearValue(SSELPORTNUM, (1 << SSELPIN));
}

static void SSELUnselect(void)
{
	GPIO_SetValue(SSELPORTNUM, (1 << SSELPIN));
	ReceiveDatafromSDCard(NULL, 1);
}

/* --------------------------------- End Of File ------------------------------ */
