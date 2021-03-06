# KomaRoof

An Arduino sketch for controlling a vnh5019 motor shield for a roll-on/off roof. The sketch includes a serial interface using [NMEA commands](http://www.hhhh.org/wiml/proj/nmeaxor.html).

The roof consists a roof that moves back and forth on a set of rails and a lock functionality using a linear actuator.

The sketch supports a 1-wire temperature sensor connected

## Configuration

The configurable parameters can be modified in `Settings.h`.

`BOARD_NAME` - Identifier for the board, returned as a response to the TEST command.  
`PIN_ONE_WIRE_BUS` - Pin number for the temperature sensor bus  
`PIN_BUTTON_EMERGENCYSTOP` - Pin number for the emergency stop button. This must be an interrupt-capable pin.  
`PIN_BUTTON_OPEN` - Pin number for the 'Open' button  
`PIN_BUTTON_CLOSE` - Pin number for the 'Close' button  
`PIN_LIMITSWITCH_OPEN` - Pin number for the 'Open' limit switch
`PIN_LIMITSWITCH_CLOSE` - Pin number for the 'Close' limit switch
`PIN_ENCODER_GATE_1` - Pin number for the first encoder gate
`PIN_ENCODER_GATE_2` - Pin number for the second encoder gate
`PIN_BATTERY_VOLTAGE A2` - Pin number for connecting a 1-wire temperature sensor
`MOTOR_CURRENT_LIMIT_MILLIAMPS 2500` - Current limit that will trigger an error condition
`MOTOR_POLARITY` - Change 1 to -1 to invert the motor movement direction.  
`FULL_SPEED` - Maximum motor speed from 0 to 400
`CLOSING_SPEED` - Motor speed to use for final tightening of the roof
`RAMP_LENGTH` - The duration during which the motor ramps up to FULL_SPEED. In motor ticks (1/10th second).  
`ENCODER_MAX_POSITION` - Encoder position where the roof will be stopped when opening
`ENCODER_MIN_POSITION` - Encoder position where the roof will slow down when closing
`MAX_MOVE_DURATION` - Maximum duration (milliseconds) for any movement until error is triggered
`RAMP_DELTA` - Amount to change the motor speed per tick

## Command set

### TEST

Queries the board name and version number. This message is also sent automatically when the serial port is opened.

Example: `$TEST*16`  
Response: `$KOMAROOF,VER=1.0*63`

### STATUS

Queries the current status of the roof.

Example: `$STATUS*14`  
Response: `$STATUS,ROOF=OPEN,PHASE=,TEMP1=11.22*74` and `$POWER,12818,12920,13056,13158,13260,13396,13532,13634,13770,13906*58`

`ROOF` - Status of the roof. The valid states are `OPEN`, `CLOSED`, `STOPPED`, `OPENING`, `CLOSING`, `STOPPING` and `ERROR`.  
`PHASE` - State of the motor control loop. Valid states are `IDLE`, `RAMP_UP`, `MOVE_UNTIL_NEAR`, `RAMP_DOWN` and `CLOSE_TIGHTLY`.  
`TEMP1` - Latest temperature measurement.

The `POWER` response contains the latest power draw data from the last 10 ticks (1/10th second).

### OPEN

Unlocks the lock and opens the roof.

Example: `$OPEN*14`  
Response: `$OPEN,OK*3C`  

### CLOSE

Closes the roof and locks the lock.

Example: `$CLOSE*56`  
Response: `$CLOSE,OK*7E`  

### STOP

Stops the roof if it is moving.

Example: `$STOP*18`  
Response: `$STOP,OK*30`  
