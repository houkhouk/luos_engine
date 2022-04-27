/******************************************************************************
 * @file luosHAL
 * @brief Luos Hardware Abstration Layer. Describe Low layer fonction
 * @MCU Family ATSAMD21
 * @author Luos
 * @version 0.0.0
 ******************************************************************************/
#include "luos_hal.h"

#include <stdbool.h>
#include <string.h>

// MCU dependencies this HAL is for family Atmel ATSAMD21 you can find

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define DEFAULT_TIMEOUT 20
#define TIMEOUT_ACK     DEFAULT_TIMEOUT / 4
/*******************************************************************************
 * Variables
 ******************************************************************************/
volatile uint32_t tick;

// timestamp variable
static ll_timestamp_t ll_timestamp;
/*******************************************************************************
 * Function
 ******************************************************************************/
static void LuosHAL_SystickInit(void);
static void LuosHAL_FlashInit(void);
static void LuosHAL_FlashEraseLuosMemoryInfo(void);

/////////////////////////Luos Library Needed function///////////////////////////

/******************************************************************************
 * @brief Luos HAL general initialisation
 * @param None
 * @return None
 ******************************************************************************/
void LuosHAL_Init(void)
{
    // Systick Initialization
    LuosHAL_SystickInit();

    // Flash Initialization
    LuosHAL_FlashInit();

    // start timestamp
    LuosHAL_StartTimestamp();
}
/******************************************************************************
 * @brief Luos HAL general disable IRQ
 * @param None
 * @return None
 ******************************************************************************/
void LuosHAL_SetIrqState(uint8_t Enable)
{
    if (Enable == true)
    {
        __enable_irq();
    }
    else
    {
        __disable_irq();
    }
}
/******************************************************************************
 * @brief Luos HAL general systick tick at 1ms initialize
 * @param None
 * @return tick Counter
 ******************************************************************************/
static void LuosHAL_SystickInit(void)
{
    SysTick->CTRL = 0;
    SysTick->VAL  = 0;
    SysTick->LOAD = 0xbb80 - 1;
    SysTick->CTRL = SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_CLKSOURCE_Msk;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
}
/******************************************************************************
 * @brief Luos HAL general systick tick at 1ms
 * @param None
 * @return tick Counter
 ******************************************************************************/
uint32_t LuosHAL_GetSystick(void)
{
    return tick;
}
/******************************************************************************
 * @brief Luos HAL general systick tick at 1ms
 * @param None
 * @return tick Counter
 ******************************************************************************/
void SysTick_Handler(void)
{
    tick++;
}

/******************************************************************************
 * @brief Luos GetTimestamp
 * @param None
 * @return uint64_t
 ******************************************************************************/
uint64_t LuosHAL_GetTimestamp(void)
{
    ll_timestamp.lower_timestamp  = (SysTick->LOAD - SysTick->VAL) * (1000000000 / MCUFREQ);
    ll_timestamp.higher_timestamp = (uint64_t)(LuosHAL_GetSystick() - ll_timestamp.start_offset);

    return ll_timestamp.higher_timestamp * 1000000 + (uint64_t)ll_timestamp.lower_timestamp;
}

/******************************************************************************
 * @brief Luos start Timestamp
 * @param None
 * @return None
 ******************************************************************************/
void LuosHAL_StartTimestamp(void)
{
    ll_timestamp.start_offset = LuosHAL_GetSystick();
}

/******************************************************************************
 * @brief Luos stop Timestamp
 * @param None
 * @return None
 ******************************************************************************/
void LuosHAL_StopTimestamp(void)
{
    ll_timestamp.lower_timestamp  = 0;
    ll_timestamp.higher_timestamp = 0;
    ll_timestamp.start_offset     = 0;
}
/******************************************************************************
 * @brief Flash Initialisation
 * @param None
 * @return None
 ******************************************************************************/
static void LuosHAL_FlashInit(void)
{
    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_READMODE_NO_MISS_PENALTY | NVMCTRL_CTRLB_SLEEPPRM_WAKEONACCESS
                                  | NVMCTRL_CTRLB_RWS(1) | NVMCTRL_CTRLB_MANW_Msk;
}
/******************************************************************************
 * @brief Erase flash page where Luos keep permanente information
 * @param None
 * @return None
 ******************************************************************************/
