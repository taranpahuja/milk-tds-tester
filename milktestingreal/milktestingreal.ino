#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <cmath>
#include <algorithm>
#include <string.h>
#include <stdio.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

// --- Function Prototypes ---
String getDateTimeString();
String getDateOnlyString();
String getTimeOnlyString();
float getTdsValue(int analogPin);
void readTdsSensors();

// Pin definitions for TFT
#define TFT_CS 15
#define TFT_DC 4
#define TFT_RST 2

// Pin definitions for Buttons
#define BTN_TOGGLE 5
#define BTN_SETTINGS 27
#define BTN_INCREASE 26
#define BTN_DECREASE 12

// Pin definitions for TDS Analog Inputs
#define ANALOG_PIN_TDS_0 36
#define ANALOG_PIN_TDS_1 34
#define ANALOG_PIN_TDS_2 35
#define ANALOG_PIN_TDS_3 32

// Initialize TFT display object
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

const int SCREEN_W = 320; // TFT screen width
const int SCREEN_H = 240; // TFT screen height

// Global variables for sensor data and calibration
float liveSensorReadings[4];
float calibrationFactors[4] = { 1.0, 1.0, 1.0, 1.0 }; // Initial calibration factors
float prevCalibrationFactors[4]; // Stores previous factors for 'undo' functionality

// Button state and debounce variables
unsigned long lastPressToggle = 0;
unsigned long lastPressIncreaseTime = 0;
unsigned long lastPressDecreaseTime = 0;
unsigned long settingsButtonPressStartTime = 0;

bool lastBtnIncreaseState = HIGH;
bool lastBtnDecreaseState = HIGH;
bool lastBtnSettingsState = HIGH;
bool lastBtnToggleState = HIGH;

const unsigned long debounceDelay = 200;       // Debounce time for buttons
const unsigned long longPressDuration = 1000;  // Duration for a long press
const unsigned long refreshDisplayInterval = 1000; // Interval for refreshing dynamic TFT elements
unsigned long lastDisplayRefreshTime = 0;

unsigned long lastTdsReadTime = 0;
const unsigned long tdsReadInterval = 500; // Interval for reading TDS sensors

// Enum for managing different display modes on the TFT
enum DisplayMode {
  MAIN_DISPLAY,
  SAMPLE_SELECTION_MODE,
  DIGIT_EDITING_MODE,
  WIFI_INFO_DISPLAY
};
DisplayMode currentDisplayMode = MAIN_DISPLAY;

// Enum for managing which value is being edited
enum EditTarget {
  NONE_EDITING,
  EDIT_SAMPLE,
  EDIT_CALIBRATION_TARGET
};
EditTarget currentEditTarget = NONE_EDITING;

int currentSampleEditIndex = 0; // Index of the sample currently being edited
int currentDigitIndex = 0;      // Index of the digit being edited in calibration
char calibrationTargetBuffer[5]; // Buffer for entering calibration target (e.g., "1234\0")

// Variables for blinking effect during editing
unsigned long lastBlinkToggleTime = 0;
const unsigned long blinkInterval = 250;
bool digitVisible = true;

// --- GLOBAL VARIABLES FOR PARTIAL DISPLAY UPDATES (TFT) ---
String prevDateStr = "";
String prevTimeStr = "";
int prevSensorDisplayValue[4] = { -1, -1, -1, -1 };
int prevBatteryLevel = -1;
// --- END GLOBAL VARIABLES FOR TFT ---

// --- Web Server Variables ---
WebServer server(80);
Preferences preferences; // Used for saving WiFi credentials

bool wifiConnected = false;
String localIPStr = ""; // Stores local IP address when connected to STA mode
bool softAPActive = false; // Flag to indicate if SoftAP is active
unsigned long lastWiFiAttemptTime = 0;
const unsigned long wifiRetryInterval = 15000; // Retry connecting to WiFi every 15 seconds if disconnected

