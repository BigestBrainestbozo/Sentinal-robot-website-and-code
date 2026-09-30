#include <Servo.h>

enum RoverCommand {
  STOP,
  FORWARD,
  BACKWARD,
  LEFT,
  RIGHT,
  FORWARD_LEFT,
  FORWARD_RIGHT,
  BACKWARD_LEFT,
  BACKWARD_RIGHT
};

enum RoverMode {
  MANUAL,
  AUTONOMOUS
};

enum AutoState {
  IDLE,
  DRIVING,
  REVERSING,
  SCANNING_LEFT,
  SCANNING_CENTER,
  SCANNING_RIGHT,
  DECIDING,
  TURNING,
  FAILSAFE,
  RETRACING,
  RECOVERY_SCANNING
};

const int TRIG_PIN = 7;
const int ECHO_PIN = 6;

const int SERVO_PIN = 9;

const int PWMA_PIN = 5;
const int AIN1_PIN = 22;
const int AIN2_PIN = 23;

const int PWMB_PIN = 4;
const int BIN1_PIN = 24;
const int BIN2_PIN = 25;

const int STBY_PIN = 26;

const int SERVO_LEFT = 150;
const int SERVO_CENTER = 90;
const int SERVO_RIGHT = 30;

Servo sensorServo;

int currentServoPosition = SERVO_CENTER;

struct RoverSettings {
  float stopDistanceCM;
  float minimumTurnClearanceCM;

  unsigned long distanceInterval;
  unsigned long reverseDuration;
  unsigned long turnDuration;

  int forwardSpeed;
  int reverseSpeed;
  int turnSpeed;
  int slowSpeed;
};

RoverSettings settings = {
  20.0,
  25.0,

  300,
  400,
  700,
  
  255,
  255,
  180,
  120
};

RoverCommand currentCommand = STOP;
RoverMode currentMode = MANUAL;
AutoState autoState = IDLE;

unsigned long lastDistanceCheck = 0;
unsigned long stateStartTime = 0;
unsigned long missionStartTime = 0;

float latestDistance = -1;
float leftDistance = -1;
float rightDistance = -1;

bool turnRightNext = true;

int obstacleCount = 0;
int leftTurnCount = 0;
int rightTurnCount = 0;
long distanceReadingCount = 0;

int blockedPathRetryCount = 0;
int sensorFailureCount = 0;
int failsafeCount = 0;
int successfulAvoidanceCount = 0;


const int MAX_BLOCKED_PATH_RETRIES = 3;

const char* failsafeReason = "NONE";

const int PATH_HISTORY_SIZE = 20;

RoverCommand pathHistory[PATH_HISTORY_SIZE];

int pathHistoryCount = 0;

int retraceIndex = -1;

void sendState(const char* state) {
  Serial.print("STATE:");
  Serial.println(state);
}

void sendEvent(const char* event) {
  Serial.print("EVENT:");
  Serial.println(event);
}

void sendMode(const char* mode) {
  Serial.print("MODE:");
  Serial.println(mode);
}

void sendDistance(float distance) {
  Serial.print("DIST:");

  if (distance < 0) {
    Serial.println("NONE");
  } else {
    Serial.println(distance, 1);
  }
}

void sendCommand(const char* command) {
  Serial.print("CMD:");
  Serial.println(command);
}

