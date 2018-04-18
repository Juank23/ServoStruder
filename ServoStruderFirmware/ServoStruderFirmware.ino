#include <Encoder.h> //include encoder library
#include <PID_v1.h> //include PID library
#include "GPIO.h" //include GPIO library

// ServoStruder Firmware
// Developed by SZiv, released under GPLv3
// Uses PJRC's Encoder library, mikaelpatel's Arduino-GPIO library, as well as Brett Beauregard's PID library. Kudos to them for their hard work!
// Be sure to check out http://reprap.org/wiki/RAMPSSB for more information.

//if you have a better idea of control systems than I, please let me know so I can update the firmware on the official github. It does work, but it has some wobbling error.

//Configuration variables 

//Change these values for your hardware!
#define quadrature true //Is this a quadrature encoder? If so, make true. If you are using a single encoder motor, you will lose a lot of accuracy, but make true (Coming soon!).  
#define cpr 100.0 //How many counts per revolution? This should be written on the motor datasheet, but its best to test it anyway, because sometimes quadrature encoders have 4 times the noted ticks, because each quadrature counts as 4.
#define mmpr 30 //Millimeters per revolution. This is the DC motor version of "steps per mm" and varies with hardware and gearing. this is easy to test by marking your carriage, rotating the encoder shaft 1 full revolution and measure the distance the carriage moved. 
#define steps_per_mm 80 //What are the steps per mm that the firmware on the RAMPS is using?
#define kp 10 //Proportional Constant. Used for PID control. Tune this to your hardware to get better accuracy. 
#define ki 0 //Integral Constant. Used for PID control. Tune this to your hardware to get better accuracy.
#define kd 1 //Derivative Constant. Used for PID control. Tune this to your hardware to get better accuracy.
#define diagnostic true //If true, sets machine to diagnostic. Prints out location values over serial and can do PID autotuning. Causes a heart attack and bad location data if kept on, so uses it to get PID values and then turn it off.
#define endstopinverted false //By default, endstops are NO. If you are using an NC endstop (like an optical one), change to true.
#define maxpwm 255 //Max PWM you can use on the motor and still read the encoder. For most small encoders that are 100 counts per rev or so, or the speed is low, you can go up to 255, but if you are using faster motors with higher encoder counts, the arduino may freeze up unless you lower this number.  
#define minpwm 110 //Min PWM you can use to make the motor spin.
#define acceptable_error 0.02 //acceptable error for positioning in mm. so long at the carriage is within this value, it will stop. Lowering this value increases accuracy, but my cause wobbling. 

//end of configuration variables



//Define Pins
#define dir 1
#define stp 0
#define en 4
#define pwmout 5
#define encod1 2
#define encod2 3
#define endstop 7
//direction pin setup
GPIO<BOARD::D8> direction1;
GPIO<BOARD::D9> direction2;

//encoder Setup
Encoder myEnc(encod1, encod2); //declare encoder

//PID Setup
double Setpoint = 0;
double Input;
double Output;
PID pidController(&Input, &Output, &Setpoint, kp, ki, kd, DIRECT); //setup PID Controller

//Global Variables
volatile int direct = 0; //What direction are we going? 0 is towards home, 1 is away
//volatile float target = 0; //What is out target location in mm? Updated with new values every step.
//volatile float current = 0; //where are we now? Reset at home
volatile long ticks = 0; //the number of encoder ticks from home we are.
volatile long counts = 0; //number of encoder ticks we are trying to target.
float error = acceptable_error; //acceptable error in mm of final position.
float CPR = cpr; //turn #define into a float
float MMPR = mmpr; //turn #define into a float
float spmm = steps_per_mm; //turn #define into a float
float target = 0;
float current = 0;

void setup()
{
  pinMode(endstop, INPUT);
  pinMode(stp, INPUT);
  pinMode(dir, INPUT);
  pinMode(en, INPUT);
  pinMode(pwmout, OUTPUT);
  direction1.output();
  direction2.output();
  
  //start diagnostic mode
  if (diagnostic == true)
  {
    Serial.begin(250000);
    Serial.println("Diagnostic Mode is Enabled. Turn this off before running the real thing!");
  }

  //set endstop inturrupts
  if (endstopinverted == false)
  {
    attachInterrupt(digitalPinToInterrupt(endstop), reset, RISING); //Reset when the endstop goes from low to high
  }
  else
  {
    attachInterrupt(digitalPinToInterrupt(endstop), reset, FALLING); //Reset when the endstop goes from high to low
  }

  //set encoder inturrupt
  attachInterrupt(digitalPinToInterrupt(encod1), incretick, RISING); //Add one more step to the total

  //set step inturrupt
  attachInterrupt(digitalPinToInterrupt(stp), increstep, RISING); //Add one more step to the total
  //set DIR inturrupt
  attachInterrupt(digitalPinToInterrupt(dir), directionchange, CHANGE); //flip direction when


  //turn the PID on
  pidController.SetOutputLimits(minpwm, maxpwm); //constrain PID outputs so that it makes the motor run at all levels
  pidController.SetMode(AUTOMATIC);

  //toggle direction
  toggledirection(); //toggle the direction on the DC motor board.
}

void loop()
{
  target = counts * (1 / spmm); //this is the location in mm we want to be at. (number of steps from home * Steps/mm)
  current = ticks * MMPR * (1 / CPR); //we are currently at this many mm (Encoder ticks from home * mm per revolution / encoder ticks per revolution)
  Input = abs(target - current) * -1; //Input of the PID is negative of the error
  toggledirection(); //check which direction we are going using the direction pin
  pidController.Compute();   //do the PID computation

  if ((digitalRead(en) == 1)||(abs(Input) < error)) //if the motors are disabled or we are within the error of our target, shut motor off
  {
    analogWrite(pwmout, 0); //shut off motors
  }
  else
  {
    analogWrite(pwmout, Output); //output the PID result
  }

  //print location and diagnostic data
  if (diagnostic == true)
  {
    Serial.print("Target:");
    Serial.print(target);
    Serial.print("mm Actual:");
    Serial.print(current);
    Serial.print("mm Steps:");
    Serial.print(counts);
    Serial.print(" Enc_Ticks:");
    Serial.print(ticks);
    Serial.print(" Error:");
    Serial.print(Input);
    Serial.print(" Direction:");
    Serial.print(direct);
    Serial.print(" PWM:");
    Serial.println(Output);
  }
}

void reset()
{
  //reset supposed an actual position
  counts = 0; //reset out encoder counts to 0

  //Flip direction to make sure we go forward.
  direction1 = LOW;
  direction2 = HIGH;
}

void increstep()
{
  //add or remove a step from the count
  if (direct == 0) //if we are going backwards
  {
    counts--;
  }
  else
  {
    counts++;
  }
}

void incretick()
{
  //add or remove encoder tick from the count
  if (direct == 0)
  {
    ticks--;
  }
  else
  {
    ticks++;
  }
}


void directionchange()
{
  //check direction and tell the motors
  direct = digitalRead(dir);
  toggledirection();
}

void toggledirection()
{
  //make sure the dc motor is moving in the direction we want it to be
  if (current > target)
  {
    direction2 = LOW;
    direction1 = HIGH;
  }
  else if (current < target)
  {
    direction1 = LOW;
    direction2 = HIGH;  
  }
}


