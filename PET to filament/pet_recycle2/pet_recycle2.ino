#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// ================= OLED DISPLAY INITIALIZATION =================
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ================= PIN DEFINITIONS =================
const int HEATER_PIN     = 3;   // IRF520 SIG for PID heating[cite: 2]
const int THERMISTOR_PIN  = A0; // Thermistor analog input[cite: 3]
const int RPWM_PIN       = 5;   // BTS7960 Forward[cite: 1]
const int LPWM_PIN       = 6;   // BTS7960 Reverse[cite: 1]

// --- NEW 3-BUTTON SETUP ---
const int BTN_UP         = 2;   // Moves highlight up / Increases value
const int BTN_DOWN       = 4;   // Moves highlight down / Decreases value
const int BTN_SEL        = 7;   // Selects row / Confirms edit

// ================= THERMISTOR & PID =================
const float R_FIXED = 4700.0;  // 4.7k ohm pull-up resistor[cite: 3]
const float R_0     = 100000.0;// 100k thermistor baseline[cite: 3]
const float T_0     = 298.15;  // 25 C in Kelvin[cite: 3]
const float BETA    = 3950.0;  // Typical beta for 3D printer thermistors[cite: 3]

float setpoint = 0.0, currentTemp = 0.0, pwmOutput = 0.0;
float Kp = 30.0, Ki = 1.5, Kd = 60.0; // PID Tuning parameters[cite: 2]
float integralError = 0.0, previousError = 0.0;
unsigned long lastPidTime = 0;

int motorSpeed = 0; // -255 to 255[cite: 1]

// ================= UI STATE MACHINE =================
enum MenuState { NAVIGATE, EDIT_TEMP, EDIT_MOTOR };
MenuState currentUIState = NAVIGATE;
int selectedRow = 0; // 0 = Temp, 1 = Motor

float editTemp = 0.0;
int editMotorSpeed = 0;

// Button timing tracking
unsigned long lastButtonPress = 0;
const int debounceDelay = 150; // Milliseconds between allowed button clicks

unsigned long lastDisplayTime = 0;
unsigned long lastSerialTime = 0;
bool triggerSerialPrint = true; // Forces an immediate print on startup

// Function prototypes
float readThermistor();
void runPidController(float dt);
void setMotorSpeed(int speedVal);
void handleButtons();
void drawScreen();
void printSerialUI();

void setup() {
  Serial.begin(9600);
  pinMode(HEATER_PIN, OUTPUT);
  pinMode(RPWM_PIN, OUTPUT);
  pinMode(LPWM_PIN, OUTPUT);
  
  // Enable internal pull-ups for the buttons
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SEL, INPUT_PULLUP);

  // Safety initializations[cite: 1, 2]
  analogWrite(HEATER_PIN, 0);
  analogWrite(RPWM_PIN, 0);
  analogWrite(LPWM_PIN, 0);

  u8g2.begin();
  Serial.println("3-Button System Initialized...");
}

void loop() {
  // 1. Process 3-Button Inputs
  handleButtons();

  // 2. PID Control (Runs every 200ms)[cite: 2]
  unsigned long currentTime = millis();
  if (currentTime - lastPidTime >= 200) {
    float dt = (currentTime - lastPidTime) / 1000.0;
    lastPidTime = currentTime;
    runPidController(dt);
  }

  // 3. Update OLED Display (Runs every 100ms)
  if (currentTime - lastDisplayTime >= 100) {
    lastDisplayTime = currentTime;
    drawScreen();
  }
  
  // 4. Update Serial Monitor (On Interaction OR every 2000ms)
  if (triggerSerialPrint || (currentTime - lastSerialTime >= 2000)) {
    lastSerialTime = currentTime;
    triggerSerialPrint = false;
    printSerialUI();
  }
}

// ================= 3-BUTTON LOGIC =================
void handleButtons() {
  bool upPressed = (digitalRead(BTN_UP) == LOW);
  bool downPressed = (digitalRead(BTN_DOWN) == LOW);
  bool selPressed = (digitalRead(BTN_SEL) == LOW);

  // If any button is pressed and the debounce timer has passed
  if ((upPressed || downPressed || selPressed) && (millis() - lastButtonPress > debounceDelay)) {
    lastButtonPress = millis();
    triggerSerialPrint = true; // Force Serial UI update

    // --- SELECT BUTTON PRESSED ---
    if (selPressed) {
      if (currentUIState == NAVIGATE) {
        if (selectedRow == 0) {
          editTemp = setpoint;
          currentUIState = EDIT_TEMP;
        } else if (selectedRow == 1) {
          editMotorSpeed = motorSpeed;
          currentUIState = EDIT_MOTOR;
        }
      } 
      else if (currentUIState == EDIT_TEMP) {
        setpoint = editTemp;
        integralError = 0; // Reset anti-windup accumulator[cite: 2]
        currentUIState = NAVIGATE;
      } 
      else if (currentUIState == EDIT_MOTOR) {
        motorSpeed = editMotorSpeed;
        setMotorSpeed(motorSpeed);
        currentUIState = NAVIGATE;
      }
    } 
    // --- UP OR DOWN BUTTON PRESSED ---
    else {
      int direction = upPressed ? 1 : -1; // 1 for Up, -1 for Down

      if (currentUIState == NAVIGATE) {
        // Toggle between row 0 and row 1
        selectedRow = (selectedRow + direction + 2) % 2; 
      } 
      else if (currentUIState == EDIT_TEMP) {
        editTemp += (direction * 5.0); 
        editTemp = constrain(editTemp, 0.0, 260.0); // Safe 3D printing limits[cite: 2]
      } 
      else if (currentUIState == EDIT_MOTOR) {
        editMotorSpeed += (direction * 15); 
        editMotorSpeed = constrain(editMotorSpeed, -255, 255); // BTS7960 PWM range[cite: 1]
      }
    }
  }
}

