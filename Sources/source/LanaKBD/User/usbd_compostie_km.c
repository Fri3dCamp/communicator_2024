/********************************** (C) COPYRIGHT *******************************
 * File Name          : usbd_composite_km.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/20
 * Description        : USB keyboard and mouse processing.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


/*******************************************************************************/
/* Header Files */
#include "usbd_composite_km.h"
#include "usb_pwr.h"

#define I2COUTPUT (1)
#define UARTOUTPUT (0)

uint8_t USBD_ENDPx_DataUp( uint8_t endp, uint8_t *pbuf, uint16_t len );

/*******************************************************************************/
/* Variable Definition */

#define N_COLS (9)
#define N_ROWS (8)
#define N_LEDS (12)

#define LED_LANA_OFFSET (0)
#define LED_LANA_LEN (3)
#define LED_BACKLIGHT_OFFSET (3)
#define LED_BACKLIGHT_LEN (8)
#define LED_RED_OFFSET (11)
#define LED_RED_LEN (1)

/* Keyboard */
volatile uint8_t  KB_Scan_Done = 0x00;                                          // Keyboard Keys Scan Done
uint8_t KB_Scan_Result[N_COLS] = {                                              // Keyboard Keys Current Scan Result
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};
uint8_t KB_Scan_Last_Result[N_COLS] = {                                         // Keyboard Keys Last Scan Result
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};
uint8_t  KB_Data_Pack[ DEF_ENDP_SIZE_KB ] = { 0x00 };                           // Keyboard IN Data Packet
volatile uint8_t  KB_LED_Cur_Status = 0x00;                                     // Keyboard LED Current Result

static uint8_t keycodes[N_COLS][N_ROWS] = {
    { KEY_ESC, KEY_1, KEY_0, KEY_Y, KEY_S, KEY_APOSTROPHE, KEY_M, KEY_BACKSLASH, },
    { KEY_F1, KEY_2, KEY_MINUS, KEY_U, KEY_D, KEY_ENTER, KEY_COMMA, KEY_SPACE, },
    { KEY_F2, KEY_3, KEY_EQUAL, KEY_I, KEY_F, KEY_NONE, KEY_DOT, KEY_SPACE, },
    { KEY_F3, KEY_4, KEY_TAB, KEY_O, KEY_G, KEY_Z, KEY_SLASH, KEY_SPACE, },
    { KEY_F4, KEY_5, KEY_Q, KEY_P, KEY_H, KEY_X, KEY_UP, KEY_NONE, },
    { KEY_F5, KEY_6, KEY_W, KEY_LEFTBRACE, KEY_J, KEY_C, KEY_NONE, KEY_LEFT, },
    { KEY_F6, KEY_7, KEY_E, KEY_RIGHTBRACE, KEY_K, KEY_V, KEY_NONE, KEY_DOWN, },
    { KEY_BACKSPACE, KEY_8, KEY_R, KEY_NONE, KEY_L, KEY_B, KEY_LEFTMETA, KEY_RIGHT, },
    { KEY_GRAVE, KEY_9, KEY_T, KEY_A, KEY_SEMICOLON, KEY_N, KEY_NONE, KEY_NONE, },
};

static uint8_t modifiers[N_COLS][N_ROWS] = {
    { KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, },
    { KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, },
    { KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_MOD_LSHIFT, KEY_NONE, KEY_NONE, },
    { KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, },
    { KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_MOD_RALT, },
    { KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_MOD_RSHIFT, KEY_NONE, },
    { KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_MOD_LCTRL, KEY_NONE, },
    { KEY_NONE, KEY_NONE, KEY_NONE, KEY_MOD_RMETA, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, },
    { KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_MOD_LALT, KEY_NONE, },
};

static uint8_t leds[N_LEDS];

/*******************************************************************************/
/* Interrupt Function Declaration */
void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      TIM3_Init
 *
 * @brief   Initialize timer3 for keyboard and mouse scan.
 *
 * @param   arr - The specific period value
 *          psc - The specifies prescaler value
 *
 * @return  none
 */
