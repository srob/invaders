#include <M5Cardputer.h>
#include <Preferences.h>

const int SCREEN_W = 240;
const int SCREEN_H = 135;

Preferences preferences;

int volumeLevel;

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

unsigned long lastAlienMove = 0;
const int alienMoveInterval = 400;

struct Alien {
  int x, y;
  bool alive;
};

Alien aliens[numRows][numCols];

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

void drawAliens() {
  for (int row = 0; row < numRows; row++) {
    for (int col = 0; col < numCols; col++) {
      Alien &a = aliens[row][col];
      if (a.alive) {
        if (alienAnimationFrame) {
          M5Cardputer.Display.fillRect(a.x, a.y, alienW, alienH, RED);
        } else {
          M5Cardputer.Display.drawRect(a.x, a.y, alienW, alienH, RED);
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

void drawScore() {
  M5Cardputer.Display.fillRect(0, 0, 80, 10, BLACK);
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.print("Score: ");
  M5Cardputer.Display.print(score);
}

void drawLives() {
  M5Cardputer.Display.fillRect(100, 0, 80, 10, BLACK);
  M5Cardputer.Display.setCursor(100, 0);
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.print("Lives: ");
  M5Cardputer.Display.print(lives);
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

void showGameOver() {
  M5Cardputer.Display.fillScreen(BLACK);
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.setTextSize(2);
  int textWidth = 12 * strlen("GAME OVER");
  M5Cardputer.Display.setCursor((SCREEN_W - textWidth) / 2, SCREEN_H / 2 - 10);
  M5Cardputer.Display.println("GAME OVER");
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setCursor((SCREEN_W - 110) / 2, SCREEN_H / 2 + 15);
  M5Cardputer.Display.println("Reset to try again");
  while (true)
    ;
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

void setup() {
  M5Cardputer.begin();
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.fillScreen(BLACK);
  initAliens();

  preferences.begin("settings", true);  // true = read-only
  int savedVol = preferences.getInt("volume", -1);
  preferences.end();

  if (savedVol >= 0 && savedVol <= 255) {
    volumeLevel = savedVol;
  } else {
    volumeLevel = map(3, 0, 9, 0, 255);  // default to key 3 volume
  }

  M5Cardputer.Speaker.setVolume(volumeLevel);
}

void loop() {
  M5Cardputer.update();
  if (millis() - lastFrame > frameDelay) {
    lastFrame = millis();
    clearShip();
    if (bulletActive) clearBullet();
    clearAliens();
    clearExplosions();
    clearAlienBullets();

    auto keys = M5Cardputer.Keyboard.keysState();
    unsigned long now = millis();

    // Handle key press (fire bullet or movement start)
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      for (char key : keys.word) {
        if (key >= '0' && key <= '9') {
          // Map key '0'–'9' to volume 0–255
          volumeLevel = map(key - '0', 0, 9, 0, 255);
          preferences.begin("settings", false); // false = write mode
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

    bool skipShipLogic = false;

    if (shipHit) {
      clearExplosions();
      drawExplosions();
      skipShipLogic = true;

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
        if (pendingGameOver) {
          showGameOver();
        } else {
          shipX = SCREEN_W / 2 - 5;
        }
      }
    }

    if (!skipShipLogic) {

      drawShip();

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
            pendingGameOver = true;  // defer showing Game Over
          }
        }
      }
    }

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

    if (random(0, 20) == 0) {
      int col = random(numCols);
      for (int row = numRows - 1; row >= 0; row--) {
        if (aliens[row][col].alive) {
          fireAlienBullet(aliens[row][col].x, aliens[row][col].y);
          break;
        }
      }
    }

    if (aliensReachedPlayer()) {
      lives--;
      startCrackleAndBoom();
      if (lives <= 0) showGameOver();
      initAliens();
    }

    if (allAliensDead()) {
      initAliens();
    }

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

    drawShip();
    if (bulletActive) drawBullet();
    drawAliens();
    drawExplosions();
    drawAlienBullets();

    updateCrackleAndBoom();

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

    drawScore();
    drawLives();
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
      showGameOver();
      pendingGameOver = false;
    }
  }
}