static void LuosHAL_FlashEraseLuosMemoryInfo(void)
{
    uint32_t address            = ADDRESS_ALIASES_FLASH;
    NVMCTRL_REGS->NVMCTRL_ADDR  = address >> 1;
    NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_ER_Val | NVMCTRL_CTRLA_CMDEX_KEY;
    NVMCTRL_REGS->NVMCTRL_ADDR  = (address + 256) >> 1;
    NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_ER_Val | NVMCTRL_CTRLA_CMDEX_KEY;
    NVMCTRL_REGS->NVMCTRL_ADDR  = (address + 512) >> 1;
    NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_ER_Val | NVMCTRL_CTRLA_CMDEX_KEY;
    NVMCTRL_REGS->NVMCTRL_ADDR  = (address + 768) >> 1;
    NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_ER_Val | NVMCTRL_CTRLA_CMDEX_KEY;
}
/******************************************************************************
 * @brief Write flash page where Luos keep permanente information
 * @param Address page / size to write / pointer to data to write
 * @return
 ******************************************************************************/
void LuosHAL_FlashWriteLuosMemoryInfo(uint32_t addr, uint16_t size, uint8_t *data)
{
    uint32_t i         = 0;
    uint32_t *paddress = (uint32_t *)addr;

    // Before writing we have to erase the entire page
    // to do that we have to backup current falues by copying it into RAM
    uint8_t page_backup[16 * PAGE_SIZE];
    memcpy(page_backup, (void *)ADDRESS_ALIASES_FLASH, 16 * PAGE_SIZE);

    // Now we can erase the page
    LuosHAL_FlashEraseLuosMemoryInfo();

    // Then add input data into backuped value on RAM
    uint32_t RAMaddr = (addr - ADDRESS_ALIASES_FLASH);
    memcpy(&page_backup[RAMaddr], data, size);

    /* writing 32-bit data into the given address */
    for (i = 0; i < (PAGE_SIZE / 4); i++)
    {
        *paddress++ = page_backup[i];
    }

    /* Set address and command */
    NVMCTRL_REGS->NVMCTRL_ADDR = addr >> 1;

    NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_WP | NVMCTRL_CTRLA_CMDEX_KEY;
}
/******************************************************************************
 * @brief read information from page where Luos keep permanente information
 * @param Address info / size to read / pointer callback data to read
 * @return
 ******************************************************************************/
void LuosHAL_FlashReadLuosMemoryInfo(uint32_t addr, uint16_t size, uint8_t *data)
{
    memcpy(data, (void *)(addr), size);
}

/******************************************************************************
 * @brief Set boot mode in shared flash memory
 * @param
 * @return
 ******************************************************************************/
void LuosHAL_SetMode(uint8_t mode)
{
    uint32_t data_to_write = ~BOOT_MODE_MASK | (mode << BOOT_MODE_OFFSET);
    uint32_t address       = SHARED_MEMORY_ADDRESS;
    uint32_t *paddress     = (uint32_t *)address;

    // erase shared mem sector
    NVMCTRL_REGS->NVMCTRL_ADDR  = address >> 1;
    NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_ER_Val | NVMCTRL_CTRLA_CMDEX_KEY;

    // write 32 bits data into 64B page buffer
    *paddress = data_to_write;

    /* Set address and command */
    NVMCTRL_REGS->NVMCTRL_ADDR  = address >> 1;
    NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_WP | NVMCTRL_CTRLA_CMDEX_KEY;
}

/******************************************************************************
 * @brief Save node ID in shared flash memory
 * @param Address, node_id
 * @return
 ******************************************************************************/
void LuosHAL_SaveNodeID(uint16_t node_id)
{
    uint32_t address       = SHARED_MEMORY_ADDRESS;
    uint32_t *paddress     = (uint32_t *)address;
    uint32_t saved_data    = *paddress;
    uint32_t data_tmp      = ~NODE_ID_MASK | (node_id << NODE_ID_OFFSET);
    uint32_t data_to_write = saved_data & data_tmp;

    // erase shared mem sector
    NVMCTRL_REGS->NVMCTRL_ADDR  = address >> 1;
    NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_ER_Val | NVMCTRL_CTRLA_CMDEX_KEY;

    // write 32 bits data into 64B page buffer
    *paddress = data_to_write;

    /* Set address and command */
    NVMCTRL_REGS->NVMCTRL_ADDR  = address >> 1;
    NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_WP | NVMCTRL_CTRLA_CMDEX_KEY;
}

