#include <ch32v20x.h>
#include <stdlib.h> /* atoi() */
#include <string.h> /* memset() */

#include <usb_lib.h>
#include <usb_pwr.h>

#include "keycodes.h"
/* we use our own custom debug lib
 * because the framework-wch-noneos-sdk does not allow UART4
 * as debug output
 */
#include "debug.h"

/* I2C on the expansion connector towards the badge */
#define SDA_PORT         GPIOB
#define SDA_PIN          GPIO_Pin_7
#define SCL_PORT         GPIOB
#define SCL_PIN          GPIO_Pin_6
#define I2C_ADDRESS      (0x38)
#define I2C_TIMEOUT      (-2)
#define I2C_TIMEOUT_TICK ((SystemCoreClock / 10) - 1) /* 100 ms */
#define I2C_SPEED        (400000)
#define UART_BAUDRATE    (115200)

/* digital outputs (button matrix) */
#define COL0_PORT GPIOB // PB4: col0
#define COL0_PIN  GPIO_Pin_4
#define COL1_PORT GPIOB // PB3: col1
#define COL1_PIN  GPIO_Pin_3
#define COL2_PORT GPIOA // PA0: col2
#define COL2_PIN  GPIO_Pin_0
#define COL3_PORT GPIOA // PA15: col3
#define COL3_PIN  GPIO_Pin_15
#define COL4_PORT GPIOA // PA1: col4
#define COL4_PIN  GPIO_Pin_1
#define COL5_PORT GPIOA // PA14: (SWC) col5
#define COL5_PIN  GPIO_Pin_14
#define COL6_PORT GPIOB // PB0: col6
#define COL6_PIN  GPIO_Pin_0
#define COL7_PORT GPIOA // PA13: (SWD) col7
#define COL7_PIN  GPIO_Pin_13
#define COL8_PORT GPIOB // PB1: col8
#define COL8_PIN  GPIO_Pin_1
#define N_COLS    (9)

#define ROW0_PORT GPIOD // PD1: row0
#define ROW0_PIN  GPIO_Pin_1
#define ROW1_PORT GPIOA // PA3: row1
#define ROW1_PIN  GPIO_Pin_3
#define ROW2_PORT GPIOA // PA5: row2
#define ROW2_PIN  GPIO_Pin_5
#define ROW3_PORT GPIOA // PA4: row3
#define ROW3_PIN  GPIO_Pin_4
#define ROW4_PORT GPIOA // PA6: row4
#define ROW4_PIN  GPIO_Pin_6
#define ROW5_PORT GPIOA // PA9: row5
#define ROW5_PIN  GPIO_Pin_9
#define ROW6_PORT GPIOA // PA7: row6
#define ROW6_PIN  GPIO_Pin_7
#define ROW7_PORT GPIOB // PB5: row7
#define ROW7_PIN  GPIO_Pin_5
#define N_ROWS    (8) // fits perfectly in uint8_t

#define TIMER_FREQ ((SystemCoreClock / 10000) - 1) /* the output frequency of all timers: 100Hz */

#define LED_PORT             GPIOD // PD0: Lana led
#define LED_PIN              GPIO_Pin_0
#define LED_LANA_OFFSET      (0)
#define LED_LANA_LEN         (3)
#define LED_BACKLIGHT_OFFSET (LED_LANA_OFFSET + LED_LANA_LEN)
#define LED_BACKLIGHT_LEN    (8)
#define LED_RED_OFFSET       (LED_BACKLIGHT_OFFSET + LED_BACKLIGHT_LEN)
#define LED_RED_LEN          (1)
#define N_LEDS               (LED_LANA_LEN + LED_BACKLIGHT_LEN + LED_RED_LEN) // 1 RGB led on LANA and 9 normal LEDs on the communicator

#define RESULT_BUFFER_SIZE      (3 + 8 + 1 + 2 + 3 + 1)
#define RESULT_KB_OFFSET        (3)
#define RESULT_CONFIG_OFFSET    (RESULT_KB_OFFSET + 8)
#define RESULT_BACKLIGHT_OFFSET (RESULT_CONFIG_OFFSET + 1)
#define RESULT_RGB_OFFSET       (RESULT_BACKLIGHT_OFFSET + 2)
#define RESULT_RED_OFFSET       (RESULT_RGB_OFFSET + 3)

#define HID_REPORT_KEYS (6)

typedef struct
{
    uint8_t r; // Red
    uint8_t g; // Green
    uint8_t b; // Blue
} ws2812b_color_t;

typedef struct
{
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[HID_REPORT_KEYS];
} key_report_t;

/*
 * This struct contains all data that is available through I2C.
 * Use the following command with a Buspirate to test:
 * read version number : [ 0x70 0x00 [ 0x71 r:3 ]
 * read key report : [ 0x70 0x03 [ 0x71 r:8 ]
 * read output : [ 0x70 0x0b [ 0x71 r:1 ]
 * read backlight : [ 0x70 0x0c [ 0x71 r:2 ]
 * turn off backlight : [ 0x70 0x0c 0x00 0x00 ]
 * turn on backlight : [ 0x70 0x0c 0x64 0x00 ]
 * read RGB led : [ 0x70 0x0e [ 0x71 r:3 ]
 * turn off RGB led : [ 0x70 0x0e 0x00:3 ]
 * turn on RGB led : [ 0x70 0x0e 0xFF:3 ]
 * turn on RGB led : [ 0x70 0x0e 0xFF 0x00 0x00 ]
 * turn on caps led : [ 0x70 0x11 0xFF ]
 * turn off caps led : [ 0x70 0x11 0x00 ]
 * read caps led: [ 0x70 0x11 [ 0x71 r ]
 */
