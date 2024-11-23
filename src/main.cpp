#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>  // WiFiManager library
#include <PubSubClient.h>
#include <Preferences.h> // For non-volatile storage (NVS)

// Define custom parameters for additional SSID and Password
char customSSID2[32] = "";  // Placeholder for the second Wi-Fi SSID
char customPass2[64] = "";  // Placeholder for the second Wi-Fi Password

// Create custom parameter objects for the web portal
WiFiManagerParameter custom_ssid2("ssid2", "WiFi SSID 2", customSSID2, 32);
WiFiManagerParameter custom_pass2("pass2", "WiFi Password 2", customPass2, 64);

// Preferences object for NVS storage
Preferences preferences;

// Function to save Wi-Fi credentials to NVS
void saveCredentials(const char* ssid1, const char* pass1, const char* ssid2, const char* pass2) {
    preferences.begin("wifi-config", false);
    preferences.putString("ssid1", ssid1);
    preferences.putString("pass1", pass1);
    preferences.putString("ssid2", ssid2);
    preferences.putString("pass2", pass2);
    preferences.end();
    Serial.println("Wi-Fi credentials saved to NVS!");
}

// Function to load Wi-Fi credentials from NVS
void loadCredentials(String& ssid1, String& pass1, String& ssid2, String& pass2) {
    preferences.begin("wifi-config", true);
    ssid1 = preferences.getString("ssid1", "");
    pass1 = preferences.getString("pass1", "");
    ssid2 = preferences.getString("ssid2", "");
    pass2 = preferences.getString("pass2", "");
    preferences.end();
}

// WiFi and MQTT credentials
const char* mqtt_server = "broker2.dma-bd.com";
const char* mqtt_user = "broker2";
const char* mqtt_password = "Secret!@#$1234";




const int maxAttempts = 30;              // 15 seconds: 30 x 500ms
const TickType_t restTime = pdMS_TO_TICKS(120000); // 2 minutes
const int maxRetries = 5;                // Maximum retries before restart
int retryCount = 0;                      // Counter for retry attempts


// WiFi and MQTT connection settings
#define WIFI_ATTEMPT_COUNT 6
#define WIFI_ATTEMPT_DELAY 1000
#define WIFI_WAIT_COUNT 5
#define WIFI_WAIT_DELAY 1000
#define MAX_WIFI_ATTEMPTS 2
#define MQTT_ATTEMPT_COUNT 5
#define MQTT_ATTEMPT_DELAY 5000

WiFiClient espClient;
PubSubClient client(espClient);

TaskHandle_t networkTask;
TaskHandle_t mainTask;
TaskHandle_t wifiResetTask;
TaskHandle_t secondTask;

// WiFi and MQTT attempt counters
int wifiAttemptCount = WIFI_ATTEMPT_COUNT;
int wifiWaitCount = WIFI_WAIT_COUNT;
int maxWifiAttempts = MAX_WIFI_ATTEMPTS;
int mqttAttemptCount = MQTT_ATTEMPT_COUNT;

// Debug mode setting
#define DEBUG_MODE true
#define DEBUG_PRINT(x)  if (DEBUG_MODE) { Serial.print(x); }
#define DEBUG_PRINTLN(x) if (DEBUG_MODE) { Serial.println(x); }

// Button setup for WiFi reset
#define WIFI_RESET_BUTTON_PIN 23  // GPIO pin for reset button
bool wifiResetFlag = false;


// MQTT callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    DEBUG_PRINTLN("Message arrived: " + message);
}








// Function to connect to a specific Wi-Fi network
bool connectToWiFi(const char* ssid, const char* password) {
    Serial.printf("Attempting to connect to Wi-Fi: %s\n", ssid);

    WiFi.begin(ssid, password); // Start connection to Wi-Fi
    int attempts = 0;

    while (attempts < maxAttempts) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWi-Fi Connected!");
            Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
            return true; // Successful connection
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Non-blocking delay (500 ms)
        Serial.print(".");
        attempts++;
    }

    Serial.println("\nConnection attempt failed.");
    return false; // Connection failed
}




void connectToMQTT() {
    const int maxRetries = 5;                       // Maximum connection attempts before rest
    const int maxRestCycles = 5;                    // Maximum rest cycles before restart
    const TickType_t retryDelay = pdMS_TO_TICKS(5000);  // 5 seconds between retries
    const TickType_t restDelay = pdMS_TO_TICKS(180000); // 3 minutes rest time
    int retryCount = 0;                             // Retry attempt counter
    int restCycleCount = 0;                         // Rest cycle counter

    while (true) {
        while (!client.connected() && retryCount < maxRetries) {
            // Generate a unique client ID
            String clientId = "ESP32Client-" + String(random(0xffff), HEX);

            DEBUG_PRINTLN("Attempting MQTT connection...");

            // Attempt to connect
            if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
                DEBUG_PRINTLN("MQTT connected successfully!");
                client.subscribe("DMA/Energy/1101"); // Subscribe to topic
                return; // Exit once connected
            }

            // Log failure and increment retry count
            DEBUG_PRINTLN("MQTT connection failed. Error Code: " + String(client.state()));
            retryCount++;
            DEBUG_PRINTLN("Retry attempt: " + String(retryCount) + "/" + String(maxRetries));
            vTaskDelay(retryDelay); // Delay before next attempt
        }

        // Check if maximum retries have been reached
        if (!client.connected()) {
            restCycleCount++;
            DEBUG_PRINTLN("Maximum retries reached. Resting for 3 minutes...");
            DEBUG_PRINTLN("Rest cycle: " + String(restCycleCount) + "/" + String(maxRestCycles));
            vTaskDelay(restDelay); // Rest for 3 minutes

            // Check if maximum rest cycles have been exceeded
            if (restCycleCount >= maxRestCycles) {
                DEBUG_PRINTLN("Max rest cycles reached. Restarting ESP...");
                ESP.restart(); // Restart the ESP32
            }

            // Reset retry count after a rest cycle
            retryCount = 0;
        }
    }
}





