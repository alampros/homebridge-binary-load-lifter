#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <VL53L1X.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "secrets.h"

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint8_t MOTOR_CHANNEL_1_PIN = 32;
constexpr uint8_t MOTOR_CHANNEL_2_PIN = 33;
constexpr uint8_t BUZZER_PIN = 27;
constexpr uint8_t STATUS_LED_PIN = 2;
constexpr uint32_t ACTIVE_SENSOR_SAMPLE_INTERVAL_MS = 100;
constexpr uint32_t IDLE_SENSOR_SAMPLE_INTERVAL_MS = 5000;
constexpr uint32_t POST_STOP_CONTINUOUS_MS = 2000;
constexpr uint32_t MOTOR_INPUT_DEBOUNCE_MS = 50;
constexpr uint32_t SENSOR_FAULT_TONE_DELAY_MS = 2000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
// At the 10 Hz sampling rate this covers about 0.9 seconds. Requiring a
// majority of 5 samples suppresses brief changes while allowing sustained
// lift movement to appear after roughly 0.5 seconds.
constexpr size_t DISTANCE_FILTER_SIZE = 9;
constexpr size_t DISTANCE_FILTER_MIN_SAMPLES = 5;
constexpr char MDNS_HOSTNAME[] = "binary-load-lifter";
constexpr size_t MAX_CALLBACK_URL_LENGTH = 255;
constexpr uint32_t CALLBACK_URL_TTL_MS = 120000;
constexpr uint32_t CALLBACK_CONNECT_TIMEOUT_MS = 500;
constexpr uint32_t CALLBACK_RESPONSE_TIMEOUT_MS = 750;
constexpr uint32_t FIRMWARE_UPDATE_LED_INTERVAL_MS = 150;

struct BuzzerStep {
  uint16_t frequencyHz;
  uint16_t durationMs;
};

struct MotorCallbackEvent {
  bool channel1Active;
  bool channel2Active;
  char callbackUrl[MAX_CALLBACK_URL_LENGTH + 1];
};

constexpr BuzzerStep READY_PATTERN[] = {
    {1047, 85}, {0, 35}, {1319, 85}, {0, 35},
    {1568, 95}, {0, 45}, {2093, 190}};
constexpr BuzzerStep CHANNEL_1_START_PATTERN[] = {{1900, 100}};
constexpr BuzzerStep CHANNEL_2_START_PATTERN[] = {{1100, 100}};
constexpr BuzzerStep MOVEMENT_STOP_PATTERN[] = {
    {2200, 60}, {0, 40}, {1600, 90}};
constexpr BuzzerStep SENSOR_FAULT_PATTERN[] = {
    {500, 180}, {0, 100}, {500, 180}, {0, 100}, {500, 260}};

const char *AUTHORIZATION_HEADER = "Authorization";
const char *EXPECTED_AUTHORIZATION_PREFIX = "Bearer ";

WebServer server(80);
VL53L1X sensor;

uint16_t rawDistanceMm = 0;
uint16_t filteredDistanceCm = 0;
uint16_t filteredDistanceMm = 0;
uint16_t distanceSamplesCm[DISTANCE_FILTER_SIZE] = {};
size_t distanceSampleCount = 0;
size_t distanceSampleIndex = 0;
bool distanceFilterReady = false;
bool sensorTimedOut = true;
bool motorChannel1Active = false;
bool motorChannel2Active = false;
bool motorChannel1CandidateActive = false;
bool motorChannel2CandidateActive = false;
unsigned long motorCandidateSinceMs = 0;
unsigned long lastSensorSampleMs = 0;
unsigned long motorStoppedAtMs = 0;
bool sensorContinuous = false;
bool idleMeasurementPending = false;
unsigned long idleMeasurementStartedMs = 0;
const BuzzerStep *activeBuzzerPattern = nullptr;
size_t activeBuzzerPatternLength = 0;
size_t activeBuzzerStep = 0;
unsigned long buzzerStepStartedMs = 0;
unsigned long sensorFaultSinceMs = 0;
bool sensorFaultTonePlayed = false;
QueueHandle_t motorCallbackQueue = nullptr;
String homebridgeCallbackUrl;
unsigned long homebridgeCallbackUpdatedAtMs = 0;

enum class FirmwareUpdateResult {
  NotStarted,
  InProgress,
  Unauthorized,
  InvalidRequest,
  MotorActive,
  BeginFailed,
  WriteFailed,
  UploadAborted,
  Success,
};

