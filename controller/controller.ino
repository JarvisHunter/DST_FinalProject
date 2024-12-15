#include <esp_now.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>

typedef struct SentDataProperties {
  int new_state;
} SentDataProperties;

typedef struct ReceivedDataProperties {
  int current_state;
} ReceivedDataProperties;

class Button {
  private:
    int id;
    uint8_t mac_address[6];
    int state;

  public:
    Button() : id(-1), state(LOW) {
      memset(mac_address, 0, sizeof(mac_address));
    }

    Button(const uint8_t mac[6], int initialState = LOW) {
      for (int i = 0; i < 6; i++) {
        mac_address[i] = mac[i];
      }
      state = initialState;
    }

    void setState(int new_state) {
      SentDataProperties sentData;
      sentData.new_state = new_state;

      esp_err_t result = esp_now_send(mac_address, (uint8_t *)&sentData, sizeof(SentDataProperties));

      if (result == ESP_OK) {
        state = new_state;
      }
      delay(100);
    }

    int getState() const {
      return state;
    }

    bool is_sent_from_this_button(const uint8_t mac[6]) const {
      for (int i = 0; i < 6; i++) {
        if (mac_address[i] != mac[i]) {
          return false;
        }
      }

      return true;
    }
};

//---------------------------------------------

const uint8_t broadcastAddresses[][6] = {
  {0x24, 0xd7, 0xeb, 0xc6, 0xc2, 0x24},
  {0x08, 0xf9, 0xe0, 0x6a, 0x99, 0x91},
  {0x08, 0xf9, 0xe0, 0x6b, 0x78, 0x05},
  {0x08, 0xf9, 0xe0, 0x6a, 0xad, 0x32},
};

const int MAX_BUTTONS = 10; // Max number of buttons
Button active_buttons[MAX_BUTTONS];
int active_button_count = 0;

const int pinLED = 2;
const int pinButton1 = 4;
const int pinButton2 = 5;

// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 4;
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);  

enum GameState { GAME_PENDING, ERROR_STATE, PLAYING, PAUSED, ENDED };
GameState game_state = GAME_PENDING;

esp_now_peer_info_t peerInfo;

//---------------------------------------------
const unsigned long GAME_DURATION = 20000;
int score;
bool timer_running = false;
unsigned long start_time, current_time;

void activate_random_button() {
    bool is_new_button_activated = false;

     // Create a list to hold the indices of the off buttons
    int off_buttons[active_button_count];
    int off_button_count = 0;

    // Collect the indices of the off buttons
    for (int i = 0; i < active_button_count; i++) {
      if (active_buttons[i].getState() == LOW) {
        off_buttons[off_button_count++] = i;
      }
    }

    // Randomly shuffle the off_buttons array
    for (int i = 0; i < off_button_count; i++) {
      int random_index = random(i, off_button_count);
      // Swap buttons
      int temp = off_buttons[i];
      off_buttons[i] = off_buttons[random_index];
      off_buttons[random_index] = temp;
    }

    // Try turning on the shuffled buttons
    for (int i = 0; i < off_button_count; i++) {
      int button_index = off_buttons[i];
      active_buttons[button_index].setState(HIGH);

      // Check if the button state is successfully changed to HIGH
      if (active_buttons[button_index].getState() == HIGH) {
        Serial.print("Successfully activated button ");
        Serial.println(button_index);
        is_new_button_activated = true;
        break;
      }
    }

    if (!is_new_button_activated) {
      Serial.println("Cannot activate any button!");
      game_state = ERROR_STATE;
    }
}


// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  char macStr[18];
  Serial.print("Packet to: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
  Serial.print(" send status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// callback when data is received
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  ReceivedDataProperties receivedData;
  bool is_new_button = true;
  memcpy(&receivedData, incomingData, sizeof(ReceivedDataProperties));

  for (int i = 0; i < active_button_count; i++) {
    if (active_buttons[i].is_sent_from_this_button(recv_info->src_addr)) {
      is_new_button = false;
      Serial.print("Deactivating button ");
      Serial.println(i);
      active_buttons[i].setState(LOW);
      break;
    }
  }

  if (is_new_button && active_button_count < MAX_BUTTONS) {
    for (int i = 0; i < sizeof(broadcastAddresses) / sizeof(broadcastAddresses[0]); i++) {
      if (memcmp(broadcastAddresses[i], recv_info->src_addr, 6) == 0) {
        memcpy(peerInfo.peer_addr, broadcastAddresses[i], 6);
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
          Serial.print("Failed to add peer: ");
          Serial.println(i);
          return;
        } else {
          active_buttons[active_button_count++] = Button(broadcastAddresses[i], LOW);
        }
      }
    }
  }

  if (game_state == PLAYING) {
    score += 1;
    activate_random_button();
  }
}

