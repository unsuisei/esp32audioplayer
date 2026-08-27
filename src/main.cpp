#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <vector>

#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// Hardware Pins
#define SD_CS 5
#define BOOT_BUTTON_PIN 0  // Play / Pause / Skip (BOOT button)
#define VOL_BUTTON_PIN 32  // 3-Pin Volume Cycle Button

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Audio Volume Presets (Gain values safe for PAM8403)
const float volumeLevels[] = {0.02f, 0.05f, 0.10f, 0.18f, 0.25f};
const int volumePercentages[] = {8, 20, 40, 72, 100};
const int TOTAL_VOL_STEPS = 5;
int currentVolIndex = 1; // Default starting index (0.05f / 20%)

// Global Audio Objects
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *file = nullptr;
AudioOutputI2S *out = nullptr;

// Playlist & Control State
std::vector<String> playlist;
size_t currentTrackIndex = 0;
bool isPaused = false;

// Button Debounce States
unsigned long bootPressTime = 0;
bool bootWasPressed = false;

unsigned long volPressTime = 0;
bool volWasPressed = false;

void updateDisplay(const char* status, String trackName) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Header
    display.setCursor(0, 0);
    display.println(">> ESP32 MUSIC PLAYER <<");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // Clean leading slash for display
    if (trackName.startsWith("/")) {
        trackName = trackName.substring(1);
    }

    // Track Name
    display.setCursor(0, 22);
    display.print("Track: ");
    display.println(trackName);

    // Volume Level Display
    display.setCursor(0, 38);
    display.print("Volume: ");
    display.print(volumePercentages[currentVolIndex]);
    display.println("%");

    // Status / Track Counter
    display.setCursor(0, 52);
    display.print("Status: ");
    display.print(status);
    
    if (!playlist.empty()) {
        display.setCursor(95, 52);
        display.printf("[%d/%d]", currentTrackIndex + 1, playlist.size());
    }

    display.display();
}

void scanSDCard() {
    File root = SD.open("/");
    if (!root) return;

    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String filename = String(entry.name());
            if (!filename.startsWith("/")) filename = "/" + filename;
            
            String lowerName = filename;
            lowerName.toLowerCase();
            if (lowerName.endsWith(".mp3") && !filename.startsWith("/._")) {
                playlist.push_back(filename);
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

void playTrack(size_t index) {
    if (playlist.empty()) return;

    isPaused = false;

    if (file) {
        delete file;
        file = nullptr;
    }

    String path = playlist[index];
    file = new AudioFileSourceSD(path.c_str());
    
    if (mp3->begin(file, out)) {
        updateDisplay("Playing", path);
    } else {
        updateDisplay("Skip Error", path);
    }
}

// Handles Play / Pause (Short Press) and Skip Track (Long Press)
void handleBootButton() {
    bool isPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);

    if (isPressed && !bootWasPressed) {
        bootPressTime = millis();
        bootWasPressed = true;
    }

    if (!isPressed && bootWasPressed) {
        unsigned long pressDuration = millis() - bootPressTime;
        bootWasPressed = false;

        if (pressDuration >= 50 && pressDuration < 800) {
            isPaused = !isPaused;
            const char* statusStr = isPaused ? "Paused" : "Playing";
            updateDisplay(statusStr, playlist[currentTrackIndex]);
        } 
        else if (pressDuration >= 800) {
            if (mp3) mp3->stop();
            currentTrackIndex = (currentTrackIndex + 1) % playlist.size();
            playTrack(currentTrackIndex);
        }
    }
}

// Fixed Volume Button Handler (Active LOW)
void handleVolumeButton() {
    // Reads LOW when the button is pressed
    bool isPressed = (digitalRead(VOL_BUTTON_PIN) == LOW);
    
    // Press detected (Debounce trigger)
    if (isPressed && !volWasPressed && (millis() - volPressTime > 150)) {
        volPressTime = millis();
        volWasPressed = true;

        // Cycle through volume levels
        currentVolIndex = (currentVolIndex + 1) % TOTAL_VOL_STEPS;
        
        if (out) {
            out->SetGain(volumeLevels[currentVolIndex]);
        }

        Serial.printf("Volume Set to: %d%%\n", volumePercentages[currentVolIndex]);

        const char* statusStr = isPaused ? "Paused" : "Playing";
        updateDisplay(statusStr, playlist[currentTrackIndex]);
    }

    // Reset button press state when released
    if (!isPressed) {
        volWasPressed = false;
    }
}

void setup() {
    Serial.begin(115200);

    // Pin Configurations
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(VOL_BUTTON_PIN, INPUT_PULLUP);

    // Initialize Display
    Wire.begin(21, 22);
    display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
    updateDisplay("Initializing...", "None");

    // Initialize SD Card
    if (!SD.begin(SD_CS)) {
        updateDisplay("SD Error!", "Check Wiring");
        while (1);
    }

    scanSDCard();

    if (playlist.empty()) {
        updateDisplay("No MP3s", "Add files to SD");
        while (1);
    }

    // Configure Audio Hardware
    out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);
    out->SetOutputModeMono(true);
    out->SetGain(volumeLevels[currentVolIndex]); // Initialize at default preset

    mp3 = new AudioGeneratorMP3();

    // Start First Track
    playTrack(currentTrackIndex);
}

void loop() {
    handleBootButton();
    handleVolumeButton();

    if (mp3 && mp3->isRunning() && !isPaused) {
        if (!mp3->loop()) {
            mp3->stop();
            currentTrackIndex = (currentTrackIndex + 1) % playlist.size();
            playTrack(currentTrackIndex);
        }
    }
}