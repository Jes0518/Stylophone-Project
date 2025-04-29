#include "main.h"
#include "app.h"
#include <stdbool.h>
#include <string.h>
#include "stm32l4xx_hal.h"

//------------------------------------------------------------------------------
// extern handles (from main.c)
extern TIM_HandleTypeDef htim2;   // PWM → speaker on PB3
extern TIM_HandleTypeDef htim3;   // LED blink on PA5
extern ADC_HandleTypeDef hadc1;   // stylus ADC on PA0
extern UART_HandleTypeDef huart2; // UART2 for PC <-> MCU comms

//------------------------------------------------------------------------------
// UART-controlled LED
#define LED_PORT    GPIOA
#define LED_PIN     GPIO_PIN_5

#define LED_MODE_OFF       0
#define LED_MODE_ON        1
#define LED_MODE_FLASHING  2

volatile char rxData;        // incoming byte via UART
volatile int  ledMode = LED_MODE_FLASHING;

//------------------------------------------------------------------------------
// Synth modes + constants
volatile bool playPreset = false;  // toggled by B1 or UART “T”  ← now also set by T
volatile bool pwmEnabled = true;   // toggled by B2

#define R27               22500.0f
#define RB_MAX           250000.0f   // was 117500.0f; now 25×10 kΩ pots
#define CT                0.1e-6f
#define OVERSAMPLE_COUNT      4

// forward prototypes
void ShowCommands(void);
void UART_TransmitString(UART_HandleTypeDef*, char*, int);
void PWM_SetDutyCycle(float);
void PlayPresetTune(void);

//------------------------------------------------------------------------------
// App_Init: banner, UART Rx, TIM2 synth, TIM3 LED
void App_Init(void)
{
    // LED cold-start on
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);

    // UART banner + help
    UART_TransmitString(&huart2,"-----------------",1);
    UART_TransmitString(&huart2,"~ Nucleo-L476RG ~",1);
    UART_TransmitString(&huart2,"-----------------",1);
    ShowCommands();

    // UART Rx interrupt
    HAL_UART_Receive_IT(&huart2,(uint8_t*)&rxData,1);

    // TIM2 → synth PWM + interrupts
    HAL_TIM_Base_Start_IT(&htim2);
    HAL_TIM_PWM_Start  (&htim2, TIM_CHANNEL_2);
    HAL_TIM_OC_Start_IT(&htim2, TIM_CHANNEL_2);
    PWM_SetDutyCycle(50.0f);

    // TIM3 → LED blink
    HAL_TIM_Base_Start_IT(&htim3);
}

//------------------------------------------------------------------------------
// main loop
void App_MainLoop(void)
{
    // 1) PWM off?
    if (!pwmEnabled) {
        HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_2);
        return;
    }
    // 2) preset melody?
    if (playPreset) {
        PlayPresetTune();
        return;
    }
    // 3) stylus‐controlled 555
    uint32_t sum=0;
    for(int i=0;i<OVERSAMPLE_COUNT;i++){
        HAL_ADC_Start(&hadc1);
        if(HAL_ADC_PollForConversion(&hadc1,10)==HAL_OK)
            sum+=HAL_ADC_GetValue(&hadc1);
    }
    uint32_t raw = (sum+(OVERSAMPLE_COUNT/2))/OVERSAMPLE_COUNT;
    float V    = raw * (3.3f/4095.0f);
    float RB   = (V/3.3f)*RB_MAX;
    if(RB<100.0f)   RB=100.0f;
    if(RB>RB_MAX)   RB=RB_MAX;
    float f = 1.44f/((R27+2.0f*RB)*CT);

    uint32_t clk = SystemCoreClock/(htim2.Init.Prescaler+1);
    uint32_t arr = (uint32_t)(clk/f)-1;
    if(arr<10)    arr=10;
    if(arr>0xFFFF)arr=0xFFFF;

    __HAL_TIM_SET_AUTORELOAD (&htim2, arr);
    __HAL_TIM_SET_COMPARE    (&htim2, TIM_CHANNEL_2, arr/2);
}

