#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "ch.h"
#include "hal.h"
#include "hal_spi.h"
#include "chprintf.h"
#include "shell.h"

#include "gfx.h"

#include "ble_lld.h"

#include "ff.h"
#include "ffconf.h"
#include "diskio.h"

#include "async_io_lld.h"
#include "joypad_lld.h"
#include "nullprot_lld.h"

#include "orchard-ui.h"
#include "orchard-app.h"

#include "userconfig.h"

#ifdef DRV_QSPI
#include "m25q.h"
#endif
#include "nrf52flash_lld.h"
#include "hal_flash.h"

#include "i2s_lld.h"

#include "badge.h"

#ifdef DRV_QSPI
FATFS qspi_fs;
#endif

#include "led.h"

struct evt_table orchard_events;

/* linker set for command objects */

orchard_command_start();
orchard_command_end();

#define LED_EXT 14

bool watchdog_started = false;

static const SPIConfig spi1_config = {
	NULL,			/* enc_cp */
	NRF5_SPI_FREQ_8MBPS,	/* freq */
	0x20|IOPORT2_SPI_SCK,	/* sckpad */
	0x20|IOPORT2_SPI_MOSI,	/* mosipad */
	0x20|IOPORT2_SPI_MISO,	/* misopad */
	IOPORT1_SDCARD_CS,	/* sspad */
	FALSE,			/* lsbfirst */
	2,			/* mode */
	0xFF			/* dummy data for spiIgnore() */
};

static const SPIConfig spi4_config = {
	NULL,			/* enc_cp */
	NRF5_SPI_FREQ_32MBPS,	/* freq */
	IOPORT1_SPI_SCK,	/* sckpad */
	IOPORT1_SPI_MOSI,	/* mosipad */
	IOPORT1_SPI_MISO,	/* misopad */
	IOPORT1_SCREEN_CS,	/* sspad */
	FALSE,			/* lsbfirst */
	2,			/* mode */
	0xFF			/* dummy data for spiIgnore() */
};

static const I2CConfig i2c2_config = {
	400000,			/* clock */
	IOPORT1_I2C_SCK,	/* scl_pad */
	IOPORT1_I2C_SDA		/* sda_pad */
};

#ifdef DRV_QSPI
static const QSPIConfig qspi1_config = {
	NULL,			/* enc_cp */
	IOPORT1_QSPI_SCK,	/* SCK */
	IOPORT1_QSPI_CS,	/* CS */
	IOPORT1_QSPI_DIO0,	/* DIO0 */
	IOPORT1_QSPI_DIO1,	/* DIO1 */
	IOPORT1_QSPI_DIO2,	/* DIO2 */
	IOPORT1_QSPI_DIO3,	/* DIO3 */
	0x18000000		/* membase */
};
#endif

static const NRF52FLASHConfig nrf52_config = {
	256		/* NRF52840 has 1MB flash (256 * 4096) */
};

#ifdef DRV_QSPI
static const M25QConfig m25qcfg1 = {
  .busp             = &QSPID1,
  .buscfg           = &qspi1_config
};

M25QDriver FLASHD1;
#endif

NRF52FLASHDriver FLASHD2;

static void
gpt_callback(GPTDriver *gptp)
{
	(void)gptp;
	return;
}

static void
mmc_callback(GPTDriver *gptp)
{
	(void)gptp;
	disk_timerproc ();
}

/*
 * GPT configuration
 * Frequency: 16MHz
 * Resolution: 32 bits
 */

static const GPTConfig gpt1_config = {
    .frequency  = NRF5_GPT_FREQ_16MHZ,
    .callback   = gpt_callback,
    .resolution = 32,
};

static const GPTConfig gpt2_config = {
    .frequency  = NRF5_GPT_FREQ_62500HZ,
    .callback   = mmc_callback,
    .resolution = 32,
};

static THD_WORKING_AREA(shell_wa, 1280);
static thread_t *shell_tp = NULL;

static SerialConfig serial_config = {
    .speed   = 115200,
    .tx_pad  = UART_TX,
    .rx_pad  = UART_RX,
#if NRF5_SERIAL_USE_HWFLOWCTRL	== TRUE
    .rts_pad = UART_RTS,
    .cts_pad = UART_CTS,
#endif
};

