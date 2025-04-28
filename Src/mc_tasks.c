/**
  ******************************************************************************
  * @file    mc_tasks.c
  * @author  Motor Control SDK Team, ST Microelectronics
  * @brief   This file implements tasks definition
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "mc_type.h"
#include "mc_math.h"
#include "motorcontrol.h"
#include "regular_conversion_manager.h"
#include "mc_interface.h"
#include "digital_output.h"
#include "pwm_common.h"
#include "mc_tasks.h"
#include "parameters_conversion.h"
#include "mcp_config.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private define */
/* Private define ------------------------------------------------------------*/
#define M1_CHARGE_BOOT_CAP_MS  10
#define M2_CHARGE_BOOT_CAP_MS  10
#define STOPPERMANENCY_MS      400
#define STOPPERMANENCY_MS2     400
#define CHARGE_BOOT_CAP_TICKS  (uint16_t)((SYS_TICK_FREQUENCY * M1_CHARGE_BOOT_CAP_MS)/ 1000)
#define CHARGE_BOOT_CAP_TICKS2 (uint16_t)((SYS_TICK_FREQUENCY * M2_CHARGE_BOOT_CAP_MS)/ 1000)
#define STOPPERMANENCY_TICKS   (uint16_t)((SYS_TICK_FREQUENCY * STOPPERMANENCY_MS)/ 1000)
#define STOPPERMANENCY_TICKS2  (uint16_t)((SYS_TICK_FREQUENCY * STOPPERMANENCY_MS2)/ 1000)

/* Un-Comment this macro define in order to activate the smooth
   braking action on over voltage */
/* #define  MC.SMOOTH_BRAKING_ACTION_ON_OVERVOLTAGE */
#define M1_CHARGE_BOOT_CAP_DUTY_CYCLES (uint32_t)(0.000*(PWM_PERIOD_CYCLES/2))
#define M2_CHARGE_BOOT_CAP_DUTY_CYCLES (uint32_t)(0*(PWM_PERIOD_CYCLES2/2))
/* USER CODE END Private define */

#define VBUS_TEMP_ERR_MASK (MC_OVER_VOLT| MC_UNDER_VOLT| MC_OVER_TEMP)

/* Private variables----------------------------------------------------------*/
FOCVars_t FOCVars[NBR_OF_MOTORS];
PWMC_Handle_t * pwmcHandle[NBR_OF_MOTORS];
DOUT_handle_t *pR_Brake[NBR_OF_MOTORS];
DOUT_handle_t *pOCPDisabling[NBR_OF_MOTORS];
CircleLimitation_Handle_t *pCLM[NBR_OF_MOTORS];
ACIM_LSO_Handle_t *pACIM_LSO[NBR_OF_MOTORS];

static volatile uint16_t hMFTaskCounterM1 = 0; //cstat !MISRAC2012-Rule-8.9_a
static volatile uint16_t hBootCapDelayCounterM1 = 0;
static volatile uint16_t hStopPermanencyCounterM1 = 0;

uint8_t bMCBootCompleted = 0;

/* USER CODE BEGIN Private Variables */

/* USER CODE END Private Variables */

static volatile uint8_t bACIM_MagnValidCount = 0;

/* Private functions ---------------------------------------------------------*/
void TSK_MediumFrequencyTaskM1(void);
void FOC_Clear(uint8_t bMotor);
void FOC_InitAdditionalMethods(uint8_t bMotor);
void FOC_CalcCurrRef(uint8_t bMotor);
static uint16_t FOC_CurrControllerM1(void);
void TSK_SetChargeBootCapDelayM1(uint16_t hTickCount);
bool TSK_ChargeBootCapDelayHasElapsedM1(void);
void TSK_SetStopPermanencyTimeM1(uint16_t hTickCount);
bool TSK_StopPermanencyTimeHasElapsedM1(void);
void TSK_SafetyTask_PWMOFF(uint8_t motor);

/* USER CODE BEGIN Private Functions */

/* USER CODE END Private Functions */

bool ACIM_IsMagnetized(void);

/**
  * @brief  It initializes the whole MC core according to user defined
  *         parameters.
  * @param  pMCIList pointer to the vector of MCInterface objects that will be
  *         created and initialized. The vector must have length equal to the
  *         number of motor drives.
  */
