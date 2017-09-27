/*
 * hardware.c
 *
 *  Created on: 2Sep.,2017
 *      Author: Ben V. Brown
 */

//These are all the functions for interacting with the hardware

#include "hardware.h"
volatile uint16_t PWMSafetyTimer = 0;
volatile int16_t CalibrationTempOffset = 0;
void setCalibrationOffset(int16_t offSet)
{
	CalibrationTempOffset=offSet;
}
uint16_t getHandleTemperature() {
	// We return the current handle temperature in X10 C
	// TMP36 in handle, 0.5V offset and then 10mV per deg C (0.75V @ 25C for example)
	// STM32 = 4096 count @ 3.3V input -> But
	// We oversample by 32/(2^3) = 4 times oversampling
	// Therefore 16384 is the 3.3V input, so 0.201416015625 mV per count
	// So we need to subtract an offset of 0.5V to center on 0C (2482 counts)
	//
	uint16_t result = getADC(0);
	if (result < 2482)
		return 0;
	result -= 2482;
	result /= 5;    //convert to X10 C
	return result;

}
uint16_t tipMeasurementToC(uint16_t raw) {
	return ((raw-532) / 33) + (getHandleTemperature()/10) - CalibrationTempOffset;
	//Surprisingly that appears to be a fairly good linear best fit
}
uint16_t ctoTipMeasurement(uint16_t temp) {
	//We need to compensate for cold junction temp
	return ((temp-(getHandleTemperature()/10) + CalibrationTempOffset) * 33)+532;
}
uint16_t getTipInstantTemperature() {
	uint16_t sum = 0;
	sum += HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
	sum += HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
	sum += HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_3);
	sum += HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_4);
	return sum;

}
uint16_t getTipRawTemp(uint8_t instant) {
#define  filterDepth1 2
#define filterDepth2 8
	static uint16_t filterLayer1[filterDepth1];
	static uint16_t filterLayer2[filterDepth2];
	static uint8_t index = 0;
	static uint8_t indexFilter = 0;

	if (instant) {
		uint16_t itemp = getTipInstantTemperature();
		filterLayer1[index] = itemp;
		index = (index + 1) % filterDepth1;
		uint32_t total = 0;
		for (uint8_t i = 0; i < filterDepth1; i++)
			total += filterLayer1[i];

		return total / filterDepth1;
	} else {
		uint32_t total = 0;
		for (uint8_t i = 0; i < filterDepth1; i++)
			total += filterLayer1[i];
		filterLayer2[indexFilter] = total / filterDepth1;
		indexFilter = (indexFilter + 1) % filterDepth2;
		total = 0;
		for (uint8_t i = 0; i < filterDepth2; i++)
			total += filterLayer2[i];

		return total / filterDepth2;
	}
}
uint16_t getInputVoltageX10() {
	//ADC maximum is 16384 == 3.3V at input == 28V at VIN
	//Therefore we can divide down from there
	//Ideal term is 57.69.... 58 is quite close
	return getADC(1) / 58;
}
uint8_t getTipPWM()
{
	return htim2.Instance->CCR4;
}
void setTipPWM(uint8_t pulse) {
	PWMSafetyTimer = 100;    //This is decremented in the handler for PWM so that the tip pwm is disabled if the PID task is not scheduled often enough.
	if (pulse > 100)
		pulse = 100;
	if (pulse) {
		htim2.Instance->CCR4 = pulse;
	} else {
		htim2.Instance->CCR4 = 0;
	}

}

//Thse are called by the HAL after the corresponding events from the system timers.

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	//Period has elapsed
	if (htim->Instance == TIM2) {
		//we want to turn on the output again
		PWMSafetyTimer--;    //We decrement this safety value so that lockups in the scheduler will not cause the PWM to become locked in an active driving state.
		//While we could assume this could never happened, its a small price for increased safety
		if (htim2.Instance->CCR4 && PWMSafetyTimer) {
			htim3.Instance->CCR1 = 50;
			HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
		} else {
			HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
			htim3.Instance->CCR1 = 0;
		}
	} else if (htim->Instance == TIM1) {
		HAL_IncTick();
	}
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance == TIM2) {
		//This was a pulse event
		if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_4) {
			HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
			htim3.Instance->CCR1 = 0;
		}
	}
}