int game_mode = 1;
int last_game_mode = -1; 
int last_game_state = -1;
unsigned long last_time_left = 0;
int last_score = -1;

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.println("Intializing...");
  // initialize LCD
  lcd.init();
  // turn on LCD backlight                      
  lcd.backlight();

  if (esp_now_init() != ESP_OK) {
    game_state = ERROR_STATE;
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  active_button_count = 0;

  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  for (int i = 0; i < sizeof(broadcastAddresses) / sizeof(broadcastAddresses[0]); i++) {
    memcpy(peerInfo.peer_addr, broadcastAddresses[i], 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.print("Failed to add peer: ");
      Serial.println(i);
      return;
    } else {
      active_buttons[active_button_count++] = Button(broadcastAddresses[i], LOW);
    }
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  pinMode(pinLED, OUTPUT);
  pinMode(pinButton1, INPUT_PULLUP);
  pinMode(pinButton2, INPUT_PULLUP);

}

void handle_pending_state() {
}

void handle_playing_state() {
  current_time = millis();

  // Only calculate the time left if in Game Mode 1
  unsigned long time_left = 0;
  if (game_mode == 1) {
    unsigned long elapsed_time = current_time - start_time;
    time_left = (GAME_DURATION > elapsed_time) ? (GAME_DURATION - elapsed_time) / 1000 : 0;

    // Transition to ENDED state when time is up in Game Mode 1
    if (time_left == 0) {
      game_state = ENDED;
    }
  }

  // Update LCD only if values have changed
  if (game_mode == 1 && (time_left != last_time_left || score != last_score)) {
    lcd.setCursor(0, 2);
    lcd.print("Time left: ");
    lcd.print(time_left);
    lcd.print("  "); // Clear extra digits
    last_time_left = time_left;
  }

  if (score != last_score) {
    lcd.setCursor(0, 3);
    lcd.print("Score: ");
    lcd.print(score);
    lcd.print("  "); // Clear extra digits
    last_score = score; // Update the last displayed score
  }

  Serial.print("Current score: ");
  Serial.print(score);
  if (game_mode == 1) {
    Serial.print(". Time left: ");
    Serial.println(time_left);
  } else {
    Serial.println(" (unlimited mode)");
  }
}


void handle_paused_state() {
  if (game_mode == 1) {
    lcd.setCursor(0, 2);
    lcd.print("Time left: ");
    lcd.print(last_time_left);
    lcd.print("  "); // Clear extra digits
  }
  lcd.setCursor(0, 3);
  lcd.print("Score: ");
  lcd.print(score);
  lcd.print("  "); // Clear extra digits

  Serial.print("Time left: ");
  Serial.print(last_time_left);
  Serial.print(". Score: ");
  Serial.println(score);
}


void handle_error_state() {
  Serial.println("Error occurred!");
  delay(1000);
}


void handle_ended_state() {
  Serial.print("Game over. Final score: ");
  Serial.println(score);
  lcd.setCursor(0, 3);
  lcd.print("Score: ");
  lcd.print(score);
  lcd.print("  "); // Clear extra digits
}

void loop() {
  int button1 = digitalRead(pinButton1);
  int button2 = digitalRead(pinButton2);

  // Handle Button 1 for toggling game mode
  if (button1 == LOW) {
    Serial.println("Button 1 pushed!");
    if (game_state == GAME_PENDING) {
      game_mode = (game_mode == 1) ? 2 : 1; // Toggle between game modes
      delay(300); // Debounce delay
    }
    if (game_state == PAUSED) {
      game_state = ENDED;
    }
  }

  // Handle Button 2 for starting, pausing, or resetting the game
  if (button2 == LOW) {
    Serial.println("Button 2 pushed!");
    if (game_state == GAME_PENDING) {
      start_time = millis();
      score = 0;
      last_time_left = GAME_DURATION / 1000; // Reset time display
      last_score = -1;                      // Force LCD update
      for (int i = 0; i < active_button_count; i++) {
        active_buttons[i].setState(LOW);
      }
      game_state = PLAYING;
      activate_random_button();
    } else if (game_state == PLAYING) {
      game_state = PAUSED; // Transition to PAUSED state
    } else if (game_state == PAUSED) {
      // Resume the game
      if (game_mode == 1) {
        start_time = millis() - (GAME_DURATION - last_time_left * 1000); // Adjust start time
      }
      game_state = PLAYING;
    } else if (game_state == ENDED) {
      game_state = GAME_PENDING;
    }
    delay(300); // Debounce delay
  }

  // Update game mode display if it changes
  if (game_mode != last_game_mode) {
    lcd.setCursor(0, 0);
    lcd.print("Game Mode: ");
    lcd.print(game_mode);
    lcd.print("  "); // Clear extra digits
    last_game_mode = game_mode; // Update the last displayed game mode
  }

  // Handle game state transitions
  if (game_state != last_game_state) {
    lcd.clear(); // Clear the LCD only when the state changes
    lcd.setCursor(0, 1);
    lcd.print("Game state: ");
    switch (game_state) {
      case ERROR_STATE:
        Serial.println("ERROR");
        lcd.print("ERROR");
        handle_error_state();
        break;
      case GAME_PENDING:
        Serial.println("PENDING");
        lcd.print("PENDING");
        handle_pending_state();
        break;
      case PLAYING:
        Serial.println("PLAYING");
        lcd.print("PLAYING");
        handle_playing_state();
        break;
      case PAUSED:
        Serial.println("PAUSED");
        lcd.print("PAUSED");
        handle_paused_state();
        break;
      case ENDED:
        Serial.println("ENDED");
        lcd.print("ENDED");
        handle_ended_state();
        break;
    }
    last_game_state = game_state; // Track the last state
  }

  if (game_state == PLAYING) {
    handle_playing_state(); // Update the display during gameplay
  }
}