static void shellRestart(void)
{
	static ShellConfig shellConfig;
	static const ShellCommand *shellCommands;

	shellCommands = orchard_commands();

	shellConfig.sc_channel =  (BaseSequentialStream *)&SD1;
	shellConfig.sc_commands = shellCommands;

	/* Recovers memory of the previous shell. */
	if (shell_tp && chThdTerminatedX(shell_tp))
		chThdRelease(shell_tp);

    	shell_tp = chThdCreateStatic (shell_wa, sizeof(shell_wa),
	    NORMALPRIO + 5, shellThread, (void *)&shellConfig);

	return;
}

static void
orchard_app_restart(eventid_t id)
{
	(void)id;

	orchardAppRestart();
}

static void
shell_termination_handler(eventid_t id)
{
	static int i = 1;

	printf ("\nRespawning shell (shell #%d, event %ld)\n", ++i, id);
	shellRestart ();
}

static void
unlock_update_handler(eventid_t id)
{
	userconfig * config;

	(void)id;

	config = getConfig();
	config->unlocks = __builtin_bswap32 (ble_unlocks);
	configSave (config);

	return;
}

static THD_WORKING_AREA(waThread1, 64);
static THD_FUNCTION(Thread1, arg) {

    (void)arg;
    uint8_t led = LED4;

    chRegSetThreadName ("blinker");

    while (1) {
			palTogglePad(IOPORT1, led);
			chThdSleepMilliseconds(1000);
    }
}

#ifdef notyet
static void
setup_flash (void)
{
	int i;
	FIL f;
	UINT br;
	uint8_t * buf;
	uint8_t * orig;
	uint32_t addr;

	printf ("ERASING... ");
	for (i = 0; i < 128; i++) {
    		flashStartEraseSector (&FLASHD1, i);
    		flashWaitErase ((void *)&FLASHD1);
		printf (".");
	}
	printf ("DONE!\n");


	if (f_open (&f, "8MB.DMG", FA_READ) != FR_OK)
		printf ("OPEN FAILED!!!\n");

	orig = buf = malloc (65536 + 32);
	addr = (uint32_t)buf;
	addr &= ~0x1F;
	addr += 32;
	buf = (uint8_t *)addr;

	printf ("PROGRAMMING... ");
	for (i = 0; i < 128; i++) {
		f_read(&f, buf, 65536, &br);
		if (br == 0)
			break;
    		flashProgram (&FLASHD1, i * 65536, 65536, buf);
		printf (".");
	}
	printf ("DONE!\n");

	free (orig);
	f_close (&f);

	return;
}
#endif

/**@brief Function for application main entry.
 */
