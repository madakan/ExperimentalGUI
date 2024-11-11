// Code to stimulate the tibial nerve of both legs for the Spinal Adaptation
// study to measure H-reflexes during split-belt adaptation. Updated to
// accept Serial input from MATLAB to indicate whether to stimulate on the
// current stride. Includes the gait event detection state machine and
// removes speed dependence (i.e., the need to manually update the treadmill
// speeds before each trial) by storing the single stance duration of the
// previous two strides.
// date (started): 26 Mar. 2024
// author(s): SL, NWB

// initialize variables to track gait events and phases
bool new_stanceL = false;         // is current step left foot stance?
bool new_stanceR = false;         // is current step right foot stance?
bool old_stanceL = false;         // is previous step left foot stance?
bool old_stanceR = false;         // is previous step right foot stance?
bool LHS = false;                 // is there a left heel strike event?
bool RHS = false;                 // is there a right heel strike event?
bool LTO = false;                 // is there a left toe off event?
bool RTO = false;                 // is there a right toe off event?
unsigned long timeLHS = 0;        // time of most recent LHS events
unsigned long timeRHS = 0;        // time of most recent RHS events
unsigned long timeLTO = 0;        // time of most recent LTO events
unsigned long timeRTO = 0;        // time of most recent RTO events
unsigned long stimDelayL = 100;   // initialize RTO delay to 100 ms
unsigned long stimDelayR = 100;   // initialize LTO delay to 100 ms
float stimPercent = 0.65;         // Percentage of single stance phase
unsigned long durSSL[2] = {0, 0}; // two left single stance durations
unsigned long durSSR[2] = {0, 0}; // two right single stance durations
bool canStim = false;             // is stimulation allowed at this time?
bool shouldStimL = false;         // should stimulate left leg this stride
bool shouldStimR = false;         // should stimulate right leg this stride
// gait phase: 0 = initial double support, 1 = single L support, 2 = single R
// support, 3 = double support from single L support, 4 = double support from
// single R support
int phase = 0;
int LstepCount = 0; // left step counter
int RstepCount = 0; // right step counter

// z-axis force threshold in DAQ bits (estimated by observing the z-axis
// force plate voltages during walking and converting to bits based on 10-bit
// ADC; we also verified that the Arduino-read bits are comparable to what we
// expect given the Vicon-read voltages, although we found a small bias)
// based on the 30 N force threshold implemented in MATLAB
// TODO: it may be necessary to increase to account for left FP noise and
// higher baud rate in Arduino than in MATLAB (more susceptible to false gait
// event detection))
const int threshFz = 5;           // force thresh. bits to detect stance phase
const int stimPulseDuration = 20; // stimulation pulse duration [ms]

// right and left stimulator pin configurations
const int rightSensorPin = A1;
const int rightOutputPin = 8;
const int rightViconOut = 11;
const int leftSensorPin = A0;
const int leftOutputPin = 9;
const int leftViconOut = 12;

void setup()
{
  Serial.begin(9600);
  Serial.println("START");
}

void loop()
{
  processSerialCommands();
  updateGaitEventStateMachine();
  triggerStimulation();
}

void processSerialCommands()
{
  // check for input from MATLAB
  if (Serial.available() > 0)
  {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "RESET_COUNTERS")
    {
      LstepCount = 0;
      RstepCount = 0;
      Serial.println("Counters reset.");
    }
    else if (command == "STIM_RIGHT")
    {
      shouldStimR = true;
      Serial.println("Right leg stimulation enabled.");
    }
    else if (command == "STIM_LEFT")
    {
      shouldStimL = true;
      Serial.println("Left leg stimulation enabled.");
    }
    else
    {
      Serial.println("Unknown command received.");
    }
  }
}

