#include <M5Cardputer.h>
#include <Preferences.h>

#define ENABLE_DEBUG 0  // Set to 0 to disable all debug output

#if ENABLE_DEBUG
#define DEBUG_BEGIN() \
  Serial.begin(115200); \
  while (!Serial && millis() < 2000)
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_BEGIN()
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

const int SCREEN_W = 240;
const int SCREEN_H = 135;

Preferences preferences;

int volumeLevel;

char sessionName[11] = "";  // Up to 10 characters, null-terminated

const int leaderboardSize = 4;
int highScores[leaderboardSize];
char highScoreNames[leaderboardSize][11];  // 10 chars + null terminator

// Player / ship
int shipX = SCREEN_W / 2 - 5;
int shipY = SCREEN_H - 15;
const int shipW = 10;
const int shipH = 5;
int lives = 3;

bool shipHit = false;
unsigned long shipExplosionStart = 0;
const int shipExplosionDuration = 400;  // You can tweak this
bool pendingGameOver = false;

bool inGameOverScreen = false;

// Bullet
int bulletX = 0;
int bulletY = 0;
bool bulletActive = false;

// Alien config
const int numRows = 3;
const int numCols = 8;
const int alienW = 12;
const int alienH = 6;
const int alienSpacingX = 24;
const int alienSpacingY = 14;
int alienStartX = 16;
int alienStartY = 20;
int alienDir = 1;
bool alienAnimationFrame = false;

const int alienAreaTop = 20;     // Y position where aliens start
const int alienAreaHeight = 50;  // Approximate height covering all alien rows

unsigned long lastAlienMove = 0;
unsigned long alienMoveInterval = 400;  // starting speed (in ms)

struct Alien {
  int x, y;
  bool alive;
};

Alien aliens[numRows][numCols];

// Two animation frames for the aliens. Switching between these frames every
// few updates gives a simple walking animation.
const uint8_t alienSprites[2][8] = {
    {
        B00100010,
        B01110111,
        B11111111,
        B10111101,
        B11111111,
        B10001001,
        B00100100,
        B01000010
    },
    {
        B00100010,
        B01110111,
        B11111111,
        B10111101,
        B11111111,
        B01000010,
        B00111100,
        B10000001
    }
};

const int maxAlienBullets = 5;

struct AlienBullet {
  int x, y;
  bool active;
};

AlienBullet alienBullets[maxAlienBullets];

const int alienMarchTones[4] = { 800, 900, 1000, 1100 };
int currentAlienTone = 0;

// Mothership
int mothershipX = 0;
int mothershipY = 10;
int mothershipW = 16;
int mothershipH = 8;
bool mothershipActive = false;
bool mothershipDirectionRight = true;
unsigned long lastMothershipSpawn = 0;
const unsigned long mothershipSpawnInterval = 15000;  // every 15 seconds
unsigned long lastMothershipSound = 0;
const int mothershipSoundInterval = 300;  // ms between beeps

struct Explosion {
  int x, y;
  int frame;
  bool active;
  uint16_t color;
  unsigned long lastFrameTime;
  int framesTotal;
};

const int maxExplosions = 10;
const int explosionFrames = 6;
Explosion explosions[maxExplosions];

bool crackleActive = false;
unsigned long crackleStart = 0;
int crackleStep = 0;
bool boomPhase = false;
unsigned long lastCrackleTime = 0;

int score = 0;

bool showBonus = false;
unsigned long bonusStart = 0;
const int bonusDuration = 800;  // ms
int bonusX = 0;
int bonusY = 0;

unsigned long lastFrame = 0;
const int frameDelay = 30;

char lastMoveKey = 0;
unsigned long lastMoveTime = 0;
const unsigned long repeatInterval = 60;  // ms between repeats
const int moveStep = 3;

void initAliens() {
  for (int row = 0; row < numRows; row++) {
    for (int col = 0; col < numCols; col++) {
      aliens[row][col].x = alienStartX + col * alienSpacingX;
      aliens[row][col].y = alienStartY + row * alienSpacingY;
      aliens[row][col].alive = true;
    }
  }
}