int main(void)
{
    uint32_t info;
    uint8_t * p;
    const flash_descriptor_t * pFlash;
#ifdef DRV_QSPI
    uint8_t * memp;
    uint8_t reg;
    qspi_command_t cmd;
#endif
    uint32_t * faultPtr;

#ifdef CRT0_VTOR_INIT
    __disable_irq();
    SCB->VTOR = 0;
    __enable_irq();
#endif

    /*
     * Enable NULL pointer protection. We use the MPU to make the first
     * 256 bytes of the address space unreadable and unwritable. If someone
     * tries to dereference a NULL pointer, it will result in a load
     * or store at that location, and we'll get a memory manager trap
     * right away instead of possibly dying somewhere else further
     * on down the line.
     */

    nullProtStart ();

    /*
     * Enable division by 0 traps. We do not enable unaligned access
     * traps because the SoftDevice code performs unaligned accesses
     * all over the place.
     */

    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;

    /*
     * Enable memory management, usage and bus fault exceptions, so that
     * we don't always end up diverting through the hard fault handler.
     * Note: the memory management fault only applies if the MPU is
     * enabled, which it currently is (for stack guard pages).
     */

    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk |
        SCB_SHCSR_BUSFAULTENA_Msk |
        SCB_SHCSR_MEMFAULTENA_Msk;

    halInit();
    chSysInit();
    shellInit();

    /* Initialize newlib facilities. */

    newlibStart ();

    sdStart (&SD1, &serial_config);
    chThdSleepMilliseconds (50);
    printf ("\n");
    chThdSleepMilliseconds (50);

    palClearPad (IOPORT1, LED1);
    palClearPad (IOPORT1, LED2);
    palClearPad (IOPORT1, LED3);
    palClearPad (IOPORT1, LED4);

    gptStart (&GPTD2, &gpt1_config);
    gptStart (&GPTD3, &gpt2_config);

    /* Launch test blinker thread. */

    chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO+1,
		      Thread1, NULL);

    printf ("SYSTEM START\n");

    /* Display reset reason */

    if (NRF_POWER->RESETREAS) {
        info = NRF_POWER->RESETREAS;
        /* Clear accumulated reasons. */
        NRF_POWER->RESETREAS = 0xFFFFFFFF;

        printf ("Reset event (0x%lX):", info);
        if (info & POWER_RESETREAS_VBUS_Msk)
            printf (" Wake up due to VBUS level");
        if (info & POWER_RESETREAS_NFC_Msk)
            printf (" Wake up due to NFC detect");
        if (info & POWER_RESETREAS_DIF_Msk)
            printf (" Wake up due to entering debug IF mode");
        if (info & POWER_RESETREAS_LPCOMP_Msk)
            printf (" Wake up due to reaching LPCOMP power threshold");
        if (info & POWER_RESETREAS_OFF_Msk)
            printf (" Wake up due to GPIO detect");
        if (info & POWER_RESETREAS_LOCKUP_Msk)
            printf (" Reset due to CPU lockup");
        if (info & POWER_RESETREAS_SREQ_Msk)
            printf (" Soft reset");
        if (info & POWER_RESETREAS_DOG_Msk)
            printf (" Watchdog reset");
        if (info & POWER_RESETREAS_RESETPIN_Msk)
            printf (" Reset pin asserted");
        printf ("\n");
    } else {
        printf ("Power on\n");
    }

    /* Check if we rebooted due to a SoftDevice crash. */

    faultPtr = (uint32_t *)NRF_FAULT_INFO_ADDR;
    if (*faultPtr == NRF_FAULT_INFO_MAGIC) {
        printf ("Reset after SoftDevice fault, "
            "Id: 0x%lX PC: 0x%lX INFO: 0x%lX\n",
            faultPtr[1], faultPtr[2], faultPtr[3]);
        faultPtr[0] = 0;
    }

    info = NRF_FICR->INFO.VARIANT;
    p = (uint8_t *)&info;
    printf ("Nordic Semiconductor nRF%lx Variant: %c%c%c%c ",
        NRF_FICR->INFO.PART, p[3], p[2], p[1], p[0]);
    printf ("RAM: %ldKB Flash: %ldKB\n", NRF_FICR->INFO.RAM,
        NRF_FICR->INFO.FLASH);
    printf ("Device ID: %lX%lX\n", NRF_FICR->DEVICEID[0],
        NRF_FICR->DEVICEID[1]);
    chThdSleep(2);
    printf (PORT_INFO "\n");
    chThdSleep(2);

    /* Check if the reset pin has been set, and update the UICR if not */

    if (NRF_UICR->PSELRESET[0] != IOPORT1_RESET) {
        printf ("Reset pin not configured, programming... ");
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
        NRF_NVMC->ERASEUICR = 1;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
                ;
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;

        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;

        NRF_UICR->PSELRESET[0] = IOPORT1_RESET;
        NRF_UICR->PSELRESET[1] = IOPORT1_RESET;

        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
                ;
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
        printf ("Done.\n");
    }

    printf("Priority levels %d\n", CORTEX_PRIORITY_LEVELS);

    /* Set up I/O pins */

    palSetPad (IOPORT1, IOPORT1_SPI_MOSI);
    palSetPad (IOPORT1, IOPORT1_SPI_MISO);
    palSetPad (IOPORT1, IOPORT1_SPI_SCK);
    palSetPad (IOPORT1, IOPORT1_SDCARD_CS);
    palSetPad (IOPORT1, IOPORT1_SCREEN_CS);
    palSetPad (IOPORT1, IOPORT1_SCREEN_CD);
    palSetPad (IOPORT1, IOPORT1_TOUCH_CS);

    /* Enable joypad/button support */

    joyStart ();

    printf ("Joypad enabled\n");

    /* Start async I/O thread */

    asyncIoStart ();

    /* Start SPI buses */

    spiStart (&SPID1, &spi1_config);
    printf ("SPI bus 1 enabled\n");
    if (NRF_FICR->INFO.VARIANT == 0x41414141)
        printf ("Skipping SPI bus 4 setup on broken preview silicon\n");
    else {
        spiStart (&SPID4, &spi4_config);
        printf ("SPI bus 4 enabled\n");
    }

