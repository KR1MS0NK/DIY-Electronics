// Define the PWM pin connected to the SIG pin of the IRF520N module
// Pin 9 is a standard PWM-capable pin on Arduino Uno/Nano
const int pwmPin = 3; 

// Variable to store the current PWM value
int currentPwmValue = 0;

const int thermistorPin = A0;
const float R_fixed = 4700.0; // 4.7k ohm pull-up resistor
const float R_0 = 100000.0;   // 100k thermistor baseline
const float T_0 = 298.15;     // 25 C in Kelvin
const float Beta = 3950.0;    // Typical beta for 3D printer thermistors

void setup() {
  // Initialize serial communication at 9600 baud
  Serial.begin(9600);
  
  // Set the PWM pin as an output
  pinMode(pwmPin, OUTPUT);
  
  // Ensure the heater is completely off on startup for safety
  analogWrite(pwmPin, 0); 
  
  Serial.println("Enter a PWM value between 0 and 255 :");
}

void loop() {

  temp();
  
  // Check if data is available in the Serial buffer
  if (Serial.available() > 0) {
    
    // Read the incoming integer
    int inputValue = Serial.parseInt();
    
    // Constrain the value to the valid 8-bit PWM range (0-255)
    // and filter out weird inputs
    if (inputValue >= 0 && inputValue <= 255) {
        currentPwmValue = inputValue;
        
        // Apply the PWM value to the MOSFET
        analogWrite(pwmPin, currentPwmValue);
        
        // Print feedback to the Serial Monitor
        Serial.print("Heater PWM set to: ");
        Serial.println(currentPwmValue);
    } else {
        Serial.println("Error: Value must be between 0 and 255.");
    }
    
    // Delay slightly to allow the rest of the serial message (like line endings) to arrive
    delay(10);
    
    // Clear out any trailing newline or carriage return characters from the buffer
    // so they aren't accidentally read as '0' on the next loop
    while(Serial.available() > 0) {
        Serial.read();
    }
  }
  delay(500);
}

void temp() {

  int adcValue = analogRead(thermistorPin);

  // Safety check for open/shorted circuits
  if (adcValue <= 0 || adcValue >= 1023) {
    Serial.println("Error: Check temp wiring.");
    delay(1000);
    return;
  }

  // Calculate resistance
  float R_therm = R_fixed * (adcValue / (1023.0 - adcValue));

  // Calculate temperature in Celsius directly
  float tempC = (1.0 / ( (1.0 / T_0) + (log(R_therm / R_0) / Beta) )) - 273.15;

  // Print only the Celsius temperature
  Serial.print("Temperature: ");
  Serial.print(tempC);
  Serial.println(" °C");

}