__weak void MCboot( MCI_Handle_t* pMCIList[NBR_OF_MOTORS] )
{
  /* USER CODE BEGIN MCboot 0 */

  /* USER CODE END MCboot 0 */

  bMCBootCompleted = 0;
  pCLM[M1] = &CircleLimitationM1;
  /**********************************************************/
  /*    PWM and current sensing component initialization    */
  /**********************************************************/

  /* If the V/f is used the pwmcHandle is used to perform the
   * regular conversions (Temp, Bus Voltage, and user conversions)
   * Better name for this component should be something like
   * PWM and Analog Conversions (PWMAC)
   * In this case the PWMC child component is is used only for Analog Conversions
   *
   */

  pwmcHandle[M1] = &PWM_Handle_M1._Super;
  R3_2_Init(&PWM_Handle_M1);
  ASPEP_start (&aspepOverUartA);

  /* USER CODE BEGIN MCboot 1 */

  /* USER CODE END MCboot 1 */

  /**************************************/
  /*    Start timers synchronously      */
  /**************************************/
  startTimers();

  /******************************************************/
  /*   PID component initialization: speed regulation   */
  /******************************************************/
  PID_HandleInit(&PIDSpeedHandle_M1);

  /******************************************************/
  /*   Speed & torque component initialization          */
  /******************************************************/
  STC_Init(pSTC[M1], &PIDSpeedHandle_M1, &ACIM_LSO_Component_M1._SpeedEstimator);

  /********************************************************/
  /*   PID component initialization: current regulation   */
  /********************************************************/
  PID_HandleInit(&PIDIqHandle_M1);
  PID_HandleInit(&PIDIdHandle_M1);

  /********************************************************/
  /*   Bus voltage sensor component initialization        */
  /********************************************************/
  (void)RCM_RegisterRegConv(&VbusRegConv_M1);
  RVBS_Init(&BusVoltageSensor_M1);

  /*************************************************/
  /*   Power measurement component initialization  */
  /*************************************************/
  pMPM[M1]->pVBS = &(BusVoltageSensor_M1._Super);
  pMPM[M1]->pFOCVars = &FOCVars[M1];

  /*******************************************************/
  /*   Temperature measurement component initialization  */
  /*******************************************************/
  (void)RCM_RegisterRegConv(&TempRegConv_M1);
  NTC_Init(&TempSensor_M1);
  FOC_Clear(M1);
  FOCVars[M1].bDriveInput = EXTERNAL;
  FOCVars[M1].Iqdref = STC_GetDefaultIqdref(pSTC[M1]);
  FOCVars[M1].UserIdref = STC_GetDefaultIqdref(pSTC[M1]).d;

  MCI_Init(&Mci[M1], pSTC[M1], &FOCVars[M1],pwmcHandle[M1] );
  MCI_ExecSpeedRamp(&Mci[M1],
  STC_GetMecSpeedRefUnitDefault(pSTC[M1]),0); /*First command to STC*/
  pMCIList[M1] = &Mci[M1];
  Mci[M1].pScale = &scaleParams_M1;

  /********************************************************/
  /*   ACIM LSO-foc component initialization              */
  /********************************************************/
  pACIM_LSO[M1] = pACIM_LSO_Component_M1;
  pACIM_LSO[M1]->pFOCVars = &FOCVars[M1];
  ACIM_LSO_Init(pACIM_LSO[M1], &(BusVoltageSensor_M1._Super));

  /* USER CODE BEGIN MCboot 2 */

  /* USER CODE END MCboot 2 */

  bMCBootCompleted = 1;
}

/**
 * @brief Runs all the Tasks of the Motor Control cockpit
 *
 * This function is to be called periodically at least at the Medium Frequency task
 * rate (It is typically called on the Systick interrupt). Exact invokation rate is
 * the Speed regulator execution rate set in the Motor Contorl Workbench.
 *
 * The following tasks are executed in this order:
 *
 * - Medium Frequency Tasks of each motors
 * - Safety Task
 * - Power Factor Correction Task (if enabled)
 * - User Interface task.
 */
__weak void MC_RunMotorControlTasks(void)
{
  if ( bMCBootCompleted ) {
    /* ** Medium Frequency Tasks ** */
    MC_Scheduler();

    /* Safety task is run after Medium Frequency task so that
     * it can overcome actions they initiated if needed. */
    TSK_SafetyTask();
  }
  else
  {
    /* Nothing to do */
  }
}

/**
 * @brief Performs stop process and update the state machine.This function
 *        shall be called only during medium frequency task
 */
void TSK_MF_StopProcessing(  MCI_Handle_t * pHandle, uint8_t motor)
{
  R3_2_SwitchOffPWM(pwmcHandle[motor]);
  FOC_Clear(motor);
  PQD_Clear(pMPM[motor]);
  TSK_SetStopPermanencyTimeM1(STOPPERMANENCY_TICKS);
  Mci[motor].State = STOP;
}

/**
 * @brief  Executes the Medium Frequency Task functions for each drive instance.
 *
 * It is to be clocked at the Systick frequency.
 */