// Function to start the WiFiManager portal
void startConfigPortal() {
    Serial.println("Starting configuration portal...");
    WiFiManager wifiManager;

    // Add custom parameters for the second SSID and password
    wifiManager.addParameter(&custom_ssid2);
    wifiManager.addParameter(&custom_pass2);

    // Start the configuration portal
    if (!wifiManager.startConfigPortal("ESP32_ConfigPortal", "password123")) {
        Serial.println("Failed to connect or configure Wi-Fi. Restarting...");
        ESP.restart();
    }

    // Retrieve the values from the custom parameters
    const char* ssid2 = custom_ssid2.getValue();
    const char* pass2 = custom_pass2.getValue();

    // Save the entered Wi-Fi credentials to NVS
    saveCredentials(WiFi.SSID().c_str(), WiFi.psk().c_str(), ssid2, pass2);

    Serial.println("Configuration complete. Reconnecting to the best Wi-Fi...");
    // connectToBestWiFi();
    ESP.restart();
}



// Task for network and MQTT management
void WifiMqtt_Task(void *param) {
    int restart_count = 0;

    while (1) {
        // If not connected to Wi-Fi
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.mode(WIFI_STA);
            Serial.println("\nDisconnected from Wi-Fi. Attempting to reconnect...");
            
            String ssid1, pass1, ssid2, pass2;
            loadCredentials(ssid1, pass1, ssid2, pass2); // Load stored credentials

            // Attempt to connect to the first Wi-Fi network
            if (connectToWiFi(ssid1.c_str(), pass1.c_str())) {
                Serial.println("Connected to the primary network.");
            }
            // If the first network fails, attempt the second
            else if (connectToWiFi(ssid2.c_str(), pass2.c_str())) {
                Serial.println("Connected to the secondary network.");
            }
            // If both networks fail, handle retries
            else {
                restart_count++;
                Serial.printf("Retry count: %d\n", restart_count);
                if (restart_count > 5) {
                    Serial.println("Maximum retry attempts reached. Restarting ESP...");
                    ESP.restart(); // Restart ESP32 after too many failed attempts
                }
                vTaskDelay(restTime); // Delay before next retry
                continue; // Retry connection
            }
        }

        // If Wi-Fi is connected but MQTT is not connected
        if (WiFi.status() == WL_CONNECTED && !client.connected()) {
            connectToMQTT(); // Reconnect to MQTT
        }

        // Handle MQTT loop
        client.loop();
        vTaskDelay(pdMS_TO_TICKS(10)); // Non-blocking delay (10 ms)
    }
}

// Task for WiFi reset and WiFiManager setup
void wifiResetLoop(void *param) {
    while (1) {
        if (digitalRead(WIFI_RESET_BUTTON_PIN) == LOW) {
            // Suspend other tasks to avoid conflict
            vTaskSuspend(networkTask);
            vTaskSuspend(mainTask);
            vTaskSuspend(secondTask);

            DEBUG_PRINTLN("Starting WiFiManager for new WiFi setup...");
            WiFiManager wifiManager;
            wifiManager.resetSettings();  // Clear previous settings
            wifiManager.autoConnect("ESP32-WiFi-Setup");  // Start AP for new configuration

            DEBUG_PRINTLN("New WiFi credentials set, restarting...");
            ESP.restart();
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);  // Check every 100 ms
    }

}

// Task for main loop (e.g., performing your application logic)
void mainLoop(void *param) {
    while (1) {
        // DEBUG_PRINTLN("Main task running...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void secondLoop(void *param) {
    while (1) {
        // DEBUG_PRINTLN("Second task running...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// Setup function
void setup() {
    Serial.begin(115200);

    // Button setup
    pinMode(WIFI_RESET_BUTTON_PIN, INPUT_PULLUP);

    // Set up MQTT client
    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttCallback);

    // Create tasks
    xTaskCreatePinnedToCore(WifiMqtt_Task, "Network Task", 6144, NULL, 1, &networkTask, 0);
    xTaskCreatePinnedToCore(wifiResetLoop, "WiFi Reset Task", 2048, NULL, 1, &wifiResetTask, 0);
    xTaskCreatePinnedToCore(mainLoop, "Main Task", 10240, NULL, 1, &mainTask, 1);
    xTaskCreatePinnedToCore(secondLoop, "Secondary Task", 4096, NULL, 1, &secondTask, 1);
}

// Check for WiFi reset button press in loop
void loop() {
    
}