void drawShip() {
  M5Cardputer.Display.fillRect(shipX, shipY, shipW, shipH, WHITE);
}

void clearShip() {
  M5Cardputer.Display.fillRect(shipX, shipY, shipW, shipH, BLACK);
}

void drawBullet() {
  M5Cardputer.Display.drawFastVLine(bulletX, bulletY, 5, GREEN);
}

void clearBullet() {
  M5Cardputer.Display.drawFastVLine(bulletX, bulletY, 5, BLACK);
}

// void drawAliens() {
//   for (int row = 0; row < numRows; row++) {
//     for (int col = 0; col < numCols; col++) {
//       Alien &a = aliens[row][col];
//       if (a.alive) {
//         if (alienAnimationFrame) {
//           M5Cardputer.Display.fillRect(a.x, a.y, alienW, alienH, RED);
//         } else {
//           M5Cardputer.Display.drawRect(a.x, a.y, alienW, alienH, RED);
//         }
//       }
//     }
//   }
// }

void updateAliens() {
  // Clear the entire area where aliens live
  M5Cardputer.Display.fillRect(0, alienAreaTop, SCREEN_W, alienAreaHeight, BLACK);

  for (int row = 0; row < numRows; row++) {
    for (int col = 0; col < numCols; col++) {
      Alien &a = aliens[row][col];
      if (a.alive) {
        for (int y = 0; y < 8; y++) {
          const uint8_t rowData =
              alienSprites[alienAnimationFrame ? 1 : 0][y];
          for (int x = 0; x < 8; x++) {
            if (rowData & (1 << (7 - x))) {
              M5Cardputer.Display.drawPixel(a.x + x, a.y + y, WHITE);
            }
          }
        }
      }
    }
  }
}

bool isExplosionAt(int x, int y) {
  for (int i = 0; i < maxExplosions; i++) {
    if (explosions[i].active && explosions[i].x == x && explosions[i].y == y) {
      return true;
    }
  }
  return false;
}

void clearAliens() {
  for (int row = 0; row < numRows; row++) {
    for (int col = 0; col < numCols; col++) {
      Alien &a = aliens[row][col];
      if (a.alive && !isExplosionAt(a.x, a.y)) {
        M5Cardputer.Display.fillRect(a.x, a.y, alienW, alienH, BLACK);
      }
    }
  }
}

void drawScoreAndLives() {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "Score: %3d   Lives: %d", score, lives);

  int textSize = 1;              // Assuming setTextSize(1)
  int charWidth = 6 * textSize;  // 6 pixels per char in default font
  int textWidth = strlen(buffer) * charWidth;
  int x = (SCREEN_W - textWidth) / 2;

  // Clear a wide enough area to overwrite previous text
  M5Cardputer.Display.fillRect(0, 2, SCREEN_W, 12, BLACK);

  M5Cardputer.Display.setTextSize(textSize);
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.setCursor(x, 1);
  M5Cardputer.Display.print(buffer);
}

void fireBullet() {
  if (!bulletActive) {
    bulletX = shipX + shipW / 2;
    bulletY = shipY - 5;
    bulletActive = true;
    M5Cardputer.Speaker.tone(1000, 80);
  }
}

bool checkCollision(Alien &a) {
  return bulletX >= a.x && bulletX <= (a.x + alienW) && bulletY >= a.y && bulletY <= (a.y + alienH);
}

bool allAliensDead() {
  for (int row = 0; row < numRows; row++) {
    for (int col = 0; col < numCols; col++) {
      if (aliens[row][col].alive) return false;
    }
  }
  return true;
}

bool aliensReachedPlayer() {
  for (int row = 0; row < numRows; row++) {
    for (int col = 0; col < numCols; col++) {
      Alien &a = aliens[row][col];
      if (a.alive && a.y + alienH >= shipY) return true;
    }
  }
  return false;
}