typedef struct __attribute__((packed))
{
    uint8_t version[3];      // version number
    key_report_t key_report; // reference to the button state byte in the result buffer
    uint8_t enable_int : 1;  // configuration flag to enable interrupt output instead of UART output (TODO)
    uint8_t reboot : 1;      // configuration flag to trigger a reboot to bootloader
    uint8_t reserved : 6;    // reserved
    uint16_t backlight;      // backlight PWM value
    ws2812b_color_t rgb_led; // RGB led value
    uint8_t red_led;         // red LED value
} addon_data_t;

_Static_assert(sizeof(addon_data_t) == RESULT_BUFFER_SIZE, "raw data and struct size are not aligned!");

typedef struct
{
    uint8_t flag_matrix_scan_done : 1;    // flag to indicate that the button matrix state has changed
    uint8_t flag_int_should_clear : 1;    // flag to indicate that the interrupt should be cleared
    uint8_t flag_update_backlight : 1;    // flag to indicate that the backlight should be updated
    uint8_t flag_update_rgb : 1;          // flag to indicate that the LANA RGB LED should be updated
    uint8_t flag_update_red : 1;          // flag to indicate that the red LED should be updated
    uint8_t flag_update_leds : 1;         // flag to indicate that the LED state should be written to the WS2812 LEDs
    uint8_t flag_button_scan_halfway : 1; // flag to indicate that the matrix scan is halfway
    uint8_t flag_caps_lock : 1;           // flag to indicate that the caps lock has been activated
    uint8_t matrix_state[N_COLS];         // current matrix state
    uint8_t leds[N_LEDS];                 // current led state
    uint8_t raw_data_ptr;                 // current index in the raw_data buffer to read/write using I2C
    union
    {
        addon_data_t data;
        uint8_t raw_data[RESULT_BUFFER_SIZE];
    };
} addon_state_t;

/* Global Variables */
static addon_state_t state;
volatile uint8_t KB_LED_Cur_Status = 0x00;
volatile uint8_t kb_led_last_status = 0x00; // last status of the keyboard LEDs
/* for USB lib */
uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len);

/* initialize timer 3 to periodically generate an interrupt */
static void TIM3_Init(uint16_t arr, uint16_t psc)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    /* Enable Timer3 Clock */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    /* Initialize Timer3 */
    TIM_TimeBaseStructure.TIM_Period = arr;
    TIM_TimeBaseStructure.TIM_Prescaler = psc;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    /* enable timer interrupts */
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    /* configure timer interrupt */
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* Enable Timer3 */
    TIM_Cmd(TIM3, ENABLE);
}

/* set a matrix column active */
static void KB_Set_Col(uint8_t col)
{
    switch (col)
    {
        case 0:
            GPIO_WriteBit(COL8_PORT, COL8_PIN, Bit_SET);
            GPIO_WriteBit(COL0_PORT, COL0_PIN, Bit_RESET);
            break;
        case 1:
            GPIO_WriteBit(COL0_PORT, COL0_PIN, Bit_SET);
            GPIO_WriteBit(COL1_PORT, COL1_PIN, Bit_RESET);
            break;
        case 2:
            GPIO_WriteBit(COL1_PORT, COL1_PIN, Bit_SET);
            GPIO_WriteBit(COL2_PORT, COL2_PIN, Bit_RESET);
            break;
        case 3:
            GPIO_WriteBit(COL2_PORT, COL2_PIN, Bit_SET);
            GPIO_WriteBit(COL3_PORT, COL3_PIN, Bit_RESET);
            break;
        case 4:
            GPIO_WriteBit(COL3_PORT, COL3_PIN, Bit_SET);
            GPIO_WriteBit(COL4_PORT, COL4_PIN, Bit_RESET);
            break;
        case 5:
            GPIO_WriteBit(COL4_PORT, COL4_PIN, Bit_SET);
            GPIO_WriteBit(COL5_PORT, COL5_PIN, Bit_RESET);
            break;
        case 6:
            GPIO_WriteBit(COL5_PORT, COL5_PIN, Bit_SET);
            GPIO_WriteBit(COL6_PORT, COL6_PIN, Bit_RESET);
            break;
        case 7:
            GPIO_WriteBit(COL6_PORT, COL6_PIN, Bit_SET);
            GPIO_WriteBit(COL7_PORT, COL7_PIN, Bit_RESET);
            break;
        case 8:
            GPIO_WriteBit(COL7_PORT, COL7_PIN, Bit_SET);
            GPIO_WriteBit(COL8_PORT, COL8_PIN, Bit_RESET);
            break;
        default:
            /* disable all */
            GPIO_WriteBit(GPIOA, COL2_PIN | COL3_PIN | COL4_PIN | COL5_PIN | COL7_PIN, Bit_SET);
            GPIO_WriteBit(GPIOB, COL0_PIN | COL1_PIN | COL6_PIN | COL8_PIN, Bit_SET);
    }
}