__weak void MC_Scheduler(void)
{
/* USER CODE BEGIN MC_Scheduler 0 */

/* USER CODE END MC_Scheduler 0 */

  if (((uint8_t)1) == bMCBootCompleted)
  {
    if(hMFTaskCounterM1 > 0u)
    {
      hMFTaskCounterM1--;
    }
    else
    {
      TSK_MediumFrequencyTaskM1();

      MCP_Over_UartA.rxBuffer = MCP_Over_UartA.pTransportLayer->fRXPacketProcess(MCP_Over_UartA.pTransportLayer,
                                                                                &MCP_Over_UartA.rxLength);
      if ( 0U == MCP_Over_UartA.rxBuffer)
      {
        /* Nothing to do */
      }
      else
      {
        /* Synchronous answer */
        if (0U == MCP_Over_UartA.pTransportLayer->fGetBuffer(MCP_Over_UartA.pTransportLayer,
                                                     (void **) &MCP_Over_UartA.txBuffer, //cstat !MISRAC2012-Rule-11.3
                                                     MCTL_SYNC))
        {
          /* no buffer available to build the answer ... should not occur */
        }
        else
        {
          MCP_ReceivedPacket(&MCP_Over_UartA);
          MCP_Over_UartA.pTransportLayer->fSendPacket(MCP_Over_UartA.pTransportLayer, MCP_Over_UartA.txBuffer,
                                                      MCP_Over_UartA.txLength, MCTL_SYNC);
          /* no buffer available to build the answer ... should not occur */
        }
      }

      /* USER CODE BEGIN MC_Scheduler 1 */

      /* USER CODE END MC_Scheduler 1 */
      hMFTaskCounterM1 = (uint16_t)MF_TASK_OCCURENCE_TICKS;
    }
    if(hBootCapDelayCounterM1 > 0U)
    {
      hBootCapDelayCounterM1--;
    }
    if(hStopPermanencyCounterM1 > 0U)
    {
      hStopPermanencyCounterM1--;
    }
    else
    {
      /* Nothing to do */
    }
  }
  else
  {
    /* Nothing to do */
  }
  /* USER CODE BEGIN MC_Scheduler 2 */

  /* USER CODE END MC_Scheduler 2 */
}

/**
  * @brief Executes medium frequency periodic Motor Control tasks
  *
  * This function performs some of the control duties on Motor 1 according to the
  * present state of its state machine. In particular, duties requiring a periodic
  * execution at a medium frequency rate (such as the speed controller for instance)
  * are executed here.
  */
__weak void TSK_MediumFrequencyTaskM1(void)
{
  /* USER CODE BEGIN MediumFrequencyTask M1 0 */

  /* USER CODE END MediumFrequencyTask M1 0 */

  PQD_CalcElMotorPower(pMPM[M1]);

  if (MCI_GetCurrentFaults(&Mci[M1]) == MC_NO_FAULTS)
  {
    if (MCI_GetOccurredFaults(&Mci[M1]) == MC_NO_FAULTS)
    {
      switch (Mci[M1].State)
      {
        case IDLE:
        {
          if ((MCI_START == Mci[M1].DirectCommand) || (MCI_MEASURE_OFFSETS == Mci[M1].DirectCommand))
          {
            if (pwmcHandle[M1]->offsetCalibStatus == false)
            {
              PWMC_CurrentReadingCalibr(pwmcHandle[M1], CRC_START);
              Mci[M1].State = OFFSET_CALIB;
            }
            else
            {
              /* calibration already done. Enables only TIM channels */
              pwmcHandle[M1]->OffCalibrWaitTimeCounter = 1u;
              PWMC_CurrentReadingCalibr(pwmcHandle[M1], CRC_EXEC);
              R3_2_TurnOnLowSides(pwmcHandle[M1],M1_CHARGE_BOOT_CAP_DUTY_CYCLES);
              TSK_SetChargeBootCapDelayM1(CHARGE_BOOT_CAP_TICKS);
              Mci[M1].State = CHARGE_BOOT_CAP;
            }
          }
          else
          {
            /* nothing to be done, FW stays in IDLE state */
          }
          break;
        }

        case OFFSET_CALIB:
        {
          if (MCI_STOP == Mci[M1].DirectCommand)
          {
            TSK_MF_StopProcessing(&Mci[M1], M1);
          }
          else
          {
            if (PWMC_CurrentReadingCalibr(pwmcHandle[M1], CRC_EXEC))
            {
              if (MCI_MEASURE_OFFSETS == Mci[M1].DirectCommand)
              {
                FOC_Clear(M1);
                PQD_Clear(pMPM[M1]);
                Mci[M1].DirectCommand = MCI_NO_COMMAND;
                Mci[M1].State = IDLE;
              }
              else
              {
                R3_2_TurnOnLowSides(pwmcHandle[M1],M1_CHARGE_BOOT_CAP_DUTY_CYCLES);
                TSK_SetChargeBootCapDelayM1(CHARGE_BOOT_CAP_TICKS);
                Mci[M1].State = CHARGE_BOOT_CAP;
              }
            }
            else
            {
              /* nothing to be done, FW waits for offset calibration to finish */
            }
          }
          break;
        }

        case CHARGE_BOOT_CAP:
        {
          if (MCI_STOP == Mci[M1].DirectCommand)
          {
            TSK_MF_StopProcessing(&Mci[M1], M1);
          }
          else
          {
            if (TSK_ChargeBootCapDelayHasElapsedM1())
            {
              R3_2_SwitchOffPWM(pwmcHandle[M1]);
              FOC_Clear( M1 );
              bACIM_MagnValidCount = 0;
              ACIM_LSO_Clear(pACIM_LSO[M1]);
              Mci[M1].State = START;
              R3_2_SwitchOnPWM(pwmcHandle[M1]);
            }
            else
            {
              /* nothing to be done, FW waits for bootstrap capacitor to charge */
            }
          }
          break;
        }

        case START:
        {
          if (MCI_STOP == Mci[M1].DirectCommand)
          {
            TSK_MF_StopProcessing(&Mci[M1], M1);
          }
          else
          {
#if (defined(DEBUG_LSO))
            Mci[M1].State = RUN;
#else /* If it is LSO-FOC without debug */
            /*ACIM_Magnetization stage in order to start the
            *speed control once the IM is magnetized.... */
            if(ACIM_IsMagnetized())
            {
              FOC_InitAdditionalMethods(M1);
              FOC_CalcCurrRef(M1);
              STC_ForceSpeedReferenceToCurrentSpeed(pSTC[M1]); /* Init the reference speed to current speed */
              MCI_ExecBufferedCommands(&Mci[M1]); /* Exec the speed ramp after changing of the speed sensor */
              Mci[M1].State = RUN;
            }
#endif
          }
          break;
        }

        /* Nothing to do for ACIM No REVUP*/
        case SWITCH_OVER:
        {
          if (MCI_STOP == Mci[M1].DirectCommand)
          {
            TSK_MF_StopProcessing(&Mci[M1], M1);
          }
          else
          {
              /* USER CODE BEGIN MediumFrequencyTask M1 1 */

              /* USER CODE END MediumFrequencyTask M1 1 */
              Mci[M1].State = RUN;
          }
          break;
        }

        case RUN:
        {
          if (MCI_STOP == Mci[M1].DirectCommand)
          {
            TSK_MF_StopProcessing(&Mci[M1], M1);
          }
          else
          {
            /* USER CODE BEGIN MediumFrequencyTask M1 2 */

            /* USER CODE END MediumFrequencyTask M1 2 */
            MCI_ExecBufferedCommands(&Mci[M1]);
            #if (!defined(DEBUG_LSO))
            if(!ACIM_LSO_CheckIntegrity(pACIM_LSO[M1]))
            {
              MCI_FaultProcessing(&Mci[M1], MC_SPEED_FDBK, 0);
            }
            #endif
            FOC_CalcCurrRef(M1);
            /* USER CODE BEGIN MediumFrequencyTask M1 3 */

            /* USER CODE END MediumFrequencyTask M1 3 */
          }
          break;
        }

        case STOP:
        {

          R3_2_SwitchOffPWM(pwmcHandle[M1]);
          FOC_Clear(M1);
          ACIM_LSO_Clear(pACIM_LSO[M1]);

          /* USER CODE BEGIN MediumFrequencyTask M1 4 */

          /* USER CODE END MediumFrequencyTask M1 4 */

          if(TSK_StopPermanencyTimeHasElapsedM1())
          {
            Mci[M1].State = IDLE;
          }
          else
          {
            /* Nothing to do */
          }
          break;
        }

        case FAULT_OVER:
        {
          if (MCI_ACK_FAULTS == Mci[M1].DirectCommand)
          {
            Mci[M1].DirectCommand = MCI_NO_COMMAND;
            Mci[M1].State = IDLE;
          }
          else
          {
            /* nothing to do, FW stays in FAULT_OVER state until acknowledgement */
          }
          break;
        }

        case FAULT_NOW:
        {
          Mci[M1].State = FAULT_OVER;
          break;
        }

        default:
          break;
      }
    }
    else
    {
      Mci[M1].State = FAULT_OVER;
    }
  }
  else
  {
    Mci[M1].State = FAULT_NOW;
  }

  /* USER CODE BEGIN MediumFrequencyTask M1 6 */

  /* USER CODE END MediumFrequencyTask M1 6 */
}
/**
  * @brief  It re-initializes the current and voltage variables. Moreover
  *         it clears qd currents PI controllers, voltage sensor and SpeednTorque
  *         controller. It must be called before each motor restart.
  *         It does not clear speed sensor.
  * @param  bMotor related motor it can be M1 or M2
  */
