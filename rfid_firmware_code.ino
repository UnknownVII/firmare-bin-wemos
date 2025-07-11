#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "secrets.h"
#define CURRENT_FIRMWARE_VERSION "1.0.0"
#define EEPROM_SIZE 512
#define TOKEN_ADDR 0

#define SS_PIN D8
#define RST_PIN D4

#define LED_GREEN D1
#define LED_RED D0
#define LED_YELLOW D2
#define BUZZER D3

MFRC522 mfrc522(SS_PIN, RST_PIN);

String channelName = "";

// Function prototypes
void shortBeep();
void doubleBeep();
void longBeep();
void indicateStatus(bool success);
String readChannelFromEEPROM();
void writeChannelToEEPROM(const String& value);
String accessToken = "";
const String& ACCESS_TOKEN = accessToken;


void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("📡 RFID reader initialized.");

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(LED_YELLOW, HIGH);  // Startup

  WiFiManager wifiManager;
  wifiManager.autoConnect("RFID-Config");

  EEPROM.begin(EEPROM_SIZE);

  accessToken = readAccessTokenFromEEPROM();

  // if (accessToken.length() > 0 && accessToken.startsWith("ey")) {
  //   Serial.println("🔁 Using saved access token:");
  //   Serial.println(accessToken);
  // } else {
  Serial.println("🌐 Requesting new access token...");

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    http.begin(client, String(BASE_URL) + "/oauth/token");
    http.addHeader("Content-Type", "application/json");

    String payload = "{";
    payload += "\"grant_type\": \"" + String(GRANT_TYPE) + "\",";
    payload += "\"client_id\": \"" + String(CLIENT_ID) + "\",";
    payload += "\"client_secret\": \"" + String(CLIENT_SECRET) + "\",";
    payload += "\"scope\": \"" + String(SCOPE) + "\"";
    payload += "}";
    Serial.printf("Payload", payload);

    int httpCode = http.POST(payload);
    Serial.printf("[Token Request] HTTP %d\n", httpCode);

    if (httpCode == 200) {
      String response = http.getString();
      StaticJsonDocument<1024> doc;
      DeserializationError err = deserializeJson(doc, response);
      if (!err && doc["access_token"]) {
        accessToken = doc["access_token"].as<String>();
        writeAccessTokenToEEPROM(accessToken);
        Serial.println("✅ New access token saved:");
        Serial.println(accessToken);
      } else {
        Serial.println("❌ JSON parse error.");
      }
    } else {
      Serial.println("❌ Token request failed.");
      Serial.println(http.getString());
    }

    http.end();
  } else {
    Serial.println("⚠️ No WiFi. Cannot request token.");
  }
  // }

  digitalWrite(LED_YELLOW, LOW);  // Ready

  ArduinoOTA.setHostname("wemos-rfid");
  ArduinoOTA.begin();

  Serial.println("✅ Ready for OTA updates v3");

  checkAndUpdateFirmware();
}

void loop() {
  ArduinoOTA.handle();

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  shortBeep();

  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();
  Serial.println("🔍 Scanned UID: " + uid);

  if (WiFi.status() == WL_CONNECTED) {
    if (ACCESS_TOKEN.length() == 0) {
      Serial.println("⚠️ No access token available.");
      return;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = String(BASE_URL) + "/api/v1/clock-in";
    Serial.println("🌐 Requesting URL: " + url);

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + ACCESS_TOKEN);

    // Send UID as JSON body
    String payload = "{\"rfid_uid\": \"" + uid + "\"}";

    int httpCode = http.POST(payload);

    Serial.print("HTTP Response Code: ");
    Serial.println(httpCode);

    if (httpCode == 200) {
      Serial.println("✅ Access Granted");
      indicateStatus(true);
    } else if (httpCode == 429 || httpCode == 425) {
      Serial.println("[-] Access Hold");
      indicateHoldStatus();
    } else {
      Serial.println("❌ Access Denied");
      indicateStatus(false);
    }

    http.end();
  } else {
    Serial.println("⚠️ No WiFi!");
    digitalWrite(LED_YELLOW, HIGH);
    delay(1000);
    digitalWrite(LED_YELLOW, LOW);
  }

  delay(1000);  // debounce
}


void shortBeep() {
  digitalWrite(BUZZER, HIGH);
  delay(100);
  digitalWrite(BUZZER, LOW);
}

void doubleBeep() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(100);
    digitalWrite(BUZZER, LOW);
    delay(100);
  }
}

void longBeep() {
  digitalWrite(BUZZER, HIGH);
  delay(600);
  digitalWrite(BUZZER, LOW);
}

void tripleBeep() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(100);
    digitalWrite(BUZZER, LOW);
    delay(100);
  }
}


void indicateStatus(bool success) {
  if (success) {
    digitalWrite(LED_GREEN, HIGH);
    doubleBeep();
    delay(1000);
    digitalWrite(LED_GREEN, LOW);
  } else {
    digitalWrite(LED_RED, HIGH);
    longBeep();
    delay(1000);
    digitalWrite(LED_RED, LOW);
  }
}

void indicateHoldStatus() {
  digitalWrite(LED_YELLOW, HIGH);
  tripleBeep();
  delay(1000);
  digitalWrite(LED_YELLOW, LOW);
}

String readAccessTokenFromEEPROM() {
  char buffer[EEPROM_SIZE];
  for (int i = 0; i < EEPROM_SIZE; i++) {
    buffer[i] = EEPROM.read(TOKEN_ADDR + i);
    if (buffer[i] == '\0') break;
  }
  return String(buffer);
}

void writeAccessTokenToEEPROM(const String& token) {
  int len = token.length();
  if (len >= EEPROM_SIZE) len = EEPROM_SIZE - 1;

  for (int i = 0; i < len; i++) {
    EEPROM.write(TOKEN_ADDR + i, token[i]);
  }
  EEPROM.write(TOKEN_ADDR + len, '\0');  // Null terminator
  EEPROM.commit();
}

void checkAndUpdateFirmware() {
  Serial.println("🌐 Checking firmware version...");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (http.begin(client, VERSION_URL)) {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String latestVersion = http.getString();
      latestVersion.trim(); // Remove whitespace and newline

      Serial.print("🆚 Current firmware version: ");
      Serial.println(CURRENT_FIRMWARE_VERSION);
      Serial.print("🆕 Latest firmware version: ");
      Serial.println(latestVersion);

      if (strcmp(CURRENT_FIRMWARE_VERSION, latestVersion.c_str()) != 0) {
        Serial.println("⬆️ New firmware available. Starting OTA update...");

        t_httpUpdate_return ret = ESPhttpUpdate.update(client, FIRMWARE_URL);
        switch (ret) {
          case HTTP_UPDATE_FAILED:
            Serial.printf("❌ OTA failed: %s\n", ESPhttpUpdate.getLastErrorString().c_str());
            break;
          case HTTP_UPDATE_NO_UPDATES:
            Serial.println("ℹ️ No update available.");
            break;
          case HTTP_UPDATE_OK:
            Serial.println("✅ Update successful, rebooting...");
            break;
        }
      } else {
        Serial.println("✅ Firmware is up to date.");
      }

    } else {
      Serial.printf("❌ Failed to fetch version.txt, HTTP code: %d\n", httpCode);
    }
    http.end();
  } else {
    Serial.println("❌ Connection to VERSION_URL failed.");
  }
}
