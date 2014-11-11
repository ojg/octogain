/*
 * main.c
 *
 * Created: 06.11.2014 20:32:10
 *  Author: ole
 */ 

#define F_CPU 32000000

#include "board.h"
#include "clksys_driver.h"
#include "usart_driver.h"
#include "twi_master_driver.h"
#include "cli.h"

#include <avr/io.h>
#include <stdio.h>

typedef struct {
	bool (*taskfunc)(void);
	const Task_flag_t bitmask;
} Tasklist_t;

void cmd_MasterVol(char *);
void cmd_MasterVol_help(void);

Task_flag_t taskflags = 0;
const char welcomeMsg[] = "\nWelcome to Octogain!\nType help for list of commands\n";

int main(void)
{
	int i;
    
	Tasklist_t tasklist[] =
	{
		{cli_task, Task_CLI_bm},
        {NULL, 0}
	};

	LED_PORT.DIRSET = LED_PIN_bm;
	
	CLKSYS_Enable( OSC_RC32MEN_bm );
	do {} while ( CLKSYS_IsReady( OSC_RC32MRDY_bm ) == 0 );
	CLKSYS_Main_ClockSource_Select( CLK_SCLKSEL_RC32M_gc );
	CLKSYS_Disable( OSC_RC2MEN_bm | OSC_RC32KEN_bm );

	SLEEP.CTRL = (SLEEP.CTRL & ~SLEEP_SMODE_gm) | SLEEP_SMODE_IDLE_gc;
	SLEEP.CTRL |= SLEEP_SEN_bm;

    register_cli_command("help", cmd_help, cmd_help);
    register_cli_command("iicr", cmd_iicr, cmd_iicr_help);
    register_cli_command("iicw", cmd_iicw, cmd_iicw_help);
    register_cli_command("vol", cmd_MasterVol, cmd_MasterVol_help);

    /* Initialize debug USART */
	USART_init(&USARTD0);
    
	/* Initialize TWI master. */
	TWI_MasterInit(&TWIC, TWI_MASTER_INTLVL_LO_gc, TWI_BAUD(F_CPU, 100000));

	// Enable global interrupts
	sei();

	printf("%s%s", welcomeMsg, CLI_PROMPT);

	while(1)
	{
        while (taskflags) { //run as long as any taskflag is set
            i=0;
            while (tasklist[i].taskfunc) { // go through all tasks
                if (taskflags & tasklist[i].bitmask) { //check taskflag
                    if (tasklist[i].taskfunc()) { //run taskfunction
                        taskflags &= ~tasklist[i].bitmask; //if it returns true (done), clear the taskflag
                    }
                }
                i++;
            }
        }
        LED_PORT.OUTTGL = LED_PIN_bm;
        __asm__ __volatile__ ("sleep");
    }
}

typedef int16_t q13_2;
#define CS3318_ADDR 0x40
extern TWI_Master_t twiMaster;

void cs3318_write(uint8_t addr, uint8_t value)
{
    uint8_t data[2] = {addr, value};
    TWI_MasterWrite(&twiMaster, CS3318_ADDR, data, 2);

    while (twiMaster.status != TWIM_STATUS_READY) {
        /* Wait until transaction is complete. */
    }

}

uint8_t cs3318_read(uint8_t addr)
{
    TWI_MasterWriteRead(&twiMaster, CS3318_ADDR, &addr, 1, 1);

    while (twiMaster.status != TWIM_STATUS_READY) {
        /* Wait until transaction is complete. */
    }

    printf("0x%02X\n", twiMaster.readData[0]);
    return twiMaster.readData[0];
}


void cs3318_setVolReg(uint8_t regaddr, q13_2 volume_in_db_x4) 
{
    uint8_t regval = (volume_in_db_x4 >> 1) + 210;
    uint8_t quarterdb_val = volume_in_db_x4 & 1;
    cs3318_write(regaddr, regval);
    if (regaddr == 0x11 || regaddr == 0x14 || regaddr == 0x17) {
        cs3318_write(regaddr+1, (cs3318_read(regaddr+1) & 0xFE) | quarterdb_val);
    }        
    else {
        cs3318_write(0x09, (cs3318_read(0x09) & (1 << regaddr)) | (quarterdb_val << regaddr));
    }
}

void cmd_MasterVol(char * stropt)
{
    int numparams, msd, lsd, sign = 0;
    q13_2 volume_in_db_x4;

    numparams = sscanf(stropt, "%d.%d\n", &msd, &lsd);
    if (numparams < 1 || numparams > 2) {
        printf("Unknown options\n");
        cmd_MasterVol_help();
        return;
    }
  
    volume_in_db_x4 = ((q13_2)msd) << 2;
    
    if (msd < 0) 
        sign = -1;
    
    if (lsd >= 75) {
        volume_in_db_x4 += sign * 3;
    }
    else if (lsd >= 50 || lsd == 5) {
        volume_in_db_x4 += sign * 2;
    }
    else if (lsd >= 25) {
        volume_in_db_x4 += sign * 1;
    }
    
    if (volume_in_db_x4 > (22 * 4) || volume_in_db_x4 < (-96 * 4)) {
        printf("Value outside range 22 to -96dB\n");
        return;
    }
    
    cs3318_setVolReg(0x11, volume_in_db_x4);
}

void cmd_MasterVol_help()
{
    printf("vol [value in dB]\nExample: vol -23.75\n");
    
}