/* Initialize the I2C perfipheral and activate interrupts */
static void IIC_Init(uint32_t bound, uint16_t address)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    I2C_InitTypeDef I2C_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStruct = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* configure the SDA and SCL pins */
    GPIO_InitStructure.GPIO_Pin = SDA_PIN | SCL_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* configure I2C1 */
    I2C_InitStructure.I2C_ClockSpeed = bound;                                 // bus speed
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;                                // there is only 1 mode
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_16_9;                     // I2C fast mode Tlow/Thigh = 16/9
    I2C_InitStructure.I2C_OwnAddress1 = address << 1;                         // 7 or 10 bit address
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;                               // automatic acknowledge
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit; // use 7 bit address
    I2C_Init(I2C1, &I2C_InitStructure);

    /* configure I2C interrupts */
    NVIC_InitStruct.NVIC_IRQChannel = I2C1_EV_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    NVIC_InitStruct.NVIC_IRQChannel = I2C1_ER_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    /* enable I2C interrupts */
    I2C_ITConfig(I2C1, I2C_IT_EVT | I2C_IT_ERR | I2C_IT_BUF, ENABLE); // TODO: also I2C_IT_BUF?

    /* enable clock stretching */
    I2C_StretchClockCmd(I2C1, ENABLE);

    /* enable I2C1 */
    I2C_Cmd(I2C1, ENABLE);
}

/* initialize the WS2812 LED pin */
static void LED_Init_Neopixel(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    /* remap OSC_IN/OSC_OUT as PD0/PD1, which is used as LEDDATA/row0 */
    GPIO_PinRemapConfig(AFIO_PCFR1_PD01_REMAP, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Pin = LED_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);
}

/* send a buffer of LED data to the WS2812 LEDs */
static void WS2812BSimpleSend(GPIO_TypeDef *port, uint16_t GPIO_Pin, uint8_t *data, int len_in_bytes)
{
    port->BCR = GPIO_Pin;

    uint8_t *end = data + len_in_bytes;
    while (data != end)
    {
        uint8_t byte = *data;

        int i;
        for (i = 0; i < 8; i++)
        {
            if (byte & 0x80)
            {
                // WS2812B's need AT LEAST 625ns for a logical "1"
                port->BSHR = GPIO_Pin;
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                port->BCR = GPIO_Pin;
            }
            else
            {
                // WS2812B's need BETWEEN 62.5 to about 500 ns for a logical "0"
                port->BSHR = GPIO_Pin;
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                port->BCR = GPIO_Pin;
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
                __asm__("nop");
            }
            byte <<= 1;
        }
        data++;
    }

    port->BCR = GPIO_Pin;
}

/* set the color of the LANA TNY RGB LED */
static void LED_Lana_SetColour(ws2812b_color_t color)
{
    state.leds[LED_LANA_OFFSET + 0] = color.g;
    state.leds[LED_LANA_OFFSET + 1] = color.r;
    state.leds[LED_LANA_OFFSET + 2] = color.b;
}

/* set the brightness of the communicator keyboard backlight LEDs */
static void LED_Backlight_SetBrightness(uint16_t brightness)
{
    // scale from 0-100 to 0-255
    brightness = brightness > 100 ? 100 : brightness;
    uint8_t v = (uint8_t)((brightness * 255) / 100);
    // set all 8 backligt leds to the same value
    memset(&state.leds[LED_BACKLIGHT_OFFSET], v, LED_BACKLIGHT_LEN);
}

/* initialize all GPIOs related to the keyboard */
static void KB_Scan_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* Enable GPIOA B and D clock */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOD, ENABLE);

    /* remap SWD pins, which are used as col5 and col7 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    /* Initialize GPIOD (row0) as input */
    GPIO_InitStructure.GPIO_Pin = ROW0_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ROW0_PORT, &GPIO_InitStructure);

    /* Initialize GPIOA (row1-row6) as input */
    GPIO_InitStructure.GPIO_Pin = ROW1_PIN | ROW2_PIN | ROW3_PIN | ROW4_PIN | ROW5_PIN | ROW6_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* Initialize GPIOB (row7) as input */
    GPIO_InitStructure.GPIO_Pin = ROW7_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ROW7_PORT, &GPIO_InitStructure);

    /* Initialize GPIOA (col2-col5, col7) as outputs */
    GPIO_InitStructure.GPIO_Pin = COL2_PIN | COL3_PIN | COL4_PIN | COL5_PIN | COL7_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* Initialize GPIOB (col0, col1, col6, col8) as outputs */
    GPIO_InitStructure.GPIO_Pin = COL0_PIN | COL1_PIN | COL6_PIN | COL8_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* put all outputs high*/
    KB_Set_Col(99);

    /* put col0 to low */
    KB_Set_Col(0);
}