__weak void FOC_Clear(uint8_t bMotor)
{
  /* USER CODE BEGIN FOC_Clear 0 */

  /* USER CODE END FOC_Clear 0 */

  ab_t        NULL_ab        = {(int16_t)0, (int16_t)0};
  qd_t        NULL_qd        = {(int16_t)0, (int16_t)0};
  alphabeta_t NULL_alphabeta = {(int16_t)0, (int16_t)0};

  FOCVars[bMotor].Iab = NULL_ab;
  FOCVars[bMotor].Ialphabeta = NULL_alphabeta;
  FOCVars[bMotor].Iqd = NULL_qd;
  FOCVars[bMotor].Iqdref = NULL_qd;
  FOCVars[bMotor].hTeref = (int16_t)0;
  FOCVars[bMotor].Vqd = NULL_qd;
  FOCVars[bMotor].Valphabeta = NULL_alphabeta;
  FOCVars[bMotor].hElAngle = (int16_t)0;

  PID_SetIntegralTerm(pPIDIq[bMotor], (int32_t)0);
  PID_SetIntegralTerm(pPIDId[bMotor], (int32_t)0);

  STC_Clear(pSTC[bMotor]);

  PWMC_SwitchOffPWM(pwmcHandle[bMotor]);

  /* USER CODE BEGIN FOC_Clear 1 */

  /* USER CODE END FOC_Clear 1 */
}

/**
  * @brief  Use this method to initialize additional methods (if any) in
  *         START_TO_RUN state
  * @param  bMotor related motor it can be M1 or M2
  */
