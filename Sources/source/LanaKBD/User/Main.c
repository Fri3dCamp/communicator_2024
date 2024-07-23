/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Author             : Bert Outtier
* Version            : V1.0.0
* Date               : 2024/06/17
* Description        : Main program body.
*********************************************************************************
* Copyright (c) 2024 Fri3D Camp
*******************************************************************************/

/*
 * @Note
 * Lana Keyboard:
 * This codes runs on the Lana board mounted on the Fri3D Badge 2024 keyboard expansion.
 * It presents as a USB HID device over USB and it also uses USART2 (TX only)
 * to send the HID reports to the badge.
 *
 */

#include "debug.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_pwr.h"
#include "usb_prop.h"
#include "usbd_composite_km.h"

#define I2C_ADDRESS   0x38
#define I2C_SPEED     400000

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    Delay_Init();
    USART_Badge_Init(115200);

    /* initialize i2c */
    IIC_Init(I2C_SPEED, I2C_ADDRESS);

    /* makes sure that we can still flash using SWD */
    Delay_Ms(500);

    LED_Init_Neopixel();

    /* Initialize GPIO for keyboard scan */
    KB_Scan_Init();
    KB_Sleep_Wakeup_Cfg();

    /* Initialize timer for Keyboard and mouse scan timing */
    TIM3_Init(1, SystemCoreClock / 10000 - 1);

    /* initialize the USB HID endpoints */
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();

    while(1)
    {
        /* Handle keyboard scan data */
        KB_Scan_Handle();
    }
}