void updateGaitEventStateMachine()
{
  // implement gait event state machine to update gait phase
  old_stanceL = new_stanceL;
  old_stanceR = new_stanceR;

  // read z-axis force plate sensor values to detect new stance phase
  float rightSensorVal = analogRead(rightSensorPin);
  float leftSensorVal = analogRead(leftSensorPin);
  // current step is stance if foot in contact with force plate
  new_stanceL = leftSensorVal > threshFz;  // detect left stance
  new_stanceR = rightSensorVal > threshFz; // detect right stance
  LHS = new_stanceL && !old_stanceL;       // left heel strike detection
  RHS = new_stanceR && !old_stanceR;       // right heel strike detection
  LTO = !new_stanceL && old_stanceL;       // left toe off detection
  RTO = !new_stanceR && old_stanceR;       // right toe off detection

  // gait event state machine to determine phase transitions
  switch (phase)
  {
  case 0:    // initial double support phase
    if (RTO) // if right toe off, enter left single stance
    {
      phase = 1;
      RstepCount++;       // increment right step count
      timeRTO = millis(); // store time of RTO event
      Serial.print("Right Step: ");
      Serial.println(RstepCount);
    }
    else if (LTO) // if left toe off, enter right single stance
    {
      phase = 2;
      LstepCount++;       // increment left step count
      timeLTO = millis(); // store time of LTO event
      Serial.print("Left Step: ");
      Serial.println(LstepCount);
    }
    break;

  case 1:    // left single stance
    if (RHS) // if right heel strike, enter double stance
    {
      phase = 3;
      // TODO: consider reimplementing if beneficial
      // if (LTO) {
      //   // if missed entire phase switch due to short double stance
      //   phase = 2;
      //   LstepCount++;
      //   timeLTO = millis();
      // }
      // RHS marks the end of single stance L
      durSSL[0] = durSSL[1]; // overwrite previous SSL duration
      // compute duration of left leg single stance phase
      durSSL[1] = millis() - timeRTO;
      // delay of left leg stimulation from RTO is mean of single stance
      // duration divided by two (since targeting mid-point of single
      // stance for stimulus pulse)
      // TODO: consider weighting most recent SSL duration more heavily
      // (e.g., 75% since likely more predictive)
      stimDelayL = ((durSSL[0] / 3) + (2 * durSSL[1] / 3)) * stimPercent;
      timeRHS = millis(); // update current RHS time
      canStim = true;     // enable stimulation
    }
    break;

  case 2:    // right single stance
    if (LHS) // if left heel strike, enter double stance
    {
      phase = 4;
      // TODO: consider reimplementing if beneficial
      // if (RTO) {
      //   // if missed entire phase switch due to short double stance
      //   phase = 1;
      //   RstepCount++;
      //   timeRTO = millis();
      // }
      // LHS marks the end of single stance R
      durSSR[0] = durSSR[1]; // overwrite previous SSR duration
      // compute duration of right leg single stance phase
      durSSR[1] = millis() - timeLTO;
      // delay of right leg stimulation from LTO is mean of single stance
      // duration divided by two (since targeting mid-point of single
      // stance for stimulus pulse)
      stimDelayR = ((durSSR[0] / 3) + (2 * durSSR[1] / 3)) * stimPercent;
      timeLHS = millis(); // update current LHS time
      canStim = true;     // enable stimulation
    }
    break;

  case 3:    // double stance after left single stance
    if (LTO) // if left toe off, enter right single stance
    {
      phase = 2;
      timeLTO = millis(); // update current LTO time
      LstepCount++;       // increment left step count
      Serial.print("Left Step: ");
      Serial.println(LstepCount);
    }
    break;

  case 4:    // double stance after right single stance
    if (RTO) // if right toe off, enter left single stance
    {
      phase = 1;
      timeRTO = millis(); // update current RTO time
      RstepCount++;       // increment right step count
      Serial.print("Right Step: ");
      Serial.println(RstepCount);
    }
    break;
  }
}

void triggerStimulation()
{
  unsigned long timeSinceLTO = millis() - timeLTO;
  unsigned long timeSinceRTO = millis() - timeRTO;

  // right leg stimulation trigger conditions
  // use contralateral leg (i.e., LHS - LTO) to determine R mid-single stance
  if (phase == 2 && canStim && shouldStimR && timeSinceLTO >= stimDelayR)
  {
    Serial.println("Right Stimulation Triggered");
    digitalWrite(rightOutputPin, HIGH);
    digitalWrite(rightViconOut, HIGH);
    // TODO: consider removing delay here too to allow state machine to
    // continue running for these 20 ms
    delay(stimPulseDuration);
    digitalWrite(rightOutputPin, LOW);
    digitalWrite(rightViconOut, LOW);
    canStim = false;
    shouldStimR = false; // Reset trigger for next cycle
  }

  // left leg stimulation trigger conditions
  // use contralateral leg (i.e., RHS - RTO) to determine L mid-single stance
  if (phase == 1 && canStim && shouldStimL && timeSinceRTO >= stimDelayL)
  {
    Serial.println("Left Stimulation Triggered");
    digitalWrite(leftOutputPin, HIGH);
    digitalWrite(leftViconOut, HIGH);
    // TODO: consider removing delay here too to allow state machine to
    // continue running for these 20 ms
    delay(stimPulseDuration);
    digitalWrite(leftOutputPin, LOW);
    digitalWrite(leftViconOut, LOW);
    canStim = false;
    shouldStimL = false; // Reset trigger for next cycle
  }
}