FirmwareUpdateResult firmwareUpdateResult = FirmwareUpdateResult::NotStarted;
unsigned long firmwareUpdateLedChangedAtMs = 0;
bool firmwareUpdateLedOn = false;

void sendMotorCallback(const MotorCallbackEvent &event) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  http.setConnectTimeout(CALLBACK_CONNECT_TIMEOUT_MS);
  http.setTimeout(CALLBACK_RESPONSE_TIMEOUT_MS);
  if (!http.begin(event.callbackUrl)) {
    Serial.println("Homebridge motor callback could not start.");
    return;
  }

  http.addHeader("Authorization", String("Bearer ") + API_TOKEN);
  http.addHeader("Content-Type", "application/json");
  const String body =
      String("{\"motor_channel_1_active\":") +
      (event.channel1Active ? "true" : "false") +
      ",\"motor_channel_2_active\":" +
      (event.channel2Active ? "true" : "false") + "}";
  const int statusCode = http.POST(body);
  http.end();

  if (statusCode != 204) {
    Serial.print("Homebridge motor callback failed: HTTP ");
    Serial.println(statusCode);
  }
}

void motorCallbackTask(void *) {
  MotorCallbackEvent event;
  while (true) {
    if (xQueueReceive(motorCallbackQueue, &event, portMAX_DELAY) == pdTRUE) {
      sendMotorCallback(event);
    }
  }
}

void startMotorCallbackTask() {
  motorCallbackQueue = xQueueCreate(4, sizeof(MotorCallbackEvent));
  if (motorCallbackQueue == nullptr ||
      xTaskCreate(motorCallbackTask, "motor-callback", 6144, nullptr, 1,
                  nullptr) != pdPASS) {
    motorCallbackQueue = nullptr;
    Serial.println("Homebridge motor callback task could not start.");
    return;
  }

  Serial.println("Homebridge motor callback task is ready.");
}

void queueMotorCallback() {
  if (motorCallbackQueue == nullptr || homebridgeCallbackUrl.isEmpty()) {
    return;
  }
  MotorCallbackEvent event = {motorChannel1Active, motorChannel2Active, {}};
  strlcpy(event.callbackUrl, homebridgeCallbackUrl.c_str(),
          sizeof(event.callbackUrl));
  if (xQueueSend(motorCallbackQueue, &event, 0) != pdTRUE) {
    Serial.println("Homebridge motor callback queue is full; event dropped.");
  }
}

void expireHomebridgeCallbackUrl() {
  if (!homebridgeCallbackUrl.isEmpty() &&
      millis() - homebridgeCallbackUpdatedAtMs >= CALLBACK_URL_TTL_MS) {
    homebridgeCallbackUrl = "";
    homebridgeCallbackUpdatedAtMs = 0;
    Serial.println("Homebridge motor callback expired.");
  }
}

void startBuzzerStep() {
  const BuzzerStep &step = activeBuzzerPattern[activeBuzzerStep];
  if (step.frequencyHz == 0) {
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    tone(BUZZER_PIN, step.frequencyHz);
  }
  buzzerStepStartedMs = millis();
}

void playBuzzerPattern(const BuzzerStep *pattern, size_t length) {
  activeBuzzerPattern = pattern;
  activeBuzzerPatternLength = length;
  activeBuzzerStep = 0;
  startBuzzerStep();
}

void serviceBuzzer() {
  if (activeBuzzerPattern == nullptr ||
      millis() - buzzerStepStartedMs <
          activeBuzzerPattern[activeBuzzerStep].durationMs) {
    return;
  }

  activeBuzzerStep++;
  if (activeBuzzerStep >= activeBuzzerPatternLength) {
    noTone(BUZZER_PIN);
    // This module is low-level triggered, so HIGH is its silent state.
    digitalWrite(BUZZER_PIN, HIGH);
    activeBuzzerPattern = nullptr;
    activeBuzzerPatternLength = 0;
    return;
  }

  startBuzzerStep();
}

void printMotorInputs() {
  Serial.print("Motor inputs: channel_1=");
  Serial.print(motorChannel1Active ? "active" : "inactive");
  Serial.print(", channel_2=");
  Serial.println(motorChannel2Active ? "active" : "inactive");
}

