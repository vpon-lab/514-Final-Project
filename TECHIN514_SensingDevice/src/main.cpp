#include <Arduino.h>
#include <Wire.h>
#include <vl53l4cx_class.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// --- BLE CONFIGURATION (Must match Gauge exactly) ---
#define SERVICE_UUID        "fe2599cc-5e96-4709-aa84-26be54013e1d"
#define CHARACTERISTIC_UUID "aa63dad2-5083-45ff-a7ba-bef7cba872ae"

// --- SENSOR CONFIGURATION ---
#define SDA_PIN D4
#define SCL_PIN D5
VL53L4CX sensor_vl53l4cx_sat(&Wire, -1);

// --- BLE GLOBALS ---
static BLEUUID serviceUUID(SERVICE_UUID);
static BLEUUID charUUID(CHARACTERISTIC_UUID);
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;
static bool doConnect = false;
static bool connected = false;
// Moving average filter
#define FILTER_SIZE 10
float readings[FILTER_SIZE];
int readIndex = 0;
float total = 0;

// 1. SCANNING CALLBACK
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

// 2. CONNECTION LOGIC
bool connectToServer() {
    BLEClient* pClient = BLEDevice::createClient();
    if (!pClient->connect(myDevice)) return false;
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) return false;
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) return false;
    connected = true;
    return true;
}

void setup() {
  Serial.begin(115200);
  
  // SENSOR SETUP
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(500);
  sensor_vl53l4cx_sat.begin();
  sensor_vl53l4cx_sat.VL53L4CX_Off();
  sensor_vl53l4cx_sat.InitSensor(0x52); 
  sensor_vl53l4cx_sat.VL53L4CX_StartMeasurement();
  Serial.println("Sensor ready.");

  // BLE SETUP
  BLEDevice::init("XIAO_SENDER");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

void loop() {
  // Timer for Rate Limiting (Industry standard to avoid flooding)
  static unsigned long lastSendTime = 0; 

  // --- BLUETOOTH CONNECTION HANDLING ---
  if (doConnect) {
    if (connectToServer()) Serial.println("Connected to Gauge!");
    doConnect = false;
  }

  // --- SENSOR DATA COLLECTION ---
  VL53L4CX_MultiRangingData_t MultiRangingData;
  VL53L4CX_MultiRangingData_t *pMultiRangingData = &MultiRangingData;
  uint8_t NewDataReady = 0;
  int status;

  status = sensor_vl53l4cx_sat.VL53L4CX_GetMeasurementDataReady(&NewDataReady);
  
  if ((!status) && (NewDataReady != 0)) {
    status = sensor_vl53l4cx_sat.VL53L4CX_GetMultiRangingData(pMultiRangingData);
    int objects = pMultiRangingData->NumberOfObjectsFound;
    
    for (int j = 0; j < objects; j++) {
      if (pMultiRangingData->RangeData[j].RangeStatus == 0) {
        float dist_mm = pMultiRangingData->RangeData[j].RangeMilliMeter;
        float dist_feet = dist_mm / 304.8; // Convert mm to FEET
        
        // Apply moving average filter
        total -= readings[readIndex];
        readings[readIndex] = dist_feet;
        total += readings[readIndex];
        readIndex = (readIndex + 1) % FILTER_SIZE;
        float filtered_feet = total / FILTER_SIZE;

        Serial.print("Distance: ");
        Serial.print(filtered_feet);
        Serial.println(" ft");

        // --- RATE LIMITED SEND DATA TO GAUGE ---
        // Only send if connected AND it's been at least 200ms since the last send
        if (connected && (millis() - lastSendTime > 200)) {
          String toSend = String(filtered_feet);
          pRemoteCharacteristic->writeValue(toSend.c_str(), toSend.length());
          
          lastSendTime = millis(); // Reset the timer
          Serial.println(">>> Pushed to Gauge via BLE");
        }
        break;
      }
    }
    if (status == 0) {
      sensor_vl53l4cx_sat.VL53L4CX_ClearInterruptAndStartMeasurement();
    }
  }

  // RECONNECT IF DROPPED
  if (!connected && !doConnect) {
    BLEDevice::getScan()->start(0);
  }
}