// HTML for the WiFi configuration page (SoftAP mode)
const char* wifiConfigPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>WiFi Setup</title>
  <style>
    body { font-family: sans-serif; text-align: center; background-color: #f0f0f0; margin: 0; padding: 20px; }
    .container { max-width: 400px; margin: 20px auto; padding: 20px; background-color: #fff; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    h2 { color: #333; }
    form { margin-top: 20px; }
    input[type="text"], input[type="password"] {
      width: calc(100% - 20px);
      padding: 10px;
      margin-bottom: 15px;
      border: 1px solid #ddd;
      border-radius: 4px;
      box-sizing: border-box;
    }
    input[type="submit"] {
      background-color: #007bff;
      color: white;
      padding: 10px 15px;
      border: none;
      border-radius: 4px;
      cursor: pointer;
      font-size: 16px;
    }
    input[type="submit"]:hover {
      background-color: #0056b3;
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>ESP32 TDS - WiFi Setup</h2>
    <form action="/save" method="post">
      SSID: <input type="text" name="ssid" placeholder="Enter WiFi SSID"><br>
      Password: <input type="password" name="pass" placeholder="Enter WiFi Password (optional)"><br>
      <input type="submit" value="Connect">
    </form>
  </div>
</body>
</html>
)rawliteral";

// --- Function to generate the HTML for the main dashboard (with auto-refresh) ---
String generateDashboardHTML() {
  String page = "<!DOCTYPE html><html><head><title>TDS Monitor</title>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1'>"; // Added for better mobile display
  page += "<style>";
  page += "body{font-family:sans-serif; text-align:center; background-color:#f0f0f0; margin: 0; padding: 20px;}";
  page += ".container{max-width:600px; margin:20px auto; padding:15px; background-color:#fff; border-radius:8px; box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
  page += "h2{color:#333;}";
  page += ".card{border:1px solid #ccc; padding:10px; margin:10px auto; display:inline-block; width:120px; border-radius:5px; background-color:#e9e9e9;}";
  page += ".card h3{margin-top:0; color:#555;}";
  page += ".card p{font-size:1.2em; font-weight:bold; color:#007bff;}";
  page += ".time-display{margin-top:20px; font-size:1em; color:#666;}";
  page += "</style>";
  page += "</head><body>";
  page += "<div class='container'>";
  page += "<h2>TDS Dashboard</h2>";

  // Initial display of sensor readings (these will be updated by JavaScript)
  for (int i = 0; i < 4; i++) {
    int val = round(liveSensorReadings[i] * calibrationFactors[i]);
    page += "<div class='card'><h3>Sample ";
    page += String(i);
    page += "</h3><p><span id='tds_"; // Unique ID for each TDS value
    page += String(i);
    page += "'>";
    page += String(val); // Initial value
    page += "</span> ppm</p></div>"; // Added 'ppm' for clarity
  }

  page += "<div class='time-display' id='current_time'>Time: " + getDateTimeString() + "</div>";
  page += "</div>"; // Close container div

  // --- JavaScript for Auto-Refresh ---
  // This script will run in the user's browser
  page += "<script>";
  page += "function fetchData() {";
  page += "  var xhr = new XMLHttpRequest();"; // Create a new HTTP request object
  page += "  xhr.onreadystatechange = function() {"; // Define what happens when the state changes
  page += "    if (this.readyState == 4 && this.status == 200) {"; // Check if request is complete and successful
  page += "      var data = JSON.parse(this.responseText);"; // Parse the JSON string received from the server
  page += "      for (var i = 0; i < 4; i++) {";
  page += "        var tdsElement = document.getElementById('tds_' + i);";
  page += "        if (tdsElement) {"; // Check if element exists before updating
  page += "          tdsElement.innerHTML = data.tds[i];"; // Update the TDS value for each sample
  page += "        }";
  page += "      }";
  page += "      var timeElement = document.getElementById('current_time');";
  page += "      if (timeElement) {"; // Check if element exists
  page += "        timeElement.innerHTML = 'Time: ' + data.time;"; // Update the time display
  page += "      }";
  page += "    }";
  page += "  };";
  page += "  xhr.open('GET', '/data', true);"; // Configure the request: GET method, to '/data' endpoint, asynchronous
  page += "  xhr.send();"; // Send the request
  page += "}";
  page += "setInterval(fetchData, 3000);"; // Call fetchData() every 3000 milliseconds (3 seconds)
  page += "window.onload = fetchData;"; // Call fetchData() immediately when the page loads
  page += "</script>";

  page += "</body></html>";
  return page;
}

// --- TFT Display Functions ---
void drawMainDisplayStatic();
void updateMainDisplayDynamic(bool forceRedraw = false);
void drawBatteryIcon(int x, int y, int width, int height, int level, uint16_t color);
void showCalibrationTargetEditingScreenStatic();
void updateCalibrationTargetEditingScreenDynamic();
void drawWifiInfoScreen();
void displayConnecting();
void displayConnectionFailed();

// --- Button Handling Functions ---
void handleToggleButton();
void handleSettingsButton();
void handleIncreaseDecreaseButtons();

// --- WiFi Management Function ---
void manageWiFiConnection();

// --- Utility Functions ---
long stringToInt(const char* strBuffer);


void setup() {
  Serial.begin(115200);
  Serial.println("System Starting...");

  randomSeed(analogRead(0)); // Seed random number generator with analog noise

  // Configure button pins as input with pull-up resistors
  pinMode(BTN_TOGGLE, INPUT_PULLUP);
  pinMode(BTN_SETTINGS, INPUT_PULLUP);
  pinMode(BTN_INCREASE, INPUT_PULLUP);
  pinMode(BTN_DECREASE, INPUT_PULLUP);

  // Configure analog input pins for TDS sensors
  pinMode(ANALOG_PIN_TDS_0, INPUT);
  pinMode(ANALOG_PIN_TDS_1, INPUT);
  pinMode(ANALOG_PIN_TDS_2, INPUT);
  pinMode(ANALOG_PIN_TDS_3, INPUT);

  // Initialize TFT display
  tft.begin(20000000); // Set SPI clock speed
  tft.setRotation(1); // Set display rotation (landscape)
  tft.fillScreen(ILI9341_BLACK); // Clear screen to black

  // Set initial system time (useful for testing if no NTP is used)
  // This sets the time to July 14, 2025, 17:47:00
  struct tm timeinfo;
  timeinfo.tm_year = 2025 - 1900; // Years since 1900
  timeinfo.tm_mon = 7 - 1;      // Month (0-11)
  timeinfo.tm_mday = 14;
  timeinfo.tm_hour = 17;
  timeinfo.tm_min = 47;
  timeinfo.tm_sec = 0;
  timeinfo.tm_isdst = -1; // Not knowing daylight savings time
  time_t epochTime = mktime(&timeinfo);
  if (epochTime != -1) {
    struct timeval tv;
    tv.tv_sec = epochTime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    Serial.println("System time set successfully.");
  } else {
    Serial.println("Failed to set system time.");
  }

  // Initialize calibration factors with default and previous values
  for (int i = 0; i < 4; i++) {
    prevCalibrationFactors[i] = calibrationFactors[i];
  }

  // --- WiFi Connection Attempt ---
  preferences.begin("wifiCreds", false); // Open Preferences in read-write mode
  String savedSSID = preferences.getString("ssid", ""); // Get saved SSID
  String savedPASS = preferences.getString("pass", ""); // Get saved password
  preferences.end(); // Close Preferences

  if (savedSSID.length() > 0) { // If credentials are saved
    Serial.println("Trying saved WiFi credentials...");
    displayConnecting(); // Show "Connecting..." on TFT
    
    WiFi.disconnect(true, true); // Disconnect from any previous WiFi and clear settings
    WiFi.mode(WIFI_STA); // Set WiFi to Station mode (connect to an AP)
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) { // Wait up to 10 seconds (20 * 500ms)
      delay(500);
      Serial.print(".");
      timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to WiFi!");
      wifiConnected = true;
      localIPStr = WiFi.localIP().toString(); // Store local IP address
      Serial.println("IP Address: " + localIPStr);
    } else {
      Serial.println("\nFailed to connect to saved WiFi. Will start SoftAP.");
      displayConnectionFailed(); // Show "Connection Failed!" on TFT
      delay(2000); // Give user time to read message
    }
  } else {
    Serial.println("No saved WiFi credentials. Starting SoftAP for configuration.");
  }
  // --- End WiFi Connection Attempt ---

  // --- Web Server Setup (Conditional: SoftAP or Station) ---
  if (!wifiConnected) {
    // Start SoftAP for configuration if not connected to an AP
    WiFi.softAP("ESP_TDS_Setup", "12345678"); // SSID, Password for the AP
    softAPActive = true;
    IPAddress IP = WiFi.softAPIP(); // Get SoftAP IP address
    Serial.print("Access Point started. Connect to: ");
    Serial.println(IP);

    // Handle requests for the WiFi configuration page
    server.on("/", []() {
      server.send(200, "text/html", wifiConfigPage);
    });

    // Handle saving WiFi credentials
    server.on("/save", []() {
      String ssid = server.arg("ssid"); // Get SSID from form
      String pass = server.arg("pass"); // Get Password from form

      preferences.begin("wifiCreds", false); // Open Preferences for writing
      preferences.putString("ssid", ssid);
      preferences.putString("pass", pass);
      preferences.end(); // Close Preferences

      server.send(200, "text/html", "<h2>Credentials Saved! Rebooting ESP32...</h2>");
      delay(2000);
      ESP.restart(); // Reboot to connect with new credentials
    });

    server.begin(); // Start the web server
  } else {
    // Setup server for data dashboard if connected to an AP
    server.on("/", []() {
      server.send(200, "text/html", generateDashboardHTML()); // Serve the main dashboard
    });

    // NEW ENDPOINT: To provide live data for AJAX requests from the dashboard
    server.on("/data", []() {
      String jsonResponse = "{";
      jsonResponse += "\"tds\": [";
      for (int i = 0; i < 4; i++) {
        jsonResponse += String(round(liveSensorReadings[i] * calibrationFactors[i]));
        if (i < 3) {
          jsonResponse += ",";
        }
      }
      jsonResponse += "],";
      jsonResponse += "\"time\": \"" + getDateTimeString() + "\""; // Get current time string
      jsonResponse += "}";
      server.send(200, "application/json", jsonResponse); // Send JSON response
    });

    server.begin(); // Start the web server
    Serial.println("Web server started at: http://" + localIPStr);
  }

  // Draw initial display on TFT based on connection result
  drawMainDisplayStatic();
  updateMainDisplayDynamic(true); // Force a full update
}

void loop() {
  // Handle button presses
  handleToggleButton();
  handleSettingsButton();
  handleIncreaseDecreaseButtons();

  // Manage WiFi connection status and retry if necessary
  manageWiFiConnection();

  // Read TDS sensors at regular intervals
  if (millis() - lastTdsReadTime >= tdsReadInterval) {
    lastTdsReadTime = millis();
    readTdsSensors();
    if (currentDisplayMode == MAIN_DISPLAY) {
      updateMainDisplayDynamic(); // Update TFT sensor readings if on main display
    }
  }

  // Handle blinking cursor in editing modes
  if (currentDisplayMode == SAMPLE_SELECTION_MODE || currentDisplayMode == DIGIT_EDITING_MODE) {
    if (millis() - lastBlinkToggleTime >= blinkInterval) {
      lastBlinkToggleTime = millis();
      digitVisible = !digitVisible; // Toggle digit visibility for blinking effect
      if (currentDisplayMode == SAMPLE_SELECTION_MODE) {
        updateMainDisplayDynamic(); // Update main display for sample selection blinking
      } else if (currentDisplayMode == DIGIT_EDITING_MODE) {
        updateCalibrationTargetEditingScreenDynamic(); // Update calibration screen for digit blinking
      }
    }
  } else if (currentDisplayMode == MAIN_DISPLAY) {
    // Regular refresh for time/battery on main display
    if (millis() - lastDisplayRefreshTime >= refreshDisplayInterval) {
      lastDisplayRefreshTime = millis();
      updateMainDisplayDynamic(); // Update dynamic elements on main display
    }
  }
  
  server.handleClient(); // Process web server client requests
}

// --- WiFi Management Functions ---

// Function to handle WiFi reconnection logic
void manageWiFiConnection() {
  if (softAPActive) {
    // If SoftAP is active, we are in configuration mode, no need to attempt STA mode connection.
    return;
  }

  // Check if we are connected
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
        // We just connected (transition from disconnected to connected)
        wifiConnected = true;
        localIPStr = WiFi.localIP().toString();
        Serial.println("WiFi Connected (Loop). IP: " + localIPStr);
        // Refresh display if IP related elements were to be shown on main screen (they aren't now)
        // drawMainDisplayStatic(); // Only redraw static if the content itself changes (e.g., if we were displaying "Connecting..." before)
        // updateMainDisplayDynamic(true); // Force a full update of dynamic elements
    }
  } else {
    // If not connected, check if it's time to retry connection
    wifiConnected = false; // Ensure flag is false if disconnected
    if (millis() - lastWiFiAttemptTime >= wifiRetryInterval) {
      Serial.println("WiFi disconnected. Attempting reconnection...");
      lastWiFiAttemptTime = millis(); // Reset retry timer

      // Retrieve saved credentials to attempt reconnection
      preferences.begin("wifiCreds", false);
      String savedSSID = preferences.getString("ssid", "");
      String savedPASS = preferences.getString("pass", "");
      preferences.end();

      if (savedSSID.length() > 0) {
        // Try to connect using saved credentials
        WiFi.mode(WIFI_STA);
        WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
        // Note: We don't block here. We let the loop run and check WiFi.status() in subsequent cycles.
      } else {
        Serial.println("No saved credentials found. SoftAP should be running.");
        // If no saved credentials, it means we didn't connect in setup and SoftAP is active.
      }
    }
  }
}