__weak void FOC_InitAdditionalMethods(uint8_t bMotor)
{
  /* USER CODE BEGIN FOC_InitAdditionalMethods 0 */

  /* USER CODE END FOC_InitAdditionalMethods 0 */
}

/**
  * @brief  It computes the new values of Iqdref (current references on qd
  *         reference frame) based on the required electrical torque information
  *         provided by oTSC object (internally clocked).
  *         If implemented in the derived class it executes flux weakening and/or
  *         MTPA algorithm(s). It must be called with the periodicity specified
  *         in oTSC parameters
  * @param  bMotor related motor it can be M1 or M2
  */
__weak void FOC_CalcCurrRef(uint8_t bMotor)
{

  /* USER CODE BEGIN FOC_CalcCurrRef 0 */

  /* USER CODE END FOC_CalcCurrRef 0 */

  if(FOCVars[bMotor].bDriveInput == INTERNAL)
  {
    FOCVars[bMotor].hTeref = STC_CalcTorqueReference(pSTC[bMotor]);
    FOCVars[bMotor].Iqdref.q = FOCVars[bMotor].hTeref;
  }
  else
  {
    /* Nothing to do */
  }

  /* USER CODE BEGIN FOC_CalcCurrRef 1 */

  /* USER CODE END FOC_CalcCurrRef 1 */
}

/**
  * @brief  It set a counter intended to be used for counting the delay required
  *         for drivers boot capacitors charging of motor 1
  * @param  hTickCount number of ticks to be counted
  * @retval void
  */
__weak void TSK_SetChargeBootCapDelayM1(uint16_t hTickCount)
{
   hBootCapDelayCounterM1 = hTickCount;
}

/**
  * @brief  Use this function to know whether the time required to charge boot
  *         capacitors of motor 1 has elapsed
  * @param  none
  * @retval bool true if time has elapsed, false otherwise
  */
__weak bool TSK_ChargeBootCapDelayHasElapsedM1(void)
{
  bool retVal = false;

  if (hBootCapDelayCounterM1 == 0)
  {
    retVal = true;
  }
  else
  {
    /* Nothing to do */
  }

  return (retVal);
}

/**
  * @brief  It set a counter intended to be used for counting the permanency
  *         time in STOP state of motor 1
  * @param  hTickCount number of ticks to be counted
  * @retval void
  */
__weak void TSK_SetStopPermanencyTimeM1(uint16_t hTickCount)
{
  hStopPermanencyCounterM1 = hTickCount;
}

/**
  * @brief  Use this function to know whether the permanency time in STOP state
  *         of motor 1 has elapsed
  * @param  none
  * @retval bool true if time is elapsed, false otherwise
  */
__weak bool TSK_StopPermanencyTimeHasElapsedM1(void)
{
  bool retVal = false;
  if (hStopPermanencyCounterM1 == 0)
  {
    retVal = true;
  }
  else
  {
    /* Nothing to do */
  }
  return (retVal);
}

#if (defined (CCMRAM) || defined(CCMRAM_ENABLED))
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#elif defined (__GNUC__)
__attribute__((section ("ccmram")))
#endif
#endif

/**
  * @brief  Executes the Motor Control duties that require a high frequency rate and a precise timing
  *
  *  This is mainly the FOC current control loop. It is executed depending on the state of the Motor Control
  * subsystem (see the state machine(s)).
  *
  * @retval Number of the  motor instance which FOC loop was executed.
  */
__weak uint8_t TSK_HighFrequencyTask(void)
{
  /* USER CODE BEGIN HighFrequencyTask 0 */

  /* USER CODE END HighFrequencyTask 0 */
  uint16_t returnValue;
  uint8_t bMotorNbr = 0;

  ACIM_LSO_CalcAngle(pACIM_LSO[M1]);
#if defined(DEBUG_LSO)
  float fFreqHz = (float) MCI_GetMecSpeedRef01Hz(oMCInterface[M1])/10.0f;
  ACIM_DBG_LSO_CalcAngle(pACIM_LSO[M1],fFreqHz);
#endif

  /* USER CODE BEGIN HighFrequencyTask SINGLEDRIVE_1 */

  /* USER CODE END HighFrequencyTask SINGLEDRIVE_1 */

  returnValue = FOC_CurrControllerM1();

  /* USER CODE BEGIN HighFrequencyTask SINGLEDRIVE_2 */

  /* USER CODE END HighFrequencyTask SINGLEDRIVE_2 */
  if(returnValue == MC_DURATION)
  {
    MCI_FaultProcessing(&Mci[M1], MC_DURATION, 0);
  }
  else
  {
    /* USER CODE BEGIN HighFrequencyTask SINGLEDRIVE_3 */

    /* USER CODE END HighFrequencyTask SINGLEDRIVE_3 */
  }

  /* USER CODE BEGIN HighFrequencyTask 1 */

  /* USER CODE END HighFrequencyTask 1 */

  GLOBAL_TIMESTAMP++;
  if (0U == MCPA_UART_A.Mark)
  {
    /* Nothing to do */
  }
  else
  {
    MCPA_dataLog (&MCPA_UART_A);
  }

  return (bMotorNbr);
}

