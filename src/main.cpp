#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Hardware Pins
#define SD_CS 5

// Audio Pipeline Pointers
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *file = nullptr;
AudioOutputI2S *out = nullptr;

void updateDisplay(const char* status, const char* trackName) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Header
    display.setCursor(0, 0);
    display.println(">> ESP32 MUSIC PLAYER <<");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // Track Name Display
    display.setCursor(0, 24);
    display.print("Track: ");
    display.println(trackName);

    // Player Status
    display.setCursor(0, 48);
    display.print("Status: ");
    display.println(status);

    display.display();
}

void setup() {
    Serial.begin(115200);

    // 1. Initialize OLED Screen (I2C: SDA=21, SCL=22)
    Wire.begin(21, 22);
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("OLED Allocation Failed"));
    } else {
        updateDisplay("Initializing...", "None");
    }

    // 2. Initialize MicroSD Card
    if (!SD.begin(SD_CS)) {
        Serial.println(F("❌ SD Card Mount Failed!"));
        updateDisplay("SD Error!", "Check Wiring");
        while (1); 
    }
    Serial.println(F("✅ SD Card Mounted Successfully"));

    // 3. Configure ESP32 Internal DAC Output (GPIO 25)
    out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);
    out->SetOutputModeMono(true);
    out->SetGain(0.2); // Lower gain (20%) prevents distortion on fixed-gain PAM8403

    // 4. Load & Play Track
    const char* currentTrack = "/song.mp3";
    file = new AudioFileSourceSD(currentTrack);
    mp3 = new AudioGeneratorMP3();

    if (mp3->begin(file, out)) {
        Serial.println(F("Playback Started"));
        updateDisplay("Playing", "song.mp3");
    } else {
        Serial.println(F("Error starting MP3 decoder"));
        updateDisplay("Decoder Error", "Invalid MP3");
    }
}

void loop() {
    if (mp3 && mp3->isRunning()) {
        if (!mp3->loop()) {
            mp3->stop();
            updateDisplay("Finished", "Track Ended");
            Serial.println(F("Playback Finished"));
        }
    }
}