// --- TFT Display Drawing Functions ---

// Function to draw static elements on the main display
void drawMainDisplayStatic() {
  tft.fillScreen(ILI9341_BLACK); // Clear screen
  tft.drawRoundRect(5, 5, SCREEN_W - 10, SCREEN_H - 10, 15, ILI9341_YELLOW); // Outer border
  tft.drawRoundRect(6, 6, SCREEN_W - 12, SCREEN_H - 12, 14, ILI9341_ORANGE); // Inner border

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(15, 15);
  tft.print("MD4x4"); // Top-left branding

  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(15, 60);
  tft.print("SIMENTAL"); // Main branding

  // --- Bottom Menu Items (Static) ---
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);

  int menuY = SCREEN_H - 35; // Y position for menu items
  int columnWidth = (SCREEN_W - 2 * 15) / 4; // Divide screen into 4 columns

  // Print menu item labels centrally within their columns
  tft.setCursor(15 + (columnWidth - (4 * 6 * 2)) / 2, menuY); // (Char_width * Char_height * Text_size)
  tft.print("List");

  tft.setCursor(15 + columnWidth + (columnWidth - (4 * 6 * 2)) / 2, menuY);
  tft.print("RSLT");

  tft.setCursor(15 + 2 * columnWidth + (columnWidth - (4 * 6 * 2)) / 2, menuY);
  tft.print("Menu");

  tft.setCursor(15 + 3 * columnWidth + (columnWidth - (4 * 6 * 2)) / 2, menuY);
  tft.print("Save");

  // --- Removed IP address display from main screen here ---
  // The IP address will now only be visible on the WiFi Status screen,
  // accessed by pressing the TOGGLE button.
}