void drawExplosions() {
  unsigned long now = millis();
  for (int i = 0; i < maxExplosions; i++) {
    if (explosions[i].active) {
      // Check if it's time to update this explosion frame
      if (now - explosions[i].lastFrameTime >= 100) {
        // Clear previous frame before updating
        int oldSize = 4 + explosions[i].frame * 4;
        int oldHalf = oldSize / 2;
        M5Cardputer.Display.fillCircle(explosions[i].x, explosions[i].y, oldHalf, BLACK);

        explosions[i].frame++;
        explosions[i].lastFrameTime = now;

        if (explosions[i].frame >= explosions[i].framesTotal) {
          explosions[i].active = false;
          continue;  // Don’t draw below
        }
      }

      // Draw new frame
      int size = 4 + explosions[i].frame * 4;
      int half = size / 2;
      M5Cardputer.Display.fillCircle(explosions[i].x, explosions[i].y, half, explosions[i].color);
    }
  }
}

void spawnExplosion(int x, int y, uint16_t color, int frames) {
  for (int i = 0; i < maxExplosions; i++) {
    if (!explosions[i].active) {
      explosions[i].x = x;
      explosions[i].y = y;
      explosions[i].frame = 0;
      explosions[i].active = true;
      explosions[i].color = color;
      explosions[i].lastFrameTime = millis();
      explosions[i].framesTotal = frames;
      break;
    }
  }
}

void clearExplosions() {
  for (int i = 0; i < maxExplosions; i++) {
    if (explosions[i].active) {
      int size = 4 + explosions[i].frame * 4;
      int half = size / 2;
      M5Cardputer.Display.fillCircle(explosions[i].x, explosions[i].y, half, BLACK);
    }
  }
}

void startCrackleAndBoom() {
  crackleActive = true;
  crackleStart = millis();
  crackleStep = 0;
  boomPhase = false;
  lastCrackleTime = millis();
}

void updateCrackleAndBoom() {
  if (!crackleActive) return;

  unsigned long now = millis();

  if (!boomPhase) {
    // Crackle phase (6 quick random tones)
    if (crackleStep < 6 && now - lastCrackleTime >= 15) {
      int freq = random(800, 1600);
      M5Cardputer.Speaker.tone(freq, 20);
      lastCrackleTime = now;
      crackleStep++;
    } else if (crackleStep >= 6) {
      boomPhase = true;
      crackleStep = 1000;  // start freq for boom
      lastCrackleTime = now;
    }
  } else {
    // Boom phase (descending tone)
    if (crackleStep >= 200 && now - lastCrackleTime >= 20) {
      M5Cardputer.Speaker.tone(crackleStep, 30);
      lastCrackleTime = now;
      crackleStep -= 100;
    } else if (crackleStep < 200) {
      crackleActive = false;
    }
  }
}

void fireAlienBullet(int x, int y) {
  for (int i = 0; i < maxAlienBullets; i++) {
    if (!alienBullets[i].active) {
      alienBullets[i].x = x + alienW / 2;
      alienBullets[i].y = y + alienH;
      alienBullets[i].active = true;
      break;
    }
  }
}

void clearAlienBullets() {
  for (int i = 0; i < maxAlienBullets; i++) {
    if (alienBullets[i].active) {
      M5Cardputer.Display.drawFastVLine(alienBullets[i].x, alienBullets[i].y, 5, BLACK);
    }
  }
}

void drawAlienBullets() {
  for (int i = 0; i < maxAlienBullets; i++) {
    if (alienBullets[i].active) {
      M5Cardputer.Display.drawFastVLine(alienBullets[i].x, alienBullets[i].y, 5, RED);
    }
  }
}

void resetHighScores() {
  preferences.begin("settings", false);
  for (int i = 0; i < leaderboardSize; i++) {
    preferences.remove(("score" + String(i)).c_str());
    preferences.remove(("name" + String(i)).c_str());
    highScores[i] = 0;
    strncpy(highScoreNames[i], "--------", sizeof(highScoreNames[i]));
  }
  preferences.end();
}

