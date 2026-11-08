/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = sci_uart_rxi_isr, /* SCI5 RXI (Receive data full) */
            [1] = sci_uart_txi_isr, /* SCI5 TXI (Transmit data empty) */
            [2] = sci_uart_tei_isr, /* SCI5 TEI (Transmit end) */
            [3] = sci_uart_eri_isr, /* SCI5 ERI (Receive error) */
            [4] = iic_master_rxi_isr, /* IIC2 RXI (Receive data full) */
            [5] = iic_master_txi_isr, /* IIC2 TXI (Transmit data empty) */
            [6] = iic_master_tei_isr, /* IIC2 TEI (Transmit end) */
            [7] = iic_master_eri_isr, /* IIC2 ERI (Transfer error) */
            [8] = sci_uart_rxi_isr, /* SCI0 RXI (Receive data full) */
            [9] = sci_uart_txi_isr, /* SCI0 TXI (Transmit data empty) */
            [10] = sci_uart_tei_isr, /* SCI0 TEI (Transmit end) */
            [11] = sci_uart_eri_isr, /* SCI0 ERI (Receive error) */
            [12] = sci_uart_rxi_isr, /* SCI3 RXI (Receive data full) */
            [13] = sci_uart_txi_isr, /* SCI3 TXI (Transmit data empty) */
            [14] = sci_uart_tei_isr, /* SCI3 TEI (Transmit end) */
            [15] = sci_uart_eri_isr, /* SCI3 ERI (Receive error) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_SCI5_RXI,GROUP0), /* SCI5 RXI (Receive data full) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_SCI5_TXI,GROUP1), /* SCI5 TXI (Transmit data empty) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_SCI5_TEI,GROUP2), /* SCI5 TEI (Transmit end) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_SCI5_ERI,GROUP3), /* SCI5 ERI (Receive error) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_IIC2_RXI,GROUP4), /* IIC2 RXI (Receive data full) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_IIC2_TXI,GROUP5), /* IIC2 TXI (Transmit data empty) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_IIC2_TEI,GROUP6), /* IIC2 TEI (Transmit end) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_IIC2_ERI,GROUP7), /* IIC2 ERI (Transfer error) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_SCI0_RXI,GROUP0), /* SCI0 RXI (Receive data full) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_SCI0_TXI,GROUP1), /* SCI0 TXI (Transmit data empty) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_SCI0_TEI,GROUP2), /* SCI0 TEI (Transmit end) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_SCI0_ERI,GROUP3), /* SCI0 ERI (Receive error) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_SCI3_RXI,GROUP4), /* SCI3 RXI (Receive data full) */
            [13] = BSP_PRV_VECT_ENUM(EVENT_SCI3_TXI,GROUP5), /* SCI3 TXI (Transmit data empty) */
            [14] = BSP_PRV_VECT_ENUM(EVENT_SCI3_TEI,GROUP6), /* SCI3 TEI (Transmit end) */
            [15] = BSP_PRV_VECT_ENUM(EVENT_SCI3_ERI,GROUP7), /* SCI3 ERI (Receive error) */
        };
        #endif
        #endif