void readMotorInputs() {
  // The isolated PC817 outputs pull the GPIO low when their input polarity is
  // present. Keep these as raw channel states until installation establishes
  // which physical motor polarity corresponds to up and down.
  const bool nextChannel1Active = digitalRead(MOTOR_CHANNEL_1_PIN) == LOW;
  const bool nextChannel2Active = digitalRead(MOTOR_CHANNEL_2_PIN) == LOW;

  if (nextChannel1Active != motorChannel1CandidateActive ||
      nextChannel2Active != motorChannel2CandidateActive) {
    motorChannel1CandidateActive = nextChannel1Active;
    motorChannel2CandidateActive = nextChannel2Active;
    motorCandidateSinceMs = millis();
    return;
  }

  if ((motorChannel1CandidateActive == motorChannel1Active &&
       motorChannel2CandidateActive == motorChannel2Active) ||
      millis() - motorCandidateSinceMs < MOTOR_INPUT_DEBOUNCE_MS) {
    return;
  }

  const bool wasMoving = motorChannel1Active || motorChannel2Active;
  motorChannel1Active = motorChannel1CandidateActive;
  motorChannel2Active = motorChannel2CandidateActive;
  const bool isMoving = motorChannel1Active || motorChannel2Active;
  printMotorInputs();
  queueMotorCallback();

  if (!wasMoving && isMoving) {
    motorStoppedAtMs = 0;
    if (motorChannel1Active && !motorChannel2Active) {
      playBuzzerPattern(CHANNEL_1_START_PATTERN,
                        sizeof(CHANNEL_1_START_PATTERN) /
                            sizeof(CHANNEL_1_START_PATTERN[0]));
    } else if (motorChannel2Active && !motorChannel1Active) {
      playBuzzerPattern(CHANNEL_2_START_PATTERN,
                        sizeof(CHANNEL_2_START_PATTERN) /
                            sizeof(CHANNEL_2_START_PATTERN[0]));
    } else {
      playBuzzerPattern(SENSOR_FAULT_PATTERN,
                        sizeof(SENSOR_FAULT_PATTERN) /
                            sizeof(SENSOR_FAULT_PATTERN[0]));
    }
  } else if (wasMoving && !isMoving) {
    motorStoppedAtMs = millis();
    playBuzzerPattern(MOVEMENT_STOP_PATTERN,
                      sizeof(MOVEMENT_STOP_PATTERN) /
                          sizeof(MOVEMENT_STOP_PATTERN[0]));
  }
}

void updateSensorFaultTone() {
  if (!sensorTimedOut) {
    sensorFaultSinceMs = 0;
    sensorFaultTonePlayed = false;
    return;
  }

  if (sensorFaultSinceMs == 0) {
    sensorFaultSinceMs = millis();
    return;
  }

  if (!sensorFaultTonePlayed &&
      millis() - sensorFaultSinceMs >= SENSOR_FAULT_TONE_DELAY_MS) {
    sensorFaultTonePlayed = true;
    playBuzzerPattern(SENSOR_FAULT_PATTERN,
                      sizeof(SENSOR_FAULT_PATTERN) /
                          sizeof(SENSOR_FAULT_PATTERN[0]));
  }
}

void resetDistanceFilter() {
  distanceSampleCount = 0;
  distanceSampleIndex = 0;
  distanceFilterReady = false;
}

bool addDistanceSample(uint16_t sampleMm) {
  const uint16_t sampleCm = static_cast<uint16_t>((sampleMm + 5) / 10);
  distanceSamplesCm[distanceSampleIndex] = sampleCm;
  distanceSampleIndex = (distanceSampleIndex + 1) % DISTANCE_FILTER_SIZE;
  if (distanceSampleCount < DISTANCE_FILTER_SIZE) {
    distanceSampleCount++;
  }

  if (distanceSampleCount < DISTANCE_FILTER_MIN_SAMPLES) {
    return false;
  }

  uint16_t sorted[DISTANCE_FILTER_SIZE];
  for (size_t i = 0; i < distanceSampleCount; i++) {
    sorted[i] = distanceSamplesCm[i];
  }
  for (size_t i = 1; i < distanceSampleCount; i++) {
    const uint16_t value = sorted[i];
    size_t j = i;
    while (j > 0 && sorted[j - 1] > value) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = value;
  }

  const size_t middle = distanceSampleCount / 2;
  filteredDistanceCm = distanceSampleCount % 2 == 1
                           ? sorted[middle]
                           : static_cast<uint16_t>(
                                 (static_cast<uint32_t>(sorted[middle - 1]) +
                                  sorted[middle] + 1) /
                                 2);
  filteredDistanceMm = filteredDistanceCm * 10;
  distanceFilterReady = true;
  return true;
}