void showHighScores(const char *title = "HIGH SCORES") {
  M5Cardputer.Display.fillScreen(BLACK);

  // --- Title ---
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(RED);
  int titleLen = strlen(title);
  int titleWidth = titleLen * 12;  // Approximate for size 2
  int titleX = (SCREEN_W - titleWidth) / 2;
  M5Cardputer.Display.setCursor(titleX, 15);
  M5Cardputer.Display.print(title);

  // --- High Score List ---
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(WHITE);
  int y = 50;

  for (int i = 0; i < leaderboardSize; i++) {
    char line[32];
    snprintf(line, sizeof(line), "%d. %-10s %5d", i + 1, highScoreNames[i], highScores[i]);
    M5Cardputer.Display.setCursor(20, y);
    M5Cardputer.Display.print(line);
    y += 15;
  }

  // --- Instructions ---
  const char *instr = "R Reset, Any other key Continue";
  int instrLen = strlen(instr);
  int instrWidth = instrLen * 6;  // Approximate for size 1
  int instrX = (SCREEN_W - instrWidth) / 2;
  M5Cardputer.Display.setCursor(instrX, SCREEN_H - 20);
  M5Cardputer.Display.setTextColor(GREEN);
  M5Cardputer.Display.print(instr);

  // --- Wait for Input ---
  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      auto keys = M5Cardputer.Keyboard.keysState();
      for (char key : keys.word) {
        if (key == 'r' || key == 'R') {
          resetHighScores();
          showHighScores("HIGH SCORES RESET");
          return;
        } else {
          // Clear screen and reset game
          M5Cardputer.Display.fillScreen(BLACK);
          lives = 3;
          score = 0;
          bulletActive = false;
          mothershipActive = false;
          initAliens();
          shipX = SCREEN_W / 2 - shipW / 2;
          return;
        }
      }
    }
  }
}

void showGameOverScreen() {
  const char *text = "GAME OVER";

  M5Cardputer.Display.fillScreen(BLACK);
  M5Cardputer.Display.setTextColor(RED);
  M5Cardputer.Display.setTextSize(3);  // Large text

  int charWidth = 6 * 3;   // 6 pixels per char × text size
  int charHeight = 8 * 3;  // 8 pixels per char × text size
  int textLen = strlen(text);

  int textWidth = textLen * charWidth;
  int screenWidth = M5Cardputer.Display.width();
  int screenHeight = M5Cardputer.Display.height();

  int centerX = (screenWidth - textWidth) / 2;
  int centerY = (screenHeight - charHeight) / 2;

  M5Cardputer.Display.setCursor(centerX, centerY);
  M5Cardputer.Display.print(text);

  delay(3000);  // Pause so user sees GAME OVER before moving on
}

void updateLeaderboard(int newScore, const char *newName) {
  int insertPos = -1;

  // Find insert position
  for (int i = 0; i < leaderboardSize; ++i) {
    if (newScore > highScores[i]) {
      insertPos = i;
      break;
    }
  }

  if (insertPos == -1) return;  // Not a high score

  // Shift down to make room
  for (int j = leaderboardSize - 1; j > insertPos; --j) {
    highScores[j] = highScores[j - 1];
    strncpy(highScoreNames[j], highScoreNames[j - 1], sizeof(highScoreNames[j]));
  }

  // Insert new score
  highScores[insertPos] = newScore;
  strncpy(highScoreNames[insertPos], newName, sizeof(highScoreNames[insertPos]));
  highScoreNames[insertPos][sizeof(highScoreNames[insertPos]) - 1] = '\0';

  // Store to preferences
  preferences.begin("settings", false);
  for (int i = 0; i < leaderboardSize; i++) {
    preferences.putInt(("score" + String(i)).c_str(), highScores[i]);
    preferences.putString(("name" + String(i)).c_str(), highScoreNames[i]);
  }
  preferences.end();
}

void drawBonusText() {
  if (showBonus && millis() - bonusStart < bonusDuration) {
    M5Cardputer.Display.setCursor(bonusX - 10, bonusY);
    M5Cardputer.Display.setTextColor(MAGENTA);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.print("+50");
  } else if (showBonus) {
    showBonus = false;
    M5Cardputer.Display.fillRect(bonusX - 10, bonusY, 30, 10, BLACK);  // Clear text
  }
}

