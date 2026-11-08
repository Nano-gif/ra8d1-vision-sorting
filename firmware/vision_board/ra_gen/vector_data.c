/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        #if __has_include("r_ioport.h")
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = sci_b_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
            [1] = sci_b_uart_txi_isr, /* SCI9 TXI (Transmit data empty) */
            [2] = sci_b_uart_tei_isr, /* SCI9 TEI (Transmit end) */
            [3] = sci_b_uart_eri_isr, /* SCI9 ERI (Receive error) */
            [4] = glcdc_line_detect_isr, /* GLCDC LINE DETECT (Specified line) */
            [5] = drw_int_isr, /* DRW INT (DRW interrupt) */
            [6] = sci_b_i2c_txi_isr, /* SCI3 TXI (Transmit data empty) */
            [7] = sci_b_i2c_tei_isr, /* SCI3 TEI (Transmit end) */
            [8] = ceu_isr, /* CEU CEUI (CEU interrupt) */
            [9] = sdhimmc_accs_isr, /* SDHIMMC1 ACCS (Card access) */
            [10] = sdhimmc_card_isr, /* SDHIMMC1 CARD (Card detect) */
            [11] = dmac_int_isr, /* DMAC0 INT (DMAC0 transfer end 0) */
            [12] = sci_b_spi_rxi_isr, /* SCI2 RXI (Receive data full) */
            [13] = sci_b_spi_txi_isr, /* SCI2 TXI (Transmit data empty) */
            [14] = sci_b_spi_tei_isr, /* SCI2 TEI (Transmit end) */
            [15] = sci_b_spi_eri_isr, /* SCI2 ERI (Receive error) */
            [16] = gpt_counter_overflow_isr, /* GPT5 COUNTER OVERFLOW (Overflow) */
            [17] = gpt_counter_overflow_isr, /* GPT4 COUNTER OVERFLOW (Overflow) */
            [18] = r_icu_isr, /* ICU IRQ13 (External pin interrupt 13) */
        };
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] =
        {
            [0] = BSP_PRV_IELS_ENUM(EVENT_SCI9_RXI), /* SCI9 RXI (Receive data full) */
            [1] = BSP_PRV_IELS_ENUM(EVENT_SCI9_TXI), /* SCI9 TXI (Transmit data empty) */
            [2] = BSP_PRV_IELS_ENUM(EVENT_SCI9_TEI), /* SCI9 TEI (Transmit end) */
            [3] = BSP_PRV_IELS_ENUM(EVENT_SCI9_ERI), /* SCI9 ERI (Receive error) */
            [4] = BSP_PRV_IELS_ENUM(EVENT_GLCDC_LINE_DETECT), /* GLCDC LINE DETECT (Specified line) */
            [5] = BSP_PRV_IELS_ENUM(EVENT_DRW_INT), /* DRW INT (DRW interrupt) */
            [6] = BSP_PRV_IELS_ENUM(EVENT_SCI3_TXI), /* SCI3 TXI (Transmit data empty) */
            [7] = BSP_PRV_IELS_ENUM(EVENT_SCI3_TEI), /* SCI3 TEI (Transmit end) */
            [8] = BSP_PRV_IELS_ENUM(EVENT_CEU_CEUI), /* CEU CEUI (CEU interrupt) */
            [9] = BSP_PRV_IELS_ENUM(EVENT_SDHIMMC1_ACCS), /* SDHIMMC1 ACCS (Card access) */
            [10] = BSP_PRV_IELS_ENUM(EVENT_SDHIMMC1_CARD), /* SDHIMMC1 CARD (Card detect) */
            [11] = BSP_PRV_IELS_ENUM(EVENT_DMAC0_INT), /* DMAC0 INT (DMAC0 transfer end 0) */
            [12] = BSP_PRV_IELS_ENUM(EVENT_SCI2_RXI), /* SCI2 RXI (Receive data full) */
            [13] = BSP_PRV_IELS_ENUM(EVENT_SCI2_TXI), /* SCI2 TXI (Transmit data empty) */
            [14] = BSP_PRV_IELS_ENUM(EVENT_SCI2_TEI), /* SCI2 TEI (Transmit end) */
            [15] = BSP_PRV_IELS_ENUM(EVENT_SCI2_ERI), /* SCI2 ERI (Receive error) */
            [16] = BSP_PRV_IELS_ENUM(EVENT_GPT5_COUNTER_OVERFLOW), /* GPT5 COUNTER OVERFLOW (Overflow) */
            [17] = BSP_PRV_IELS_ENUM(EVENT_GPT4_COUNTER_OVERFLOW), /* GPT4 COUNTER OVERFLOW (Overflow) */
            [18] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQ13), /* ICU IRQ13 (External pin interrupt 13) */
        };
        #elif __has_include("r_ioport_b.h")
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_IRQ_VECTOR_MAX_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
            [BSP_PRV_IELS_ENUM(SCI9_RXI)] = sci_b_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
            [BSP_PRV_IELS_ENUM(SCI9_TXI)] = sci_b_uart_txi_isr, /* SCI9 TXI (Transmit data empty) */
            [BSP_PRV_IELS_ENUM(SCI9_TEI)] = sci_b_uart_tei_isr, /* SCI9 TEI (Transmit end) */
            [BSP_PRV_IELS_ENUM(SCI9_ERI)] = sci_b_uart_eri_isr, /* SCI9 ERI (Receive error) */
            [BSP_PRV_IELS_ENUM(GLCDC_LINE_DETECT)] = glcdc_line_detect_isr, /* GLCDC LINE DETECT (Specified line) */
            [BSP_PRV_IELS_ENUM(DRW_INT)] = drw_int_isr, /* DRW INT (DRW interrupt) */
            [BSP_PRV_IELS_ENUM(SCI3_TXI)] = sci_b_i2c_txi_isr, /* SCI3 TXI (Transmit data empty) */
            [BSP_PRV_IELS_ENUM(SCI3_TEI)] = sci_b_i2c_tei_isr, /* SCI3 TEI (Transmit end) */
            [BSP_PRV_IELS_ENUM(CEU_CEUI)] = ceu_isr, /* CEU CEUI (CEU interrupt) */
            [BSP_PRV_IELS_ENUM(SDHIMMC1_ACCS)] = sdhimmc_accs_isr, /* SDHIMMC1 ACCS (Card access) */
            [BSP_PRV_IELS_ENUM(SDHIMMC1_CARD)] = sdhimmc_card_isr, /* SDHIMMC1 CARD (Card detect) */
            [BSP_PRV_IELS_ENUM(DMAC0_INT)] = dmac_int_isr, /* DMAC0 INT (DMAC0 transfer end 0) */
            [BSP_PRV_IELS_ENUM(SCI2_RXI)] = sci_b_spi_rxi_isr, /* SCI2 RXI (Receive data full) */
            [BSP_PRV_IELS_ENUM(SCI2_TXI)] = sci_b_spi_txi_isr, /* SCI2 TXI (Transmit data empty) */
            [BSP_PRV_IELS_ENUM(SCI2_TEI)] = sci_b_spi_tei_isr, /* SCI2 TEI (Transmit end) */
            [BSP_PRV_IELS_ENUM(SCI2_ERI)] = sci_b_spi_eri_isr, /* SCI2 ERI (Receive error) */
            [BSP_PRV_IELS_ENUM(GPT5_COUNTER_OVERFLOW)] = gpt_counter_overflow_isr, /* GPT5 COUNTER OVERFLOW (Overflow) */
            [BSP_PRV_IELS_ENUM(GPT4_COUNTER_OVERFLOW)] = gpt_counter_overflow_isr, /* GPT4 COUNTER OVERFLOW (Overflow) */
            [BSP_PRV_IELS_ENUM(ICU_IRQ13)] = r_icu_isr, /* ICU IRQ13 (External pin interrupt 13) */
        };
        #endif
        #endif