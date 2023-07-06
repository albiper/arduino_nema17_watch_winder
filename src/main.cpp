#include <Arduino.h>
#include <AccelStepper.h>
#include <jled.h>
#include <AceButton.h>
#include <stdarg.h>

using namespace ace_button;

void moveStepperTo(int position);
void handleButtonEvent(AceButton *, uint8_t, uint8_t);
void handleButtonClick();
void handleButtonLongClick();
void handleButtonDoubleClick();
void managePowerLed();

void stopState();
void rightState();
void leftState();
void pauseState();
void rthState();
void logger(const char *messagePattern, ...);

// pins
#define STEP_PIN 3
#define DIR_PIN 2
#define EN_PIN 4
#define BTN_PIN 5
#define PWR_LED_PIN 6

// Configuration
#define CYCLES_PER_RUN 5
#define ROTATIONS_R_PER_CYCLE 5 // default 2
#define ROTATIONS_L_PER_CYCLE 5 // default 2
// #define PAUSE_MIN 30            // default 35
#define PAUSE_MIN 1 // default 35
#define ROT_STEPS 4096

// motor
#define MAX_SPEED 2000
#define ACCELERATION 1000
#define ROT_R 5 // default 2
#define ROT_L 5 // default 2

// process state enum
enum StateType
{
  W_NONE,
  W_STOP,  // 0 - Winder unoperational
  W_RIGHT, // 1 - Winder rotating CLOCKWISE
  W_LEFT,  // 2 - Winder rotating ANTICLOCKWISE
  W_PAUSE, // 3 - Winder waiting between cycles
  W_RTH    // 4 - Winder return to center
};

AccelStepper stepper(1, STEP_PIN, DIR_PIN);
JLed pwr_led = JLed(PWR_LED_PIN).FadeOn(1000);
AceButton pwr_sw(BTN_PIN);

// configuration
StateType startupState = W_STOP;

// loop variables
StateType currentState = startupState;
StateType previousState = W_NONE;
int LedOn = true;
int remainingCycles = CYCLES_PER_RUN;
int stateUpdated = false;
int lastPauseMinute = false;
int rthActivated = false;
int hasToStart = false;
int hasToContinue = false;
long StartTime = 0;
int targetPos = 0;