void handleDistanceTimeout() {
  sensorTimedOut = true;
  resetDistanceFilter();
}

void processDistanceReading(uint16_t distanceMm) {
  rawDistanceMm = distanceMm;

  distanceFilterReady = addDistanceSample(rawDistanceMm);
  // Keep the reading unavailable until the filter is warm.
  sensorTimedOut = !distanceFilterReady;
  if (distanceFilterReady) {
    Serial.print("Distance: raw=");
    Serial.print(rawDistanceMm);
    Serial.print(" mm, filtered=");
    Serial.print(filteredDistanceCm);
    Serial.println(" cm");
  }
}

void updateDistanceSampling() {
  const unsigned long now = millis();
  const bool motorMoving = motorChannel1Active || motorChannel2Active;
  const bool withinPostStopPeriod =
      motorStoppedAtMs != 0 && now - motorStoppedAtMs < POST_STOP_CONTINUOUS_MS;
  // Continuous ranging is also used to quickly warm or rebuild the filter.
  const bool shouldRunContinuously =
      motorMoving || withinPostStopPeriod || !distanceFilterReady;

  if (shouldRunContinuously && !sensorContinuous) {
    if (idleMeasurementPending) {
      // Abort an unfinished single-shot measurement before changing modes.
      sensor.stopContinuous();
      idleMeasurementPending = false;
    }
    sensor.startContinuous(ACTIVE_SENSOR_SAMPLE_INTERVAL_MS);
    sensorContinuous = true;
    lastSensorSampleMs = now;
    Serial.println("Distance sampling: continuous");
  } else if (!shouldRunContinuously && sensorContinuous) {
    sensor.stopContinuous();
    sensorContinuous = false;
    lastSensorSampleMs = now;
    Serial.println("Distance sampling: idle (5 seconds)");
  }

  if (sensorContinuous) {
    if (now - lastSensorSampleMs < ACTIVE_SENSOR_SAMPLE_INTERVAL_MS) {
      return;
    }

    if (sensor.dataReady()) {
      lastSensorSampleMs = now;
      processDistanceReading(sensor.read(false));
    } else if (now - lastSensorSampleMs >= 500) {
      lastSensorSampleMs = now;
      handleDistanceTimeout();
    }
    return;
  }

  if (!idleMeasurementPending) {
    if (now - lastSensorSampleMs < IDLE_SENSOR_SAMPLE_INTERVAL_MS) {
      return;
    }

    // Start a single-shot measurement without waiting for it to finish. The
    // loop remains free to service HTTP requests while the sensor ranges.
    sensor.readSingle(false);
    idleMeasurementPending = true;
    idleMeasurementStartedMs = now;
    return;
  }

  if (sensor.dataReady()) {
    processDistanceReading(sensor.read(false));
    idleMeasurementPending = false;
    lastSensorSampleMs = now;
  } else if (now - idleMeasurementStartedMs >= 500) {
    sensor.stopContinuous();
    idleMeasurementPending = false;
    lastSensorSampleMs = now;
    handleDistanceTimeout();
  }
}

bool hasPlaceholderConfiguration() {
  return String(WIFI_SSID) == "YOUR_WIFI_NETWORK" ||
         String(WIFI_PASSWORD) == "YOUR_WIFI_PASSWORD" ||
         String(API_TOKEN) == "REPLACE_WITH_A_LONG_RANDOM_TOKEN" ||
         strlen(API_TOKEN) < 32;
}

bool isAuthorized() {
  if (!server.hasHeader(AUTHORIZATION_HEADER)) {
    return false;
  }

  const String expected = String(EXPECTED_AUTHORIZATION_PREFIX) + API_TOKEN;
  return server.header(AUTHORIZATION_HEADER) == expected;
}

void sendUnauthorized() {
  server.sendHeader("WWW-Authenticate", "Bearer");
  server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
}

bool motorInputActiveNow() {
  return digitalRead(MOTOR_CHANNEL_1_PIN) == LOW ||
         digitalRead(MOTOR_CHANNEL_2_PIN) == LOW;
}

void setFirmwareUpdateLed(bool on) {
  firmwareUpdateLedOn = on;
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
  firmwareUpdateLedChangedAtMs = millis();
}

