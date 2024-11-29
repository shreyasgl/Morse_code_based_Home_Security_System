#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Replace with your network credentials
const char* ssid = "Redmi note 11 pro + 5g";
const char* password = "redmi888";

// Replace with your MQTT broker details
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char* mqttUser = "your-username";
const char* mqttPassword = "your-password";

WiFiClient espClient;
PubSubClient client(espClient);

// Morse code for digits 0-9
const char* morseCode[] = {".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.", "-----"};

// MQTT topic
const char* otpTopic = "home/security/otp";

// Pin definitions
const int dotButtonPin = 5;  // D1 on NodeMCU
const int dashButtonPin = 4;  // D2 on NodeMCU
const int ledPin = 0;         // D3 on NodeMCU

// Variables for OTP
String triggeredOTP = "";
String userInputOTP = "";
unsigned long cycleStartTime = 0;
bool cycleActive = false;
const unsigned long otpDuration = 60000; // 1 minute in milliseconds
const unsigned long resetDelay = 10000; // 10 seconds to wait before generating a new OTP
bool mqttConnected = false; // Track MQTT connection status

void setup() {
    Serial.begin(115200);
    setupWiFi();
    client.setServer(mqttServer, mqttPort);
    reconnectMQTT(); // Connect to MQTT once at startup

    pinMode(dotButtonPin, INPUT_PULLUP);
    pinMode(dashButtonPin, INPUT_PULLUP);
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW); // Ensure LED is OFF initially

    generateAndPublishOTP(); // Generate OTP when the system starts
}

void loop() {
    // Only check MQTT connection if we haven't connected yet
    if (!mqttConnected) {
        reconnectMQTT();
    } else {
        client.loop(); // Continue to process MQTT messages
    }

    unsigned long currentTime = millis();

    if (cycleActive) {
        // Check if within the 1-minute OTP entry window
        if (currentTime - cycleStartTime <= otpDuration) {
            checkUserInput(); // Monitor button presses for user input
        } else {
            // Time's up, check OTP
            if (userInputOTP == "") {
                Serial.println("No input given, starting a new cycle.");
                handleOTPVerification(false); // No OTP entered
            } else if (userInputOTP == triggeredOTP) {
                handleOTPVerification(true); // OTP matched
            } else {
                handleOTPVerification(false); // Incorrect OTP entered
            }
        }
    } else {
        // If the cycle has ended and we're starting a new session, generate a new OTP
        if (currentTime - cycleStartTime >= otpDuration + resetDelay) { 
            Serial.println("Starting a completely new OTP cycle...");
            generateAndPublishOTP(); // Generate a new OTP and start the next cycle
            resetSystem(true); // Reset system for the next cycle
        }
    }
}

void setupWiFi() {
    delay(10);
    Serial.println();
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
    int maxAttempts = 5; // Set a maximum number of connection attempts
    int attemptCount = 0; // Track the number of attempts

    while (!client.connected() && attemptCount < maxAttempts) {
        Serial.print("Connecting to MQTT... Attempt ");
        Serial.print(attemptCount + 1);
        Serial.print(" of ");
        Serial.println(maxAttempts);

        if (client.connect("ESP8266Client", mqttUser, mqttPassword)) {
            Serial.println("connected");
            mqttConnected = true; // Mark as connected
            break; // Exit the loop if connected
        } else {
            Serial.print("Failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 10 seconds");

            attemptCount++;
            delay(10000); // Delay for 10 seconds before trying again
        }
    }

    if (attemptCount >= maxAttempts) {
        Serial.println("Max connection attempts reached. Waiting before retrying.");
        delay(60000); // Wait 1 minute before retrying if all attempts fail
    }
}


void generateAndPublishOTP() {
    int otp = random(0, 10); // Generate a 1-digit random OTP (0-9)
    triggeredOTP = convertToMorseCode(otp);
    userInputOTP = ""; // Clear any previous input

    Serial.print("Generated OTP: ");
    Serial.println(otp);
    Serial.print("Morse Code: ");
    Serial.println(triggeredOTP);

    if (!client.connected()) {
        reconnectMQTT();
    }

    if (client.publish(otpTopic, triggeredOTP.c_str())) {
        Serial.println("Morse code published successfully to MQTT");
    } else {
        Serial.println("Failed to publish Morse code to MQTT");
    }

    // Reset timer
    cycleStartTime = millis();
    cycleActive = true; // Start the OTP entry cycle
}

String convertToMorseCode(int number) {
    return morseCode[number]; // Convert the single digit to its corresponding Morse code
}

void checkUserInput() {
    static unsigned long lastDebounceTimeDot = 0;
    static unsigned long lastDebounceTimeDash = 0;
    const unsigned long debounceDelay = 500; // Increased debounce delay to 500ms

    // Check the current state of the dot button
    if (digitalRead(dotButtonPin) == LOW) {
        Serial.println("Dot button pressed (detected LOW)");
        if ((millis() - lastDebounceTimeDot) > debounceDelay) {
            userInputOTP += "."; // Add dot to user input
            Serial.println("Dot entered");
            lastDebounceTimeDot = millis(); // Update debounce time
        }
    }

    // Check the current state of the dash button
    if (digitalRead(dashButtonPin) == LOW) {
        Serial.println("Dash button pressed (detected LOW)");
        if ((millis() - lastDebounceTimeDash) > debounceDelay) {
            userInputOTP += "-"; // Add dash to user input
            Serial.println("Dash entered");
            lastDebounceTimeDash = millis(); // Update debounce time
        }
    }

    // Check if the length of user input matches the OTP length
    if (userInputOTP.length() == triggeredOTP.length()) {
        Serial.print("User Input OTP: ");
        Serial.println(userInputOTP);
        Serial.print("Triggered OTP: ");
        Serial.println(triggeredOTP);
        if (userInputOTP == triggeredOTP) {
            handleOTPVerification(true); // OTP matched
            resetSystem(true); // Stop further cycles as the correct OTP was entered
        } else {
            handleOTPVerification(false); // Incorrect OTP entered
        }
    }
}

void handleOTPVerification(bool matched) {
    if (matched) {
        Serial.println("OTP matched!");
        // Blink the LED twice
        for (int i = 0; i < 2; i++) {
            digitalWrite(ledPin, HIGH);
            delay(500);
            digitalWrite(ledPin, LOW);
            delay(500);
        }
        cycleActive = false; // Stop further cycles as the OTP was matched
    } else {
        Serial.println("OTP did not match.");
        // Blink LED thrice to indicate incorrect OTP
        for (int i = 0; i < 3; i++) {
            digitalWrite(ledPin, HIGH);
            delay(500);
            digitalWrite(ledPin, LOW);
            delay(500);
        }
        // Reset the system and generate a new OTP
        resetSystem(true);
        generateAndPublishOTP(); // Generate new OTP after a mismatch
    }
}

void resetSystem(bool stopCycle) {
    if (!stopCycle) {
        // Only restart the cycle WITHOUT generating a new OTP
        Serial.println("Restarting cycle without new OTP...");
        cycleStartTime = millis(); // Reset cycle start time
        cycleActive = true; // Restart the active cycle
    } else {
        Serial.println("Stopping further cycles...");
        cycleActive = false; // Stop the loop if OTP is verified
    }
    userInputOTP = ""; // Clear user input for the next cycle
    digitalWrite(ledPin, LOW); // Ensure LED is OFF after reset
}  