void TIM3_Init( uint16_t arr, uint16_t psc )
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = { 0 };
    NVIC_InitTypeDef NVIC_InitStructure = { 0 };

    /* Enable Timer3 Clock */
    RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM3, ENABLE );

    /* Initialize Timer3 */
    TIM_TimeBaseStructure.TIM_Period = arr;
    TIM_TimeBaseStructure.TIM_Prescaler = psc;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit( TIM3, &TIM_TimeBaseStructure );

    TIM_ITConfig( TIM3, TIM_IT_Update, ENABLE );

    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init( &NVIC_InitStructure );

    /* Enable Timer3 */
    TIM_Cmd( TIM3, ENABLE );
}

/*********************************************************************
 * @fn      TIM3_IRQHandler
 *
 * @brief   This function handles TIM3 global interrupt request.
 *
 * @return  none
 */
void TIM3_IRQHandler( void )
{
    if( TIM_GetITStatus( TIM3, TIM_IT_Update ) != RESET )
    {
        /* Clear interrupt flag */
        TIM_ClearITPendingBit( TIM3, TIM_IT_Update );

        /* Handle keyboard scan */
        KB_Scan( );
    }
}

static void KB_Set_Col(uint8_t colIdx)
{
    if (colIdx == 0) {
        GPIO_Write(GPIOA, 0);
        GPIO_Write(GPIOB, GPIO_Pin_4);
    } else if (colIdx == 1) {
        GPIO_Write(GPIOA, 0);
        GPIO_Write(GPIOB, GPIO_Pin_3);
    } else if (colIdx == 2) {
        GPIO_Write(GPIOA, GPIO_Pin_0);
        GPIO_Write(GPIOB, 0);
    } else if (colIdx == 3) {
        GPIO_Write(GPIOA, GPIO_Pin_15);
        GPIO_Write(GPIOB, 0);
    } else if (colIdx == 4) {
        GPIO_Write(GPIOA, GPIO_Pin_1);
        GPIO_Write(GPIOB, 0);
    } else if (colIdx == 5) {
        GPIO_Write(GPIOA, GPIO_Pin_14);
        GPIO_Write(GPIOB, 0);
    } else if (colIdx == 6) {
        GPIO_Write(GPIOA, 0);
        GPIO_Write(GPIOB, GPIO_Pin_0);
    } else if (colIdx == 7) {
        GPIO_Write(GPIOA, GPIO_Pin_13);
        GPIO_Write(GPIOB, 0);
    } else if (colIdx == 8) {
        GPIO_Write(GPIOA, 0);
        GPIO_Write(GPIOB, GPIO_Pin_1);
    }
}

/*********************************************************************
 * @fn      USART_Printf_Init
 *
 * @brief   Initializes the USARTx peripheral.
 *
 * @param   baudrate - USART communication baud rate.
 *
 * @return  None
 */
void USART_Badge_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx;

    USART_Init(USART2, &USART_InitStructure);
    USART_Cmd(USART2, ENABLE);
}

/*********************************************************************
 * @fn      IIC_Init
 *
 * @brief   Initializes the IIC peripheral.
 *
 * @return  none
 */
void IIC_Init(uint32_t bound, uint16_t address)
{
    GPIO_InitTypeDef GPIO_InitStructure={0};
    I2C_InitTypeDef I2C_InitTSturcture={0};

    RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE );
    RCC_APB1PeriphClockCmd( RCC_APB1Periph_I2C1, ENABLE );

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init( GPIOB, &GPIO_InitStructure );

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init( GPIOB, &GPIO_InitStructure );

    I2C_InitTSturcture.I2C_ClockSpeed = bound;
    I2C_InitTSturcture.I2C_Mode = I2C_Mode_I2C;
    I2C_InitTSturcture.I2C_DutyCycle = I2C_DutyCycle_16_9;
    I2C_InitTSturcture.I2C_OwnAddress1 = address << 1;
    I2C_InitTSturcture.I2C_Ack = I2C_Ack_Enable;
    I2C_InitTSturcture.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init( I2C1, &I2C_InitTSturcture );

    I2C_Cmd( I2C1, ENABLE );
}