/* Configure keyboard wake up mode. */
static void KB_Sleep_Wakeup_Cfg(void)
{
    EXTI_InitTypeDef EXTI_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOD, GPIO_PinSource1);
    EXTI_InitStructure.EXTI_Line = EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising; // TODO: falling?
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource3);
    EXTI_InitStructure.EXTI_Line = EXTI_Line3;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource4);
    EXTI_InitStructure.EXTI_Line = EXTI_Line4;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource5);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource5);
    EXTI_InitStructure.EXTI_Line = EXTI_Line5;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource6);
    EXTI_InitStructure.EXTI_Line = EXTI_Line6;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource7);
    EXTI_InitStructure.EXTI_Line = EXTI_Line7;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource9);
    EXTI_InitStructure.EXTI_Line = EXTI_Line9;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    EXTI->INTENR |= EXTI_INTENR_MR1 | EXTI_INTENR_MR3 | EXTI_INTENR_MR4 | EXTI_INTENR_MR5 | EXTI_INTENR_MR6 | EXTI_INTENR_MR7 | EXTI_INTENR_MR9;
}

/* get the activated rows when a certain column is active */
static uint8_t io_to_scan_result(uint16_t a, uint16_t b, uint16_t d)
{
    uint8_t out = 0;

    if (!(b & (ROW7_PIN))) // PB5: row7
    {
        out |= 1;
    }
    out <<= 1;

    if (!(a & (ROW6_PIN))) // PA7: row6
    {
        out |= 1;
    }
    out <<= 1;

    if (!(a & (ROW5_PIN))) // PA9: row5
    {
        out |= 1;
    }
    out <<= 1;

    if (!(a & (ROW4_PIN))) // PA6: row4
    {
        out |= 1;
    }
    out <<= 1;

    if (!(a & (ROW3_PIN))) // PA4: row3
    {
        out |= 1;
    }
    out <<= 1;

    if (!(a & (ROW2_PIN))) // PA5: row2
    {
        out |= 1;
    }
    out <<= 1;

    if (!(a & (ROW1_PIN))) // PA3: row1
    {
        out |= 1;
    }
    out <<= 1;

    if (!(d & (ROW0_PIN))) // PD1: row0
    {
        out |= 1;
    }
    return out;
}

/* Perform the keyboard scan. */
static void KB_Scan(void)
{
    static uint8_t scan_cnt = 0;
    static uint8_t scan_col = 0;
    static uint8_t scan_result[N_COLS] = {0x00};
    static uint8_t scan = 0;

    scan_cnt++;
    if ((scan_cnt % 10) == 0) // every 100 ms
    {
        scan_cnt = 0;

        /* Determine whether the two scan results for this column are consistent (debouncing) */
        if (scan == io_to_scan_result(GPIO_ReadInputData(GPIOA), GPIO_ReadInputData(GPIOB), GPIO_ReadInputData(GPIOD)))
        {
            scan_result[scan_col] = scan;
        }

        /* activate the next column */
        scan_col = (scan_col + 1) % N_COLS;
        KB_Set_Col(scan_col);

        /* copy the full scan result to the global state */
        if (scan_col == 0)
        {
            memcpy(state.matrix_state, scan_result, N_COLS);
            state.flag_matrix_scan_done = 1; // indicate that a full scan was finished
            memset(scan_result, 0, N_COLS);
        }
    }
    else if ((scan_cnt % 5) == 0) // every 50 ms
    {
        /* Save the first scan result */
        scan = io_to_scan_result(GPIO_ReadInputData(GPIOA), GPIO_ReadInputData(GPIOB), GPIO_ReadInputData(GPIOD));
        state.flag_int_should_clear = 1;
    }
}

/* get the keycode of a keyboard key at row and column */
static uint8_t get_keycode(uint8_t col, uint8_t row, uint8_t *keycode)
{
    *keycode = modifiers[col][row];

    if (*keycode != KEY_NONE)
    {
        return 1;
    }

    *keycode = keycodes[col][row];
    if (*keycode != KEY_NONE)
    {
        return 0;
    }

    PRINT("ERROR: no keycode for row %d col %d\r\n", row, col);
    return 2; // error
}

/* update the key report with a new key press/release */
static void determine_hid_report(uint8_t keycode, uint8_t is_modifier, uint8_t pressed, key_report_t *out)
{
    uint8_t j;
    static uint8_t key_cnt = 0;

    if (is_modifier)
    {
        if (pressed)
        {
            out->modifiers |= keycode;
        }
        else
        {
            out->modifiers &= (~keycode);
        }
        return;
    }

    if (pressed)
    {
        if (key_cnt < HID_REPORT_KEYS)
        {
            out->keys[key_cnt++] = keycode;
        }
        else
        {
            PRINT("too many keys at the same time to report\r\n");
        }
    }
    else
    {
        for (j = 0; j < HID_REPORT_KEYS; j++)
        {
            if (out->keys[j] == keycode)
            {
                /* key found in the report */
                break;
            }
        }
        /* remove the key from the report */
        if (j == HID_REPORT_KEYS)
        {
            PRINT("released key not found in the report, removing last one\r\n");
        }
        else
        {
            memcpy(&out->keys[j], &out->keys[j + 1], (HID_REPORT_KEYS - j - 1));
        }
        /* clear the last key report entry */
        out->keys[5] = 0;
        if (key_cnt > 0)
        {
            key_cnt--;
        }
    }
}

