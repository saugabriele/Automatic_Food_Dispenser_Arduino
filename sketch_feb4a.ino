#include <Servo.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#define trigPin D5
#define echoPin D7
#define pirPin D6
#define servoPIN D8
#define ssid "HUAWEI P20"
#define password "1f657a642e3b"
#define DEV_ID 11
#define DISPENSER_MEASURE 60000
#define PIR_MEASURE 3000
#define MAX_MEASURES 15
#define SCHEDULE_DEBOUNCE 80000

typedef struct {
  float value;
  String date;
} Measure;

int remote_feeding_request = 0;
long lastMeasure_dispenser = 0;
long lastMeasure_pirSensor = 0;
long last_schedule_hour = 0;
int numMeasures_dispenser = 0;
int numMeasures_pir_sensor = 0;  //openings
int schedule_minute = 0;
int scheduledHours[] = { 4, 7, 10, 13, 16, 19 };
Measure measures_disp[MAX_MEASURES];
Measure measures_pir_sensor[MAX_MEASURES];
String IP = "192.168.43.65";
String PORT = "5000";
String ENDPOINT = "/sendData";
String SERVER_URL = "http://" + IP + ":" + PORT + ENDPOINT;

const char* mqtt_server = "broker.mqttdashboard.com";
const char* topic_distance = "IoT_measures/distance";
const char* topic_request = "remote_feed";

WiFiClient espClient;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
PubSubClient client(espClient);
Servo microservo;

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("WiFi Connected");
}
void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }
}
void reconnect() {
  while (!client.connected()) {
    String ClientId = "ESP8266-23142446y6554";
    if (client.connect(ClientId.c_str())) {
      client.subscribe("remote_feed");
      return;
    }
  }
}

void reconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFiconnection lost. Reconnecting...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.println("Reconnecting...");
    }
    Serial.println("Reconnected to the WiFinetwork");
  }
}

void setup() {
  // Begin Serial communication at a baud rate of 9600
  Serial.begin(9600);
  // Define the trigPin as Output and echoPinas Input
  pinMode(pirPin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  microservo.attach(servoPIN, 500, 2400);
  microservo.write(0);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  long now = millis();
  timeClient.update();
  time_t n = timeClient.getEpochTime();

  if (minute(n) == schedule_minute && (now - last_schedule_hour) > SCHEDULE_DEBOUNCE) {
    for (int i = 0; i < 6; i++) {
      if (hour(n) == scheduledHours[i]) {
        feed();
        last_schedule_hour = now;
        break;
      }
    }
  }

  if (now - lastMeasure_dispenser > DISPENSER_MEASURE) {
    lastMeasure_dispenser = now;
    float distance = getDistance();
    Serial.println("Reading Dispenser Status...");
    Serial.print("Distance between sensor and food: ");
    Serial.println(distance);
    measures_disp[numMeasures_dispenser].value = distance;
    measures_disp[numMeasures_dispenser].date = getNtpTime();
    numMeasures_dispenser++;
    if (numMeasures_dispenser >= MAX_MEASURES) {
      const char* type = "dispenser_status";
      String s_id = String(DEV_ID);
      sendData(measures_disp, numMeasures_dispenser, type, s_id);
      numMeasures_dispenser = 0;
    }
  }

  if (remote_feeding_request && (now - lastMeasure_pirSensor) > PIR_MEASURE) {
    int pirState = digitalRead(pirPin);
    measures_pir_sensor[numMeasures_pir_sensor].value = pirState;
    measures_pir_sensor[numMeasures_pir_sensor].date = getNtpTime();
    numMeasures_pir_sensor++;
    if (pirState) {
      Serial.print("Pir State:");
      Serial.print(pirState);
      feed();

      remote_feeding_request = 0;
    }
    if (numMeasures_pir_sensor >= MAX_MEASURES) {
      const char* type = "pir_status";
      String s_id = String(DEV_ID);
      sendData(measures_pir_sensor, numMeasures_pir_sensor, type, s_id);
      numMeasures_pir_sensor = 0;
    }
  }

  //client.publish(topic_distance, String(distance).c_str());
  delay(1000);

  reconnectWiFi();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  if (String(topic) == "remote_feed" && message == "1") {
    remote_feeding_request = 1;
  }
}

void feed() {
  microservo.write(180);
  delay(3000);
  microservo.write(0);
}

float getDistance() {
  // Clear the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Set the trigPinHIGH for 10 microseconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  // Read the echoPin. pulseIn() returns the duration (length of the pulse) in microseconds
  long duration = pulseIn(echoPin, HIGH);
  // Calculate and return the distance in centimeters. Speed of sound is approximately 343 meters per second, or 0.0343 cm per microsecond.
  float distance = duration * 0.0343 / 2;
  return distance;
}

String getNtpTime() {
  timeClient.update();
  time_t now = timeClient.getEpochTime();
  String formattedTime = format_date(now);
  return formattedTime;
}

String format_date(time_t t) {
  char buf[20];
  sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d",
          year(t), month(t), day(t), hour(t), minute(t), second(t));
  return String(buf);
}

void sendData(Measure* measures, int numMeasures, const char* type, String s_id) {
  checkWiFi();
  if (numMeasures <= 0) {
    return;
  }
  Serial.println("Attempting to send data");
  HTTPClient http;
  http.begin(espClient, SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  String payload = "[";
  for (int i = 0; i < numMeasures; i++) {
    payload += "{\"date\":\"" + measures[i].date + "\",\"value\":\"" + String(measures[i].value) + "\",\"type\":\"" + type + "\",\"s_id\":\"" + s_id + "\"}";
    if (i < numMeasures - 1) {
      payload += ",";
    }
  }
  payload += "]";
  int httpResponseCode = http.POST(payload);
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}