#if (defined (CCMRAM) || defined(CCMRAM_ENABLED))
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#elif defined (__GNUC__)
__attribute__((section ("ccmram")))
#endif
#endif
/**
  * @brief It executes the core of FOC drive that is the controllers for Iqd
  *        currents regulation. Reference frame transformations are carried out
  *        accordingly to the active speed sensor. It must be called periodically
  *        when new motor currents have been converted
  * @param this related object of class CFOC.
  * @retval uint16_t It returns MC_NO_FAULTS if the FOC has been ended before
  *         next PWM Update event, MC_DURATION otherwise
  */
  inline uint16_t FOC_CurrControllerM1(void)
{
  qd_t Iqd, Vqd;
  ab_t Iab;
  alphabeta_t Ialphabeta, Valphabeta;
  int16_t hElAngle;
  uint16_t hCodeError;
  hElAngle = ACIM_LSO_GetElAngle(pACIM_LSO[M1]);
#if defined(DEBUG_LSO)
  int16_t hElAngle_DBG =  ACIM_DBG_LSO_GetdbgElAngle(pACIM_LSO[M1]);
  int16_t hElAngle_bckp;
#endif

  PWMC_GetPhaseCurrents(pwmcHandle[M1], &Iab);
  Ialphabeta = MCM_Clarke(Iab);

  /* Alpha-Beta to d- conversion begin*/
  Vector_s16_Components * tmp_aplhabeta = (Vector_s16_Components *) &Ialphabeta;
  Vector_s16_Components Iqd_tmp = MCM_Park_Generic( *tmp_aplhabeta, hElAngle);
  qd_t* tmpIqd = (qd_t*)&Iqd_tmp;
  Iqd = *tmpIqd;

  /* Alpha-Beta to d- conversion end */
  Vqd.q = PI_Controller(pPIDIq[M1], (int32_t)(FOCVars[M1].Iqdref.q) - Iqd.q);
  Vqd.d = PI_Controller(pPIDId[M1], (int32_t)(FOCVars[M1].Iqdref.d) - Iqd.d);

#if defined(DEBUG_LSO)
  Vqd = ACIM_DBG_LSO_CalcVoltage(pACIM_LSO[M1]);
#else
  FOCVars[M1].Vqd = Vqd;
#endif

  Vqd = Circle_Limitation(&CircleLimitationM1, Vqd);
#if defined(DEBUG_LSO)
  hElAngle_bckp = hElAngle;
  hElAngle = hElAngle_DBG;
#endif

  /* d-q to Alpha-Beta conversion begin*/
  Vector_s16_Components * tmp_qd   = (Vector_s16_Components *)&Vqd;
  Vector_s16_Components tmp_AlphaBeta = MCM_Rev_Park_Generic(*tmp_qd, hElAngle);
  alphabeta_t* tmp_V =(alphabeta_t*)&tmp_AlphaBeta;
  Valphabeta = *tmp_V;
  /* d-q to Alpha-Beta conversion end*/
  hCodeError = PWMC_SetPhaseVoltage(pwmcHandle[M1], Valphabeta);

#if defined(DEBUG_LSO)
  hElAngle = hElAngle_bckp;

  /* Alpha-Beta to d- conversion begin*/
  Vector_s16_Components * tmp_dbgaplhabeta = (Vector_s16_Components *)&Valphabeta;
  Vector_s16_Components Vect_tmp = MCM_Park_Generic(*tmp_dbgaplhabeta, hElAngle);
  Volt_Components * tmp_Vqd = (Volt_Components *)&Vect_tmp;
  FOCVars[M1].Vqd = *tmp_Vqd;
  /* Alpha-Beta to d- conversion end*/
#endif

  FOCVars[M1].Iab = Iab;
  FOCVars[M1].Ialphabeta = Ialphabeta;
  FOCVars[M1].Iqd = Iqd;
  FOCVars[M1].Valphabeta = Valphabeta;
  FOCVars[M1].hElAngle = hElAngle;

  return(hCodeError);
}

/**
  * @brief  Executes safety checks (e.g. bus voltage and temperature) for all drive instances.
  *
  * Faults flags are updated here.
  */
__weak void TSK_SafetyTask(void)
{
  /* USER CODE BEGIN TSK_SafetyTask 0 */

  /* USER CODE END TSK_SafetyTask 0 */
  if (bMCBootCompleted == 1)
  {
    TSK_SafetyTask_PWMOFF(M1);
    /* User conversion execution */
    RCM_ExecUserConv ();
  /* USER CODE BEGIN TSK_SafetyTask 1 */

  /* USER CODE END TSK_SafetyTask 1 */
  }
  else
  {
    /* Nothing to do */
  }
}

/**
  * @brief  Safety task implementation if  MC.M1_ON_OVER_VOLTAGE == TURN_OFF_PWM
  * @param  bMotor Motor reference number defined
  *         \link Motors_reference_number here \endlink
  */