void LED_Init_Neopixel(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    GPIO_PinRemapConfig(AFIO_PCFR1_PD01_REMAP, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    memset(leds, 0, N_LEDS);
}

static void WS2812BSimpleSend( GPIO_TypeDef * port, uint16_t GPIO_Pin, uint8_t * data, int len_in_bytes )
{
    port->BCR = GPIO_Pin;

    uint8_t * end = data + len_in_bytes;
    while( data != end )
    {
        uint8_t byte = *data;

        int i;
        for( i = 0; i < 8; i++ ) {
            if( byte & 0x80 )
            {
                // WS2812B's need AT LEAST 625ns for a logical "1"
                port->BSHR = GPIO_Pin;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                port->BCR = GPIO_Pin;
            }
            else
            {
                // WS2812B's need BETWEEN 62.5 to about 500 ns for a logical "0"
                port->BSHR = GPIO_Pin;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                port->BCR = GPIO_Pin;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            }
            byte <<= 1;
        }
        data++;
    }

    port->BCR = GPIO_Pin;
}

static void LED_Lana_SetColour(uint8_t red, uint8_t green, uint8_t blue)
{
    leds[LED_LANA_OFFSET+0] = green;
    leds[LED_LANA_OFFSET+1] = red;
    leds[LED_LANA_OFFSET+2] = blue;
    WS2812BSimpleSend(GPIOD, GPIO_Pin_0, leds, N_LEDS);
}

static void LED_Backlight_SetBrightness(uint8_t brightness)
{
    memset(&leds[LED_BACKLIGHT_OFFSET], brightness, LED_BACKLIGHT_LEN);
    WS2812BSimpleSend(GPIOD, GPIO_Pin_0, leds, N_LEDS);
}

static void LED_Red_Toggle(void)
{
    if (leds[LED_RED_OFFSET] < 0x0f) {
        memset(&leds[LED_RED_OFFSET], 0xff, LED_RED_LEN);
    }
    else {
        memset(&leds[LED_RED_OFFSET], 0x00, LED_RED_LEN);
    }
    WS2812BSimpleSend(GPIOD, GPIO_Pin_0, leds, N_LEDS);
}

/*********************************************************************
 * @fn      KB_Scan_Init
 *
 * @brief   Initialize IO for keyboard scan.
 *
 * @return  none
 */
void KB_Scan_Init( void )
{
    GPIO_InitTypeDef GPIO_InitStructure = { 0 };

    /* Enable GPIOA B and D clock */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOD, ENABLE);

    /* remap SWD pins */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    /* remap PD1 */
    GPIO_PinRemapConfig(AFIO_PCFR1_PD01_REMAP, ENABLE);

    /* Initialize GPIOD (row0) as input */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init( GPIOD, &GPIO_InitStructure );

    /* Initialize GPIOA (row1-row6) as input */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init( GPIOA, &GPIO_InitStructure );

    /* Initialize GPIOB (row7) as input */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init( GPIOB, &GPIO_InitStructure );

    /* Initialize GPIOA (col2-col5, col7) as outputs */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init( GPIOA, &GPIO_InitStructure );

    /* Initialize GPIOB (col0, col1, col6, col8) as outputs */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_3 | GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init( GPIOB, &GPIO_InitStructure );

    /* put col0 to high */
    KB_Set_Col(0);

    /* put all outputs high*/

    /*
    output: 
    col0: PB4
    col1: PB3
    col2: PA0
    col3: PA15
    col4: PA1
    col5: PA14 / SWC
    col6: PB0
    col7: PA13 / SWD
    col8: PB1
    
    input: 
    row0: PD1
    row1: PA3
    row2: PA5
    row3: PA4
    row4: PA6
    row5: PA9
    row6: PA7
    row7: PB5
    */
}

/*********************************************************************
 * @fn      KB_Sleep_Wakeup_Cfg
 *
 * @brief   Configure keyboard wake up mode.
 *
 * @return  none
 */