// Function to update dynamic elements (time, battery, sensor readings) on the TFT
void updateMainDisplayDynamic(bool forceRedraw) {
  // Update time and date
  String currentDateStr = getDateOnlyString();
  String currentTimeStr = getTimeOnlyString();

  tft.setTextSize(2);
  int maxDateWidth = strlen("DD.MM.YYYY") * 6 * 2; // Max pixel width for date string
  int maxTimeWidth = strlen("HH:MM") * 6 * 2;     // Max pixel width for time string


  if (currentDateStr != prevDateStr || forceRedraw) {
    // Clear previous date area and print new date
    tft.fillRect(SCREEN_W - maxDateWidth - 15, 15, maxDateWidth, 16, ILI9341_BLACK);
    tft.setCursor(SCREEN_W - currentDateStr.length() * 6 * 2 - 15, 15);
    tft.setTextColor(ILI9341_WHITE);
    tft.print(currentDateStr);
    prevDateStr = currentDateStr;
  }
  if (currentTimeStr != prevTimeStr || forceRedraw) {
    // Clear previous time area and print new time
    tft.fillRect(SCREEN_W - maxTimeWidth - 15, 30, maxTimeWidth, 16, ILI9341_BLACK);
    tft.setCursor(SCREEN_W - currentTimeStr.length() * 6 * 2 - 15, 30);
    tft.setTextColor(ILI9341_WHITE);
    tft.print(currentTimeStr);
    prevTimeStr = currentTimeStr;
  }

  // Update battery icon
  int batteryWidth = 40;
  int batteryHeight = 16;
  int batteryX = (SCREEN_W / 2) - (batteryWidth / 2); // Center horizontally
  int batteryY = 15;
  int currentBatteryLevel = map(analogRead(39), 0, 4095, 0, 5); // Assuming GPIO 39 for battery voltage (adjust as needed)
  if (currentBatteryLevel != prevBatteryLevel || forceRedraw) {
    // Clear previous battery icon area and redraw
    tft.fillRect(batteryX, batteryY, batteryWidth + 3, batteryHeight + 1, ILI9341_BLACK);
    drawBatteryIcon(batteryX, batteryY, batteryWidth, batteryHeight, currentBatteryLevel, ILI9341_WHITE);
    prevBatteryLevel = currentBatteryLevel;
  }

  // Update sensor readings (TDS values)
  int displayValue[4];
  displayValue[0] = round(liveSensorReadings[0] * calibrationFactors[0]);
  displayValue[1] = round(liveSensorReadings[1] * calibrationFactors[1]);
  displayValue[2] = round(liveSensorReadings[2] * calibrationFactors[2]);
  displayValue[3] = round(liveSensorReadings[3] * calibrationFactors[3]);

  // Define positions and sizes for each sensor value display
  struct NumberPos {
    int x, y, size, maxWidth;
  } numPos[4];
  numPos[0] = { 60, 95, 5, 5 * 6 * 5 };        // Sample 0 (larger size)
  numPos[1] = { SCREEN_W / 2 + 30, 95, 5, 5 * 6 * 5 }; // Sample 1 (larger size)
  numPos[2] = { 60, 150, 4, 5 * 6 * 4 };       // Sample 2 (smaller size)
  numPos[3] = { SCREEN_W / 2 + 30, 150, 4, 5 * 6 * 4 }; // Sample 3 (smaller size)

  for (int i = 0; i < 4; ++i) {
    bool needsUpdate = false;
    // Update if not in sample selection mode OR if it's the currently selected sample (for blinking)
    if (currentDisplayMode != SAMPLE_SELECTION_MODE || currentSampleEditIndex != i) {
      if (displayValue[i] != prevSensorDisplayValue[i] || forceRedraw) {
        needsUpdate = true; // Update if value changed or forced redraw
      }
    } else {
      needsUpdate = true; // Always update if it's the selected sample (to handle blinking)
    }

    if (needsUpdate) {
      tft.setTextSize(numPos[i].size);

      char prevValStr[6]; // Buffer for previous value string
      sprintf(prevValStr, "%d", prevSensorDisplayValue[i]);
      int prevTextWidth = strlen(prevValStr) * 6 * numPos[i].size; // Width of previous text

      // Clear the area if digit is not visible (for blinking) or if value changed/forced redraw
      if (!digitVisible || forceRedraw || displayValue[i] != prevSensorDisplayValue[i]) {
        tft.fillRect(numPos[i].x, numPos[i].y, prevTextWidth, 8 * numPos[i].size, ILI9341_BLACK);
      }

      tft.setCursor(numPos[i].x, numPos[i].y);

      // Set text color based on display mode and blinking
      if (currentDisplayMode == SAMPLE_SELECTION_MODE && currentSampleEditIndex == i) {
        if (digitVisible) {
          tft.setTextColor(ILI9341_WHITE); // Visible (selected)
        } else {
          tft.setTextColor(ILI9341_BLACK); // Hidden (for blink effect)
        }
      } else {
        tft.setTextColor(ILI9341_LIGHTGREY); // Normal color for non-selected/main display
      }
      tft.print(displayValue[i]); // Print current value
      prevSensorDisplayValue[i] = displayValue[i]; // Store for next comparison
    }
  }
}