__weak void TSK_SafetyTask_PWMOFF(uint8_t bMotor)
{
  /* USER CODE BEGIN TSK_SafetyTask_PWMOFF 0 */

  /* USER CODE END TSK_SafetyTask_PWMOFF 0 */

   uint16_t CodeReturn = MC_NO_ERROR;
   uint8_t lbMotor = M1;
  const uint16_t errMask[NBR_OF_MOTORS] = {VBUS_TEMP_ERR_MASK};
  /* Check for fault if FW protection is activated. It returns MC_OVER_TEMP or MC_NO_ERROR */
  if (M1 == bMotor)
  {
    uint16_t rawValueM1 = RCM_ExecRegularConv(&TempRegConv_M1);
    CodeReturn |= errMask[bMotor] & NTC_CalcAvTemp(&TempSensor_M1, rawValueM1);
  }
  else
  {
    /* Nothing to do */
  }

  /* Due to warning array subscript 1 is above array bounds of PWMC_Handle_t *[1] [-Warray-bounds] */
   CodeReturn |= PWMC_IsFaultOccurred(pwmcHandle[lbMotor]);     /* check for fault. It return MC_OVER_CURR or MC_NO_FAULTS
                                                     (for STM32F30x can return MC_OVER_VOLT in case of HW Overvoltage) */

  if (M1 == bMotor)
  {
    uint16_t rawValueM1 =  RCM_ExecRegularConv(&VbusRegConv_M1);
    CodeReturn |= errMask[bMotor] & RVBS_CalcAvVbus(&BusVoltageSensor_M1, rawValueM1);
  }
  else
  {
    /* Nothing to do */
  }
  MCI_FaultProcessing(&Mci[bMotor], CodeReturn, ~CodeReturn); /* process faults */

  if (MCI_GetFaultState(&Mci[bMotor]) != (uint32_t)MC_NO_FAULTS)
  {
    PWMC_SwitchOffPWM(pwmcHandle[bMotor]);
    FOC_Clear(bMotor);

    if (MCPA_UART_A.Mark != 0)
    {
      MCPA_flushDataLog (&MCPA_UART_A);
    }
    else
    {
      /* Nothing to do */
    }

    PQD_Clear(pMPM[bMotor]); //cstat !MISRAC2012-Rule-11.3

    /* USER CODE BEGIN TSK_SafetyTask_PWMOFF 1 */

    /* USER CODE END TSK_SafetyTask_PWMOFF 1 */
  }
  else
  {
    /* no errors */
  }

  /* USER CODE BEGIN TSK_SafetyTask_PWMOFF 3 */

  /* USER CODE END TSK_SafetyTask_PWMOFF 3 */
}

/**
  * @brief  Safety task implementation if  MC.M1_ON_OVER_VOLTAGE == TURN_ON_R_BRAKE
  * @param  motor Motor reference number defined
  *         \link Motors_reference_number here \endlink
  */
__weak void TSK_SafetyTask_RBRK(uint8_t bMotor)
{
  /* USER CODE BEGIN TSK_SafetyTask_RBRK 0 */

  /* USER CODE END TSK_SafetyTask_RBRK 0 */
  uint16_t CodeReturn = MC_NO_ERROR;
  uint16_t BusVoltageFaultsFlag = MC_OVER_VOLT;
  uint8_t lbMotor = M1;

  uint16_t errMask[NBR_OF_MOTORS] = {VBUS_TEMP_ERR_MASK};
  /* Brake resistor management */
  if (M1 == bMotor)
  {
    uint16_t rawValueM1 =  RCM_ExecRegularConv(&VbusRegConv_M1);
    BusVoltageFaultsFlag =  errMask[bMotor] & RVBS_CalcAvVbus(&BusVoltageSensor_M1, rawValueM1);
  }
  else
  {
    /* Nothing to do */
  }

  if (MC_OVER_VOLT == BusVoltageFaultsFlag)
  {
    DOUT_SetOutputState(pR_Brake[lbMotor], ACTIVE);
  }
  else
  {
    DOUT_SetOutputState(pR_Brake[lbMotor], INACTIVE);
  }
  /* Check for fault if FW protection is activated. It returns MC_OVER_TEMP or MC_NO_ERROR */
  if (M1 == bMotor)
  {
    uint16_t rawValueM1 = RCM_ExecRegularConv(&TempRegConv_M1);
    CodeReturn |= errMask[bMotor] & NTC_CalcAvTemp(&TempSensor_M1, rawValueM1);
  }
  else
  {
    /* Nothing to do */
  }

  CodeReturn |= PWMC_IsFaultOccurred (pwmcHandle[bMotor]);    /* check for fault. It return MC_OVER_CURR or MC_NO_FAULTS
                                                                 (for STM32F30x can return MC_OVER_VOLT in case of HW Overvoltage) */
  CodeReturn |= (BusVoltageFaultsFlag & MC_UNDER_VOLT);       /* MC_UNDER_VOLT generates fault if FW protection is activated,
                                                                 MC_OVER_VOLT doesn't generate fault */
  MCI_FaultProcessing(&Mci[bMotor], CodeReturn, ~CodeReturn); /* Update the STM according error code */

  if (MCI_GetFaultState(&Mci[bMotor]) != (uint32_t)MC_NO_FAULTS)
  {

    PWMC_SwitchOffPWM(pwmcHandle[bMotor]);
    FOC_Clear(bMotor);
    if (MCPA_UART_A.Mark != 0)
    { /* Dual motor not yet supported */
      MCPA_flushDataLog (&MCPA_UART_A);
    }
    else
    {
      /* Nothing to do */
    }

    PQD_Clear(pMPM[bMotor]); //cstat !MISRAC2012-Rule-11.3
    /* USER CODE BEGIN TSK_SafetyTask_RBRK 1 */

    /* USER CODE END TSK_SafetyTask_RBRK 1 */
  }
  /* USER CODE BEGIN TSK_SafetyTask_RBRK 2 */

  /* USER CODE END TSK_SafetyTask_RBRK 2 */
}