/* toggle the backlight on or off */
static void toggle_backlight(void)
{
    state.data.backlight -= state.data.backlight % 20;
    state.data.backlight = (state.data.backlight + 20) % 120;
}

static void handle_fn(key_report_t *in, key_report_t *out)
{
    uint8_t j;

    /* caps lock is enabled by pressing the FN key and right shift at the same time */
    if ((in->modifiers & (KEY_MOD_RMETA | KEY_MOD_RSHIFT)) == (KEY_MOD_RMETA | KEY_MOD_RSHIFT))
    {
        determine_hid_report(KEY_CAPSLOCK, 0, 1, in);
        state.flag_caps_lock = 1;
    }
    else
    {
        if (state.flag_caps_lock)
        {
            determine_hid_report(KEY_CAPSLOCK, 0, 0, in);
            state.flag_caps_lock = 0;
        }
    }

    /* take a copy of the input report */
    memcpy(out, in, sizeof(key_report_t));

    /*
      if the FN key is pressed, modify the output,
      and trigger special functions
    */
    if (out->modifiers & KEY_MOD_RMETA)
    {
        /* replace keys and/or trigger special functions */
        for (j = 0; j < HID_REPORT_KEYS; j++)
        {
            switch (out->keys[j])
            {
                case KEY_LEFTMETA:
                    // turn of the RGB led
                    state.data.rgb_led.r = 0;
                    state.data.rgb_led.g = 0;
                    state.data.rgb_led.b = 0;
                    state.flag_update_rgb = 1;
                    // remove key from the output
                    memcpy(&out->keys[j], &out->keys[j + 1], (HID_REPORT_KEYS - j - 1));
                    out->keys[5] = 0;
                    break;
                case KEY_F1:
                    // SET RGB led to color
                    state.data.rgb_led.r = 0xff;
                    state.data.rgb_led.g = 0x00;
                    state.data.rgb_led.b = 0x00;
                    state.flag_update_rgb = 1;
                    // remove key from the output
                    memcpy(&out->keys[j], &out->keys[j + 1], (HID_REPORT_KEYS - j - 1));
                    out->keys[5] = 0;
                    break;
                case KEY_F2:
                    state.data.rgb_led.r = 0xff;
                    state.data.rgb_led.g = 44;
                    state.data.rgb_led.b = 0x00;
                    state.flag_update_rgb = 1;
                    memcpy(&out->keys[j], &out->keys[j + 1], (HID_REPORT_KEYS - j - 1));
                    out->keys[5] = 0;
                    break;
                case KEY_F3:
                    state.data.rgb_led.r = 0xff;
                    state.data.rgb_led.g = 0xff;
                    state.data.rgb_led.b = 0x00;
                    state.flag_update_rgb = 1;
                    memcpy(&out->keys[j], &out->keys[j + 1], (HID_REPORT_KEYS - j - 1));
                    out->keys[5] = 0;
                    break;
                case KEY_F4:
                    state.data.rgb_led.r = 0x00;
                    state.data.rgb_led.g = 0xff;
                    state.data.rgb_led.b = 0x00;
                    state.flag_update_rgb = 1;
                    memcpy(&out->keys[j], &out->keys[j + 1], (HID_REPORT_KEYS - j - 1));
                    out->keys[5] = 0;
                    break;
                case KEY_F5:
                    state.data.rgb_led.r = 0x00;
                    state.data.rgb_led.g = 0x00;
                    state.data.rgb_led.b = 0xff;
                    state.flag_update_rgb = 1;
                    memcpy(&out->keys[j], &out->keys[j + 1], (HID_REPORT_KEYS - j - 1));
                    out->keys[5] = 0;
                    break;
                case KEY_F6:
                    state.data.rgb_led.r = 0x80;
                    state.data.rgb_led.g = 0x00;
                    state.data.rgb_led.b = 0x80;
                    state.flag_update_rgb = 1;
                    memcpy(&out->keys[j], &out->keys[j + 1], (HID_REPORT_KEYS - j - 1));
                    out->keys[5] = 0;
                    break;
                case KEY_SPACE:
                    toggle_backlight();
                    state.flag_update_backlight = 1;
                    memcpy(&out->keys[j], &out->keys[j + 1], (HID_REPORT_KEYS - j - 1));
                    out->keys[5] = 0;
                    break;
                case KEY_BACKSPACE:
                    out->keys[j] = KEY_DELETE;
                    break;
                case KEY_LEFT:
                    out->keys[j] = KEY_HOME;
                    break;
                case KEY_RIGHT:
                    out->keys[j] = KEY_END;
                    break;
                case KEY_UP:
                    out->keys[j] = KEY_PAGEUP;
                    break;
                case KEY_DOWN:
                    out->keys[j] = KEY_PAGEDOWN;
                    break;
                default:
                    break;
            }
        }

        /* clear the FN key in the reporting */
        out->modifiers &= (~KEY_MOD_RMETA);
    }
}