void handleGameOver() {
  inGameOverScreen = true;

  showGameOverScreen();

  if (score > highScores[leaderboardSize - 1]) {
    const char *name = promptHighScoreName();
    // Store entered name for reuse later in session
    strncpy(sessionName, name, sizeof(sessionName));
    sessionName[sizeof(sessionName) - 1] = '\0';  // Ensure null-terminated
    updateLeaderboard(score, name);
  }

  showHighScores("HIGH SCORES");

  // Reset state for new game
  lives = 3;
  score = 0;
  bulletActive = false;
  mothershipActive = false;
  shipX = SCREEN_W / 2 - shipW / 2;
  initAliens();

  inGameOverScreen = false;
}

void playSplashMelody() {
  int melody[] = { 880, 659, 587, 523 };     // Approx. A5, E5, D5, C5
  int durations[] = { 150, 150, 150, 300 };  // Slightly longer on the last note

  for (int i = 0; i < 4; i++) {
    M5Cardputer.Speaker.tone(melody[i], durations[i]);
    delay(durations[i] + 30);
  }

  M5Cardputer.Speaker.end();
}

void showSplashScreen() {
  M5Cardputer.Display.fillScreen(BLACK);

  // Title: INVADERS (centered, red)
  const char *title = "INVADERS";
  M5Cardputer.Display.setTextSize(3);
  M5Cardputer.Display.setTextColor(RED);
  int titleWidth = strlen(title) * 6 * 3;
  int titleX = (SCREEN_W - titleWidth) / 2;
  M5Cardputer.Display.setCursor(titleX, 25);
  M5Cardputer.Display.println(title);

  // Instructions (green)
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(GREEN);

  const char *line1 = "A Left, D Right, Space/F Fire";
  const char *line2 = "0..9 Set volume, Any other key Start";

  int line1Width = strlen(line1) * 6;
  int line2Width = strlen(line2) * 6;

  int line1X = (SCREEN_W - line1Width) / 2;
  int line2X = (SCREEN_W - line2Width) / 2;

  M5Cardputer.Display.setCursor(line1X, 75);
  M5Cardputer.Display.println(line1);

  M5Cardputer.Display.setCursor(line2X, 90);
  M5Cardputer.Display.println(line2);

  // Play theme
  playSplashMelody();

  // Wait for key press
  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      return;
    }
  }
}

const char *promptHighScoreName() {
  static char nameBuf[11] = "";
  strncpy(nameBuf, sessionName, sizeof(nameBuf));
  nameBuf[sizeof(nameBuf) - 1] = '\0';
  int nameLen = strlen(nameBuf);

  const char *title = "NEW HIGH SCORE!";
  const char *prompt = "Enter your name:";
  const char *instructions = "Enter OK, Del Delete";

  M5Cardputer.Display.fillScreen(BLACK);

  // Title (centered, red, size 2)
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(RED);
  int titleX = (SCREEN_W - strlen(title) * 6 * 2) / 2;
  M5Cardputer.Display.setCursor(titleX, 20);
  M5Cardputer.Display.println(title);

  // Prompt (centered, white, size 1)
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(WHITE);
  int promptX = (SCREEN_W - strlen(prompt) * 6) / 2;
  M5Cardputer.Display.setCursor(promptX, 50);
  M5Cardputer.Display.println(prompt);

  // Instructions (centered)
  int instrX = (SCREEN_W - strlen(instructions) * 6) / 2;
  M5Cardputer.Display.setCursor(instrX, 100);
  M5Cardputer.Display.setTextColor(GREEN);
  M5Cardputer.Display.println(instructions);

  M5Cardputer.Display.setTextColor(WHITE);

  const int inputY = 75;
  const int inputX = 10;

  while (true) {
    // Input line
    M5Cardputer.Display.fillRect(inputX, inputY, SCREEN_W - 2 * inputX, 12, BLACK);
    M5Cardputer.Display.setCursor(inputX, inputY);
    M5Cardputer.Display.print("> ");
    M5Cardputer.Display.print(nameBuf);

    // Blinking cursor
    if ((millis() / 500) % 2 && nameLen < 10) {
      int cursorX = inputX + 12 + nameLen * 6;
      M5Cardputer.Display.drawFastHLine(cursorX, inputY + 10, 6, WHITE);
    }

    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      auto st = M5Cardputer.Keyboard.keysState();

      // Backspace/Delete
      if (st.del && nameLen > 0) {
        nameBuf[--nameLen] = '\0';
      }

      // Enter (submit)
      if (st.enter && nameLen > 0) {
        M5Cardputer.Display.fillRect(inputX, inputY, SCREEN_W - 2 * inputX, 12, BLACK);
        return nameBuf;
      }

      // Printable characters
      for (char c : st.word) {
        if (isPrintable(c) && nameLen < 10) {
          nameBuf[nameLen++] = c;
          nameBuf[nameLen] = '\0';
        }
      }
    }

    delay(50);  // For cursor blink
  }
}