/**
  * @brief  This function returns the reference of the MCInterface relative to
  *         the selected drive.
  * @param  bMotor Motor reference number defined
  *         \link Motors_reference_number here \endlink
  * @retval MCI_Handle_t * Reference to MCInterface relative to the selected drive.
  *         Note: it can be MC_NULL if MCInterface of selected drive is not
  *         allocated.
  */
__weak MCI_Handle_t * GetMCI(uint8_t bMotor)
{
  MCI_Handle_t * retVal = MC_NULL;
  if (bMotor < NBR_OF_MOTORS)
  {
    retVal = &Mci[bMotor];
  }
  else
  {
    /* Nothing to do */
  }
  return retVal;
}

/**
  * @brief  Puts the Motor Control subsystem in in safety conditions on a Hard Fault
  *
  *  This function is to be executed when a general hardware failure has been detected
  * by the microcontroller and is used to put the system in safety condition.
  */
__weak void TSK_HardwareFaultTask(void)
{
  /* USER CODE BEGIN TSK_HardwareFaultTask 0 */

  /* USER CODE END TSK_HardwareFaultTask 0 */

  R3_2_SwitchOffPWM(pwmcHandle[M1]);
  MCI_FaultProcessing(&Mci[M1], MC_SW_ERROR, 0);

  /* USER CODE BEGIN TSK_HardwareFaultTask 1 */

  /* USER CODE END TSK_HardwareFaultTask 1 */
}

__weak void UI_HandleStartStopButton_cb (void)
{
/* USER CODE BEGIN START_STOP_BTN */
  if (MC_GetSTMStateMotor1() == IDLE)
  {
    /* Ramp parameters should be tuned for the actual motor */
    MC_StartMotor1();
  }
  else
  {
    MC_StopMotor1();
  }
/* USER CODE END START_STOP_BTN */
}

 /**
  * @brief  Locks GPIO pins used for Motor Control to prevent accidental reconfiguration
  */
__weak void mc_lock_pins (void)
{
LL_GPIO_LockPin(M1_CURR_AMPL_U_GPIO_Port, M1_CURR_AMPL_U_Pin);
LL_GPIO_LockPin(M1_CURR_AMPL_V_GPIO_Port, M1_CURR_AMPL_V_Pin);
LL_GPIO_LockPin(M1_CURR_AMPL_W_GPIO_Port, M1_CURR_AMPL_W_Pin);
LL_GPIO_LockPin(M1_PWM_UL_GPIO_Port, M1_PWM_UL_Pin);
LL_GPIO_LockPin(M1_PWM_UH_GPIO_Port, M1_PWM_UH_Pin);
LL_GPIO_LockPin(M1_PWM_VL_GPIO_Port, M1_PWM_VL_Pin);
LL_GPIO_LockPin(M1_PWM_VH_GPIO_Port, M1_PWM_VH_Pin);
LL_GPIO_LockPin(M1_PWM_WL_GPIO_Port, M1_PWM_WL_Pin);
LL_GPIO_LockPin(M1_PWM_WH_GPIO_Port, M1_PWM_WH_Pin);
LL_GPIO_LockPin(M1_OCP_GPIO_Port, M1_OCP_Pin);
LL_GPIO_LockPin(M1_BUS_VOLTAGE_GPIO_Port, M1_BUS_VOLTAGE_Pin);
LL_GPIO_LockPin(M1_TEMPERATURE_GPIO_Port, M1_TEMPERATURE_Pin);
}

/* USER CODE BEGIN mc_task 0 */

/* USER CODE END mc_task 0 */

bool ACIM_IsMagnetized(void)
{
  bool bRetVal = false;

  FOCVars[M1].Iqdref = STC_GetDefaultIqdref(pSTC[M1]);

  if(bACIM_MagnValidCount<MAGN_VALIDATION_TICKS)
  {
    if((FOCVars[M1].Iqd.d >= ((int16_t)((float)FOCVars[M1].Iqdref.d * LOWER_BW_LIMIT_PERC)))&&
       (FOCVars[M1].Iqd.d <= ((int16_t)((float)FOCVars[M1].Iqdref.d * UPPER_BW_LIMIT_PERC))))
    {
      bACIM_MagnValidCount++;
    }
    else
    {
      bACIM_MagnValidCount = 0;
    }
  }
  else
  {
    bRetVal = true;
  }

return bRetVal;
}

/* USER CODE BEGIN mc_task 0 */

/* USER CODE END mc_task 0 */
/******************* (C) COPYRIGHT 2024 STMicroelectronics *****END OF FILE****/