void KB_Sleep_Wakeup_Cfg( void )
{
    EXTI_InitTypeDef EXTI_InitStructure = { 0 };

    /* Enable GPIOB clock */
    RCC_APB2PeriphClockCmd( RCC_APB2Periph_AFIO, ENABLE );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOD, GPIO_PinSource1 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource3 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line3;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource4 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line4;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOB, GPIO_PinSource5 );
    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource5 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line5;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource6 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line6;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource7 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line7;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    GPIO_EXTILineConfig( GPIO_PortSourceGPIOA, GPIO_PinSource9 );
    EXTI_InitStructure.EXTI_Line = EXTI_Line9;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );

    EXTI->INTENR |= EXTI_INTENR_MR1 | EXTI_INTENR_MR3 | EXTI_INTENR_MR4 | EXTI_INTENR_MR5 | EXTI_INTENR_MR6 | EXTI_INTENR_MR7 | EXTI_INTENR_MR9;
}

static uint8_t io_to_scan_result(uint16_t a, uint16_t b, uint16_t d)
{
    uint8_t out = 0;

    // row7: PB5
    if (b & (GPIO_Pin_5)) {
        out |= 1;
    }
    out <<= 1;

    // row6: PA7
    if (a & (GPIO_Pin_7)) {
        out |= 1;
    }
    out <<= 1;

    // row5: PA9
    if (a & (GPIO_Pin_9)) {
        out |= 1;
    }
    out <<= 1;

    // row4: PA6
    if (a & (GPIO_Pin_6)) {
        out |= 1;
    }
    out <<= 1;

    // row3: PA4
    if (a & (GPIO_Pin_4)) {
        out |= 1;
    }
    out <<= 1;

    // row2: PA5
    if (a & (GPIO_Pin_5)) {
        out |= 1;
    }
    out <<= 1;

    // row1: PA3
    if (a & (GPIO_Pin_3)) {
        out |= 1;
    }
    out <<= 1;

    // row0: PD1
    if (d & (GPIO_Pin_1)) {
        out |= 1;
    }
    return out;
}

/*********************************************************************
 * @fn      KB_Scan
 *
 * @brief   Perform keyboard scan.
 *
 * @return  none
 */
void KB_Scan( void )
{
    static uint16_t scan_cnt = 0;
    static uint8_t scan_col = 0;
    static uint8_t scan_result[N_COLS] = { 0x00 };
    static uint8_t scan = 0;

    scan_cnt++;
    if( ( scan_cnt % 10 ) == 0 )
    {
        scan_cnt = 0;

        /* Determine whether the two scan results are consistent */
        if( scan == io_to_scan_result(GPIO_ReadInputData(GPIOA), GPIO_ReadInputData(GPIOB), GPIO_ReadInputData(GPIOD)))
        {
            scan_result[scan_col] = scan;
        }
        scan_col = (scan_col + 1) % N_COLS;
        KB_Set_Col(scan_col);
        if (scan_col == 0) {
            memcpy(KB_Scan_Result, scan_result, N_COLS);
            KB_Scan_Done = 1;
        }
    }
    else if( ( scan_cnt % 5 ) == 0 )
    {
        /* Save the first scan result */
        scan = io_to_scan_result(GPIO_ReadInputData(GPIOA), GPIO_ReadInputData(GPIOB), GPIO_ReadInputData(GPIOD));
    }
}

static uint8_t get_keycode(uint8_t col, uint8_t row, uint8_t *keycode)
{
    *keycode = modifiers[col][row];

    if (*keycode != KEY_NONE) {
        return 1;
    }

    *keycode = keycodes[col][row];
    if (*keycode != KEY_NONE) {
        return 0;
    }

    return 2;
}

static void determine_hid_report(uint8_t keycode, uint8_t is_modifier, uint8_t value, uint8_t *out)
{
    uint8_t j;
    static uint8_t key_cnt = 0x00;

    if (is_modifier) {
        if (value) {
            out[0] |= keycode;
         } else {
            out[0] &= (~keycode);
        }
        return;
    }

    if (value) { // key press
        out[ 2 + key_cnt ] = keycode;
        if (key_cnt < 6) {
            key_cnt++;
        }
    }
    else { // key release
        for( j = 2; j < 8; j++ ) {
            if( out[ j ] == keycode ) {
                break;
            }
        }
        if( j == 8 ) {
            out[ 5 ] = 0;
        }
        else {
            memcpy( &out[ j ], &out[ j + 1 ], ( 8 - j - 1 ) );
            out[ 7 ] = 0;
        }
        if (key_cnt > 0) {
            key_cnt--;
        }
    }
}

