/*
 * Copyright (c) 2016 Jari Saukkonen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <DallasTemperature.h>
#include <OneWire.h>
#include <TaskScheduler.h>

#include "DualVNH5019MotorShield.h"
#include "NMEASerial.h"
#include "MessageHandler.h"
#include "PowerConsumptionLog.h"
#include "Roof.h"
#include "Settings.h"
#include "Version.h"

void currentMeasurementTick();
void motorTick();
void temperatureTick();
void voltageTick();
void buttonTick();
void timerTick();
void emergencyStop();

static OneWire oneWire(PIN_ONE_WIRE_BUS);
static DallasTemperature dallasTemperature(&oneWire);
static MessageHandler handler;
static NMEASerial serial(&handler);
static DualVNH5019MotorShield motorShield;
static PowerConsumptionLog powerConsumptionLog;
static Task motorTask(100, TASK_FOREVER, &motorTick);
static Task temperatureTask(1000, TASK_FOREVER, &temperatureTick);
static Task voltageTask(1000, TASK_FOREVER, &voltageTick);
static Task buttonTask(100, TASK_FOREVER, &buttonTick);
static Task currentMeasurementTask(10, TASK_FOREVER, &currentMeasurementTick);
static Task timerTask(100, TASK_FOREVER, &timerTick);
static Scheduler taskScheduler;
static RoofState roofState = STOPPED;
static Phase phase = IDLE;
static int roofSpeed = FULL_SPEED;
static int targetRoofSpeed = 0;
static int tickCount = 0;
static int direction;
static float temperature = DEVICE_DISCONNECTED_C;
static bool temperatureRequested = false;
static float batteryVoltage = 0.0f;
static volatile bool emergencyStopPressed = false;
static volatile bool limitSwitchOpenActive = false;
static volatile bool limitSwitchCloseActive = false;
static volatile int encoderState = 0;
static volatile int encoderPosition = 0;
static unsigned long moveStartTime = 0;

static void emergencyStopISR();
static void limitSwitchCloseISR();
static void limitSwitchOpenISR();
static void encoderGate1ISR();
static void encoderGate2ISR();
static void stop(const String&);
static void open(const String&);
static void close(const String&);
static void setspeed(const String&);
static void status(const String&);
static void test(const String&);

void setup() {

    pinMode(PIN_UNUSED, INPUT);
    pinMode(PIN_BUTTON_CLOSE, INPUT);
    pinMode(PIN_BUTTON_OPEN, INPUT);
    pinMode(PIN_BUTTON_EMERGENCYSTOP, INPUT);
    pinMode(PIN_LIMITSWITCH_OPEN, INPUT);
    pinMode(PIN_LIMITSWITCH_CLOSE, INPUT);
    pinMode(PIN_ENCODER_GATE_1, INPUT);
    pinMode(PIN_ENCODER_GATE_2, INPUT);

    if (digitalRead(PIN_BUTTON_EMERGENCYSTOP) == LOW && digitalRead(PIN_BUTTON_CLOSE) == LOW) {
        encoderPosition = ENCODER_RESET_CLOSED;
        roofState = CLOSED;
    }
    if (digitalRead(PIN_BUTTON_EMERGENCYSTOP) == LOW && digitalRead(PIN_BUTTON_OPEN) == LOW) {
        encoderPosition = ENCODER_RESET_OPEN;
        roofState = OPEN;
    }

    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_EMERGENCYSTOP), emergencyStopISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMITSWITCH_CLOSE), limitSwitchCloseISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMITSWITCH_OPEN), limitSwitchOpenISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_GATE_1), encoderGate1ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_GATE_2), encoderGate2ISR, CHANGE);

    Serial.begin(57600);
    handler.registerCommand("TEST", test);
    handler.registerCommand("STATUS", status);
    handler.registerCommand("OPEN", open);
    handler.registerCommand("CLOSE", close);
    handler.registerCommand("STOP", stop);
    handler.registerCommand("SETSPEED", setspeed);

    taskScheduler.init();
    taskScheduler.addTask(motorTask);
    taskScheduler.addTask(buttonTask);
    taskScheduler.addTask(currentMeasurementTask);
    taskScheduler.addTask(voltageTask);
    taskScheduler.addTask(timerTask);

    motorShield.init();

    dallasTemperature.begin();
    if (dallasTemperature.getDeviceCount() > 0) {
        dallasTemperature.setWaitForConversion(false);
        dallasTemperature.setResolution(12);
        taskScheduler.addTask(temperatureTask);
        temperatureTask.enable();
    }

    motorTask.enable();
    buttonTask.enable();
    currentMeasurementTask.enable();
    voltageTask.enable();
    test("");
}

void logger(String msg, unsigned long timeMillis=0) {
    if (timeMillis == 0) {
        timeMillis = millis();
    }

    String logMessage = "LOG,TIME=";
    logMessage += timeMillis;
    logMessage += ",ENCODER=";
    logMessage += encoderPosition;
    logMessage += ",";
    logMessage += msg;

    serial.print(logMessage);
}

void loop() {
    taskScheduler.execute();
}

void serialEvent() {
    serial.onSerialEvent();
}

void emergencyStopISR() {
    emergencyStopPressed = (digitalRead(PIN_BUTTON_EMERGENCYSTOP) == HIGH);
}

void limitSwitchOpenISR() {
    limitSwitchOpenActive = (digitalRead(PIN_LIMITSWITCH_OPEN) == LOW);
}

void limitSwitchCloseISR() {
    limitSwitchCloseActive = (digitalRead(PIN_LIMITSWITCH_CLOSE) == LOW);
}

void updateEncoder(int previous, int current) {
    static int grayToBinary[4] = { 0, 1, 3, 2 };
    previous = grayToBinary[previous & 0x3];
    current = grayToBinary[current & 0x3];

    if (current == ((previous+1) & 0x3))
        encoderPosition++;
    if (current == ((previous-1) & 0x3))
        encoderPosition--;
}

void encoderGate1ISR() {
    int previousEncoderState = encoderState;
    encoderState = (encoderState & 0x1) | (digitalRead(PIN_ENCODER_GATE_1) == LOW ? 0x2 : 0);
    updateEncoder(previousEncoderState, encoderState);
}

void encoderGate2ISR() {
    int previousEncoderState = encoderState;
    encoderState = (encoderState & 0x2) | (digitalRead(PIN_ENCODER_GATE_2) == LOW ? 0x1 : 0);
    updateEncoder(previousEncoderState, encoderState);
}

void currentMeasurementTick() {
    powerConsumptionLog.measure(motorShield.getM1CurrentMilliamps());
}

void timerTick() {
    unsigned long t = millis();
    if ((roofState == OPENING || roofState == CLOSING) && (t - moveStartTime > MAX_MOVE_DURATION)) {
        motorShield.setM1Speed(0);
        motorShield.setM2Speed(0);
        roofSpeed = targetRoofSpeed = 0;
        roofState = ERROR;
        phase = IDLE;
        logger("ERROR=DURATION");
    }
}

void motorTick() {
    // called @ 10hz rate
    tickCount++;

    powerConsumptionLog.appendCurrentMeasurement();
    if (tickCount == 10) {
        status("");
        powerConsumptionLog.report(serial);
        tickCount = 0;
    }

    unsigned int currentThreshold = (phase == CLOSE_TIGHTLY ? MOTOR_CLOSING_CURRENT_LIMIT_MILLIAMPS : MOTOR_CURRENT_LIMIT_MILLIAMPS);
    if (powerConsumptionLog.isOverload(currentThreshold)) {
        motorShield.setM1Speed(0);
        roofSpeed = targetRoofSpeed = 0;
        if (roofState == CLOSING && phase == CLOSE_TIGHTLY) {
            roofState = CLOSED;
            phase = IDLE;
            encoderPosition = 0;
            logger(String("STATE=CLOSED,DURATION=") + (millis()-moveStartTime));
        } else if (roofState != CLOSED && roofState != OPEN) {
            roofState = ERROR;
            phase = IDLE;
            logger("ERROR=OVERCURRENT");
        }
    }

    if (motorShield.getM1Fault()) {
        motorShield.setM1Speed(0);
        roofSpeed = targetRoofSpeed = 0;
        roofState = ERROR;
        phase = IDLE;
        logger("ERROR=MOTOR1FAULT");
    }

    if (emergencyStopPressed) {
        motorShield.setM1Speed(0);
        motorShield.setM2Speed(0);
        roofSpeed = targetRoofSpeed = 0;
        if (phase != IDLE) {
            logger("CMD=EMERGENCYSTOP");
        }
        phase = IDLE;
        roofState = STOPPED;
        return;
    }

    if (limitSwitchOpenActive) {
        if ((phase == RAMP_UP || phase == MOVE_UNTIL_NEAR) && roofState == OPENING) {
            logger("LIMIT=SWITCH_OPENING");
            phase = RAMP_DOWN;
            targetRoofSpeed = 0;
        }
        limitSwitchOpenActive = false;
    }

    if (encoderPosition >= ENCODER_MAX_POSITION) {
        if ((phase == RAMP_UP || phase == MOVE_UNTIL_NEAR) && roofState == OPENING) {
            logger("LIMIT=ENCODER_OPENING");
            phase = RAMP_DOWN;
            targetRoofSpeed = 0;
        }
    }

    if (limitSwitchCloseActive) {
        if ((phase == RAMP_UP || phase == MOVE_UNTIL_NEAR) && roofState == CLOSING) {
            logger("LIMIT=SWITCH_CLOSING");
            phase = RAMP_DOWN;
            targetRoofSpeed = CLOSING_SPEED;
        }
        limitSwitchCloseActive = false;
    }

    if (encoderPosition <= ENCODER_MIN_POSITION) {
        if ((phase == RAMP_UP || phase == MOVE_UNTIL_NEAR) && roofState == CLOSING) {
            phase = RAMP_DOWN;
            targetRoofSpeed = CLOSING_SPEED;
        }
    }

    if (targetRoofSpeed != roofSpeed) {
        roofSpeed += RAMP_DELTA * (targetRoofSpeed > roofSpeed ? 1 : -1);
        if (abs(roofSpeed-targetRoofSpeed) <= (RAMP_DELTA/2)) {
            roofSpeed = targetRoofSpeed;
        }
    }

    switch (phase) {
        case RAMP_UP: {
            motorShield.setM1Speed(roofSpeed * direction);
            if (roofSpeed == targetRoofSpeed) {
                phase = MOVE_UNTIL_NEAR;
            }
            break;
        }
        case RAMP_DOWN: {
            motorShield.setM1Speed(roofSpeed * direction);
            if (roofSpeed == targetRoofSpeed) {
                phase = IDLE;
                if (roofState == STOPPING)
                {
                    logger("STATE=STOPPED");
                    roofState = STOPPED;
                }
                else if (roofState == OPENING)
                {
                    logger(String("STATE=OPENED,DURATION=") + (millis()-moveStartTime));
                    roofState = OPEN;
                }
                else if (roofState == CLOSING)
                {
                    logger("STATE=TIGHTENING");
                    phase = CLOSE_TIGHTLY;
                }
            }
            break;
        }
        case CLOSE_TIGHTLY:
            // just coast ahead, until we trigger current limit when the roof hits the wall
            break;
    }
}

void buttonTick() {
    if (digitalRead(PIN_BUTTON_OPEN) == LOW && phase == IDLE) {
        open("");
    }
    if (digitalRead(PIN_BUTTON_CLOSE) == LOW && phase == IDLE) {
        close("");
    }
}

void temperatureTick() {
    // called @ 1hz

    if (temperatureRequested) {
        temperature = dallasTemperature.getTempCByIndex(0);
    }
    dallasTemperature.requestTemperaturesByIndex(0);
    temperatureRequested = true;
}

void voltageTick() {
    batteryVoltage = analogRead(PIN_BATTERY_VOLTAGE) * REFERENCE_VOLTAGE / 1024.0 * BATTERY_RESISTOR_DIVISOR;
}

void test(const String&) {
    serial.print(String(BOARD_NAME) + String(",VER=") + String(KOMAROOF_VERSION));
}

void open(const String&) {
    if (roofState == CLOSED || roofState == STOPPED) {
        roofState = OPENING;
        phase = RAMP_UP;
        targetRoofSpeed = FULL_SPEED;
        direction = MOTOR_POLARITY;
        moveStartTime = millis();
        logger("CMD=OPEN", moveStartTime);
        serial.print("OPEN,OK");
    } else {
        serial.print("OPEN,ERR");
    }
}

void close(const String&) {
    if (roofState == OPEN || roofState == STOPPED) {
        roofState = CLOSING;
        phase = RAMP_UP;
        targetRoofSpeed = FULL_SPEED;
        direction = -MOTOR_POLARITY;
        moveStartTime = millis();
        logger("CMD=CLOSE", moveStartTime);
        serial.print("CLOSE,OK");
    } else {
        serial.print("CLOSE,ERR");
    }
}

void stop(const String&) {
    if (phase != IDLE) {
        roofState = STOPPING;
        phase = RAMP_DOWN;
        targetRoofSpeed = 0;
    }
    if (roofState == ERROR) {
        roofState = STOPPED;
        phase = IDLE;
    }
    logger("CMD=STOP");
    serial.print("STOP,OK");
}

void status(const String&) {
    static const char* roofStateNames[] = { "STOPPED", "OPEN", "CLOSED", "OPENING", "CLOSING", "STOPPING", "ERROR" };
    static const char* phaseNames[] = { "IDLE", "RAMP_UP", "MOVE_UNTIL_NEAR", "RAMP_DOWN", "CLOSE_TIGHTLY" };

    String message = "STATUS,";
    message += "ROOF=";
    message += roofStateNames[roofState];
    message += ",PHASE=";
    message += phaseNames[phase];
    message += ",ENCODER=";
    message += encoderPosition;
    message += ",BATTERYVOLTAGE=";
    message += (int)(batteryVoltage);
    message += ".";
    message += (int)(batteryVoltage*10) % 10;
    message += (int)(roundf(batteryVoltage*100)) % 10;
    message += ",SPEED=";
    message += roofSpeed;
    if (temperature != (float)DEVICE_DISCONNECTED_C) {
        message += ",TEMP1=";
        message += (int)(temperature);
        message += ".";
        message += (int)(temperature*10) % 10;
        message += (int)(roundf(temperature*100)) % 10;
    }
    serial.print(message);
}

void setspeed(const String& speedAsString) {
    targetRoofSpeed = atoi(speedAsString.c_str());
    serial.print("SETSPEED,OK");
}