int countRemainingAliens() {
  int count = 0;
  for (int row = 0; row < numRows; row++) {
    for (int col = 0; col < numCols; col++) {
      if (aliens[row][col].alive) {
        count++;
      }
    }
  }
  return count;
}

void setup() {
  DEBUG_BEGIN();
  DEBUG_PRINTLN("Starting setup...");

  M5Cardputer.begin();
  DEBUG_PRINTLN("M5Cardputer initialized");

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.fillScreen(BLACK);
  DEBUG_PRINTLN("Display configured");

  preferences.begin("settings", true);  // Read-only
  DEBUG_PRINTLN("Preferences opened (read-only)");

  // Load saved volume
  int savedVol = preferences.getInt("volume", -1);
  if (savedVol >= 0 && savedVol <= 255) {
    volumeLevel = savedVol;
    DEBUG_PRINT("Loaded saved volume level: ");
    DEBUG_PRINTLN(volumeLevel);
  } else {
    volumeLevel = map(3, 0, 9, 0, 255);  // default to key 3 volume
    DEBUG_PRINTLN("No valid saved volume found. Defaulting to key 3 volume.");
  }
  M5Cardputer.Speaker.setVolume(volumeLevel);

  // Load leaderboard entries
  for (int i = 0; i < leaderboardSize; i++) {
    String scoreKey = "score" + String(i);
    String nameKey = "name" + String(i);
    highScores[i] = preferences.getInt(scoreKey.c_str(), 0);
    String loadedName = preferences.getString(nameKey.c_str(), "--------");
    loadedName.toCharArray(highScoreNames[i], sizeof(highScoreNames[i]));
    DEBUG_PRINT("Leaderboard entry ");
    DEBUG_PRINT(i);
    DEBUG_PRINT(": ");
    DEBUG_PRINT(highScoreNames[i]);
    DEBUG_PRINT(" - ");
    DEBUG_PRINTLN(highScores[i]);
  }
  preferences.end();
  DEBUG_PRINTLN("Preferences closed");

  showSplashScreen();
  showHighScores("HIGH SCORES");

  initAliens();
  DEBUG_PRINTLN("Aliens initialized");

  DEBUG_PRINTLN("Setup complete");
}

void moveAliens() {
  calculateAliensMoveInterval();
  if (millis() - lastAlienMove > alienMoveInterval) {
    M5Cardputer.Speaker.tone(alienMarchTones[currentAlienTone], 100);
    currentAlienTone = (currentAlienTone + 1) % 4;

    alienAnimationFrame = !alienAnimationFrame;
    lastAlienMove = millis();
    bool needToReverse = false;

    for (int row = 0; row < numRows; row++) {
      for (int col = 0; col < numCols; col++) {
        Alien &a = aliens[row][col];
        if (a.alive) {
          a.x += alienDir * 2;
          if (a.x <= 0 || a.x + alienW >= SCREEN_W) {
            needToReverse = true;
          }
        }
      }
    }

    if (needToReverse) {
      alienDir *= -1;
      for (int row = 0; row < numRows; row++) {
        for (int col = 0; col < numCols; col++) {
          aliens[row][col].y += 6;
        }
      }
    }
  }
}