static void toggle_backlight(void)
{
    static uint8_t on = 0;
    if (on) {
        LED_Backlight_SetBrightness(0x00);
        on = 0;
    }
    else {
        LED_Backlight_SetBrightness(0xff);
        on = 1;
    }
}

static void handle_fn(uint8_t *in, uint8_t *out)
{
    uint8_t j;

    if ((in[0] & (KEY_MOD_RMETA | KEY_MOD_RSHIFT)) == (KEY_MOD_RMETA | KEY_MOD_RSHIFT)) {
        determine_hid_report(KEY_CAPSLOCK, 0, 1, in);
        LED_Red_Toggle();
    }
    else {
        determine_hid_report(KEY_CAPSLOCK, 0, 0, in);
    }

    /* take a copy of the report */
    memcpy(out, in, DEF_ENDP_SIZE_KB);

    /*
      if the FN key is pressed, modify the output,
      and trigger special functions
    */
    if (out[0] & KEY_MOD_RMETA) {

        /* replace keys and/or trigger special functions */
        for (j = 2; j < DEF_ENDP_SIZE_KB; j++) {
            switch (out[j]) {
            case KEY_LEFTMETA:
                LED_Lana_SetColour(0x00, 0x00, 0x00);
                memcpy(&out[j], &out[j+1], (DEF_ENDP_SIZE_KB - j - 1));
                out[7] = 0;
                break;
            case KEY_F1:
                LED_Lana_SetColour(0xff, 0x00, 0x00);
                memcpy(&out[j], &out[j+1], (DEF_ENDP_SIZE_KB - j - 1));
                out[7] = 0;
                break;
            case KEY_F2:
                LED_Lana_SetColour(0xff, 44, 0x00);
                memcpy(&out[j], &out[j+1], (DEF_ENDP_SIZE_KB - j - 1));
                out[7] = 0;
                break;
            case KEY_F3:
                LED_Lana_SetColour(0xff, 0xff, 0x00);
                memcpy(&out[j], &out[j+1], (DEF_ENDP_SIZE_KB - j - 1));
                out[7] = 0;
                break;
            case KEY_F4:
                LED_Lana_SetColour(0x00, 0xff, 0x00);
                memcpy(&out[j], &out[j+1], (DEF_ENDP_SIZE_KB - j - 1));
                out[7] = 0;
                break;
            case KEY_F5:
                LED_Lana_SetColour(0x00, 0x00, 0xff);
                memcpy(&out[j], &out[j+1], (DEF_ENDP_SIZE_KB - j - 1));
                out[7] = 0;
                break;
            case KEY_F6:
                LED_Lana_SetColour(0x80, 0x00, 0x80);
                memcpy(&out[j], &out[j+1], (DEF_ENDP_SIZE_KB - j - 1));
                out[7] = 0;
                break;
            case KEY_SPACE:
                toggle_backlight();
                memcpy(&out[j], &out[j+1], (DEF_ENDP_SIZE_KB - j - 1));
                out[7] = 0;
                break;
            case KEY_BACKSPACE:
                out[j] = KEY_DELETE;
                break;
            case KEY_LEFT:
                out[j] = KEY_HOME;
                break;
            case KEY_RIGHT:
                out[j] = KEY_END;
                break;
            case KEY_UP:
                out[j] = KEY_PAGEUP;
                break;
            case KEY_DOWN:
                out[j] = KEY_PAGEDOWN;
                break;
            default:
                break;
            }
        }

        /* clear the FN key in the reporting */
        out[0] &= (~KEY_MOD_RMETA);
    }
}

static uint8_t wait_for_event(uint32_t I2C_EVENT)
{
    uint8_t tries = 10;
    while (tries > 0) {
        if (!I2C_CheckEvent(I2C1, I2C_EVENT )) {
            Delay_Ms(20);
        } else {
            return 1;
        }
        tries--;
    }
    return 0;
}