void rememberMovement (RoverCommand command) {
  if (command == STOP) {
    return;
  }

  if (pathHistoryCount < PATH_HISTORY_SIZE) {
    pathHistory[pathHistoryCount] = command;
    pathHistoryCount++;

    return;
  }

//history full
//remove olderest movment
for (int  i= 1; i < PATH_HISTORY_SIZE; i++) {
  pathHistory[i - 1] = pathHistory[i];
}

//save newest
pathHistory[PATH_HISTORY_SIZE - 1] = command;
 
RoverCommand oppositeMovement(RoverCommand command) {
  switch (command) {

    case FORWARD:
     return BACKWARD;

    case BACKWARD:
     return FORWARD;

    case LEFT:
     return RIGHT;

    case RIGHT:
     return LEFT;

    case FORWARD_LEFT:
     return BACKWARD_RIGHT;

    case FORWARD_RIGHT:
     return BACKWARD_LEFT;

    case BACKWARD_LEFT:
     return FORWARD_RIGHT;

    case BACKWARD_RIGHT:
     return FORWARD_LEFT;

    case STOP:
    default:
     return STOP;
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(TRIG_PIN, LOW);

  pinMode(PWMA_PIN, OUTPUT);
  pinMode(AIN1_PIN, OUTPUT);
  pinMode(AIN2_PIN, OUTPUT);

  pinMode(PWMB_PIN, OUTPUT);
  pinMode(BIN1_PIN, OUTPUT);
  pinMode(BIN2_PIN, OUTPUT);

  pinMode(STBY_PIN, OUTPUT);
  digitalWrite(STBY_PIN, HIGH);

  sensorServo.attach(SERVO_PIN);
  currentServoPosition = SERVO_CENTER;
sensorServo.write(currentServoPosition);
  delay(500);

  stopMotors();

  Serial.println("=== Mars Rover Mk I ===");
  Serial.println("1 = Manual Mode");
  Serial.println("2 = Autonomous Mode");
  Serial.println("");
  Serial.println("Manual Controls:");
  Serial.println("W = Forward");
  Serial.println("A = Left");
  Serial.println("S = Backward");
  Serial.println("D = Right");
  Serial.println("Q = Forward Left");
  Serial.println("E = Forward Right");
  Serial.println("Z = Backward Left");
  Serial.println("C = Backward Right");
  Serial.println("X = Emergency Stop");
  Serial.println("F = Read Distance");
  Serial.println("P = Mission Statistics");
  Serial.println("I = Current Status");
  Serial.println("R = Reset Failsafe");
  Serial.println("");

  sendMode("MANUAL");
  sendState("IDLE");

  missionStartTime = millis();
}

void loop() {
  checkSerialInput();

  if (
    millis() - lastDistanceCheck >=
    settings.distanceInterval
  ) {
    lastDistanceCheck = millis();

    latestDistance = readDistanceCM();

    if (latestDistance >= 0) {
      distanceReadingCount++;
    }
  }

  if (currentMode == MANUAL) {
    checkManualSafety();
  } else {
    runAutonomousMode();
  }
}

void checkSerialInput() {
  if (Serial.available() <= 0) {
    return;
  }

  char input = Serial.read();

  if (input == '\n' || input == '\r') {
    return;
  }

  handleInput(input);
}

void handleInput(char input) {
  input = toupper(input);

  if (input == '1') {
    currentMode = MANUAL;
    autoState = IDLE;

    stopRover();

    sendMode("MANUAL");
    sendState("IDLE");

    return;
  }

  if (input == '2') {
    currentMode = AUTONOMOUS;
    autoState = DRIVING;

    stopRover();

    sendMode("AUTONOMOUS");
    sendState("DRIVING");

    return;
  }

  if (input == 'X') {
    currentMode = MANUAL;
    autoState = IDLE;

    stopRover();

    Serial.println("EMERGENCY STOP");

    sendMode("MANUAL");
    sendState("IDLE");

    return;
  }

  if (input == 'F') {
    printDistance();
    return;
  }

  if (input == 'P') {
    printMissionStatistics();
    return;
  }

    if (input == 'I') {
    printCurrentStatus();
    return;
  }

  if (input = 'R') {
    if (autoState == FAILSAFE) {
      Serial.println("RESETTING FAILSAFE");

      failsafeReason = "NONE";
      blockedPathRetryCount = 0;

      currentMode = MANUAL;
      autoState = IDLE;

      stopRover();
      
      sendMode("MANUAL");
      sendState("IDLE");
    } else {
      Serial.println("NO FAILSAFE TO RESET");
  }

  return;
}

  if (currentMode != MANUAL) {
    Serial.println("Press 1 for Manual Mode");
    return;
  }

  RoverCommand requestedCommand =
    commandFromCharacter(input);

  if (requestedCommand == currentCommand) {
    return;
  }

  currentCommand = requestedCommand;

  executeCommand(currentCommand);
}

RoverCommand commandFromCharacter(char input) {
  switch (input) {
    case 'W':
      return FORWARD;

    case 'S':
      return BACKWARD;

    case 'A':
      return LEFT;

    case 'D':
      return RIGHT;

    case 'Q':
      return FORWARD_LEFT;

    case 'E':
      return FORWARD_RIGHT;

    case 'Z':
      return BACKWARD_LEFT;

    case 'C':
      return BACKWARD_RIGHT;

    default:
      return currentCommand;
  }
}

void executeCommand(RoverCommand command) {
  if (
    latestDistance >= 0 &&
    latestDistance < settings.stopDistanceCM &&
    isForwardCommand(command)
  ) {
    Serial.print("BLOCKED AT ");
    Serial.print(latestDistance, 1);
    Serial.println(" CM - STOP");

    stopRover();

    return;
  }

  switch (command) {
    case FORWARD:
      moveForward();
      break;

    case BACKWARD:
      moveBackward();
      break;

    case LEFT:
      turnLeft();
      break;

    case RIGHT:
      turnRight();
      break;

    case FORWARD_LEFT:
      moveForwardLeft();
      break;

    case FORWARD_RIGHT:
      moveForwardRight();
      break;

    case BACKWARD_LEFT:
      moveBackwardLeft();
      break;

    case BACKWARD_RIGHT:
      moveBackwardRight();
      break;

    case STOP:
    default:
      stopRover();
      break;
  }
}

bool isForwardCommand(RoverCommand command) {
  return (
    command == FORWARD ||
    command == FORWARD_LEFT ||
    command == FORWARD_RIGHT
  );
}

void checkManualSafety() {
  if (latestDistance < 0) {
    return;
  }

  if (
    latestDistance < settings.stopDistanceCM &&
    isForwardCommand(currentCommand)
  ) {
    Serial.print("OBSTACLE AT ");
    Serial.print(latestDistance, 1);
    Serial.println(" CM - STOP");

    stopRover();
  }
}

void startRetracing() {
  if (pathHistoryCount <= 0) {
    Serial.println("RETRACE: NO PATH HISTORY");
}

void enterFailsafe (const char* reason) {
  failsafeReason = reason;
  failsafeCount++;

  stopRover();
  autoState = FAILSAFE;

  Serial.println();
  Serial.println("!!! FAILSAFE !!!");

  Serial.print("REASON: ");
  Serial.println(failsafeReason);

  sendState("FAILSAFE");
}

void runAutonomousMode() {
  if (autoState == IDLE) {
    stopRover();
    return;
  }
    
    if (autoState == FAILSAFE) {
      stopRover();
      return;
    }
  

  if (autoState == REVERSING) {
    if (
      millis() - stateStartTime >=
      settings.reverseDuration
    ) {
      stopRover();

      autoState = SCANNING_LEFT;

      sendState("SCANNING_LEFT");
    }

    return;
  }

  if (autoState == SCANNING_LEFT) {
    scanLeft();

    autoState = SCANNING_CENTER;

    sendState("SCANNING_CENTER");

    return;
  }

  if (autoState == SCANNING_CENTER) {
    scanCenter();

    autoState = SCANNING_RIGHT;

    sendState("SCANNING_RIGHT");

    return;
  }

  if (autoState == SCANNING_RIGHT) {
    scanRight();

    autoState = DECIDING;

    sendState("DECIDING");

    return;
  }

  if (autoState == DECIDING) {
    chooseBestDirection();

    if (autoState == REVERSING || autoState == FAILSAFE) {
      return;
    }

    autoState = TURNING;
    stateStartTime = millis();

    sendState("TURNING");

    return;
  }

  if (autoState == TURNING) {
    if (
      millis() - stateStartTime >=
      settings.turnDuration
    ) {
      stopRover();

      autoState = DRIVING;

      sendState("DRIVING");
    }

    return;
  }

  if (latestDistance < 0) {
    Serial.println(
      "AUTONOMOUS: NO SENSOR READING"
    );

    enterFailsafe("FRONT_SENSOR_FAILSAFE");

    return;
  }

  if (
    latestDistance <
    settings.stopDistanceCM
  ) {
    obstacleCount++;

    Serial.print(
      "AUTONOMOUS: OBSTACLE AT "
    );
    Serial.print(latestDistance, 1);
    Serial.println(" CM");

    stopRover();

    Serial.println(
      "AUTONOMOUS: REVERSING"
    );

    moveBackward();

    autoState = REVERSING;
    stateStartTime = millis();

    sendState("REVERSING");

    return;
  }

  if (currentCommand != FORWARD) {
    Serial.println(
      "AUTONOMOUS: PATH CLEAR"
    );

    moveForward();
    rememberMovement(FORWARD);

    sendState("DRIVING");
  }
}

void scanLeft() {
  Serial.println("SCAN LEFT");

  currentServoPosition = SERVO_LEFT;
sensorServo.write(currentServoPosition);;
  delay(500);

  leftDistance = readDistanceCM();

  Serial.print("LEFT DISTANCE: ");

  if (leftDistance < 0) {
    Serial.println("NO READING");
  } else {
    Serial.print(leftDistance, 1);
    Serial.println(" CM");
  }
}

void scanCenter() {
  Serial.println("SCAN CENTER");

  currentServoPosition = SERVO_CENTER;
  sensorServo.write(currentServoPosition);
  delay(500);

  latestDistance = readDistanceCM();

  Serial.print("CENTER DISTANCE: ");

  if (latestDistance < 0) {
    Serial.println("NO READING");
  } else {
    Serial.print(latestDistance, 1);
    Serial.println(" CM");
  }
}

void scanRight() {
  Serial.println("SCAN RIGHT");

  currentServoPosition = SERVO_RIGHT;
  sensorServo.write(currentServoPosition);
  delay(500);

  rightDistance = readDistanceCM();

  Serial.print("RIGHT DISTANCE: ");

  if (rightDistance < 0) {
    Serial.println("NO READING");
  } else {
    Serial.print(rightDistance, 1);
    Serial.println(" CM");
  }

  sensorServo.write(SERVO_CENTER);
  delay(500);
}

void chooseBestDirection() {
  bool leftValid = leftDistance >= 0;
  bool rightValid = rightDistance >= 0;

  // BOTH SIDE READINGS FAILED
  if (!leftValid && !rightValid) {
    sensorFailureCount++;

    enterFailsafe("SIDE_SENSOR_FAILURE");

    return;
  }

  // LEFT WORKED, RIGHT FAILED
  if (leftValid && !rightValid) {
    Serial.println(
      "WARNING: RIGHT SENSOR READING FAILED"
    );

    if (
      leftDistance >=
      settings.minimumTurnClearanceCM
    ) {
      blockedPathRetryCount = 0;
      successfulAvoidanceCount++;

      leftTurnCount++;
      turnLeft();

      rememberMovement(LEFT);

      return;
    }

    sensorFailureCount++;

    enterFailsafe(
      "RIGHT_SENSOR_FAILED_LEFT_BLOCKED"
    );

    return;
  }

  // RIGHT WORKED, LEFT FAILED
  if (!leftValid && rightValid) {
    Serial.println(
      "WARNING: LEFT SENSOR READING FAILED"
    );

    if (
      rightDistance >=
      settings.minimumTurnClearanceCM
    ) {
      blockedPathRetryCount = 0;
      successfulAvoidanceCount++;

      rightTurnCount++;
      turnRight();

      rememberMovement(RIGHT);

      return;
    }

    sensorFailureCount++;

    enterFailsafe(
      "LEFT_SENSOR_FAILED_RIGHT_BLOCKED"
    );

    return;
  }

  bool leftClear =
    leftDistance >=
    settings.minimumTurnClearanceCM;

  bool rightClear =
    rightDistance >=
    settings.minimumTurnClearanceCM;

  Serial.println();
  Serial.println("--- PATH DECISION ---");

  Serial.print("LEFT: ");
  Serial.print(leftDistance, 1);
  Serial.println(" CM");

  Serial.print("RIGHT: ");
  Serial.print(rightDistance, 1);
  Serial.println(" CM");

  // BOTH DIRECTIONS ARE CLEAR
  if (leftClear && rightClear) {
    blockedPathRetryCount = 0;
    successfulAvoidanceCount++;

    if (leftDistance > rightDistance) {
      Serial.println("DECISION: LEFT");

      leftTurnCount++;
      turnLeft();

    } else if (
      rightDistance > leftDistance
    ) {
      Serial.println("DECISION: RIGHT");

      rightTurnCount++;
      turnRight();

      rememberMovement(RIGHT);

    } else {
      Serial.println(
        "DECISION: EQUAL CLEARANCE"
      );

      if (turnRightNext) {
        Serial.println(
          "DECISION: RIGHT - ALTERNATING"
        );

        rightTurnCount++;
        turnRight();

        rememberMovement(RIGHT);

      } else {
        Serial.println(
          "DECISION: LEFT - ALTERNATING"
        );

        leftTurnCount++;
        turnLeft();

        rememberMovement(LEFT);
      }

      turnRightNext = !turnRightNext;
    }

    return;
  }

  // ONLY LEFT IS CLEAR
  if (leftClear && !rightClear) {
    Serial.println("DECISION: LEFT ONLY");

    blockedPathRetryCount = 0;
    successfulAvoidanceCount++;

    leftTurnCount++;
    turnLeft();

    rememberMovement(LEFT);

    return;
  }

  // ONLY RIGHT IS CLEAR
  if (!leftClear && rightClear) {
    Serial.println("DECISION: RIGHT ONLY");

    blockedPathRetryCount = 0;
    successfulAvoidanceCount++;

    rightTurnCount++;
    turnRight();

    return;
  }

  // BOTH DIRECTIONS ARE BLOCKED
  Serial.println(
    "DECISION: BOTH PATHS BLOCKED"
  );

  blockedPathRetryCount++;

  Serial.print("BLOCKED RETRY: ");
  Serial.print(blockedPathRetryCount);
  Serial.print("/");
  Serial.println(
    MAX_BLOCKED_PATH_RETRIES
  );

  if (
    blockedPathRetryCount >=
    MAX_BLOCKED_PATH_RETRIES
  ) {
    Serial.println(
      "AUTONOMOUS: MAX RETRIES REACHED"
    );

    enterFailsafe(
      "MAX_BLOCKED_RETRIES"
    );

    return;
  }

  Serial.println(
    "AUTONOMOUS: REVERSING FARTHER"
  );

  moveBackward();

  autoState = REVERSING;
  stateStartTime = millis();

  return;
}

float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration =
    pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    return -1;
  }

  return duration * 0.0343 / 2.0;
}

void printDistance() {
  latestDistance = readDistanceCM();

  if (latestDistance >= 0) {
    distanceReadingCount++;
  }

  sendDistance(latestDistance);
}

void printCurrentStatus() {
  Serial.println();
  Serial.println("==== STATUS ====");

  Serial.print("Mode: ");
  Serial.println(currentMode == MANUAL ? "MANUAL" : "AUTONOMOUS");

  Serial.print("State: ");

  switch (autoState) {
    case IDLE:
    Serial.println("IDLE");
    break;

    case DRIVING:
    Serial.println("DRIVING");
    break;

    case REVERSING:
    Serial.println("REVERSING");
    break;

    case SCANNING_LEFT:
    Serial.println("SCANNING_LEFT");
    break;

   case SCANNING_CENTER:
   Serial.println("SCANNING_CENTER");
   break;

   case SCANNING_RIGHT:
   Serial.println("SCANNING_RIGHT");
   break;

   case TURNING:
   Serial.println("TURNING");
   break;

   case FAILSAFE:
   Serial.println("FAILSAFE");
   break;
  }

  Serial.print("Failsafe Reason: ");
  Serial.println(failsafeReason);

  Serial.print("Failsafe Count: ");
  Serial.println(failsafeCount);

  Serial.print("Successful Avoidances: ");
  Serial.println(successfulAvoidanceCount);

  Serial.print("Command: ");
  switch (currentCommand) {
    case STOP: Serial.println("STOP"); break;
    case FORWARD: Serial.println("FORWARD"); break;
    case BACKWARD: Serial.println("BACKWARD"); break;
    case LEFT: Serial.println("LEFT"); break;
    case RIGHT: Serial.println("RIGHT"); break;
    case FORWARD_LEFT: Serial.println("FORWARD_LEFT"); break;
    case FORWARD_RIGHT: Serial.println("FORWARD_RIGHT"); break;
    case BACKWARD_LEFT: Serial.println("BACKWARD_LEFT"); break;
    case BACKWARD_RIGHT: Serial.println("BACKWARD_RIGHT"); break;
  }

  Serial.print("Distance: ");
  if (latestDistance < 0) {
    Serial.println("NO READING");
  } else {
    Serial.print(latestDistance,1);
    Serial.println(" CM");
  }

Serial.print("Servo Position: ");
Serial.println(currentServoPosition);

Serial.print("Left Distance: ");
Serial.println(leftDistance, 1);

Serial.print("Center Distance: ");
Serial.println(latestDistance, 1);

Serial.print("Right Distance: ");
Serial.println(rightDistance, 1);

  Serial.println("==================="); 
}

void printMissionStatistics() {
  Serial.println();
  Serial.println("===== MISSION =====");

  Serial.print("Mission Time: ");
  Serial.print(
    (millis() - missionStartTime) / 1000
  );
  Serial.println(" sec");

  Serial.print("Obstacles: ");
  Serial.println(obstacleCount);

  Serial.print("Left Turns: ");
  Serial.println(leftTurnCount);

  Serial.print("Right Turns: ");
  Serial.println(rightTurnCount);

  Serial.print("Successful Avoidances: ");
  Serial.println(successfulAvoidanceCount);

  int totalAvoidanceAttempts = 
  successfulAvoidanceCount + failsafeCount;

  Serial.print("Avoidance Success Rate:");

  if (totalAvoidanceAttempts == 0) {
    Serial.println("N/A");
  } else {
    float successRate = 
    (successfulAvoidanceCount * 100.0) /
    totalAvoidanceAttempts;

  Serial.print(successRate, 1);
  Serial.println("%");
  }

  Serial.print("Distance Readings: ");
  Serial.println(distanceReadingCount);
  
  Serial.print("Failsafes: ");
  Serial.println(failsafeCount);

  Serial.println("===================");
}

void setLeftMotor(int speedValue) {
  speedValue =
    constrain(speedValue, -255, 255);

  if (speedValue > 0) {
    digitalWrite(AIN1_PIN, HIGH);
    digitalWrite(AIN2_PIN, LOW);

    analogWrite(PWMA_PIN, speedValue);

  } else if (speedValue < 0) {
    digitalWrite(AIN1_PIN, LOW);
    digitalWrite(AIN2_PIN, HIGH);

    analogWrite(PWMA_PIN, -speedValue);

  } else {
    digitalWrite(AIN1_PIN, LOW);
    digitalWrite(AIN2_PIN, LOW);

    analogWrite(PWMA_PIN, 0);
  }

  Serial.print("LEFT MOTOR: ");
  Serial.println(speedValue);
}

void setRightMotor(int speedValue) {
  speedValue =
    constrain(speedValue, -255, 255);

  if (speedValue > 0) {
    digitalWrite(BIN1_PIN, HIGH);
    digitalWrite(BIN2_PIN, LOW);

    analogWrite(PWMB_PIN, speedValue);

  } else if (speedValue < 0) {
    digitalWrite(BIN1_PIN, LOW);
    digitalWrite(BIN2_PIN, HIGH);

    analogWrite(PWMB_PIN, -speedValue);

  } else {
    digitalWrite(BIN1_PIN, LOW);
    digitalWrite(BIN2_PIN, LOW);

    analogWrite(PWMB_PIN, 0);
  }

  Serial.print("RIGHT MOTOR: ");
  Serial.println(speedValue);
}

void stopMotors() {
  setLeftMotor(0);
  setRightMotor(0);
}

void moveForward() {
  currentCommand = FORWARD;

  setLeftMotor(settings.forwardSpeed);
  setRightMotor(settings.forwardSpeed);

  sendCommand("FORWARD");
}

void moveBackward() {
  currentCommand = BACKWARD;

  setLeftMotor(-settings.reverseSpeed);
  setRightMotor(-settings.reverseSpeed);

  sendCommand("BACKWARD");
}

void turnLeft() {
  currentCommand = LEFT;

  setLeftMotor(-settings.turnSpeed);
  setRightMotor(settings.turnSpeed);

  sendCommand("LEFT");
}

void turnRight() {
  currentCommand = RIGHT;

  setLeftMotor(settings.turnSpeed);
  setRightMotor(-settings.turnSpeed);

  sendCommand("RIGHT");
}

void moveForwardLeft() {
  currentCommand = FORWARD_LEFT;

  setLeftMotor(settings.slowSpeed);
  setRightMotor(settings.forwardSpeed);

  sendCommand("FORWARD_LEFT");
}

void moveForwardRight() {
  currentCommand = FORWARD_RIGHT;

  setLeftMotor(settings.forwardSpeed);
  setRightMotor(settings.slowSpeed);

  sendCommand("FORWARD_RIGHT");
}

void moveBackwardLeft() {
  currentCommand = BACKWARD_LEFT;

  setLeftMotor(-settings.slowSpeed);
  setRightMotor(-settings.reverseSpeed);

  sendCommand("BACKWARD_LEFT");
}

void moveBackwardRight() {
  currentCommand = BACKWARD_RIGHT;

  setLeftMotor(-settings.reverseSpeed);
  setRightMotor(-settings.slowSpeed);

  sendCommand("BACKWARD_RIGHT");
}

void stopRover() {
  currentCommand = STOP;

  stopMotors();

  sendCommand("STOP");
}

// TODO:
// add avoidance success rate
// add avoidanceatemptcount
//write next devlog
// verify