// Function to draw a simple battery icon
void drawBatteryIcon(int x, int y, int width, int height, int level, uint16_t color) {
  // Draw battery body
  tft.drawRoundRect(x, y, width, height, 2, color);
  // Draw battery terminal (nub)
  tft.fillRect(x + width, y + height / 4, 3, height / 2, color);

  // Calculate width of each charge bar
  int barWidth = (width - 4) / 5; // 5 segments inside the battery body
  for (int i = 0; i < level; i++) {
    // Draw filled bars based on battery level
    tft.fillRect(x + 2 + (i * barWidth), y + 2, barWidth - 1, height - 4, color);
  }
}

// Function to draw static elements of the calibration target editing screen
void showCalibrationTargetEditingScreenStatic() {
  tft.fillScreen(ILI9341_BLACK); // Clear screen
  tft.setTextSize(2);
  const char* headerText = "Enter Target:";
  int16_t headerX1, headerY1;
  uint16_t headerW, headerH;
  tft.getTextBounds(headerText, 0, 0, &headerX1, &headerY1, &headerW, &headerH); // Get text dimensions

  tft.setCursor(SCREEN_W / 2 - headerW / 2, (SCREEN_H / 2) - 40); // Center header
  tft.setTextColor(ILI9341_WHITE);
  tft.print(headerText);

  tft.setTextSize(1);
  tft.setCursor(10, SCREEN_H - 30); // Instructions at the bottom
  tft.setTextColor(ILI9341_WHITE);
  tft.print("TOGGLE: Select Digit | INC/DEC: Adjust | SETTINGS: Confirm");
}

// Function to update dynamic elements (blinking digit) on calibration screen
void updateCalibrationTargetEditingScreenDynamic() {
  char* displayBufPtr = calibrationTargetBuffer;
  uint16_t textColor = ILI9341_YELLOW;
  int textSz = 5; // Large text size for digits
  int maxDigits = 4; // Number of digits for the target value

  const uint16_t char_pixel_width = 6;
  uint16_t effective_char_w = char_pixel_width * textSz; // Actual pixel width of one digit
  int textHeight = textSz * 8; // Actual pixel height of text

  int textWidth = maxDigits * effective_char_w; // Total width for all digits
  int digitX = (SCREEN_W - textWidth) / 2; // Starting X for centered digits
  int digitY = (SCREEN_H - textHeight) / 2; // Starting Y for centered digits

  tft.setTextSize(textSz);

  // ALWAYS CLEAR THE ENTIRE DIGIT AREA BEFORE DRAWING to prevent ghosting from previous values/blinks
  tft.fillRect(digitX, digitY, textWidth, textHeight, ILI9341_BLACK);

  for (int i = 0; i < maxDigits; ++i) {
    tft.setCursor(digitX + (i * effective_char_w), digitY); // Position for current digit
    if (i == currentDigitIndex) {
      // If it's the currently selected digit, apply blinking logic
      if (digitVisible) {
        tft.setTextColor(ILI9341_WHITE); // Show digit
      } else {
        tft.setTextColor(ILI9341_BLACK); // Hide digit (same as background color)
      }
    } else {
      tft.setTextColor(textColor); // Other digits are yellow
    }
    tft.print(displayBufPtr[i]); // Print the digit character
  }
}

