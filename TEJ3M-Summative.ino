/*
Title: Karaoke TEJ3M Summative 
Author: Algasem Zabarah
Date: January 28, 2026
Teacher: Mr. Roller

Description: This project simulates a karaoke machine using an Arduino Uno R4, a I2C LCD, a buzzer speaker, and 5 LEDs. 
The user selects one of three songs from the Serial Monitor: Country Roads, Baa Baa Black Sheep, or Itsy Bitsy Spider. 
The system plays the melody while showing matching lyrics on the LCD, and as  each note plays, one of the LEDs light up to match 
the current note. The lyrics advance in sync with the melody so the words on the LCD change at the same time as the song. 
Each song runs at its own tempo, and the program keeps the audio, lyric display, and LEDs aligned for the full song. The overall 
goal  of this is to combine sound,lyrics and lights into one synchronized karaoke performance the user can sing along with.


VERSION HISTORY: 

algasem_z_KaraokeSummative_v1 
    - Declared the 16x2 I2C LCD object: LiquidCrystal_I2C lcd(0x27, 16, 2).
    - Started I2C and the LCD correctly so it displays reliably:
      - Wire.begin()
      - lcd.init()
      - lcd.backlight()
      - lcd.clear()
    - Started Serial output for debugging and menu display:
      - Serial.begin(9600)
    - Added basic pin setup:
      - pinMode(BUZZER_PIN, OUTPUT)
      - pinMode() for the 5 LED pins, and set them LOW so nothing starts stuck ON.

algasem_z_KaraokeSummative_v2 
  - Added the Serial song-selection menu + LCD prompt mirroring:
    - Created displayMenu() to show the instructions:
      - LCD shows “Enter 1, 2 or 3 / into monitor”
      - Serial prints the same menu list
    - Added readChoiceChar() so user input is clean:
      - Ignores spaces and Enter characters
      - Clears leftover characters on the same line so extra typing doesn’t affect the next choice
    - Added handleInvalidInput():
      - Prints an error message on Serial and LCD
      - Small readable pause, then returns to the menu

algasem_z_KaraokeSummative_v3 
  - Converted songs into 2D ARRAYS:
    - Added MAX_NOTES to force equal row length across all songs.
    - Created melodyNotes[SONG_COUNT][MAX_NOTES] and padded shorter songs with NOTE_REST.
    - Created durationUnits[SONG_COUNT][MAX_NOTES] and padded shorter songs with 1-unit durations.
    - Added melodyLen[SONG_COUNT] so only the real notes play (padding is ignored).
    - Added tempoBPM[SONG_COUNT] so each song can have its own speed.
    - Added eighthMs(bpm) helper so timing is consistent:
      - unitMs = (60000/BPM)/2

algasem_z_KaraokeSummative_v4 
  - Built the single-buzzer playback engine that works for all songs:
    - Created playSong(songIdx) as the main player function (one engine for everything).
    - Implemented note timing using durationUnits:
      - noteTotalMs = durationUnits * unitMs
    - Added GATE factor to improve sound clarity:
      - noteOnMs = noteTotalMs * GATE
      - noteOffMs = noteTotalMs - noteOnMs
      - This creates a small gap between notes so they don’t “run together”.
    - Added clean start + clean shutdown:
      - noTone(BUZZER_PIN) before/after playback
      - LCD clears and LEDs turn off when the song ends


algasem_z_KaraokeSummative_v5 
  - Added LED synchronization using a teacher-style indexing array:
    - Created ledIndex[SONG_COUNT][MAX_NOTES] where each entry is 0..4 (which LED to light).
    - During playback, the LED corresponding to the current note turns ON while the note plays.
    - Added allLEDsOff() and used it before setting a new LED to prevent leftovers.
    - Added bounds protection:
      - If ledIndex accidentally contains a value > 4, it clamps so it won’t crash or write invalid pins.


algasem_z_KaraokeSummative_v6
  - Moved lyrics into Flash (PROGMEM) to save SRAM and prevent UNO memory issues:
    - Stored each song’s lyrics as ONE big string with '\n' separating lines:
      - const char lyricsX[] PROGMEM = "line1\nline2\nline3..."
    - Added getLyricsPtr(songIdx):
      - Returns the correct Flash string pointer for the chosen song
    - Added getLyricLine(songIdx, lineIndex, out[], outSize):
      - Scans the Flash lyrics using a pointer
      - Reads characters using pgm_read_byte(pointer)
      - Copies only ONE line into a RAM buffer (out[])
      - Stops at '\n' and returns the line safely null-terminated

algasem_z_KaraokeSummative_v7
  - Added lyric synchronization to the melody using “note-count per line” mapping:
    - Created lineNoteCounts[SONG_COUNT][MAX_LINES] PROGMEM:
      - Each entry tells how many melody notes belong to that lyric line
      - 0 acts as an end marker
    - Added getLineInfoFromNotesPlayed():
      - Uses the subtraction method:
        - tmp = notesPlayed
        - subtract each line’s note count until tmp < 0
      - Determines the correct lyric lineIndex for the current playback position
    - Updated playSong() lyric display behavior:
      - When the lineIndex changes:
        - LCD shows (current lyric line) on row 0
        - LCD shows (next lyric line) on row 1
      - This gives the karaoke read ahead feel.

algasem_z_KaraokeSummative_v8 
    - Added a short “Now Playing:” screen before each song so the demo is clear.
    - Added lcdPrintFixed() so each LCD row prints exactly 16 characters and pads spaces:
      - Prevents leftover letters when switching from longer to shorter text.
    - Cleaned up menu flow:
      - After playSong finishes, displayMenu() runs again automatically.
    - Cleaned up formatting, added comments and header.
   

*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <avr/pgmspace.h>

// Initialize LCD display
LiquidCrystal_I2C lcd(0x27, 16, 2);

//PINS
// Buzzer pin 
const int BUZZER_PIN = 2;

// 5 LEDs that light up while notes play
const int LED_PINS[5] = {4, 5, 6, 7, 8};

// Notes
// Frequencies in Hz for tone()
#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_D5   587
#define NOTE_E5   659
#define NOTE_F5   698
#define NOTE_G5   784
#define NOTE_A5   880
#define NOTE_REST 0   // 0 = silence

// SONG SETTINGS
const byte SONG_COUNT = 3;


// All rows in a 2D array must have the same length.
// Our longest song is 65 notes, so MAX_NOTES = 65.
const int MAX_NOTES = 65;

// How many notes are actually used in each song row
const byte melodyLen[SONG_COUNT] = {
  65, // Song 1: Country Roads
  42, // Song 2: Baa Baa Black Sheep
  48  // Song 3: Itsy Bitsy Spider
};

// Tempo (BPM) for each song
const int tempoBPM[SONG_COUNT] = {
  45, // Country Roads (slow)
  96, // Baa Baa (faster)
  66  // Itsy (medium)
};

// We use eighth note units
// unitMs is the time length of ONE eighth note.
unsigned long eighthMs(int bpm) {
  // Quarter note = 60000/BPM ms
  // Eighth note = quarter/2
  return (unsigned long)(60000UL / (unsigned long)bpm) / 2UL;
}

// GATE controls how much of the note time the buzzer is actually ON.
// The remaining time becomes a tiny gap so notes sound separated.
const float GATE = 0.93;

// 2D ARRAYS
// melodyNotes[songIdx][noteIdx]
const int melodyNotes[SONG_COUNT][MAX_NOTES] = {

  //SONG 1: Country Roads (65 notes)
  {
    NOTE_B4, NOTE_B4, NOTE_B4, NOTE_A4,
    NOTE_G4, NOTE_G4, NOTE_A4, NOTE_B4,
    NOTE_B4, NOTE_B4, NOTE_B4, NOTE_A4,
    NOTE_G4, NOTE_G4, NOTE_A4, NOTE_B4, NOTE_A4, NOTE_G4,
    NOTE_B4, NOTE_B4, NOTE_B4, NOTE_A4,
    NOTE_G4, NOTE_G4, NOTE_A4, NOTE_B4, NOTE_D5,
    NOTE_D4, NOTE_D4, NOTE_D4, NOTE_E4, NOTE_B4, NOTE_B4,
    NOTE_A4, NOTE_G4, NOTE_A4, NOTE_B4, NOTE_G4,
    NOTE_D5, NOTE_B4, NOTE_A4,
    NOTE_G4, NOTE_A4, NOTE_G4,
    NOTE_G4, NOTE_A4, NOTE_B4,
    NOTE_D5, NOTE_E5, NOTE_D5,
    NOTE_D5, NOTE_D5, NOTE_E5, NOTE_B4,
    NOTE_B4, NOTE_A4, NOTE_G4, NOTE_G4,
    NOTE_G4, NOTE_A4, NOTE_B4,
    NOTE_A4, NOTE_G4, NOTE_G4
  },

  //SONG 2: Baa Baa Black Sheep (42 notes)
  {
    NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4,
    NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4, NOTE_D4, NOTE_C4,
    NOTE_G4, NOTE_G4, NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4,
    NOTE_G4, NOTE_G4, NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4,
    NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4,
    NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4, NOTE_D4, NOTE_C4,

    // padding to MAX_NOTES (ignored because melodyLen is 42)
    NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST,
    NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST,
    NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST,
    NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST
  },

  //SONG 3: Itsy Bitsy Spider (48 notes)
  {
    NOTE_G4, NOTE_C5, NOTE_C5, NOTE_C5, NOTE_D5, NOTE_E5, NOTE_E5,
    NOTE_E5, NOTE_D5, NOTE_C5, NOTE_D5, NOTE_E5, NOTE_C5,
    NOTE_E5, NOTE_E5, NOTE_F5, NOTE_G5,
    NOTE_G5, NOTE_F5, NOTE_E5, NOTE_F5, NOTE_G5, NOTE_E5,
    NOTE_C5, NOTE_C5, NOTE_D5, NOTE_E5,
    NOTE_E5, NOTE_D5, NOTE_C5, NOTE_D5, NOTE_E5, NOTE_C5,
    NOTE_G4, NOTE_G4, NOTE_C5, NOTE_C5, NOTE_C5, NOTE_D5, NOTE_E5, NOTE_E5,
    NOTE_E5, NOTE_D5, NOTE_C5, NOTE_D5, NOTE_E5, NOTE_C5,

    // padding to MAX_NOTES (ignored because melodyLen is 48)
    NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST,
    NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST,
    NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST,
    NOTE_REST, NOTE_REST
  }
};

// durationUnits[songIdx][noteIdx] in eighth-note units
const byte durationUnits[SONG_COUNT][MAX_NOTES] = {

  //SONG 1: Country Roads (65)
  {
    1,1,1,2,
    1,1,1,2,
    1,1,1,2,
    1,1,1,1,1,2,
    1,1,1,2,
    1,1,1,1,2,
    1,1,1,1,1,2,
    1,1,1,1,2,
    1,1,2,
    1,1,2,
    1,1,2,
    1,1,2,
    1,1,1,2,
    1,1,1,2,
    1,1,2,
    1,1,4
  },

  //SONG 2: Baa Baa (42)
  {
    2,2,2,2,2,2,4,
    2,2,2,2,2,2,4,
    2,2,2,2,2,2,4,
    2,2,2,2,2,2,4,
    2,2,2,2,2,2,4,
    2,2,2,2,2,2,4,

    // padding ignored
    1,1,1,1,1,1,
    1,1,1,1,1,1,
    1,1,1,1,1,1,
    1,1,1,1,1
  },

  //SONG 3: Itsy (48)
  {
    1,1,1,1,1,1,2,
    1,1,1,1,1,2,
    1,1,1,2,
    1,1,1,1,1,2,
    1,1,1,2,
    1,1,1,1,1,2,
    1,1,1,1,1,1,1,2,
    1,1,1,1,1,4,

    // padding ignored
    1,1,1,1,1,
    1,1,1,1,1,
    1,1,1,1,1,
    1,1
  }
};

// ledIndex[songIdx][noteIdx] tells which LED to light (0..4)
const byte ledIndex[SONG_COUNT][MAX_NOTES] = {

  //SONG 1: Country Roads
  {
    2,2,2,1,
    0,0,1,2,
    2,2,2,1,
    0,0,1,2,1,0,
    2,2,2,1,
    0,0,1,2,3,
    3,3,3,4,2,2,
    1,0,1,2,0,
    3,2,1,
    0,1,0,
    0,1,2,
    3,4,3,
    3,3,4,2,
    2,1,0,0,
    0,1,2,
    1,0,0
  },

  //SONG 2: Baa Baa
  {
    0,0,1,1,2,2,1,
    3,3,4,4,0,0,0,
    1,1,3,3,4,4,0,
    1,1,3,3,4,4,0,
    0,0,1,1,2,2,1,
    3,3,4,4,0,0,0,

    // padding ignored
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0
  },

  //SONG 3: Itsy
  {
    0,0,0,0,1,2,2,
    2,1,0,1,2,0,
    2,2,3,4,
    4,3,2,3,4,2,
    0,0,1,2,
    2,1,0,1,2,0,
    4,4,0,0,0,1,2,2,
    2,1,0,1,2,0,

    // padding ignored
    0,0,0,0,0,
    0,0,0,0,0,
    0,0,0,0,0,
    0,0
  }
};

//LYRICS IN PROGMEM
// Lyrics stored in Flash as one big '\n' separated string 

const char lyrics1[] PROGMEM =
  "Almost heaven,\nWest Virginia,\nBlue Ridge Mtns,\nShenandoah Rvr\nLife's old there,\nolder than trees,\n"
  "Younger than mtn\ngrowin like brz\nCountry roads,\ntake me home,\nTo the place\nI belong,\n"
  "West Virginia,\nmountain mama,\nTake me home,\ncountry roads.";

const char lyrics2[] PROGMEM =
  "Baa baa\nblack sheep\nhave you\nany wool?\nYes sir\nyes sir\nthree bags full\n"
  "One for the\nmaster\nAnd one for\nthe dame\nAnd one for\nthe little boy\nwho lives down\nthe lane\n"
  "Baa baa\nblack sheep\nhave you\nany wool?\nYes sir\nyes sir\nthree bags full";

const char lyrics3[] PROGMEM =
  "The itsy bitsy\nspider\nclimbed up the\nwater spout\nDown came the\nrain and\nwashed the\nspider out\n"
  "Out came the\nsun and\ndried up all\nthe rain\nAnd the itsy\nbitsy spider\nclimbed up the\nspout again";

// Return the correct lyric string pointer for a song index
// IMPORTANT: pointer points to Flash memory, so we must read characters using pgm_read_byte()
const char* getLyricsPtr(byte songIdx) {
  switch (songIdx) {
    case 0: return lyrics1;
    case 1: return lyrics2;
    case 2: return lyrics3;
    default: return NULL;
  }
}

//LYRIC SYNC (NOTE COUNTS)
// Each number tells how many melody notes belong to that lyric line.
// 0 marks the end.
const byte MAX_LINES = 30;

const byte lineNoteCounts[SONG_COUNT][MAX_LINES] PROGMEM = {
  // SONG 1: Country Roads
  {
    4, 4, 4, 6, 4, 5, 6, 5,
    3, 3, 3, 3, 4, 4, 3, 4,
    0
  },
  // SONG 2: Baa Baa
  {
    2, 2, 2, 1, 2, 2, 3,
    2, 2,
    2, 2,
    2, 2,
    2, 2,
    2, 2, 2, 1, 2, 2, 3,
    0
  },
  // SONG 3: Itsy
  {
    5, 2, 3, 3,
    3, 2, 2, 2,
    3, 2, 2, 3,
    4, 4, 3, 4,
    0
  }
};

// Prints a string on the LCD but pads spaces to clear leftover characters.
// This prevents the LCD bug where shorter text leaves old letters behind.
void lcdPrintFixed(byte col, byte row, const char* text) {
  lcd.setCursor(col, row);

  byte i = 0;

  // Print up to 16 chars or until end of string
  while (i < 16 && text[i] != '\0') {
    lcd.print(text[i]);
    i++;
  }

  // Fill rest of row with spaces to overwrite old text
  while (i < 16) {
    lcd.print(' ');
    i++;
  }
}

// Shows two lines on the LCD (karaoke style: current + next)
void showTwoLines(const char* l1, const char* l2) {
  lcdPrintFixed(0, 0, l1);
  lcdPrintFixed(0, 1, l2);
}

// Reads ONE line from the Flash lyrics (lineIndex) into out[] (RAM).
// Returns true if the line exists, false if it does not.
bool getLyricLine(byte songIdx, byte lineIndex, char* out, byte outSize) {

  // Safety: must have space for at least '\0'
  if (outSize == 0) return false;

  // Default to empty string
  out[0] = '\0';

  // Get pointer to Flash lyrics
  const char* p = getLyricsPtr(songIdx);
  if (p == NULL) return false;

  byte curLine = 0;  // which line we are currently scanning
  byte pos = 0;      // where we are writing inside out[]

  // Scan character-by-character until string ends
  while (true) {
    char c = (char)pgm_read_byte(p);
    p++;

    // End of the Flash string
    if (c == '\0') break;

    // Ignore Windows carriage return
    if (c == '\r') continue;

    // Newline means end of a lyric line
    if (c == '\n') {

      // If we just ended the target line, finish and return
      if (curLine == lineIndex) {
        out[pos] = '\0';
        return true;
      }

      // Otherwise go to next line
      curLine++;
      pos = 0;
      continue;
    }

    // Only copy characters when we are on the requested line
    if (curLine == lineIndex) {
      if (pos < (byte)(outSize - 1)) {
        out[pos++] = c;
      }
    }
  }

  // If the last line didn’t end with '\n', we might still be on the requested line
  if (curLine == lineIndex) {
    out[pos] = '\0';
    return true;
  }

  // Line not found
  out[0] = '\0';
  return false;
}

// Turns all LEDs OFF (important because we only turn ON one LED per note)
void allLEDsOff() {
  for (byte i = 0; i < 5; i++) {
    digitalWrite(LED_PINS[i], LOW);
  }
}

// Shows the menu on LCD + Serial Monitor
void displayMenu() {
  lcd.clear();
  lcdPrintFixed(0, 0, "Enter 1, 2 or 3");
  lcdPrintFixed(0, 1, "into monitor");

  Serial.println("\nSong Selection:");
  Serial.println("1 - Country Roads");
  Serial.println("2 - Baa Baa Black Sheep");
  Serial.println("3 - Itsy Bitsy Spider");
}

// If user types invalid input, show error and re-prompt
void handleInvalidInput() {
  Serial.println("ERROR: Invalid input! Enter 1, 2, or 3.");

  lcd.clear();
  lcdPrintFixed(0, 0, "Invalid input!");
  lcdPrintFixed(0, 1, "Try again!");

  // Small pause so the message can be read
  unsigned long start = millis();
  while (millis() - start < 1400) {}

  displayMenu();
}

// Reads one non-whitespace character from Serial, ignores Enter/spaces.
// Also clears leftover characters in that line to prevent ruining the next read.
char readChoiceChar() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    // Ignore whitespace / newlines
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;

    // Clear rest of the line quickly
    unsigned long t0 = millis();
    while (millis() - t0 < 25) {
      while (Serial.available() > 0) {
        char d = Serial.read();
        if (d == '\n') break;
      }
    }
    return c;
  }
  return 0;
}

// Given notesPlayed, figure out which lyric line should be showing.
// This is the same concept your friend used: subtract line note counts until we find the correct line.
void getLineInfoFromNotesPlayed(byte songIdx, int notesPlayed, int totalNotes, int &outLineIndex, int &outLineStartNote, int &outLineNoteCount) {

  int tmp = notesPlayed;
  int lineIndex = 0;

  // Find which line contains the current note position
  while (lineIndex < (int)MAX_LINES) {
    byte c = (byte)pgm_read_byte(&(lineNoteCounts[songIdx][lineIndex]));

    // 0 means "end of defined lines"
    if (c == 0) break;

    tmp -= (int)c;

    // When tmp becomes negative, we are inside this line’s note block
    if (tmp < 0) break;

    lineIndex++;
  }

  // Calculate the starting note index of that line by summing previous lines
  int startNote = 0;
  for (int i = 0; i < lineIndex; i++) {
    byte c = (byte)pgm_read_byte(&(lineNoteCounts[songIdx][i]));
    startNote += (int)c;
  }

  // Get note count for this line
  int count = (int)pgm_read_byte(&(lineNoteCounts[songIdx][lineIndex]));

  // If count is 0, fallback to remaining notes
  if (count == 0) {
    count = totalNotes - startNote;
    if (count <= 0) count = 1;
  }

  outLineIndex = lineIndex;
  outLineStartNote = startNote;
  outLineNoteCount = count;
}

// Plays one song row using melodyNotes + durationUnits + ledIndex.
// Displays lyrics from PROGMEM in sync using lineNoteCounts.
void playSong(byte songIdx) {

  int totalNotes = (int)melodyLen[songIdx];
  int bpm = tempoBPM[songIdx];

  // Safety check
  if (totalNotes <= 0 || bpm <= 0) return;

  // Convert BPM to ms for one eighth note unit
  unsigned long unitMs = eighthMs(bpm);

  lcd.clear();
  lcdPrintFixed(0, 0, "Now Playing:");

  if (songIdx == 0) lcdPrintFixed(0, 1, "Country Roads");
  if (songIdx == 1) lcdPrintFixed(0, 1, "Baa Baa Sheep");
  if (songIdx == 2) lcdPrintFixed(0, 1, "Itsy Spider");

  unsigned long previewStart = millis();
  while (millis() - previewStart < 900) {}

  // Clean start before actual karaoke
  lcd.clear();
  allLEDsOff();
  noTone(BUZZER_PIN);

  // Buffers for LCD (RAM buffers)
  char top[17];
  char bot[17];
  top[0] = '\0';
  bot[0] = '\0';

  // Tracking lyric line changes
  int notesPlayed = 0;
  int lastLineDisplayed = -999;

  // Loop through the valid notes for this song
  while (notesPlayed < totalNotes) {

    // Determine which lyric line should be displayed right now
    int lineIndex0, lineStartNote, lineNoteCount;
    getLineInfoFromNotesPlayed(songIdx, notesPlayed, totalNotes, lineIndex0, lineStartNote, lineNoteCount);

    // Only update LCD when the lyric line changes
    if (lineIndex0 != lastLineDisplayed) {

      bool ok1 = getLyricLine(songIdx, (byte)lineIndex0, top, sizeof(top));
      bool ok2 = getLyricLine(songIdx, (byte)(lineIndex0 + 1), bot, sizeof(bot));

      if (!ok1) top[0] = '\0';
      if (!ok2) bot[0] = '\0';
      
      // Scroll
      showTwoLines(top, bot);
      lastLineDisplayed = lineIndex0;
    }

    // Read the melody note and its duration units
    int noteHz = melodyNotes[songIdx][notesPlayed];
    byte durU  = durationUnits[songIdx][notesPlayed];

    // Total time for this note
    unsigned long noteTotal = (unsigned long)durU * unitMs;
    if (noteTotal == 0) noteTotal = 1;

    // Split into ON + OFF for cleaner sound
    unsigned long noteOnMs  = (unsigned long)((float)noteTotal * GATE);
    unsigned long noteOffMs = noteTotal - noteOnMs;

    // LED display for this note
    allLEDsOff();
    byte li = ledIndex[songIdx][notesPlayed];
    if (li > 4) li = 4;

    // Play note (or rest)
    if (noteHz != NOTE_REST) {
      digitalWrite(LED_PINS[li], HIGH);
      tone(BUZZER_PIN, noteHz);
    } else {
      noTone(BUZZER_PIN);
    }

    // Hold note ON time
    unsigned long tStart = millis();
    while (millis() - tStart < noteOnMs) {}

    // Release note and LED
    noTone(BUZZER_PIN);
    allLEDsOff();

    // Hold the OFF time
    unsigned long tRel = millis();
    while (millis() - tRel < noteOffMs) {}

    // Next note
    notesPlayed++;
  }

  // End song cleanup
  noTone(BUZZER_PIN);
  allLEDsOff();

  lcd.clear();
  lcdPrintFixed(0, 0, "Song Complete!");
  lcdPrintFixed(0, 1, "Choose another!");
  Serial.println("Song Complete!");

  unsigned long endStart = millis();
  while (millis() - endStart < 1200) {}
}

void setup() {
  Serial.begin(9600);

  // Minimal + correct LCD bring-up:
  //Keep the LCD object line exactly as you wanted
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Buzzer output
  pinMode(BUZZER_PIN, OUTPUT);

  // LED outputs
  for (byte i = 0; i < 5; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  // Show starting menu
  displayMenu();
}

void loop() {

  // Only react when something is typed in Serial Monitor
  if (Serial.available() > 0) {
    char choice = readChoiceChar();

    // Valid: '1', '2', '3'
    if (choice >= '1' && choice <= '3') {
      byte songNum = (byte)(choice - '0');   // '1' -> 1
      byte songIdx = (byte)(songNum - 1);    // 1..3 -> 0..2

      if (songNum == 1) Serial.println("\nPlaying: Country Roads");
      if (songNum == 2) Serial.println("\nPlaying: Baa Baa Black Sheep");
      if (songNum == 3) Serial.println("\nPlaying: Itsy Bitsy Spider");

      playSong(songIdx);

      // After the song ends, show the menu again
      displayMenu();
    } else {
      handleInvalidInput();
    }
  }
}
