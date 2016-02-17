/*
===============================================================================
 Name        : SDLogger.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
===============================================================================
*/

#ifdef __USE_CMSIS
#include "LPC17xx.h"
#endif

#include <cr_section_macros.h>
#include <stdio.h>
#include "stdbool.h"

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"

// TODO: insert other include files here
#include "ff.h"
// TODO: insert other definitions and declarations here
#include "SDLogger.h"

int main(void)
{
	FATFS FatFs;   			/* Work area (file system object) for logical drive */
	FIL fil;       			/* File object */
	bool stats = false;
	char line; 				/* Line buffer */

	PINSEL_CFG_Type PinCfg;

	PinCfg.OpenDrain = PINSEL_PINMODE_NORMAL;
	PinCfg.Pinmode = PINSEL_PINMODE_PULLUP;
	PinCfg.Funcnum = 1;							/* We need to use this because SSEL is made outside. */
	PinCfg.Pinnum = 23;
	PINSEL_ConfigPin(&PinCfg);

	GPIO_SetDir(1, (1 << 23), 0);

	SystemInit();

	SystemCoreClockUpdate();

	if(f_mount(&FatFs, "", 1) == FR_OK)
	{
		DEBUGP("\nMounted!");
		if(f_open(&fil, "logger.txt", (FA_OPEN_ALWAYS | FA_READ | FA_WRITE)) == FR_OK)
		{
			DEBUGP("\nOpened!");
		}
	}
	else
	{
		DEBUGP("\nError!");
	}

//  Byte Read Allocation.
//	FRESULT fr;          					/* FatFs function common result code */
//	UINT br, bw;         					/* File read/write count */
//	for (;;)								/* For test from f_read only. */
//	{
//		fr = f_read(&fil, &line, 1, &br);  	/* Read a chunk of byte of source file */
//		if (fr || br == 0) break; 			/* error or eof */
//		printf("%c", line);
//	}

//  Character Read Allocation.
//	f_gets(&line, sizeof(line), &fil);  	/* Read a chunk of character of source file */

    while(1)
    {
    	if((GPIO_ReadValue(1) & (1 << 23)) && (stats == false))
    	{
    		stats = true;
//    		if(f_write(&fil, "Button Enabled!", 17, &bw) == FR_OK)	// error
    		if(f_puts("\nButton Enabled!", &fil) != 0)	// error
    		{
    			f_sync(&fil);
    			DEBUGP("\nWritted and Enabled!");
    		}
    	}
    	if(!(GPIO_ReadValue(1) & (1 << 23)) && (stats == true))
    	{
    		stats = false;
//    		if(f_write(&fil, "Button Disabled!\r\n", 18, &bw) == FR_OK)
    		if(f_puts("\nButton Disabled!", &fil) != 0)
			{
    			f_sync(&fil);
				DEBUGP("\nWritted and Disabled!");
			}
    	}
    }
}