// Function to display WiFi Information on the TFT
void drawWifiInfoScreen() {
    tft.fillScreen(ILI9341_BLACK); // Clear screen
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(20, 50);
    tft.println("WiFi Status");
    tft.println("");

    tft.setTextSize(2);
    if (wifiConnected) {
        tft.setTextColor(ILI9341_GREEN);
        tft.println("Connected!");
        tft.setTextColor(ILI9341_WHITE);
        tft.setCursor(20, 120);
        tft.print("SSID: ");
        tft.println(WiFi.SSID()); // Display connected SSID
        tft.print("Connected IP: "); 
        tft.println(localIPStr); // Display connected IP address
    } else {
        tft.setTextColor(ILI9341_RED);
        tft.println("Disconnected");
        tft.setTextColor(ILI9341_WHITE);
        tft.setCursor(20, 120);
        tft.println("Web Setup AP active:");
        tft.print("Setup AP IP: "); 
        tft.println(WiFi.softAPIP()); // Display SoftAP IP address
    }

    tft.setTextSize(1);
    tft.setCursor(10, SCREEN_H - 30);
    tft.setTextColor(ILI9341_WHITE);
    tft.print("SETTINGS: Back to Main Display"); // Instruction for exit
}

void displayConnecting() {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(50, 100);
    tft.println("Connecting...");
}

void displayConnectionFailed() {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(20, 100);
    tft.println("Connection Failed!");
    tft.setTextSize(2);
    tft.setCursor(20, 150);
    tft.setTextColor(ILI9341_WHITE);
    tft.println("Check credentials.");
}

// --- Button Handling Logic ---

void handleToggleButton() {
  bool currentToggleState = digitalRead(BTN_TOGGLE);
  unsigned long now = millis();

  // Detect a short press (button pressed down and then released)
  if (currentToggleState == LOW && lastBtnToggleState == HIGH) {
    lastPressToggle = now; // Record press start time
  } else if (currentToggleState == HIGH && lastBtnToggleState == LOW) {
    unsigned long pressDuration = now - lastPressToggle;

    if (pressDuration >= debounceDelay) { // Check for debounce
      Serial.print("TOGGLE --- SHORT PRESS DETECTED! Duration: ");
      Serial.print(pressDuration);
      Serial.println("ms");

      switch (currentDisplayMode) {
        case MAIN_DISPLAY:
          // Short press on MAIN_DISPLAY switches to WIFI_INFO_DISPLAY
          currentDisplayMode = WIFI_INFO_DISPLAY;
          drawWifiInfoScreen(); // Draw the WiFi info screen where IP is visible
          Serial.println("Switched to WIFI_INFO_DISPLAY.");
          break;
        case SAMPLE_SELECTION_MODE:
          // Short press in SAMPLE_SELECTION_MODE reverts calibration for current sample
          calibrationFactors[currentSampleEditIndex] = prevCalibrationFactors[currentSampleEditIndex];
          Serial.print("UNDID calibration for Sample ");
          Serial.print(currentSampleEditIndex);
          Serial.print(". Factor reverted to: ");
          Serial.println(calibrationFactors[currentSampleEditIndex], 4);

          // Go back to main display to immediately show the effect
          currentDisplayMode = MAIN_DISPLAY;
          lastDisplayRefreshTime = 0; // Force immediate refresh
          tft.fillScreen(ILI9341_BLACK);
          drawMainDisplayStatic();
          updateMainDisplayDynamic(true);
          break;
        case DIGIT_EDITING_MODE:
          // Short press in DIGIT_EDITING_MODE moves to the next digit
          currentDigitIndex++;
          if (currentDigitIndex >= 4) {
            currentDigitIndex = 0; // Loop back to first digit
          }
          Serial.print("TOGGLE (Short Press): Moved to digit index: ");
          Serial.println(currentDigitIndex);
          digitVisible = true; // Ensure digit is visible initially after selection
          lastBlinkToggleTime = now; // Reset blink timer
          updateCalibrationTargetEditingScreenDynamic(); // Update display
          break;
        case WIFI_INFO_DISPLAY:
          // No action on toggle press in WiFi info screen
          break;
      }
    }
  }
  lastBtnToggleState = currentToggleState; // Update last state for next loop
}