static uint8_t wait_for_flag(uint32_t I2C_FLAG)
{
    uint8_t tries = 10;
    while (tries > 0) {
        if (I2C_GetFlagStatus(I2C1, I2C_FLAG) == RESET) {
            Delay_Ms(20);
        } else {
            return 1;
        }
        tries--;
    }
    return 0;
}

/*********************************************************************
 * @fn      KB_Scan_Handle
 *
 * @brief   Handle keyboard scan data.
 *
 * @return  none
 */
void KB_Scan_Handle( void )
{
    uint8_t c, i, keycode, is_modifier;
    static uint8_t flag = 0x00;
    static uint8_t reported[DEF_ENDP_SIZE_KB];

    if( KB_Scan_Done )
    {
        KB_Scan_Done = 0;

        if( memcmp(KB_Scan_Result, KB_Scan_Last_Result, N_COLS) != 0 )
        {
            for( c = 0; c < N_COLS; c++ )
            {
                if (KB_Scan_Result[c] != KB_Scan_Last_Result[c]) {
                    for( i = 0; i < N_ROWS; i++ )
                    {
                        /* Determine that there is at least one key is pressed or released */
                        if( ( KB_Scan_Result[c] & ( 1 << i ) ) != ( KB_Scan_Last_Result[c] & ( 1 << i ) ) )
                        {
                            is_modifier = get_keycode(c, i, &keycode);
                            if (is_modifier != 2) {
                                determine_hid_report(keycode, is_modifier, ( KB_Scan_Result[c] & ( 1 << i ) ), KB_Data_Pack);
                            }
                        }
                    }
                }
            }

            /* Copy the keyboard data to the buffer of endpoint 1 and set the data uploading flag */
            memcpy(KB_Scan_Last_Result, KB_Scan_Result, N_COLS);
            /* handle special FN triggers */
            handle_fn(KB_Data_Pack, reported);
            flag = 1;
        }
    }

    if( flag )
    {
#ifdef UARTOUTPUT
        /* send it to uart2 */
        for(i = 0; i < DEF_ENDP_SIZE_KB; i++){
            while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
            USART_SendData(USART2, reported[i]);
        }
#endif

        /* Load keyboard data to endpoint 1 */
        if( bDeviceState == CONFIGURED )
        {
            USBD_ENDPx_DataUp(ENDP1, reported, DEF_ENDP_SIZE_KB);
        }

        /* Clear flag always */
        flag = 0;
    }

#ifdef I2COUTPUT
    /* send it on i2c if requested by the master */
    if (I2C_CheckEvent( I2C1, I2C_EVENT_SLAVE_RECEIVER_ADDRESS_MATCHED )) {
        for (i = 0; i < DEF_ENDP_SIZE_KB; i++) {
            // send the byte to master
            if (wait_for_flag(I2C_FLAG_TXE) == 0) {
                break;
            }
            I2C_SendData(I2C1, reported[i]);
            if (i < (DEF_ENDP_SIZE_KB-1)) {
                // wait for the master to ack
                if (wait_for_event(I2C_EVENT_SLAVE_BYTE_TRANSMITTED) == 0) {
                    break;
                }
            } else {
                // wait for master to end the receiving
                if (wait_for_event(I2C_EVENT_SLAVE_ACK_FAILURE) == 0) {
                    break;
                }
            }
        }
        I2C1->CTLR1 &= I2C1->CTLR1;
    }
#endif
}

/*********************************************************************
 * @fn      MCU_Sleep_Wakeup_Operate
 *
 * @brief   Perform sleep operation
 *
 * @return  none
 */
void MCU_Sleep_Wakeup_Operate( void )
{
    EXTI_ClearFlag( EXTI_Line1 | EXTI_Line3 | EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 | EXTI_Line9 );

    __WFE( );

    if( EXTI_GetFlagStatus( EXTI_Line1 | EXTI_Line3 | EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 | EXTI_Line9 ) != RESET  )
    {
        EXTI_ClearFlag( EXTI_Line1 | EXTI_Line3 | EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 | EXTI_Line9 );
        Resume(RESUME_INTERNAL);
    }
}