void serviceFirmwareUpdateLed() {
  if (millis() - firmwareUpdateLedChangedAtMs >=
      FIRMWARE_UPDATE_LED_INTERVAL_MS) {
    setFirmwareUpdateLed(!firmwareUpdateLedOn);
  }
}

void playFirmwareUpdateStartChirp() {
  // Upload processing occupies the main HTTP handler, so play this short
  // acknowledgement synchronously before flash writing starts.
  noTone(BUZZER_PIN);
  activeBuzzerPattern = nullptr;
  activeBuzzerPatternLength = 0;
  tone(BUZZER_PIN, 900);
  delay(90);
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(55);
  tone(BUZZER_PIN, 1500);
  delay(120);
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, HIGH);
}

void failFirmwareUpdate(FirmwareUpdateResult result, const char *message) {
  if (Update.isRunning()) {
    Update.abort();
  }
  setFirmwareUpdateLed(false);
  firmwareUpdateResult = result;
  Serial.println(message);
}

void handleFirmwareUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    firmwareUpdateResult = FirmwareUpdateResult::NotStarted;
    if (!isAuthorized()) {
      firmwareUpdateResult = FirmwareUpdateResult::Unauthorized;
      return;
    }
    if (upload.name != "firmware") {
      firmwareUpdateResult = FirmwareUpdateResult::InvalidRequest;
      Serial.println("Firmware update rejected: firmware form field missing.");
      return;
    }
    if (motorInputActiveNow()) {
      firmwareUpdateResult = FirmwareUpdateResult::MotorActive;
      Serial.println("Firmware update rejected: motor input is active.");
      return;
    }

    playFirmwareUpdateStartChirp();
    Serial.print("Firmware update started: ");
    Serial.println(upload.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      firmwareUpdateResult = FirmwareUpdateResult::BeginFailed;
      Update.printError(Serial);
      return;
    }
    firmwareUpdateResult = FirmwareUpdateResult::InProgress;
    setFirmwareUpdateLed(true);
    return;
  }

  if (firmwareUpdateResult != FirmwareUpdateResult::InProgress) {
    return;
  }

  if (motorInputActiveNow()) {
    failFirmwareUpdate(FirmwareUpdateResult::MotorActive,
                       "Firmware update aborted: motor input became active.");
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    serviceFirmwareUpdateLed();
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      failFirmwareUpdate(FirmwareUpdateResult::WriteFailed,
                         "Firmware update write failed.");
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      firmwareUpdateResult = FirmwareUpdateResult::WriteFailed;
      Serial.println("Firmware update validation failed.");
      Update.printError(Serial);
      return;
    }
    setFirmwareUpdateLed(false);
    firmwareUpdateResult = FirmwareUpdateResult::Success;
    Serial.print("Firmware update complete: ");
    Serial.print(upload.totalSize);
    Serial.println(" bytes.");
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    failFirmwareUpdate(FirmwareUpdateResult::UploadAborted,
                       "Firmware update upload was aborted.");
  }
}

void handleFirmwareUpdateComplete() {
  if (!isAuthorized() ||
      firmwareUpdateResult == FirmwareUpdateResult::Unauthorized) {
    sendUnauthorized();
    return;
  }

  switch (firmwareUpdateResult) {
    case FirmwareUpdateResult::Success:
      server.sendHeader("Cache-Control", "no-store");
      server.sendHeader("Connection", "close");
      server.send(200, "application/json",
                  "{\"updated\":true,\"restarting\":true}");
      delay(250);
      ESP.restart();
      return;
    case FirmwareUpdateResult::MotorActive:
      server.send(409, "application/json", "{\"error\":\"motor_active\"}");
      return;
    case FirmwareUpdateResult::InvalidRequest:
      server.send(400, "application/json",
                  "{\"error\":\"firmware_file_required\"}");
      return;
    case FirmwareUpdateResult::BeginFailed:
      server.send(507, "application/json",
                  "{\"error\":\"update_could_not_start\"}");
      return;
    case FirmwareUpdateResult::WriteFailed:
      server.send(400, "application/json",
                  "{\"error\":\"invalid_or_incomplete_firmware\"}");
      return;
    case FirmwareUpdateResult::UploadAborted:
      server.send(400, "application/json",
                  "{\"error\":\"upload_aborted\"}");
      return;
    default:
      server.send(400, "application/json",
                  "{\"error\":\"firmware_file_required\"}");
      return;
  }
}

