#include <Arduino.h>
#include <SwitecX25.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- BLE CONFIGURATION ---
#define SERVICE_UUID        "fe2599cc-5e96-4709-aa84-26be54013e1d"
#define CHARACTERISTIC_UUID "aa63dad2-5083-45ff-a7ba-bef7cba872ae"

// --- HARDWARE PINS ---
#define TACTILE_SW D7
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define BLE_LED D6

// --- OBJECTS ---
SwitecX25 motor(315*3, D3, D2, D1, D0);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- GLOBAL VARIABLES ---
float latestBLEMeasurement = 0;
float stepsPerInch = 6.42;
bool deviceConnected = false;
void updateDisplay(float measurement);
bool showingTareMessage = false;

// --- BLE CALLBACKS ---
class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = String(pCharacteristic->getValue().c_str());
        if (value.length() > 0) {
            latestBLEMeasurement = value.toFloat();
            Serial.print(">>> BLE Received: ");
            Serial.println(latestBLEMeasurement);
            if (!showingTareMessage) {
                updateDisplay(latestBLEMeasurement);
            }
        }
    }
};

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Client Connected!");
    };
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Client Disconnected - Restarting Advertising...");
        BLEDevice::startAdvertising();
    }
};

// --- MOTOR FUNCTIONS ---
void updateX27(float distInFeet, float spi) {
    int targetStep = round(distInFeet * 12.0 * spi);
    motor.setPosition(targetStep);
}

void updateDisplay(float measurement) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // Big number in the middle
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.print(measurement, 2);  // 2 decimal places
    display.print(" ft");

    display.display();
}

void showTareMessage(float taredValue) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 10);
    display.print("Tared:");
    display.setCursor(0, 35);
    display.print(taredValue, 2);
    display.print(" ft");
    display.display();
    delay(1500);  // show for 1.5 seconds then go back to normal
    showingTareMessage = false;
}

void tare(float currentMsmt) {
    static float displayedMeasurement = 0;
    static unsigned long pressStartTime = 0;
    static bool buttonHeld = false;

    if (digitalRead(TACTILE_SW) == LOW) {
        if (!buttonHeld) {
            pressStartTime = millis();
            buttonHeld = true;
        }
    }
    else if (buttonHeld) {
        unsigned long duration = millis() - pressStartTime;
        if (duration > 2000) {
            displayedMeasurement = 0;
            Serial.println(">>> RESET: Dial at 0ft");
            showingTareMessage = true;
            showTareMessage(0);
        } else {
            displayedMeasurement = currentMsmt;
            Serial.print(">>> CAPTURED: ");
            Serial.print(displayedMeasurement);
            Serial.println(" ft");
            showingTareMessage = true;
            showTareMessage(displayedMeasurement);
        }
        buttonHeld = false;
        pressStartTime = 0;
    }
    updateX27(displayedMeasurement, stepsPerInch);
    //updateDisplay(displayedMeasurement);
}

void setup() {
    Serial.begin(115200);
    Wire.begin(D4, D5);
    pinMode(TACTILE_SW, INPUT_PULLUP);
    pinMode(BLE_LED, OUTPUT);
    digitalWrite(BLE_LED, LOW);
    delay(2000);
    motor.maxVel = 50;

    // Initialize OLED
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Homing motor...");
    display.display();

    // --- HOMING ---
    for (int i = 0; i < 6; i++) {
        motor.currentStep = 400;
        motor.setPosition(0);
        motor.updateBlocking();
        delay(100);
    }

    motor.setPosition(450);
    motor.updateBlocking();
    motor.currentStep = 0;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Homed! Starting BLE...");
    display.display();

    // --- BLE SETUP ---
    BLEDevice::init("XIAO_GAUGE_RECEIVER");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    BLECharacteristic *pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );

    pCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();

    BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
    BLEDevice::startAdvertising();
    Serial.println("Gauge is waiting for measurement data...");

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("BLE Ready!");
    display.println("Waiting for data...");
    display.display();
}

void loop() {
    motor.update();
    tare(latestBLEMeasurement);

    // LED status
    if (deviceConnected) {
        digitalWrite(BLE_LED, HIGH);  // solid on when connected
    } else {
        // Blink while waiting
        digitalWrite(BLE_LED, (millis() / 500) % 2);
    }
}