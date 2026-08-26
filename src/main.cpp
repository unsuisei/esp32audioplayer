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
#define BOOT_BUTTON_PIN 0  // Onboard BOOT button

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Global Audio Objects
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *file = nullptr;
AudioOutputI2S *out = nullptr;

// Playlist & Control State
std::vector<String> playlist;
size_t currentTrackIndex = 0;
bool isPaused = false;

// Button Debounce & Press Time Variables
unsigned long buttonPressTime = 0;
bool buttonWasPressed = false;

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
    display.setCursor(0, 24);
    display.print("Track: ");
    display.println(trackName);

    // Status / Track Counter
    display.setCursor(0, 48);
    display.print("Status: ");
    display.print(status);
    
    if (!playlist.empty()) {
        display.setCursor(95, 48);
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

    isPaused = false; // Reset pause status on new track

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

void handleButton() {
    bool isPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW); // Pressed = LOW

    // Button state transitions to PRESSED
    if (isPressed && !buttonWasPressed) {
        buttonPressTime = millis();
        buttonWasPressed = true;
    }

    // Button state transitions to RELEASED
    if (!isPressed && buttonWasPressed) {
        unsigned long pressDuration = millis() - buttonPressTime;
        buttonWasPressed = false;

        if (pressDuration >= 50 && pressDuration < 800) {
            // SHORT PRESS: Toggle Play / Pause
            isPaused = !isPaused;
            if (isPaused) {
                updateDisplay("Paused", playlist[currentTrackIndex]);
            } else {
                updateDisplay("Playing", playlist[currentTrackIndex]);
            }
        } 
        else if (pressDuration >= 800) {
            // LONG PRESS: Skip to Next Track
            if (mp3) mp3->stop();
            currentTrackIndex = (currentTrackIndex + 1) % playlist.size();
            playTrack(currentTrackIndex);
        }
    }
}

void setup() {
    Serial.begin(115200);

    // Initialize BOOT button pin
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

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

    // Configure Audio
    out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);
    out->SetOutputModeMono(true);
    out->SetGain(0.08); // Volume level

    mp3 = new AudioGeneratorMP3();

    // Start Playback
    playTrack(currentTrackIndex);
}

void loop() {
    // Monitor button input
    handleButton();

    // Process audio stream only when not paused
    if (mp3 && mp3->isRunning() && !isPaused) {
        if (!mp3->loop()) {
            mp3->stop();
            currentTrackIndex = (currentTrackIndex + 1) % playlist.size();
            playTrack(currentTrackIndex);
        }
    }
}