void calculateAliensMoveInterval() {
  // Dynamically adjust alien move interval based on remaining aliens
  int remainingAliens = countRemainingAliens();
  if (remainingAliens > 20) {
    alienMoveInterval = 400;
  } else if (remainingAliens > 10) {
    alienMoveInterval = 300;
  } else if (remainingAliens > 5) {
    alienMoveInterval = 200;
  } else if (remainingAliens > 3) {
    alienMoveInterval = 100;
  } else if (remainingAliens > 1) {
    alienMoveInterval = 75;
  } else {
    alienMoveInterval = 30;
  }
}

void handleAlienBullets() {
  for (int i = 0; i < maxAlienBullets; i++) {
    if (alienBullets[i].active) {
      alienBullets[i].y += 3;
      if (alienBullets[i].y >= SCREEN_H) {
        alienBullets[i].active = false;
      } else if (
        alienBullets[i].x >= shipX && alienBullets[i].x <= (shipX + shipW) && alienBullets[i].y >= shipY && alienBullets[i].y <= (shipY + shipH)) {
        alienBullets[i].active = false;
        if (lives == 1) {
          spawnExplosion(shipX + shipW / 2, shipY + shipH / 2, MAGENTA, 12);  // more frames
        } else {
          spawnExplosion(shipX + shipW / 2, shipY + shipH / 2, RED, explosionFrames);
        }
        shipHit = true;
        shipExplosionStart = millis();
        startCrackleAndBoom();

        lives--;
        if (lives <= 0) {
          lives = 0;
          pendingGameOver = true;  // defer showing Game Over
        }
      }
    }
  }
}

void explodeAliensWhenHit() {
  if (bulletActive) {
    bulletY -= 5;
    if (bulletY < 0) bulletActive = false;
    for (int row = 0; row < numRows; row++) {
      for (int col = 0; col < numCols; col++) {
        Alien &a = aliens[row][col];
        if (a.alive && checkCollision(a)) {
          a.alive = false;
          bulletActive = false;
          score += 10;
          spawnExplosion(a.x + alienW / 2, a.y + alienH / 2, ORANGE, explosionFrames);
          startCrackleAndBoom();
        }
      }
    }
  }
}

void fireAlienBullets() {
  if (random(0, 20) == 0) {
    int col = random(numCols);
    for (int row = numRows - 1; row >= 0; row--) {
      if (aliens[row][col].alive) {
        fireAlienBullet(aliens[row][col].x, aliens[row][col].y);
        break;
      }
    }
  }
}

void explodeMotherShipWhenHit() {
  // Bullet hits mothership
  if (mothershipActive && bulletActive) {
    if (bulletX >= mothershipX && bulletX <= (mothershipX + mothershipW) && bulletY >= mothershipY && bulletY <= (mothershipY + mothershipH)) {
      mothershipActive = false;
      bulletActive = false;
      spawnExplosion(mothershipX + mothershipW / 2, mothershipY + mothershipH / 2, MAGENTA, explosionFrames);
      score += 50;
      startCrackleAndBoom();
      showBonus = true;
      bonusStart = millis();
      bonusX = mothershipX + mothershipW / 2;
      bonusY = mothershipY - 10;
    }
  }
}

