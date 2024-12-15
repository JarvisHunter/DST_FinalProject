
#include <ESP8266WiFi.h>
#include <espnow.h>

typedef struct ReceivedDataProperties {
  int new_state;
} ReceivedDataProperties;

typedef struct SentDataProperties {
  int current_state;
} SentDataProperties;

uint8_t motherboardBroadcastAddress[] = {0x14, 0x2b, 0x2f, 0xc6, 0xf2, 0x28};


const int pinLED = D0;
const int pinIR = D1;

int current_state = LOW;
ReceivedDataProperties receivedData;

//callback function that will be executed when data is received
void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&receivedData, incomingData, sizeof(ReceivedDataProperties));
  current_state = receivedData.new_state;
  Serial.print("new state: ");
  Serial.println(current_state);
}

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Last Packet Send Status: ");
  if (sendStatus == 0) {
    Serial.println("Delivery success");
  }
  else {
    Serial.println("Delivery fail");
  }
}

void setup() {
  //Initialize Serial Monitor
  Serial.begin(115200);
  
  //Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  //Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_add_peer(motherboardBroadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  pinMode(pinLED, OUTPUT);
  pinMode(pinIR, INPUT_PULLDOWN_16); 

  current_state = LOW;
}
 
void loop() {
  int sensor = digitalRead(pinIR);
  // if (sensor == LOW) {
  //   digitalWrite(pinLED, HIGH);
  // }
  // else digitalWrite(pinLED, LOW);
  digitalWrite(pinLED, current_state);
  if (sensor == LOW && current_state == HIGH) {
    SentDataProperties sentData;
    sentData.current_state = current_state;
    Serial.println("Obstacle!");
    digitalWrite(pinLED, LOW);
    esp_now_send(0, (uint8_t *) &sentData, sizeof(SentDataProperties));
    current_state = LOW;
    delay(100);
  }

}