bool updateHomebridgeCallbackUrl() {
  if (!server.hasArg("callback_url")) {
    return true;
  }

  const String callbackUrl = server.arg("callback_url");
  if (callbackUrl.length() > MAX_CALLBACK_URL_LENGTH ||
      (!callbackUrl.isEmpty() && !callbackUrl.startsWith("http://"))) {
    server.send(400, "application/json", "{\"error\":\"invalid_callback_url\"}");
    return false;
  }

  if (callbackUrl != homebridgeCallbackUrl) {
    homebridgeCallbackUrl = callbackUrl;
    if (homebridgeCallbackUrl.isEmpty()) {
      Serial.println("Homebridge motor callbacks disabled by status client.");
    } else {
      Serial.print("Homebridge motor callback updated: ");
      Serial.println(homebridgeCallbackUrl);
    }
  }
  homebridgeCallbackUpdatedAtMs = homebridgeCallbackUrl.isEmpty() ? 0 : millis();
  return true;
}

void handleStatus() {
  if (!isAuthorized()) {
    sendUnauthorized();
    return;
  }
  if (!updateHomebridgeCallbackUrl()) {
    return;
  }

  String response;
  response.reserve(240);
  response =
      "{\"distance_mm\":" + String(filteredDistanceMm) +
      ",\"raw_distance_mm\":" + String(rawDistanceMm) +
      ",\"distance_cm\":" + String(filteredDistanceCm) +
      ",\"filtered_distance_mm\":" + String(filteredDistanceMm) +
      ",\"filter_ready\":" + (distanceFilterReady ? "true" : "false") +
      ",\"sensor_timeout\":" + (sensorTimedOut ? "true" : "false") +
      ",\"motor_channel_1_active\":" +
      (motorChannel1Active ? "true" : "false") +
      ",\"motor_channel_2_active\":" +
      (motorChannel2Active ? "true" : "false") +
      ",\"uptime_ms\":" + String(millis()) + "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", response);
}

void connectToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. ESP32 address: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi connection failed. Check include/secrets.h.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.print("Binary Load Lifter firmware starting. Build: ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);
  pinMode(STATUS_LED_PIN, OUTPUT);
  setFirmwareUpdateLed(false);

  pinMode(MOTOR_CHANNEL_1_PIN, INPUT_PULLUP);
  pinMode(MOTOR_CHANNEL_2_PIN, INPUT_PULLUP);
  motorChannel1Active = digitalRead(MOTOR_CHANNEL_1_PIN) == LOW;
  motorChannel2Active = digitalRead(MOTOR_CHANNEL_2_PIN) == LOW;
  motorChannel1CandidateActive = motorChannel1Active;
  motorChannel2CandidateActive = motorChannel2Active;
  motorCandidateSinceMs = millis();
  printMotorInputs();

  Wire.begin(SDA_PIN, SCL_PIN);

  sensor.setTimeout(500);
  if (!sensor.init()) {
    Serial.println("Could not start the VL53L1X sensor.");
    while (true) {
      delay(1000);
    }
  }

  sensor.setDistanceMode(VL53L1X::Long);
  sensor.setMeasurementTimingBudget(100000);
  sensor.startContinuous(ACTIVE_SENSOR_SAMPLE_INTERVAL_MS);
  sensorContinuous = true;
  lastSensorSampleMs = millis();
  Serial.println("Distance sensor ready.");

  if (hasPlaceholderConfiguration()) {
    Serial.println("Web API is disabled: configure include/secrets.h first.");
    return;
  }

  connectToWifi();
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!MDNS.begin(MDNS_HOSTNAME)) {
    Serial.println("mDNS could not start; use the ESP32 IP address instead.");
  } else {
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS name: http://");
    Serial.print(MDNS_HOSTNAME);
    Serial.println(".local");
  }

  server.collectHeaders(&AUTHORIZATION_HEADER, 1);
  server.on("/v1/status", HTTP_GET, handleStatus);
  server.on("/v1/update", HTTP_POST, handleFirmwareUpdateComplete,
            handleFirmwareUpload);
  server.begin();
  Serial.println("Secure status and firmware update API is ready.");
  startMotorCallbackTask();
  playBuzzerPattern(READY_PATTERN,
                    sizeof(READY_PATTERN) / sizeof(READY_PATTERN[0]));
}

void loop() {
  serviceBuzzer();
  expireHomebridgeCallbackUrl();
  readMotorInputs();

  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  updateDistanceSampling();

  updateSensorFaultTone();
}
