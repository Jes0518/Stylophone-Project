#include "main.h"
#include "app.h"
#include <stdbool.h>

extern TIM_HandleTypeDef htim1; // Defined in CubeMX-generated code

// ===== CONFIG =====
#define NUM_KEYS 25
#define SPEAKER_TIMER htim1
#define SPEAKER_CHANNEL TIM_CHANNEL_1

// === Function Prototypes ===
void playNote(float freq);
void stopNote(void);

// GPIO inputs for 25 keys
GPIO_TypeDef* keyPorts[NUM_KEYS] = {GPIOC, GPIOC, GPIOC, GPIOA, GPIOC,
                                    GPIOB, GPIOB, GPIOC, GPIOD, GPIOC,
                                    GPIOC, GPIOA, GPIOA, GPIOA, GPIOB,
                                    GPIOH, GPIOH, GPIOC, GPIOC, GPIOA,
                                    GPIOA, GPIOA, GPIOB, GPIOC, GPIOC};
uint16_t keyPins[NUM_KEYS] = {GPIO_PIN_8, GPIO_PIN_6, GPIO_PIN_5, GPIO_PIN_12, GPIO_PIN_9,
                              GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_11, GPIO_PIN_2, GPIO_PIN_10,
                              GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15, GPIO_PIN_7,
                              GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3, GPIO_PIN_0,
                              GPIO_PIN_1, GPIO_PIN_4, GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_0};

// Frequencies for 25 musical notes
float noteFreqs[NUM_KEYS] = {130.81f, 138.59f, 146.83f, 155.56f, 164.81f,
                             174.61f, 185.00f, 196.00f, 207.65f, 220.00f,
                             233.08f, 246.94f, 261.63f, 277.18f, 293.66f,
                             311.13f, 329.63f, 349.23f, 369.99f, 392.00f,
                             415.30f, 440.00f, 466.16f, 493.88f, 523.25f};

// ===== INIT =====
void App_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Initialize keys as input pull-up
    for (int i = 0; i < NUM_KEYS; i++) {
        GPIO_InitStruct.Pin = keyPins[i];
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(keyPorts[i], &GPIO_InitStruct);
    }

    // Initialize PA6 (External LED) as output
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET); // External LED off

    // Initialize PA5 (Onboard LED) as output
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); // Onboard LED off

    // Initialize blue button (PC13) as input with pull-up
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // Start PWM (speaker output)
    HAL_TIM_PWM_Start(&SPEAKER_TIMER, SPEAKER_CHANNEL);
}

// ===== MAIN LOOP =====
void App_MainLoop(void)
{
    static int8_t shiftMode = 0; // -2, -1, 0, +1, +2
    static GPIO_PinState lastButtonState = GPIO_PIN_SET;
    static uint32_t buttonPressTime = 0;
    static uint32_t lastBlinkTime = 0;
    static bool ledBlinkState = false;

    // Read blue button
    GPIO_PinState currentButtonState = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);

    // Button pressed
    if (lastButtonState == GPIO_PIN_SET && currentButtonState == GPIO_PIN_RESET) {
        buttonPressTime = HAL_GetTick();
    }

    // Button released
    if (lastButtonState == GPIO_PIN_RESET && currentButtonState == GPIO_PIN_SET) {
        uint32_t pressDuration = HAL_GetTick() - buttonPressTime;

        if (pressDuration < 300) {
            // Short press: shift down
            shiftMode--;
            if (shiftMode < -2) shiftMode = 0;
        } else {
            // Long press: shift up
            shiftMode++;
            if (shiftMode > 2) shiftMode = 0;
        }
        HAL_Delay(10); // Debounce delay
    }

    lastButtonState = currentButtonState;

    // ===== External LED (PA6) Mode Indicator =====
    uint32_t blinkInterval = 0;
    if (shiftMode == 0) {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET); // External LED OFF
    }
    else if (shiftMode > 0) { // Up-shift: Solid ON
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    }
    else if (shiftMode < 0) { // Down-shift: Blinking
        if (shiftMode == -1) blinkInterval = 400; // Slow blink
        if (shiftMode == -2) blinkInterval = 200; // Fast blink

        uint32_t now = HAL_GetTick();
        if (now - lastBlinkTime >= blinkInterval) {
            lastBlinkTime = now;
            ledBlinkState = !ledBlinkState;
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, ledBlinkState ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }

    // ===== KEY SCAN =====
    bool keyPressed = false;
    float octaveMultiplier = 1.0f;

    if (shiftMode == 1) octaveMultiplier = 2.0f;   // +1 octave
    if (shiftMode == 2) octaveMultiplier = 4.0f;   // +2 octaves
    if (shiftMode == -1) octaveMultiplier = 0.5f;  // -1 octave
    if (shiftMode == -2) octaveMultiplier = 0.25f; // -2 octaves

    for (int i = 0; i < NUM_KEYS; i++) {
        if (HAL_GPIO_ReadPin(keyPorts[i], keyPins[i]) == GPIO_PIN_RESET) {
            float freq = noteFreqs[i] * octaveMultiplier;
            playNote(freq);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET); // Onboard LED ON
            keyPressed = true;
            break;
        }
    }

    if (!keyPressed) {
        stopNote();
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); // Onboard LED OFF
    }
}

// ===== PLAY NOTE =====
void playNote(float freq)
{
    if (freq <= 0.0f) return;

    uint32_t timerClock = 1000000; // 1 MHz
    uint32_t prescaler = 79;
    uint32_t period = (uint32_t)(timerClock / freq);

    __HAL_TIM_SET_PRESCALER(&SPEAKER_TIMER, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&SPEAKER_TIMER, period - 1);
    __HAL_TIM_SET_COMPARE(&SPEAKER_TIMER, SPEAKER_CHANNEL, period / 2);

    HAL_TIM_PWM_Start(&SPEAKER_TIMER, SPEAKER_CHANNEL);
}

// ===== STOP NOTE =====
void stopNote(void)
{
    __HAL_TIM_SET_COMPARE(&SPEAKER_TIMER, SPEAKER_CHANNEL, 0); // Silence
}
