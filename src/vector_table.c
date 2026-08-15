#include "hal_data.h"

/*
 * This project builds the FSP sources directly (no e2studio Smart Configurator), so the
 * interrupt vector table that the configurator normally generates as "vector_data.c" does not
 * exist anywhere in the framework package: bsp_security.c merely declares
 * `extern const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES];` and nothing defines it.
 * Any peripheral interrupt (here: SCI2/UART) requires providing this table ourselves.
 *
 * Only the 4 SCI2 slots are populated; every other index is used only if something else calls
 * R_BSP_IrqCfgEnable() for it, which nothing in this project does, so leaving them zeroed is safe.
 */
BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES] BSP_PLACE_IN_SECTION(
    BSP_SECTION_APPLICATION_VECTORS) = {
    [VECTOR_NUMBER_SCI2_TXI] = sci_uart_txi_isr,
    [VECTOR_NUMBER_SCI2_TEI] = sci_uart_tei_isr,
    [VECTOR_NUMBER_SCI2_RXI] = sci_uart_rxi_isr,
    [VECTOR_NUMBER_SCI2_ERI] = sci_uart_eri_isr,
};

/* Links each NVIC IRQ slot above to the ELC event it should fire on (written into R_ICU->IELSR[]). */
const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] = {
    [VECTOR_NUMBER_SCI2_TXI] = ELC_EVENT_SCI2_TXI,
    [VECTOR_NUMBER_SCI2_TEI] = ELC_EVENT_SCI2_TEI,
    [VECTOR_NUMBER_SCI2_RXI] = ELC_EVENT_SCI2_RXI,
    [VECTOR_NUMBER_SCI2_ERI] = ELC_EVENT_SCI2_ERI,
};

extern const fsp_vector_t __VECTOR_TABLE[BSP_CORTEX_VECTOR_TABLE_ENTRIES];

/* Linker-script symbol (variants/UNOWIFIR4/fsp.ld, ".vector_table" section) marking the RAM
 * address reserved for the vector table copy below. Not redefined as a C variable here (the
 * platform-renesas-ra "fsp-button-isr" example does that, which duplicate-defines this same
 * linker symbol) -- referenced directly via extern instead. */
extern uint32_t APPLICATION_VECTOR_TABLE_ADDRESS_RAM[];

/*
 * Copies both the core Cortex-M exception vectors and the application (ICU) vectors above into
 * the RAM region the linker script reserves for this purpose, then points VTOR there. Follows the
 * platform-renesas-ra "fsp-button-isr" example: when the app is entered via the Arduino USB
 * bootloader, using our own vector table requires this RAM copy rather than relying on VTOR
 * already pointing at the flash-resident table.
 */
void copy_vectors_to_ram(void)
{
    __disable_irq();

    volatile uint32_t * irq_vector_table = (volatile uint32_t *) APPLICATION_VECTOR_TABLE_ADDRESS_RAM;

    for (size_t i = 0; i < BSP_CORTEX_VECTOR_TABLE_ENTRIES; i++)
    {
        *(irq_vector_table + i) = (uint32_t) __VECTOR_TABLE[i];
    }

    for (size_t i = 0; i < BSP_ICU_VECTOR_MAX_ENTRIES; i++)
    {
        *(irq_vector_table + i + BSP_CORTEX_VECTOR_TABLE_ENTRIES) = (uint32_t) g_vector_table[i];
    }

    SCB->VTOR = (uint32_t) irq_vector_table;

    __DSB();
    __enable_irq();
}
