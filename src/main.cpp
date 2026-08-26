#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

// These headers come from the ESP8266Audio library you installed
#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// Hardware Pin Definitions
#define SD_CS 5
#define BOOT_BUTTON_PIN 0
#define LED_PIN 2  // Built-in LED on ESP32

// Network Configuration
const char* AP_SSID = "suicyan";

// Server & Audio Objects
WebServer server(80);
File uploadFile;

AudioGeneratorMP3 *mp3 = NULL;
AudioFileSourceSD *file = NULL;
AudioOutputI2S *out = NULL;

// Playlist and Player State
std::vector<String> playlist;
int currentTrackIndex = 0;
bool isPlaying = false;
bool isPaused = false;

// Button Tracking
bool lastBtnState = HIGH;
unsigned long btnPressTime = 0;
bool isPressing = false;

// --- Function Declarations ---
void updatePlaylist();
void startTrack(int index);
void stopAudio();
void togglePlayPause();
void playNextTrack();

// --- Web Server Handlers ---
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>ESP32 MP3 Player</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial,sans-serif;margin:20px;max-width:500px;}";
  html += "input[type=file], input[type=submit]{margin:10px 0;padding:12px;width:100%;box-sizing:border-box;}";
  html += "ul{list-style:none;padding:0;} li{background:#f0f0f0;margin:5px 0;padding:10px;border-radius:4px;}</style></head><body>";
  html += "<h2>ESP32 MP3 Manager</h2>";
  html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
  html += "<input type='file' name='upload' accept='.mp3' required>";
  html += "<input type='submit' value='Upload MP3 to SD Card'>";
  html += "</form><hr><h3>Playlist on SD Card:</h3><ul>";

  if (playlist.empty()) {
    html += "<li>No .mp3 files found!</li>";
  } else {
    for (size_t i = 0; i < playlist.size(); i++) {
      html += "<li>";
      if ((int)i == currentTrackIndex && isPlaying) {
        html += "<strong>&#9654; " + playlist[i] + "</strong>";
      } else {
        html += playlist[i];
      }
      html += "</li>";
    }
  }
  html += "</ul></body></html>";

  server.send(200, "text/html", html);
}

void handleFileUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;

    // Pause audio while writing to SD to avoid glitches
    if (isPlaying && !isPaused) isPaused = true;

    if (SD.exists(filename)) {
      SD.remove(filename);
    }
    uploadFile = SD.open(filename, FILE_WRITE);

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.println("Upload complete.");
      updatePlaylist(); // Refresh playlist with new file
    }
  }
}

// --- Audio Player Controls ---
void updatePlaylist() {
  playlist.clear();
  File root = SD.open("/");
  if (!root) return;
  
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String name = String(f.name());
      if (name.endsWith(".mp3") || name.endsWith(".MP3")) {
        if (name.startsWith("/")) name = name.substring(1);
        playlist.push_back(name);
      }
    }
    f = root.openNextFile();
  }
  root.close();
  Serial.printf("Found %d MP3 files.\n", playlist.size());
}

void stopAudio() {
  if (mp3) {
    if (mp3->isRunning()) mp3->stop();
    delete mp3;
    mp3 = NULL;
  }
  if (file) {
    delete file;
    file = NULL;
  }
  isPlaying = false;
  isPaused = false;
}

void startTrack(int index) {
  stopAudio();
  if (playlist.empty()) return;

  currentTrackIndex = index % playlist.size();
  String path = "/" + playlist[currentTrackIndex];
  Serial.print("Now Playing: "); Serial.println(path);

  file = new AudioFileSourceSD(path.c_str());
  mp3 = new AudioGeneratorMP3();
  mp3->begin(file, out);
  
  isPlaying = true;
  isPaused = false;
}

void togglePlayPause() {
  if (playlist.empty()) updatePlaylist();
  if (playlist.empty()) return;

  if (!isPlaying) {
    startTrack(currentTrackIndex);
  } else {
    isPaused = !isPaused;
    Serial.println(isPaused ? "Playback Paused" : "Playback Resumed");
  }
}

void playNextTrack() {
  if (playlist.empty()) updatePlaylist();
  if (playlist.empty()) return;

  startTrack(currentTrackIndex + 1);
}

// --- Button Logic ---
void handleButton() {
  bool currentState = digitalRead(BOOT_BUTTON_PIN);

  // Button pressed down
  if (lastBtnState == HIGH && currentState == LOW) {
    btnPressTime = millis();
    isPressing = true;
  }

  // Button released
  if (lastBtnState == LOW && currentState == HIGH && isPressing) {
    unsigned long duration = millis() - btnPressTime;
    if (duration > 50) { // Debounce filter
      if (duration < 600) {
        Serial.println("[Button] Short Press: Play/Pause");
        togglePlayPause();
      } else {
        Serial.println("[Button] Long Press: Next Track");
        playNextTrack();
      }
    }
    isPressing = false;
  }
  lastBtnState = currentState;
}

// --- Arduino Setup & Loop ---
void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Start with LED turned off

  // 1. ALWAYS START WI-FI FIRST (Prevents Wi-Fi from failing if SD card is missing)
  WiFi.softAP(AP_SSID);
  Serial.println("\nOpen Hotspot Started!");
  Serial.print("Connect to Wi-Fi: "); Serial.println(AP_SSID);
  Serial.print("Web Interface: http://"); Serial.println(WiFi.softAPIP());

  // 2. INITIALIZE AUDIO OUTPUT ON GPIO 25 (Internal DAC)
  out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);

  // 3. INITIALIZE SD CARD WITH ERROR LED BLINK
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD Card Mount Failed!");
    Serial.println("Blinking built-in LED to signal error. Please check card/wiring and reset.");
    
    // Fast blink loop if SD card mounting fails
    while (true) {
      digitalWrite(LED_PIN, HIGH);
      delay(150);
      digitalWrite(LED_PIN, LOW);
      delay(150);
      // We still handle client so you can see the AP, though you can't upload without the SD card.
      server.handleClient(); 
    }
  }

  Serial.println("✅ SD Card mounted successfully.");
  updatePlaylist();

  // 4. SERVER ROUTES
  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/html", "<h3>Upload Successful!</h3><a href='/'>Back to Player</a>");
  }, handleFileUpload);

  server.begin();
}

void loop() {
  server.handleClient();
  handleButton();

  // Play audio loop
  if (isPlaying && !isPaused && mp3 != NULL) {
    if (mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
        playNextTrack(); // Auto-advance to next song
      }
    }
  }
}