/******************************************************************************
 * @brief software reboot the microprocessor
 * @param
 * @return
 ******************************************************************************/
void LuosHAL_Reboot(void)
{
    // DeInit RCC and HAL

    // reset systick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    // reset in bootloader mode
    NVIC_SystemReset();
}

#ifdef BOOTLOADER_CONFIG
/******************************************************************************
 * @brief DeInit Bootloader peripherals
 * @param
 * @return
 ******************************************************************************/
void LuosHAL_DeInit(void)
{
}

/******************************************************************************
 * @brief DeInit Bootloader peripherals
 * @param
 * @return
 ******************************************************************************/
typedef void (*pFunction)(void); /*!< Function pointer definition */

void LuosHAL_JumpToApp(uint32_t app_addr)
{
    uint32_t JumpAddress = *(__IO uint32_t *)(app_addr + 4);
    pFunction Jump       = (pFunction)JumpAddress;

    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    SCB->VTOR = app_addr;

    __set_MSP(*(__IO uint32_t *)app_addr);

    __enable_irq();

    Jump();
}

/******************************************************************************
 * @brief Return bootloader mode saved in flash
 * @param
 * @return
 ******************************************************************************/
uint8_t LuosHAL_GetMode(void)
{
    uint32_t *p_start = (uint32_t *)SHARED_MEMORY_ADDRESS;
    uint32_t data     = (*p_start & BOOT_MODE_MASK) >> BOOT_MODE_OFFSET;

    return (uint8_t)data;
}

/******************************************************************************
 * @brief Get node id saved in flash memory
 * @param Address
 * @return node_id
 ******************************************************************************/
uint16_t LuosHAL_GetNodeID(void)
{
    uint32_t *p_start = (uint32_t *)SHARED_MEMORY_ADDRESS;
    uint32_t data     = *p_start & NODE_ID_MASK;
    uint16_t node_id  = (uint16_t)(data >> NODE_ID_OFFSET);

    return node_id;
}

/******************************************************************************
 * @brief erase sectors in flash memory
 * @param Address, size
 * @return
 ******************************************************************************/
void LuosHAL_EraseMemory(uint32_t address, uint16_t size)
{
    uint32_t erase_address  = APP_ADDRESS;
    const uint32_t row_size = 256;

    for (erase_address = APP_ADDRESS; erase_address < ADDRESS_LAST_PAGE_FLASH; erase_address += row_size)
    {
        // wait if NVM controller is busy
        while ((NVMCTRL_REGS->NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_READY_Msk) == 0)
            ;
        // erase row
        NVMCTRL_REGS->NVMCTRL_ADDR  = erase_address >> 1;
        NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_ER_Val | NVMCTRL_CTRLA_CMDEX_KEY;
        // wait during erase
        while ((NVMCTRL_REGS->NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_READY_Msk) == 0)
            ;
    }
}

/******************************************************************************
 * @brief Save node ID in shared flash memory
 * @param Address, node_id
 * @return
 ******************************************************************************/
void LuosHAL_ProgramFlash(uint32_t address, uint16_t size, uint8_t *data)
{
    uint32_t *paddress  = (uint32_t *)address;
    uint32_t data_index = 0;
    uint8_t page_index  = 0;

    // wait if NVM controller is busy
    while ((NVMCTRL_REGS->NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_READY_Msk) == 0)
        ;

    while (data_index < size)
    {
        // set addr in NVM register
        NVMCTRL_REGS->NVMCTRL_ADDR = (uint32_t)&data[data_index] >> 1;
        // fill page buffer with data bytes
        for (page_index = 0; page_index < 16; page_index++)
        {
            // break if all data had been written
            if (data_index >= size)
                break;
            // write data
            *paddress = *(uint32_t *)&data[data_index];
            // update address
            paddress += 1;
            data_index += 4;
        }
        // Set address and command
        NVMCTRL_REGS->NVMCTRL_CTRLA = NVMCTRL_CTRLA_CMD_WP | NVMCTRL_CTRLA_CMDEX_KEY;
        // wait during programming
        while ((NVMCTRL_REGS->NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_READY_Msk) == 0)
            ;
    }
}
#endif