#ifdef DRV_QSPI
    /* Enable QSPI flash */

    m25qObjectInit (&FLASHD1);
    m25qStart (&FLASHD1, &m25qcfg1);

    /*
     * Macronix NOR flash parts default to single-I/O line out of the
     * box. To switch them to quad-I/O operation, you have to set a
     * bit in the status register. This is documented in application
     * note AN0251V1. Note: this is a non-volatile bit, so we can check
     * to see if it's already been set and if so we don't need to do
     * anything.
     */

    cmd.cfg = QSPI_CMD_READ_STATUS_REGISTER | QSPI_CFG_DATA_MODE_FOUR_LINES;
    cmd.addr = 0;
    cmd.alt = 0;
    qspiReceive (&QSPID1, &cmd, 1, &reg);
    if ((reg & 0x40) == 0) {
        reg |= 0x40; /* set the QE bit */
        qspiSend (&QSPID1, &cmd, 1, &reg);
    }

    m25qMemoryMap (&FLASHD1, &memp);
    pFlash = flashGetDescriptor (&FLASHD1);

    if (pFlash->sectors_count > 0) {
        printf ("On-board QSPI flash detected: %dMB mapped at 0x%x\n",
            (pFlash->sectors_count * pFlash->sectors_size) >> 20,
	    pFlash->address);
    }

    m25qMemoryUnmap (&FLASHD1);
#endif

    /* Enable on-board flash */
    nrf52FlashObjectInit (&FLASHD2);
    nrf52FlashStart (&FLASHD2, &nrf52_config);
    pFlash = flashGetDescriptor (&FLASHD2);

    if (pFlash->sectors_count > 0) {
        printf ("On-board NRF52840 flash detected: %ldMB mapped at 0x%lx\n",
            (pFlash->sectors_count * pFlash->sectors_size) >> 20,
	    pFlash->address);
    }

    /* Enable I2C controller */

    i2cStart (&I2CD2, &i2c2_config);
    printf ("I2C interface enabled\n");

    /* Enable I2S controller */

    i2sStart ();
    printf ("I2S interface enabled\n");

    if (NRF_FICR->INFO.VARIANT == 0x41414141)
        printf ("Skipping screen setup on broken preview silicon\n");
    else {
        /* Enable display and touch panel */
        printf ("Main screen turn on\n");
        gfxInit ();
    }

    /* Mount SD card */

    if (gfileMount ('F', "0:") == FALSE)
        printf ("No SD card found\n");
    else
        printf ("SD card detected\n");

#ifdef notyet
    m25qMemoryUnmap (&FLASHD1);
    setup_flash ();
    m25qMemoryMap (&FLASHD1, &memp);
#endif

#ifdef DRV_QSPI
    /* Mount QSPI flash as secondary filesystem */

    if (f_mount (&qspi_fs, "1:", 1) != FR_OK) {
        printf ("QSPI filesystem mount failed\n");
    } else {
        printf ("QSPI filesystem mounted\n");
        /*f_chdrive ("1:");*/
    }
#endif

    /* Enable bluetooth radio */

    bleStart ();

    /* Init the user configuration */

    configStart();

    /* start the LEDs */

    if (led_init()) {
      printf("I2C LED controller found.\r\n");
      ledStart();
    } else {
      printf("I2C LED controller not found. No Bling ;(\r\n");
    }

#ifdef flash_test

    /*
     * Note: we're compiled to use the SoftDevice for flash access,
     * which means we can only actually perform erase and program
     * operations on the internal flash after the SoftDevice has
     * been enabled.
     */

    if (flashStartEraseSector (&FLASHD2, 255) != FLASH_NO_ERROR)
      printf ("ERASE FAILED\n");

    flashWaitErase ((void *)&FLASHD2);

    if (flashProgram (&FLASHD2, 0xFF000, 1024, (uint8_t *)0x20002000) !=
      FLASH_NO_ERROR)
      printf ("PROGRAM FAILED...\n");
#endif

    NRF_P0->DETECTMODE = 0;

    /* Launch shell thread */

    evtTableInit (orchard_events, 3);
    chEvtObjectInit (&orchard_app_terminated);
    chEvtObjectInit (&unlocks_updated);
    evtTableHook (orchard_events, shell_terminated, shell_termination_handler);
    evtTableHook (orchard_events, unlocks_updated, unlock_update_handler);
    evtTableHook (orchard_events, orchard_app_terminated, orchard_app_restart);

    if (NRF_FICR->INFO.VARIANT == 0x41414141)
        printf ("Skipping UI setup on broken preview silicon\n");
    else {
        uiStart ();
        orchardAppInit ();
        orchardAppRestart ();
    }

    shellRestart ();

    while (true) {
        chEvtDispatch(evtHandlers(orchard_events), chEvtWaitOne(ALL_EVENTS));
    }

}