void handleKeys() {
  auto keys = M5Cardputer.Keyboard.keysState();
  unsigned long now = millis();

  // Handle key press (fire bullet or movement start)
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    for (char key : keys.word) {
      if (key >= '0' && key <= '9') {
        // Map key '0'–'9' to volume 0–255
        volumeLevel = map(key - '0', 0, 9, 0, 255);
        preferences.begin("settings", false);  // false = write mode
        preferences.putInt("volume", volumeLevel);
        preferences.end();
        M5Cardputer.Speaker.setVolume(volumeLevel);
      }

      if (key == 'a' || key == 'd') {
        lastMoveKey = key;
        lastMoveTime = now;
      }
      if (key == 'f' || key == ' ') {
        fireBullet();
      }
    }
  }

  // Handle key repeat for movement
  if (lastMoveKey && (now - lastMoveTime >= repeatInterval)) {
    if (lastMoveKey == 'a' && shipX > 0) shipX -= moveStep;
    if (lastMoveKey == 'd' && shipX < SCREEN_W - shipW) shipX += moveStep;
    lastMoveTime = now;
  }

  // Stop movement if key no longer held
  bool stillHolding = false;
  for (char key : keys.word) {
    if (key == lastMoveKey) {
      stillHolding = true;
      break;
    }
  }
  if (!stillHolding) {
    lastMoveKey = 0;
  }
}

void spawnAndMoveMotherShip() {
  // Mothership spawn logic
  if (!mothershipActive && millis() - lastMothershipSpawn > mothershipSpawnInterval) {
    mothershipActive = true;
    mothershipDirectionRight = random(0, 2);  // 0 = left to right, 1 = right to left
    mothershipX = mothershipDirectionRight ? -mothershipW : SCREEN_W;
    lastMothershipSpawn = millis();
  }

  // Mothership movement
  if (mothershipActive) {
    // Clear previous mothership position
    M5Cardputer.Display.fillRect(mothershipX, mothershipY, mothershipW, mothershipH, BLACK);

    mothershipX += mothershipDirectionRight ? 2 : -2;

    if ((mothershipDirectionRight && mothershipX > SCREEN_W) || (!mothershipDirectionRight && mothershipX + mothershipW < 0)) {
      mothershipActive = false;
    } else {
      M5Cardputer.Display.fillRect(mothershipX, mothershipY, mothershipW, mothershipH, PURPLE);
    }
  }

  if (mothershipActive) {
    if (millis() - lastMothershipSound > mothershipSoundInterval) {
      M5Cardputer.Speaker.tone(900, 100);  // Short beep
      lastMothershipSound = millis();
    }
  }
}

void handleShipHit() {
  clearExplosions();
  drawExplosions();

  // Wait for all explosions to complete before ending the hit state
  bool shipExplosionOngoing = false;
  for (int i = 0; i < maxExplosions; i++) {
    if (explosions[i].active) {
      shipExplosionOngoing = true;
      break;
    }
  }

  if (!shipExplosionOngoing) {
    shipHit = false;
    if (!pendingGameOver) {
      shipX = SCREEN_W / 2 - 5;
    }
  }
}

void loop() {
  if (inGameOverScreen) {
    delay(10);
    return;
  }

  M5Cardputer.update();
  if (millis() - lastFrame > frameDelay) {
    lastFrame = millis();
    clearShip();
    if (bulletActive) clearBullet();
    // clearAliens();
    clearExplosions();
    clearAlienBullets();

    handleKeys();

    if (shipHit) {
      handleShipHit();
    } else {
      drawShip();
    }

    explodeAliensWhenHit();
    handleAlienBullets();
    moveAliens();
    updateAliens();

    fireAlienBullets();

    if (aliensReachedPlayer()) {
      lives--;
      if (lives < 0) lives = 0;
      startCrackleAndBoom();
      if (lives <= 0) pendingGameOver = true;
      initAliens();
    }

    if (allAliensDead()) {
      initAliens();
    }

    explodeMotherShipWhenHit();
    drawShip();

    if (bulletActive)
      drawBullet();

    drawExplosions();
    drawAlienBullets();

    updateCrackleAndBoom();

    spawnAndMoveMotherShip();

    drawScoreAndLives();
    drawBonusText();

    if (lives <= 0) {
      pendingGameOver = true;
    }
  }
  // Only show Game Over when all explosions are finished animating
  if (pendingGameOver) {
    bool explosionsRemaining = false;
    for (int i = 0; i < maxExplosions; i++) {
      if (explosions[i].active) {
        explosionsRemaining = true;
        break;
      }
    }
    if (!explosionsRemaining) {
      handleGameOver();
      pendingGameOver = false;
    }
  }
}