/* clear the various error flags that may block further communication */
static void I2C1_ClearErrorFlags(void)
{
    /* I2C_FLAG_AF - Acknowledge failure flag */
    if (I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) != RESET)
    {
        PRINT("clear I2C_FLAG_AF flag\r\n");
        I2C_ClearFlag(I2C1, I2C_FLAG_AF);
    }
    /* I2C_FLAG_BERR -Bus Error flag.*/
    if (I2C_GetFlagStatus(I2C1, I2C_FLAG_BERR) != RESET)
    {
        PRINT("clear I2C_FLAG_BERR flag\r\n");
        I2C_ClearFlag(I2C1, I2C_FLAG_BERR);
    }
}

/* clear the stop flag */
static void I2C1_ClearStopFlag(void)
{
    if (I2C_GetFlagStatus(I2C1, I2C_FLAG_STOPF) != RESET)
    {
        /* Stop detection flag (Slave mode).
         * STOPF (STOP detection) is cleared by software sequence: a read operation
         * to I2C_STAR1 register (I2C_GetFlagStatus()) followed by a write operation
         * to I2C_CTLR1 register (I2C_Cmd() to re-enable the I2C peripheral).
         * -> Since we just read the flag, we only need to (re-)enable.
         * */
        I2C_Cmd(I2C1, ENABLE);
    }
}

/**
 * @brief  Read bytes from master using a timeout
 * @param  data: pointer to data to be read
 * @param  size: number of bytes to be write.
 * @retval status
 */
static int i2c_slave_read(uint8_t *data, uint16_t size)
{
    uint8_t i = 0;
    uint32_t tickstart = SysTick->CNT;

    while (i < size && I2C_GetFlagStatus(I2C1, I2C_FLAG_RXNE) != RESET)
    {
        data[i++] = I2C_ReceiveData(I2C1);
        if ((SysTick->CNT - tickstart) >= I2C_TIMEOUT_TICK)
        {
            break;
        }
    }
    return i;
}

/* function to process I2C slave data transfers */
/* reference: arduino implementation */
static void i2c_slave_process(void)
{
    /* Process incoming and outgoing I2C data.
     * When processing the data we can assume there is an address match.
     * We could wait for an address match, but that would be blocking
     * and isn't needed as RX/TX-flags are only set when addressed properly.
     */

    /* Process receiving data */
    if (I2C_GetFlagStatus(I2C1, I2C_FLAG_RXNE) != RESET)
    {
        /* Data register not empty (Receiver) flag
         * read all available data and store it
         */
        state.raw_data_ptr = I2C_ReceiveData(I2C1);
        switch (state.raw_data_ptr)
        {
            case RESULT_CONFIG_OFFSET: {
                uint8_t new_value;
                int ret = i2c_slave_read((uint8_t *)(&new_value), 1);
                state.raw_data_ptr += ret;
                if (ret == 1)
                {
                    state.raw_data[RESULT_CONFIG_OFFSET] = new_value;
                }
                break;
            }
            case RESULT_BACKLIGHT_OFFSET: {
                uint16_t new_value;
                int ret = i2c_slave_read((uint8_t *)(&new_value), 2);
                state.raw_data_ptr += ret;
                if (ret == 2)
                {
                    state.data.backlight = new_value;
                    state.flag_update_backlight = 1;
                }
                break;
            }
            case RESULT_RGB_OFFSET: {
                uint8_t new_value[3];
                memset(new_value, 0, 3);
                int ret = i2c_slave_read(new_value, 3);
                state.raw_data_ptr += ret;
                if (ret == 3)
                {
                    memcpy(&state.raw_data[RESULT_RGB_OFFSET], new_value, 3);
                    state.flag_update_rgb = 1; // set the flag to update the outputs
                }
                break;
            }
            case RESULT_RED_OFFSET: {
                uint8_t new_value;
                int ret = i2c_slave_read((uint8_t *)(&new_value), 1);
                state.raw_data_ptr += ret;
                if (ret == 1)
                {
                    state.raw_data[RESULT_RED_OFFSET] = new_value;
                    state.flag_update_red = 1;
                }
                else
                    break;
            }
            default:
                while (I2C_GetFlagStatus(I2C1, I2C_FLAG_RXNE) != RESET)
                {
#if (DEBUG)
                    PRINT("received %x\r\n", I2C_ReceiveData(I2C1));
#else
                    I2C_ReceiveData(I2C1);
#endif
                }
                PRINT("we do not allow writing to offset 0x%02x\r\n", state.raw_data_ptr);
        }
    }

    /* Process end of receiving data, as determined by stop flag */
    if (I2C_CheckEvent(I2C1, I2C_EVENT_SLAVE_STOP_DETECTED))
    {
        PRINT("all data received\r\n");
        /* clear the stop flag to be ready for another session */
        I2C1_ClearStopFlag();
    }

    /* Process transmitting data */
    if (I2C_GetFlagStatus(I2C1, I2C_FLAG_TXE) != RESET)
    {
        /* Data register empty flag (Transmitter).
         * It seems we need to send something
         */
        if (state.raw_data_ptr < RESULT_BUFFER_SIZE)
        {
            PRINT("sending\r\n");
            I2C_SendData(I2C1, state.raw_data[state.raw_data_ptr++]); // send register value to master
        }
        else
        {
            PRINT("ERROR: reading dummy data\r\n");
            I2C_SendData(I2C1, 0x00); // send dummy data to master
        }
    }

    // just for debugging
    if (I2C_CheckEvent(I2C1, I2C_EVENT_SLAVE_BYTE_TRANSMITTED))
    {
        PRINT("Master acked received byte (I2C_EVENT_SLAVE_BYTE_TRANSMITTED)\r\n");
    }

    if (I2C_CheckEvent(I2C1, I2C_EVENT_SLAVE_ACK_FAILURE))
    {
        PRINT("Master stopped receiving (I2C_EVENT_SLAVE_ACK_FAILURE)\r\n");
    }

    /* Clear error flags (since we don't handle them anyways) */
    I2C1_ClearErrorFlags();
}

