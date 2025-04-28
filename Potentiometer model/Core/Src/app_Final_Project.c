/* Includes ------------------------------------------------------------------*/
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "stm32l4xx_hal.h"
#include "app.h"
/* External handles ----------------------------------------------------------*/
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;

/* Private define ------------------------------------------------------------*/
#define     ADC_MIN_FREQ       200     // Minimum tone frequency (Hz)
#define     ADC_MAX_FREQ       4000    // Maximum tone frequency (Hz)
#define     ADC_RESOLUTION     4095    // 12-bit ADC

#define     PWM_CHANNEL        TIM_CHANNEL_1      // TIM2_CH1 on PA15
#define     PWM_TIMER_FREQ     (HAL_RCC_GetPCLK1Freq() * 2)  // TIM2 runs at double APB1

#define     LED_PORT           GPIOA
#define     LED_PIN            GPIO_PIN_5         // Onboard LED (LD2)

/* Private function prototypes -----------------------------------------------*/
static uint32_t Map_ADC_To_Frequency(uint32_t adc_val);
static void Set_PWM_Frequency(uint32_t frequency);
static void LED_Blink_On_Threshold(uint32_t adc_val);

/* Private variables ---------------------------------------------------------*/

void App_Init(void)
{
    // Start PWM output on TIM2 CH1 (PA15)
    HAL_TIM_PWM_Start(&htim2, PWM_CHANNEL);

    // Initialize onboard LED pin (PA5)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
}

void App_MainLoop(void)
{
    uint32_t adc_val = 0;
    uint32_t tone_freq = 0;

    // Start ADC, poll, get value
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    adc_val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    // Map ADC value to tone frequency
    tone_freq = Map_ADC_To_Frequency(adc_val);
    Set_PWM_Frequency(tone_freq);

    // Blink LED if ADC value is high
    LED_Blink_On_Threshold(adc_val);

    HAL_Delay(10);  // Small delay to stabilize loop
}

static uint32_t Map_ADC_To_Frequency(uint32_t adc_val)
{
    return ADC_MIN_FREQ + ((adc_val * (ADC_MAX_FREQ - ADC_MIN_FREQ)) / ADC_RESOLUTION);
}

static void Set_PWM_Frequency(uint32_t frequency)
{
    if (frequency == 0) return;

    uint32_t timer_period = (PWM_TIMER_FREQ / frequency) - 1;

    __HAL_TIM_SET_AUTORELOAD(&htim2, timer_period);
    __HAL_TIM_SET_COMPARE(&htim2, PWM_CHANNEL, timer_period / 2);  // 50% duty
    HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE);
}

static void LED_Blink_On_Threshold(uint32_t adc_val)
{
    if (adc_val > 1000)
    {
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
        HAL_Delay(100);
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    }
}