//------------------------------------------------------------------------------
// UART Rx → LED control & new T command
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    switch(rxData){
    case 'I': case 'i':
        HAL_GPIO_WritePin(LED_PORT,LED_PIN,GPIO_PIN_SET);
        ledMode = LED_MODE_ON;
        break;
    case 'O': case 'o':
        HAL_GPIO_WritePin(LED_PORT,LED_PIN,GPIO_PIN_RESET);
        ledMode = LED_MODE_OFF;
        break;
    case 'F': case 'f':
        ledMode = LED_MODE_FLASHING;
        break;
    case 'H': case 'h':
        ShowCommands();
        break;
    case 'T': case 't':           // ← added
        playPreset = true;        // ← start preset tune
        break;                     // ← added
    default: break;
    }
    HAL_UART_Receive_IT(&huart2,(uint8_t*)&rxData,1);
}

//------------------------------------------------------------------------------
// EXTI callback: B1 → preset mode, B2 → PWM on/off toggle
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if(pin==B1_Pin){
        playPreset = !playPreset;
    }
    else if(pin==B2_Pin){
        pwmEnabled = !pwmEnabled;
        if(pwmEnabled)
            HAL_TIM_PWM_Start(&htim2,TIM_CHANNEL_2);
        else
            HAL_TIM_PWM_Stop (&htim2,TIM_CHANNEL_2);
    }
}

//------------------------------------------------------------------------------
// TIM callbacks: TIM3 → LED flash; TIM2 → 555 discharge emulation
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance==TIM3){
        if(ledMode==LED_MODE_FLASHING)
            HAL_GPIO_TogglePin(LED_PORT,LED_PIN);
    }
    else if(htim->Instance==TIM2){
        HAL_GPIO_WritePin(GPIOA,GPIO_PIN_1,GPIO_PIN_RESET);
    }
}

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance==TIM2 &&
       htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)
    {
        HAL_GPIO_WritePin(GPIOA,GPIO_PIN_1,GPIO_PIN_SET);
    }
}

//------------------------------------------------------------------------------
// UART helpers
void ShowCommands(void)
{
    UART_TransmitString(&huart2,
      "Commands: I=LED on, O=LED off, F=flash LED, H=help, T=play tune",1);
}
void UART_TransmitString(UART_HandleTypeDef *huart, char *s, int nl)
{
    HAL_UART_Transmit(huart,(uint8_t*)s,strlen(s),HAL_MAX_DELAY);
    if(nl) HAL_UART_Transmit(huart,(uint8_t*)"\r\n",2,HAL_MAX_DELAY);
}

//------------------------------------------------------------------------------
// Duty-cycle helper
void PWM_SetDutyCycle(float duty)
{
    if(duty<0)   duty=0;
    if(duty>100) duty=100;
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim2);
    uint32_t ccr = (uint32_t)((duty/100.0f)*(arr+1));
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, ccr);
}

//------------------------------------------------------------------------------
// Preset tune
typedef struct { float freq; uint32_t dur_ms; } Note;
static const Note presetTune[] = {
    {164.81f,410},{311.13f,375},{466.16f,125},{164.81f,203},
    {493.88f,277},{622.25f,222},{932.33f,256},{185.00f,225},
    {233.08f,256},{311.13f,223},{349.23f,254},{466.16f,229},
    {523.25f,255},{622.25f,225},{415.30f,252},{370.00f,224},
    {311.13f,202},{233.08f,264},{185.00f,236},{155.56f,233},
    {146.83f,271},{155.56f,255},{185.00f,224},{233.08f,225},
    {311.13f,256},{0.0f,500}
};
#define TUNE_LEN (sizeof(presetTune)/sizeof(presetTune[0]))

void PlayPresetTune(void)
{
    uint32_t clk = SystemCoreClock/(htim2.Init.Prescaler+1);
    for(uint16_t i=0; i<TUNE_LEN && playPreset && pwmEnabled; i++){
        float f = presetTune[i].freq;
        if(f>0.0f){
            uint32_t arr = (uint32_t)(clk/f)-1;
            if(arr<10)    arr=10;
            if(arr>0xFFFF)arr=0xFFFF;
            __HAL_TIM_SET_AUTORELOAD(&htim2,arr);
            __HAL_TIM_SET_COMPARE(&htim2,TIM_CHANNEL_2,arr/2);
            HAL_TIM_PWM_Start(&htim2,TIM_CHANNEL_2);
        } else {
            HAL_TIM_PWM_Stop(&htim2,TIM_CHANNEL_2);
        }
        HAL_Delay(presetTune[i].dur_ms);
    }
    playPreset = false;
}