void setup()
{
  pinMode(EN_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(PWR_LED_PIN, OUTPUT);

  digitalWrite(EN_PIN, HIGH);

  stepper.setAcceleration(ACCELERATION);
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setCurrentPosition(0);

  ButtonConfig *buttonConfig = pwr_sw.getButtonConfig();
  buttonConfig->setEventHandler(handleButtonEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
  buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
  buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);
  buttonConfig->setClickDelay(500);

  Serial.begin(115200);
  while (!Serial)
  {
  }
}

void loop()
{
  // check for button events
  pwr_sw.check();

  if (previousState != currentState)
    stateUpdated = true;

  previousState = currentState;

  switch (currentState)
  {
  case W_RTH:
    rthState();
    break;
  case W_RIGHT:
    rightState();
    break;

  case W_LEFT:
    leftState();
    break;

  case W_STOP:
    stopState();
    break;

  case W_PAUSE:
    pauseState();
    break;

  default:
    break;
  }

  pwr_led.Update();
}

void moveStepperTo(int position)
{
  digitalWrite(EN_PIN, LOW);
  stepper.moveTo(position);
  stepper.runToPosition();
  digitalWrite(EN_PIN, HIGH);
}

void handleButtonEvent(AceButton *button, uint8_t eventType, uint8_t buttonState)
{
  switch (eventType)
  {
  case AceButton::kEventClicked:
    handleButtonClick();
    break;

  case AceButton::kEventLongPressed:
    handleButtonLongClick();
    break;

  case AceButton::kEventDoubleClicked:
    handleButtonDoubleClick();
    break;

  default:
    break;
  }
}

void handleButtonClick()
{
  if (currentState == W_STOP || currentState == W_PAUSE)
  {
    // if idle or pause, START winding
    remainingCycles = CYCLES_PER_RUN;
    logger(">> Click: Start Winding");
    rthActivated = false;
    currentState = W_RIGHT;
  }
}

void handleButtonLongClick()
{
  if (currentState == W_LEFT || currentState == W_RIGHT || currentState == W_PAUSE)
  {
    // if winding left/right or pause between rotations, STOP winding
    logger(">> LongPress: Stop Winding");
    rthActivated = true;
    hasToContinue = false;
    pwr_led.Reset();

    // if pause between rotations, led ON, else led BLINK
    if (currentState == W_PAUSE)
      pwr_led.On();
    else
      pwr_led.Blink(100, 500).Forever();
  }
}

void handleButtonDoubleClick()
{
  pwr_led.Reset();
  if (LedOn)
  {
    // toggle led activation on long click
    logger(">> DoubleClick: LED Disabled");
    pwr_led.Stop();
    LedOn = false;
  }
  else
  {
    // toggle led activation on long click
    logger(">> DoubleClick: LED Enabled");
    LedOn = true;
  }

  managePowerLed();
}

void managePowerLed()
{
  if (LedOn)
  {
    switch (currentState)
    {
    case W_STOP:
      logger("<< LED: On");
      pwr_led.On();
      break;

    case W_PAUSE:
      if (!currentState)
      {
        logger("<< LED: Breathe Slow");
        pwr_led.Breathe(5000).Forever();
      }
      else
      {
        logger("<< LED: Breathe Fast");
        pwr_led.Breathe(1000).Forever();
      }
      break;

    case W_LEFT:
    case W_RIGHT:
      logger("<< LED: Blink");
      pwr_led.Blink(1000, 200).Forever();
      break;

    default:
      logger("<< LED: WTF?");
      pwr_led.Blink(50, 50).Forever();
      break;
    }
  }
}

void rightState()
{
  if (rthActivated)
  {
    currentState = W_RTH;
    return;
  }

  if (stateUpdated)
  {
    targetPos -= (ROT_R * ROT_STEPS);
    stepper.moveTo(targetPos);
    stateUpdated = false;
    logger("Winder moving clockwise - Remaining %d cycles", remainingCycles);
  }

  if (stepper.distanceToGo() != 0)
  {
    stepper.run();
  }
  else
  {
    currentState = W_LEFT;
  }
}

void leftState()
{
  if (rthActivated)
  {
    currentState = W_RTH;
    return;
  }

  if (stateUpdated)
  {
    targetPos += (ROT_L * ROT_STEPS);
    stepper.moveTo(targetPos);
    stateUpdated = false;
    logger("Winder moving anticlockwise");
  }

  if (stepper.distanceToGo() != 0)
  {
    stepper.run();
  }
  else
  {
    if (--remainingCycles <= 0)
    {
      currentState = W_PAUSE;
    }
    else
    {
      currentState = W_RIGHT;
    }
  }
}

void stopState()
{
  if (stateUpdated)
  {
    stateUpdated = false;
    logger("Winder stopped");
  }

  if (hasToStart)
  {
    hasToStart = false;
    currentState = W_RIGHT;
  }
}

void pauseState()
{
  if (stateUpdated)
  {
    logger("Pause state");
    StartTime = millis();
    stateUpdated = false;
  }

  long temp = millis() - StartTime;
  long delta = 60L * 1000L;

  if (hasToStart)
  {
    hasToStart = false;
    currentState = W_RIGHT;
    return;
  }

  // run once, 1 minute before PAUSE_MIN elapses
  if ((!lastPauseMinute) && (temp > (delta * long(PAUSE_MIN - 1))))
  {
    logger("<< Winder: Restarting in 1 minute");
    if (hasToContinue && LedOn)
    {
      pwr_led.Reset();
      pwr_led.Breathe(1000).Forever();
    }
    lastPauseMinute = true;
  }
  // PAUSE_MIN has elapsed && Continue
  else if (hasToContinue && temp > (delta * long(PAUSE_MIN)))
  {
    lastPauseMinute = false;
    currentState = W_RIGHT;
  }
}

void rthState()
{
  logger("Returning to home");
  stepper.moveTo(0);
  stepper.runToPosition();

  currentState = W_STOP;
  hasToContinue = false;
  hasToStart = false;
}

void logger(const char *messagePattern, ...)
{
  unsigned long hardMillis = millis();
  int runMillis = hardMillis % 1000;
  unsigned long allSeconds = hardMillis / 1000;
  int runHours = allSeconds / 3600;
  int runDays = runHours / 24;
  int secsRemaining = allSeconds % 3600;
  int runMinutes = secsRemaining / 60;
  int runSeconds = secsRemaining % 60;

  // Declare a va_list macro and initialize it with va_start
  va_list args;
  va_start(args, messagePattern);

  const char *datePattern = "[%d days %02d:%02d:%02d:%04d] ";
  char datebuf[snprintf(NULL, 0, datePattern, runDays, runHours, runMinutes, runSeconds, runMillis) + 1];
  sprintf(datebuf, datePattern, runDays, runHours, runMinutes, runSeconds, runMillis);

  char buf[vsnprintf(NULL, 0, messagePattern, args) + 1];
  vsnprintf(buf, sizeof buf, messagePattern, args);
  Serial.print(datebuf);
  Serial.println(buf);
}