// ================= SERIAL MONITOR RENDERING =================
void printSerialUI() {
  Serial.println("\n==================================================");
  Serial.println("                 PET RECYCLE CTRL                 ");
  Serial.println("--------------------------------------------------");
  
  if (selectedRow == 0) Serial.print(" >>> "); 
  else Serial.print("     ");
  
  Serial.print("TEMP  | Actual: ");
  Serial.print(currentTemp, 1);
  Serial.print(" °C  | Target: ");
  
  if (currentUIState == EDIT_TEMP) {
    Serial.print(editTemp, 0);
    Serial.println(" °C  [EDITING]");
  } else {
    Serial.print(setpoint, 0);
    Serial.println(" °C");
  }

  if (selectedRow == 1) Serial.print(" >>> "); 
  else Serial.print("     ");
  
  Serial.print("MOTOR | Dir: ");
  int activeSpeed = (currentUIState == EDIT_MOTOR) ? editMotorSpeed : motorSpeed;
  
  if (activeSpeed > 0) Serial.print("FORWARD ");
  else if (activeSpeed < 0) Serial.print("REVERSE ");
  else Serial.print("STOPPED ");
  
  Serial.print(" | PWM: ");
  Serial.print(abs(activeSpeed));
  
  if (currentUIState == EDIT_MOTOR) Serial.println(" [EDITING]");
  else Serial.println();
  
  Serial.println("==================================================");
}

// ================= OLED DISPLAY RENDERING =================
void drawScreen() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(12, 10, "PET RECYCLE CTRL");
    u8g2.drawHLine(0, 13, 128);

    // ROW 1: TEMPERATURE
    if (selectedRow == 0) {
      if (currentUIState == EDIT_TEMP) u8g2.drawFrame(0, 16, 128, 22); 
      else u8g2.drawBox(0, 16, 128, 22);   
    }

    if (selectedRow == 0 && currentUIState == NAVIGATE) u8g2.setDrawColor(0);
    else u8g2.setDrawColor(1);

    u8g2.setFont(u8g2_font_7x14_tf);
    u8g2.setCursor(4, 32);
    u8g2.print("T:");
    u8g2.print(currentTemp, 0);
    u8g2.print("C");

    u8g2.setCursor(64, 32);
    u8g2.print(">");
    if (currentUIState == EDIT_TEMP) {
      u8g2.print(editTemp, 0);
      u8g2.print("C*");
    } else {
      u8g2.print(setpoint, 0);
      u8g2.print("C");
    }

    // ROW 2: MOTOR
    if (selectedRow == 1) {
      if (currentUIState == EDIT_MOTOR) u8g2.drawFrame(0, 40, 128, 22); 
      else u8g2.drawBox(0, 40, 128, 22);   
    }

    if (selectedRow == 1 && currentUIState == NAVIGATE) u8g2.setDrawColor(0);
    else u8g2.setDrawColor(1);

    u8g2.setCursor(4, 56);
    int activeSpeed = (currentUIState == EDIT_MOTOR) ? editMotorSpeed : motorSpeed;

    if (activeSpeed > 0) u8g2.print("M:FWD ");
    else if (activeSpeed < 0) u8g2.print("M:REV ");
    else u8g2.print("M:STOP");

    u8g2.setCursor(72, 56);
    u8g2.print("P:");
    u8g2.print(abs(activeSpeed));
    if (currentUIState == EDIT_MOTOR) u8g2.print("*");

    u8g2.setDrawColor(1);
  } while (u8g2.nextPage());
}

// ================= MOTOR DRIVE LOGIC =================
void setMotorSpeed(int speedVal) {
  if (speedVal > 0) {
    analogWrite(RPWM_PIN, speedVal);
    analogWrite(LPWM_PIN, 0);
  } else if (speedVal < 0) {
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, abs(speedVal));
  } else {
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, 0);
  }
}

// ================= PID HEATER LOOP =================
void runPidController(float dt) {
  currentTemp = readThermistor();
  float error = setpoint - currentTemp;
  float P = Kp * error;

  // Anti-windup calculation (Active within 30 degrees of target)[cite: 2]
  if (abs(error) < 30.0 && setpoint > 0) integralError += (error * dt);
  else integralError = 0;
  
  float I = Ki * integralError;
  float D = Kd * ((error - previousError) / dt);

  float pidCalc = P + I + D;
  pwmOutput = constrain(pidCalc, 0, 255);
  
  if (setpoint == 0) pwmOutput = 0; // Strict override[cite: 2]

  analogWrite(HEATER_PIN, (int)pwmOutput);
  previousError = error;
}

// ================= THERMISTOR READING =================
float readThermistor() {
  int adcValue = analogRead(THERMISTOR_PIN);
  if (adcValue <= 0 || adcValue >= 1023) {
    analogWrite(HEATER_PIN, 0); // Failsafe shutdown[cite: 2]
    return 0.0;
  }
  float rTherm = R_FIXED * (adcValue / (1023.0 - adcValue)); // Calculate resistance[cite: 3]
  return (1.0 / ((1.0 / T_0) + (log(rTherm / R_0) / BETA))) - 273.15; // Apply Beta equation[cite: 3]
}