/* 2 breath pulses of the backlight */
static void boot_animation(void)
{
    for (uint16_t i = 0; i < 100; i++)
    {
        LED_Backlight_SetBrightness(i);
        WS2812BSimpleSend(LED_PORT, LED_PIN, state.leds, N_LEDS);
    }
    for (uint16_t i = 100; i > 0; i--)
    {
        LED_Backlight_SetBrightness(i);
        WS2812BSimpleSend(LED_PORT, LED_PIN, state.leds, N_LEDS);
    }
    for (uint16_t i = 0; i < 100; i++)
    {
        LED_Backlight_SetBrightness(i);
        WS2812BSimpleSend(LED_PORT, LED_PIN, state.leds, N_LEDS);
    }
    for (uint16_t i = 100; i > 0; i--)
    {
        LED_Backlight_SetBrightness(i);
        WS2812BSimpleSend(LED_PORT, LED_PIN, state.leds, N_LEDS);
    }
}

static void USART_Output_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure;
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

/* Main program */
int main(void)
{
    uint8_t status;
    uint8_t previous_matrix_state[N_COLS];
    key_report_t key_report;

    memset(previous_matrix_state, 0, N_COLS);
    memset(&key_report, 0, sizeof(key_report_t));

    /* set all data and flags to 0 */
    memset(&state, 0, sizeof(addon_state_t));

    /* set the version number from git */
    char version_major[] = VERSION_MAJOR;
    char version_minor[] = VERSION_MINOR;
    char version_patch[] = VERSION_PATCH;
    state.data.version[0] = atoi(version_major) & 0xff;
    state.data.version[1] = atoi(version_minor) & 0xff;
    state.data.version[2] = atoi(version_patch) & 0xff;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    /* always configure the UART2 as TX */
    USART_Output_Init(UART_BAUDRATE);

    /* initialize i2c */
    IIC_Init(I2C_SPEED, I2C_ADDRESS);

    /* makes sure that we can still flash using SWD */
    Delay_Ms(1000);

    PRINT("SystemClk: %u\r\n", (unsigned)SystemCoreClock);
    PRINT("ChipID: %08x\r\n", (unsigned)DBGMCU_GetCHIPID());

    LED_Init_Neopixel();

    /* Initialize GPIO for keyboard scan */
    KB_Scan_Init(); // disables SWD and remap PD01 as GPIO
    KB_Sleep_Wakeup_Cfg();

    /* Initialize timer for Keyboard and mouse scan timing */
    TIM3_Init(1, TIMER_FREQ); // every 10 ms

    /* Initialize USBFS interface to communicate with the host  */
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();

    /* 2 breath pulses of the backlight */
    boot_animation();

    while (1)
    {
        if (KB_LED_Cur_Status != kb_led_last_status)
        {
            if ((KB_LED_Cur_Status & 0x01) != (kb_led_last_status & 0x01))
            {
                if (KB_LED_Cur_Status & 0x01)
                {
                    state.data.rgb_led.g = 255;
                    state.flag_update_rgb = 1;
                }
                else
                {
                    state.data.rgb_led.g = 0;
                    state.flag_update_rgb = 1;
                }
            }
            if ((KB_LED_Cur_Status & 0x02) != (kb_led_last_status & 0x02))
            {
                if (KB_LED_Cur_Status & 0x02)
                {
                    state.data.red_led = 0xff;
                    state.flag_update_red = 1;
                }
                else
                {
                    state.data.red_led = 0x00;
                    state.flag_update_red = 1;
                }
            }
            if ((KB_LED_Cur_Status & 0x04) != (kb_led_last_status & 0x04))
            {
                if (KB_LED_Cur_Status & 0x04)
                {
                    state.data.rgb_led.b = 255;
                    state.flag_update_rgb = 1;
                }
                else
                {
                    state.data.rgb_led.b = 255;
                    state.flag_update_rgb = 1;
                }
            }
            kb_led_last_status = KB_LED_Cur_Status;
        }

        if (state.flag_update_backlight)
        {
            state.flag_update_backlight = 0;
            LED_Backlight_SetBrightness(state.data.backlight);
            state.flag_update_leds = 1;
        }

        if (state.flag_update_rgb)
        {
            state.flag_update_rgb = 0;
            LED_Lana_SetColour(state.data.rgb_led);
            state.flag_update_leds = 1;
        }

        // a new red led value was written through I2C or USB
        if (state.flag_update_red)
        {
            state.flag_update_red = 0;
            state.leds[LED_RED_OFFSET] = state.data.red_led;
            state.flag_update_leds = 1;
        }

        if (state.flag_button_scan_halfway && state.data.enable_int)
        {
            state.flag_button_scan_halfway = 0;
            // TODO: set interrupt pin low
        }

        if (state.flag_matrix_scan_done)
        {
            state.flag_matrix_scan_done = 0;

            if (memcmp(state.matrix_state, previous_matrix_state, N_COLS) != 0)
            {
                // matrix state has changed
                for (int c = 0; c < N_COLS; c++)
                {
                    if (state.matrix_state[c] != previous_matrix_state[c])
                    {
                        for (int r = 0; r < N_ROWS; r++)
                        {
                            uint8_t current_button_state = (state.matrix_state[c] & (1 << r)) & 0xff;
                            uint8_t previous_button_state = (previous_matrix_state[c] & (1 << r)) & 0xff;

                            if (current_button_state != previous_button_state)
                            {
                                uint8_t keycode;
                                uint8_t is_modifier = get_keycode(c, r, &keycode);
                                if (is_modifier != 2)
                                {
                                    determine_hid_report(keycode, is_modifier, current_button_state, &key_report);
                                }
                            }
                        }
                    }
                }

                /* Copy the keyboard data to the buffer of endpoint 1 and set the data uploading flag */
                memcpy(previous_matrix_state, state.matrix_state, N_COLS);

                /* handle special FN triggers */
                handle_fn(&key_report, &state.data.key_report);

                /* the key report is ready to be fetched through I2C, so let's set the interrupt */
                if (state.data.enable_int)
                {
                    // TODO: set the UART TX to high
                    PRINT("TODO: set the output pin\r\n");
                }
                else
                {
#if (DEBUG)
                    PRINT("Reporting HID through UART:\r\n");
                    for (int i = 0; i < sizeof(key_report_t); i++)
                    {
                        PRINT("0x%02x ", state.raw_data[RESULT_KB_OFFSET + i]);
                    }
                    PRINT("\r\n");
#else
                    for (int i = 0; i < sizeof(key_report_t); i++)
                    {
                        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
                            ;
                        USART_SendData(USART2, state.raw_data[RESULT_KB_OFFSET + i]);
                    }
#endif
                }

                if (bDeviceState == CONFIGURED)
                {
                    status = USBD_ENDPx_DataUp(ENDP1, &state.raw_data[RESULT_KB_OFFSET], sizeof(key_report_t));
                    if (status != USB_SUCCESS)
                    {
                        PRINT("ERROR reporting HID through USB\r\n");
                    }
                }
            }
        }

        /* wwrite the new led state to the leds */
        if (state.flag_update_leds)
        {
            PRINT("Writing new state to leds\r\n");
            state.flag_update_leds = 0;
            WS2812BSimpleSend(LED_PORT, LED_PIN, state.leds, N_LEDS);
        }
    }
}