void handleSettingsButton() {
  bool currentSettingsState = digitalRead(BTN_SETTINGS);
  unsigned long now = millis();

  if (currentSettingsState == LOW && lastBtnSettingsState == HIGH) {
    settingsButtonPressStartTime = now; // Record press start time
  } else if (currentSettingsState == HIGH && lastBtnSettingsState == LOW) {
    unsigned long pressDuration = now - settingsButtonPressStartTime;

    if (pressDuration >= debounceDelay) {
      if (pressDuration >= longPressDuration) {
        Serial.print("SETTINGS --- LONG PRESS DETECTED! Duration: ");
        Serial.print(pressDuration);
        Serial.println("ms");
        switch (currentDisplayMode) {
          case MAIN_DISPLAY:
            // Long press on MAIN_DISPLAY enters SAMPLE_SELECTION_MODE
            currentDisplayMode = SAMPLE_SELECTION_MODE;
            currentSampleEditIndex = 0; // Start with first sample selected
            digitVisible = true; // Ensure it's visible initially
            lastBlinkToggleTime = now; // Reset blink timer
            tft.fillScreen(ILI9341_BLACK);
            drawMainDisplayStatic();
            updateMainDisplayDynamic(true); // Redraw with blinking selection
            Serial.println("Switched to SAMPLE_SELECTION_MODE (Long Press).");
            break;
          case SAMPLE_SELECTION_MODE:
            // Long press in SAMPLE_SELECTION_MODE exits to MAIN_DISPLAY
            currentDisplayMode = MAIN_DISPLAY;
            lastDisplayRefreshTime = 0; // Force immediate refresh
            tft.fillScreen(ILI9341_BLACK);
            drawMainDisplayStatic();
            updateMainDisplayDynamic(true);
            Serial.println("Switched to MAIN_DISPLAY from Sample Selection (Long Press).");
            break;
          case DIGIT_EDITING_MODE:
            // Long press in DIGIT_EDITING_MODE exits to MAIN_DISPLAY
            Serial.println("Long press in DIGIT_EDITING_MODE, exiting to MAIN_DISPLAY.");
            currentDisplayMode = MAIN_DISPLAY;
            lastDisplayRefreshTime = 0; // Force immediate refresh
            tft.fillScreen(ILI9341_BLACK);
            drawMainDisplayStatic();
            updateMainDisplayDynamic(true);
            break;
          case WIFI_INFO_DISPLAY:
            // No action on long press in WiFi info screen
            break;
        }
      } else {
        Serial.print("SETTINGS --- SHORT PRESS DETECTED! Duration: ");
        Serial.print(pressDuration);
        Serial.println("ms");
        switch (currentDisplayMode) {
          case MAIN_DISPLAY:
            // No action on short press in MAIN_DISPLAY
            break;
          case SAMPLE_SELECTION_MODE:
            // Short press in SAMPLE_SELECTION_MODE confirms sample and enters DIGIT_EDITING_MODE
            currentDisplayMode = DIGIT_EDITING_MODE;
            currentEditTarget = EDIT_CALIBRATION_TARGET;
            currentDigitIndex = 0; // Start editing the first digit
            sprintf(calibrationTargetBuffer, "0000"); // Initialize buffer with "0000"
            digitVisible = true; // Ensure digit is visible
            lastBlinkToggleTime = now; // Reset blink timer
            tft.fillScreen(ILI9341_BLACK);
            showCalibrationTargetEditingScreenStatic(); // Draw static elements
            updateCalibrationTargetEditingScreenDynamic(); // Draw dynamic (blinking) elements
            Serial.println("Switched to DIGIT_EDITING_MODE (Short Press).");
            break;
          case DIGIT_EDITING_MODE:
            // Short press in DIGIT_EDITING_MODE confirms the entered target value
            if (currentEditTarget == EDIT_CALIBRATION_TARGET) {
              long targetVal = stringToInt(calibrationTargetBuffer); // Convert buffer to integer
              if (liveSensorReadings[currentSampleEditIndex] != 0) {
                prevCalibrationFactors[currentSampleEditIndex] = calibrationFactors[currentSampleEditIndex]; // Save current factor for undo

                // Calculate new calibration factor: Target Value / Raw Sensor Reading
                calibrationFactors[currentSampleEditIndex] = (float)targetVal / liveSensorReadings[currentSampleEditIndex];
                Serial.print("Calibrated Sample ");
                Serial.print(currentSampleEditIndex);
                Serial.print(": Target = ");
                Serial.print(targetVal);
                Serial.print(", Raw = ");
                Serial.print(liveSensorReadings[currentSampleEditIndex]);
                Serial.print(", New Factor = ");
                Serial.println(calibrationFactors[currentSampleEditIndex], 4);
              } else {
                Serial.println("Cannot calibrate: Raw reading is zero. Factor set to 1.0.");
                prevCalibrationFactors[currentSampleEditIndex] = 1.0;
                calibrationFactors[currentSampleEditIndex] = 1.0; // Prevent division by zero
              }
              currentDisplayMode = MAIN_DISPLAY; // Return to main display
              lastDisplayRefreshTime = 0; // Force immediate refresh
              tft.fillScreen(ILI9341_BLACK);
              drawMainDisplayStatic();
              updateMainDisplayDynamic(true);
              Serial.println("Confirmed calibration, returned to MAIN_DISPLAY (Short Press).");
            }
            digitVisible = true; // Ensure digit visible on transition
            lastBlinkToggleTime = now;
            break;
          case WIFI_INFO_DISPLAY:
            // Short press in WIFI_INFO_DISPLAY returns to MAIN_DISPLAY
            currentDisplayMode = MAIN_DISPLAY;
            lastDisplayRefreshTime = 0; // Force immediate refresh
            tft.fillScreen(ILI9341_BLACK);
            drawMainDisplayStatic();
            updateMainDisplayDynamic(true);
            Serial.println("Returned to MAIN_DISPLAY from WIFI_INFO_DISPLAY.");
            break;
        }
      }
    }
  }
  lastBtnSettingsState = currentSettingsState; // Update last state
}