/*********************************************************************
 * @fn      USB_Sleep_Wakeup_CFG
 *
 * @brief   Configure USB wake up mode
 *
 * @return  none
 */
void USB_Sleep_Wakeup_CFG(void)
{
    EXTI_InitTypeDef EXTI_InitStructure = {0};

    EXTI_InitStructure.EXTI_Line = EXTI_Line20;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
}

/*********************************************************************
 * @fn      MCU_Sleep_Wakeup_Operate
 *
 * @brief   Perform sleep operation
 *
 * @return  none
 */
void MCU_Sleep_Wakeup_Operate(void)
{
    PRINT("Sleep\r\n");
    __disable_irq();
    // keyboard interrupt lines
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    EXTI_ClearFlag(EXTI_Line1 | EXTI_Line3 | EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 | EXTI_Line9);

    PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFE);

    SystemInit();
    SystemCoreClockUpdate();
    Set_USBConfig();

    if (EXTI_GetFlagStatus(EXTI_Line1 | EXTI_Line3 | EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 | EXTI_Line9) != RESET)
    {
        EXTI_ClearFlag(EXTI_Line1 | EXTI_Line3 | EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 | EXTI_Line9);
        Resume(RESUME_INTERNAL);
    }
    else if (EXTI_GetFlagStatus(EXTI_Line18) != RESET)
    {
        EXTI_ClearFlag(EXTI_Line18);
        PRINT("USB Wake Up\n");
    }
    __enable_irq();
    PRINT("Wake\r\n");
}

/* interrupt handlers */
void I2C1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void I2C1_IRQHandler(void)
{
    PRINT("I2C1_IRQHandler\r\n");
}

// Interrupt Service Routine for I2C1 Event
void I2C1_EV_IRQHandler(void) __attribute__((interrupt));
void I2C1_EV_IRQHandler(void)
{
    i2c_slave_process();
}

// Interrupt Service Routine for I2C1 Error
void I2C1_ER_IRQHandler(void) __attribute__((interrupt));
void I2C1_ER_IRQHandler(void)
{
}

/*********************************************************************
 * @fn      TIM3_IRQHandler
 *
 * @brief   This function handles TIM3 global interrupt request.
 *
 * @return  none
 */
void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        /* Clear interrupt flag */
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);

        /* Handle keyboard scan */
        KB_Scan();
    }
}