void handleIncreaseDecreaseButtons() {
  bool currentIncreaseState = digitalRead(BTN_INCREASE);
  bool currentDecreaseState = digitalRead(BTN_DECREASE);
  unsigned long now = millis();
  int changeVal = 0; // +1 for increase, -1 for decrease

  char* charToModify = nullptr; // Pointer to the character being modified

  // Handle Increase Button press (falling edge detection with debounce)
  if (currentIncreaseState == LOW && lastBtnIncreaseState == HIGH) {
    if (now - lastPressIncreaseTime > debounceDelay) {
      changeVal = 1;
      lastPressIncreaseTime = now;
    }
  }

  // Handle Decrease Button press (falling edge detection with debounce)
  if (currentDecreaseState == LOW && lastBtnDecreaseState == HIGH) {
    if (now - lastPressDecreaseTime > debounceDelay) {
      changeVal = -1;
      lastPressDecreaseTime = now;
    }
  }

  lastBtnIncreaseState = currentIncreaseState; // Update last state
  lastBtnDecreaseState = currentDecreaseState;

  if (changeVal != 0) { // If a button was pressed and debounced
    digitVisible = true; // Ensure digit is visible when changed
    lastBlinkToggleTime = now; // Reset blink timer

    switch (currentDisplayMode) {
      case SAMPLE_SELECTION_MODE:
        currentSampleEditIndex += changeVal; // Move to next/previous sample
        if (currentSampleEditIndex < 0) currentSampleEditIndex = 3; // Wrap around
        if (currentSampleEditIndex > 3) currentSampleEditIndex = 0; // Wrap around
        Serial.print("INC/DEC: Sample index changed to: ");
        Serial.println(currentSampleEditIndex);
        updateMainDisplayDynamic(); // Update display to show new selection
        break;
      case DIGIT_EDITING_MODE:
        if (currentEditTarget == EDIT_CALIBRATION_TARGET) {
          charToModify = &calibrationTargetBuffer[currentDigitIndex]; // Get pointer to the character to modify

          if (changeVal == 1) { // Increase
            if (*charToModify == '9') {
              *charToModify = '0'; // Wrap from 9 to 0
            } else {
              (*charToModify)++; // Increment digit
            }
          } else { // Decrease
            if (*charToModify == '0') {
              *charToModify = '9'; // Wrap from 0 to 9
            } else {
              (*charToModify)--; // Decrement digit
            }
          }
          Serial.print("INC/DEC: Digit ");
          Serial.print(currentDigitIndex);
          Serial.print(" changed to: ");
          Serial.println(*charToModify);
          updateCalibrationTargetEditingScreenDynamic(); // Update display with new digit
        }
        break;
      case MAIN_DISPLAY:
        // No action for INC/DEC in MAIN_DISPLAY
        break;
      case WIFI_INFO_DISPLAY:
        // No action for INC/DEC in WIFI_INFO_DISPLAY
        break;
    }
  }
}

// --- Utility Functions ---

// Converts a character buffer (string) to a long integer
long stringToInt(const char* strBuffer) {
  return atol(strBuffer); // atol is standard C library function for ASCII to Long
}

// Gets current date and time as a formatted string (YYYY-MM-DD HH:MM:SS)
String getDateTimeString() {
  time_t now;
  struct tm timeinfo;

  time(&now); // Get current epoch time
  localtime_r(&now, &timeinfo); // Convert to local time structure

  char date_time_buf[20]; // Buffer for formatted string
  strftime(date_time_buf, sizeof(date_time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(date_time_buf);
}

// Gets current date only as a formatted string (DD.MM.YYYY)
String getDateOnlyString() {
  time_t now;
  struct tm timeinfo;

  time(&now);
  localtime_r(&now, &timeinfo);

  char date_buf[11];
  strftime(date_buf, sizeof(date_buf), "%d.%m.%Y", &timeinfo);
  return String(date_buf);
}

// Gets current time only as a formatted string (HH:MM)
String getTimeOnlyString() {
  time_t now;
  struct tm timeinfo;

  time(&now);
  localtime_r(&now, &timeinfo);

  char time_buf[6];
  strftime(time_buf, sizeof(time_buf), "%H:%M", &timeinfo);
  return String(time_buf);
}

// --- TDS Sensor Reading Functions ---

#define ADC_OVERSAMPLES 64 // Number of samples to average for ADC reading

// Reads analog value from a pin and returns the average over ADC_OVERSAMPLES
float getTdsValue(int analogPin) {
  unsigned long sum = 0;
  for (int i = 0; i < ADC_OVERSAMPLES; i++) {
    sum += analogRead(analogPin);
    // You might want a small delay here if readings are too fast
    // delayMicroseconds(50); // Example delay, adjust based on sensor
  }
  return (float)sum / ADC_OVERSAMPLES; // Return the average
}

// Reads all TDS sensors and stores the averaged raw values
void readTdsSensors() {
  Serial.println("--- Raw TDS Readings ---");
  liveSensorReadings[0] = getTdsValue(ANALOG_PIN_TDS_0);
  Serial.print("TDS0 Raw AVG: ");
  Serial.println(liveSensorReadings[0]);
  liveSensorReadings[1] = getTdsValue(ANALOG_PIN_TDS_1);
  Serial.print("TDS1 Raw AVG: ");
  Serial.println(liveSensorReadings[1]);
  liveSensorReadings[2] = getTdsValue(ANALOG_PIN_TDS_2);
  Serial.print("TDS2 Raw AVG: ");
  Serial.println(liveSensorReadings[2]);
  liveSensorReadings[3] = getTdsValue(ANALOG_PIN_TDS_3);
  Serial.print("TDS3 Raw AVG: ");
  Serial.println(liveSensorReadings[3]);
}