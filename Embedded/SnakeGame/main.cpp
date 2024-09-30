#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <EEPROM.h>
#include <math.h>
#include <string.h>

#define TFT_CS 10
#define TFT_DC 9
#define BUZZER 5      // output pin for buzzer
#define mouseButton 3 // input pin for the mouse pushButton
#define xAxis A0      // joystick X axis
#define yAxis A1      // joystick Y axis

#define SCREEN_WIDTH 240
#define MAX_SNAKE_LENGTH 100
#define MAX_BAD_FOOD 50
#define X_BOUNDARY 10 // define the area of the screen for gameplay
#define Y_BOUNDARY 40 // define the area of the screen for gameplay

#define EEPROM_HIGH_SCORE_ADDRESS 0

Adafruit_ILI9341 screen = Adafruit_ILI9341(TFT_CS,TFT_DC);

//==========================FUNCTIONS==============================
//=================================================================

void screenDisplay(char str[], unsigned int y);
int readAxis(int thisAxis);
void menu();
void menuNavigation(int move);
void highlightMenuItem(int mode);
void unhighlightMenuItem(int mode);
void play();
void highscore();
void updateScore(int points,int level);
void gameOver(int points);
void joystickISR();
void displayCountdown(unsigned int remainingTime);
void playSound(char sound[]);
int readHighScore() ;
void writeHighScore(int highScore) ;
void displayBackButton();

//=================================================================
// Parameters for reading the joystick:

int range = 4;             // output range of X or Y movement
int responseDelay = 5;      // response delay of the mouse, in ms
int threshold = range / 4;  // resting threshold
int center = range / 2;     // resting position value

//=================================================================
// Global variables

int xReading;
int yReading;
char lastMove = 'r';  // Initialize with a default direction
volatile bool buttonPressed = false;
int currentMode = 1;
int previousMode = 1;
int best;

unsigned long foodSpawnTime = 0;  // Tracks when the food was generated
bool foodVisible = true;          // Whether the food is currently visible
const unsigned long foodDisappearTime = 5000;  // 5 seconds for food disappearance
unsigned long lastUpdateTime = 0;

//=================================================================
// Snake body

struct SnakeSegment {
  int x;
  int y;
};

//=================================================================
void setup() {
  pinMode(mouseButton, INPUT_PULLUP); // the mouse button
  attachInterrupt(digitalPinToInterrupt(mouseButton), joystickISR, FALLING);

  Serial.begin(9600);         // start serial communication
  screen.begin();
  screen.setRotation(4);
  screen.fillScreen(ILI9341_BLACK);
  randomSeed(analogRead(4));
  playSound("gameStartingMelody");
  menu();
}

void loop() {
  // Read and scale the two axes:
  xReading = readAxis(xAxis);
  yReading = readAxis(yAxis);

  // Handle joystick movements in the game menu or game
  menuNavigation(yReading);  // Move in the menu based on Y axis
}

//Functions for start-up and menu navigation
//=================================================================

void screenDisplay(char str[], unsigned int y){
  //DISPLAYS TEXT IN THE MIDDLE OF THE SCREEN

  int16_t x1, y1; // variables for text coordinates
  uint16_t w, h;  // variables for text width and height
  screen.getTextBounds(str, 0, 0, &x1, &y1, &w, &h); // Get width of text
  screen.setCursor((SCREEN_WIDTH - w) / 2, y); // Set cursor to the center
  screen.print(str);
}

void menu(){
  //DISPLAY THE MENU WITH THE TWO OPTIONS "SNAKE GAME" AND "HIGH SCORES"

  screen.setTextSize(3);
  screen.setTextColor(ILI9341_RED);
  screenDisplay("SNAKE GAME",40);
  screen.fillRect(30,90,180,20,ILI9341_ORANGE);
  screen.setTextColor(ILI9341_WHITE);
  screen.setTextSize(2);
  screenDisplay("START",90);
  screenDisplay("HIGH SCORES",130);

}

void menuNavigation(int move) {
  //NAVIGATES THE MENU BASED ON JOYSTICK CONTROL AND FEEDBACK

  // Calculate the new menu item based on joystick movement
  currentMode += (move > 0) ? -1 : (move < 0) ? 1 : 0;
  
  // Limit currentMode to the range 1 to 2
  if (currentMode < 1) currentMode = 2;
  if (currentMode > 2) currentMode = 1;

  if (currentMode != previousMode) {
    // Unhighlight the previous menu item
    unhighlightMenuItem(previousMode);

    // Highlight the new menu item
    highlightMenuItem(currentMode);

    previousMode = currentMode;
  }

  // Handle button press for the current menu item
  if (buttonPressed) {
    switch (currentMode) {
      case 1:
        play();
        break;
      case 2:
        highscore(); 
        break;
    }
    buttonPressed = false;  // Reset the button press flag
  }

  // Delay to prevent rapid menu navigation
  delay(200);
}

void highlightMenuItem(int mode) {
  //FUNCTION TO SHOW USER SELECTED MENU ITEM

  switch (mode) {
    case 1:
      screen.fillRect(30, 90, 180, 20, ILI9341_ORANGE);  // Highlight START
      screenDisplay("START", 90);
      playSound("clickSound"); 
      break;
    case 2:
      screen.fillRect(30, 130, 180, 20, ILI9341_ORANGE); // Highlight HIGHSCORES
      screenDisplay("HIGH SCORES", 130);
      playSound("clickSound"); 
      break;
  }
}

void unhighlightMenuItem(int mode) {
  //FUNCTION TO ALLOW FOR USER INTUITIVENESS
  switch (mode) {
    case 1:
      screen.fillRect(30, 90, 180, 20, ILI9341_BLACK);  // Clear START
      screenDisplay("START", 90);
      break;
    case 2:
      screen.fillRect(30, 130, 180, 20, ILI9341_BLACK); // Clear HIGHSCORES
      screenDisplay("HIGH SCORES", 130);
      break;      
  }
}

int readAxis(int thisAxis) {
  // FUNCTION TO READ AND SCALE JOYSTICK INPUT

  int reading = analogRead(thisAxis);
  reading = map(reading, 0, 1023, 0, range);
  int distance = reading - center;
  if (abs(distance) < threshold) {
    distance = 0;
  }
  return distance;
}


bool isBarrierOnSnake(int barrierX, int barrierY, SnakeSegment snake[], int snakeLength) {
  for (int i = 0; i < snakeLength; i++) {
    if (snake[i].x == barrierX && snake[i].y == barrierY) {
      return true;  // Barrier overlaps with the snake's body
    }
  }
  return false;  // Barrier does not overlap with the snake's body
}

void generateBarrier(int &barrierX, int &barrierY, SnakeSegment snake[], int snakeLength) {
  do {
    barrierX = round(random(10, SCREEN_WIDTH - 10) * 0.1) * 10;
    barrierY = round(random(30, screen.height() - 30) * 0.1) * 10;
  } while (isBarrierOnSnake(barrierX, barrierY, snake, snakeLength));
}

void play() {
  //MAIN GAMEPLAY HAPPENS HERE

  // Setting up the screen
  screen.fillScreen(ILI9341_BLACK);  // Clear the screen for the game
  screen.drawRect(0,30,240,260,ILI9341_YELLOW);
  
  // Initialize the snake with one segment
  SnakeSegment snake[MAX_SNAKE_LENGTH];
  int snakeLength = 1;  // Start with one segment
  snake[0].x = round(random(X_BOUNDARY, SCREEN_WIDTH - X_BOUNDARY)*0.1)*10;  // Adjusted to avoid edges
  snake[0].y = round(random(Y_BOUNDARY, screen.height() - Y_BOUNDARY)*0.1)*10;  // Adjusted to avoid edges
  
  bool gameRunning = true;        // variable to show if game is running/over
  bool paused = true;             // variable to show if game has been paused
  unsigned long lastMoveTime = 0; 
  unsigned long moveDelay = 200;  // Control snake speed
  unsigned short points = 0;
  unsigned short level = 1;
  char lastMove = 'r';  // Initial direction (right)

  // Initial food position
  int foodX = round(random(X_BOUNDARY, SCREEN_WIDTH - X_BOUNDARY)*0.1)*10;  // x coordinate of the food
  int foodY = round(random(Y_BOUNDARY, screen.height() - Y_BOUNDARY)*0.1)*10; // y coordinate of the food

  int barrierX; // variable to store the x coordinate of the barrier
  int barrierY; // variable to store the y coordinate of the barrier

  // multiple bad foods can be present - using an array for bad foods
  unsigned int badfoodX[MAX_BAD_FOOD];  // variable to store the x coordinate of the bad food(s)
  unsigned int badfoodY[MAX_BAD_FOOD];  // variable to store the y coordinate of the bad food(s)
  int currentBadFoodCount = 0;
  
  bool foodEaten = false; // variable to identify if the snake has consumed the food on the screen

  while (gameRunning) {
    unsigned long currentTime = millis();

    // Handle pause/resume toggle if button is pressed
    if (buttonPressed) {
      playSound("GamePauseReleaseMusic");
      paused = !paused;
      buttonPressed = false;
      delay(200);  // Debounce delay
    }

    // If paused, display pause message and skip game updates
    if (paused) {
      screen.setCursor(50, 140);
      screen.setTextColor(ILI9341_YELLOW);
      screen.setTextSize(2);
      screen.print("Game Paused!");
      
      playSound("GamePauseMusic");
      while (!buttonPressed);
      buttonPressed = false;
      delay(200);  // Debounce delay
      screen.fillRect(50, 140, 140, 20, ILI9341_BLACK);
    } 

    // Move the snake based on joystick input
    xReading = readAxis(xAxis);
    yReading = readAxis(yAxis);

    // Make the snake move without going in reverse direction 
    if (abs(xReading) > abs(yReading)) {
      if (xReading < 0 && lastMove != 'l') lastMove = 'r';
      else if (xReading > 0 && lastMove != 'r') lastMove = 'l';
    } else if (abs(yReading) > abs(xReading)) {
      if (yReading < 0 && lastMove != 'u') lastMove = 'd';
      else if (yReading > 0 && lastMove != 'd') lastMove = 'u';
    }

    // Increase speed with level progression
    if (level >= 5){
    moveDelay = 200 * pow(0.8, level - 4);
    }
    
    // Move the snake after delay
    if (currentTime - lastMoveTime >= moveDelay) {
      // Clear the last segment of the snake
      screen.fillRect(snake[snakeLength - 1].x, snake[snakeLength - 1].y, 10, 10, ILI9341_BLACK);

      // Move the body segments
      for (int i = snakeLength - 1; i > 0; i--) {
        snake[i] = snake[i - 1];  // Move each segment to the position of the previous one
      }

      // Move the head of the snake based on direction and continue through the walls
      switch (lastMove) {
        case 'r':
          snake[0].x += 10;
          if (snake[0].x > SCREEN_WIDTH - X_BOUNDARY) snake[0].x = X_BOUNDARY;
          break;
        case 'l':
          snake[0].x -= 10;
          if (snake[0].x < X_BOUNDARY) snake[0].x = SCREEN_WIDTH - X_BOUNDARY;
          break;
        case 'u':
          snake[0].y -= 10;
          if (snake[0].y < Y_BOUNDARY) snake[0].y = screen.height() - Y_BOUNDARY;
          break;
        case 'd':
          snake[0].y += 10;
          if (snake[0].y > screen.height() - Y_BOUNDARY) snake[0].y = Y_BOUNDARY;
          break;
      }

      screen.drawRect(0,30,240,260,ILI9341_YELLOW);
      
      // Draw the new head of the snake
      screen.fillRect(snake[0].x, snake[0].y, 10, 10, ILI9341_GREEN);

      lastMoveTime = currentTime;  // Update the last move time

      // Check if the snake eats the food
      if (snake[0].x == foodX && snake[0].y == foodY) {
        playSound("playFoodEatenSong");
        points++;
        level = points / 2 + 1;
        updateScore(points, level);
        if (snakeLength < MAX_SNAKE_LENGTH) {
          snakeLength++;  // Grow the snake
        }

        // Clear old food and generate new food
        screen.fillRect(foodX, foodY, 10, 10, ILI9341_GREEN);
        foodX = round(random(X_BOUNDARY, SCREEN_WIDTH - X_BOUNDARY)*0.1)*10;
        foodY = round(random(Y_BOUNDARY, screen.height() - Y_BOUNDARY)*0.1)*10;

        for (int i = 0; i < snakeLength; i++){
          if (foodX == snake[i].x && foodY == snake[i].y){
            foodX = round(random(X_BOUNDARY, SCREEN_WIDTH - X_BOUNDARY)*0.1)*10;
            foodY = round(random(Y_BOUNDARY, screen.height() - Y_BOUNDARY)*0.1)*10;
          } 
        }
        
        screen.setCursor(barrierX, barrierY);
        screen.setTextColor(ILI9341_BLACK);
        screen.setTextSize(2);
        screen.print("7");
        
        for (int i = 0; i < currentBadFoodCount; i++) {
            screen.fillCircle(static_cast<int16_t>(badfoodX[i] + 5), static_cast<int16_t>(badfoodY[i] + 5), 4, ILI9341_BLACK);
        }

        foodEaten = true;
        foodSpawnTime = millis();  // Reset the spawn time

        if (level >= 2){
          // Introducing the barrier at level 2
          
          generateBarrier(barrierX, barrierY, snake, snakeLength);
          if ((abs(foodX - barrierX) <= 30) && (abs(foodY - barrierY) <= 30)){
            // Ensuring there is a comfortable space between the food and the barrier
            barrierX += 40;
            barrierY += 40;
          }

          if (((snake[0].x >= 100) && (snake[0].x <= 140)) && ((snake[0].y >= 140) && (snake[0].y <= 180))){
            // placing the barrier at a random corner if the snake eats food near the center of the screen
            unsigned int corner = random(1,4);
            switch (corner){
              case 1:
                barrierX = round(random(10, 40)*0.1)*10;
                barrierY = round(random(35, 65)*0.1)*10;
                break;
              case 2:
                barrierX = round(random(200, 230)*0.1)*10;
                barrierY = round(random(35, 65)*0.1)*10;
                break;
              case 3:
                barrierX = round(random(10, 40)*0.1)*10;
                barrierY = round(random(230, 260)*0.1)*10;
                break;
              case 4:
                barrierX = round(random(200, 230)*0.1)*10;
                barrierY = round(random(230, 260)*0.1)*10;
                break;
            }
          }
        }

        if (level >= 4) {
          // Introducing bad food at level 4
          currentBadFoodCount = level - 3;
          if (currentBadFoodCount < MAX_BAD_FOOD) {
            for (int i = 0; i < currentBadFoodCount; i++){
              // Placing bad food once the food has been eaten
              screen.fillCircle(badfoodX[i] + 5, badfoodY[i] + 5, 4, ILI9341_BLACK);
              badfoodX[i] = round(random(X_BOUNDARY, SCREEN_WIDTH - X_BOUNDARY) * 0.1) * 10;
              badfoodY[i] = round(random(Y_BOUNDARY, screen.height() - Y_BOUNDARY) * 0.1) * 10;
              for (int j = 0; j < snakeLength; j++){
                if (badfoodX[i] == snake[j].x && badfoodY[i] == snake[j].y){
                  badfoodX[i] = round(random(X_BOUNDARY, SCREEN_WIDTH - X_BOUNDARY)*0.1)*10;
                  badfoodY[i] = round(random(Y_BOUNDARY, screen.height() - Y_BOUNDARY)*0.1)*10;
                } 
              }
              screen.fillCircle(badfoodX[i] + 5, badfoodY[i] + 5, 4, ILI9341_RED);
              playSound("redFoodDisplayRing");
            }  
          }
        }
      } else {
        foodEaten = false;
      }

      if (!foodEaten) {
        if (level < 2) {
          // Placing normal food
          screen.fillCircle(foodX + 5, foodY + 5, 4, ILI9341_ORANGE);
        }
        if (level >= 2){
          // Placing normal food and barriers
          screen.fillCircle(foodX + 5, foodY + 5, 4, ILI9341_ORANGE);
          screen.setCursor(barrierX, barrierY);
          screen.setTextColor(ILI9341_RED);
          screen.setTextSize(2);
          screen.print("7");
        } 
        if (level >= 3) {
          // Introducing a count down timer at level 3
          unsigned long currentTime =  millis();
          unsigned int remainingTime;
          if (foodVisible) {
            screen.fillCircle(foodX + 5, foodY + 5, 4, ILI9341_ORANGE);
            // Show countdown timer for disappearing food
            remainingTime = (foodDisappearTime - (currentTime - foodSpawnTime)) / 1000;
            if (currentTime - lastUpdateTime >= 1000) {
              if (remainingTime > 0) {
                displayCountdown(round(remainingTime));
              } else {
                displayCountdown(0);
            }
            lastUpdateTime = currentTime;  // Update the last update time
            }
            // Check if 5 seconds have passed and hide food if necessary
            if (currentTime - foodSpawnTime >= foodDisappearTime) {
                foodVisible = false;  // Food disappears
                screen.fillCircle(foodX + 5, foodY + 5, 4, ILI9341_BLACK);  // Hide food
                displayCountdown(0);
            }
          }

          if (!foodVisible) {
            // Placing new food
            foodX = round(random(X_BOUNDARY, SCREEN_WIDTH - X_BOUNDARY) * 0.1) * 10;
            foodY = round(random(Y_BOUNDARY, screen.height() - Y_BOUNDARY) * 0.1) * 10;
            for (int i = 0; i < snakeLength; i++){
              if (foodX == snake[i].x && foodY == snake[i].y){
                foodX = round(random(X_BOUNDARY, SCREEN_WIDTH - X_BOUNDARY)*0.1)*10;
                foodY = round(random(Y_BOUNDARY, screen.height() - Y_BOUNDARY)*0.1)*10;
              } 
            }
            foodVisible = true;  // Make the new food visible
            foodSpawnTime = millis();  // Reset the spawn time
          }
        }
      }
      for (int i = 0; i < currentBadFoodCount; i++) {
        // Checking if the snake has eaten bad food
        if (snake[0].x == badfoodX[i] && snake[0].y == badfoodY[i]) {
            points--;
            playSound("redFoodEatingRing");
            level = points / 2 + 1;
            updateScore(points, level);
            screen.fillRect(snake[snakeLength - 1].x, snake[snakeLength - 1].y, 10, 10, ILI9341_BLACK);
            snakeLength--; // Reduce snake length
        }
      }
    }

    // Check if the snake's head collides with its body
    for (int i = 3; i < snakeLength; i++) {  // Start from 2 to skip the head and quick turns
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
            gameOver(points);  // Call the gameOver function if a collision is detected
            gameRunning = false;
        }
    }

    // Check if the snake has collided with the barrier
    if (snake[0].x == barrierX && snake[0].y == barrierY){
      gameOver(points);
      gameRunning = false;
    }

    // Small delay to prevent excessive CPU usage
    delay(50);
  }
}

void joystickISR() {
  // FUNCTION TO HANDLE JOYSTICK MOVEMENTS AND BUTTON PRESS
  if (digitalRead(mouseButton) == LOW) {
    buttonPressed = true;
    playSound("clickSound"); 
  }
}

void updateScore(int points,int level){
  // FUNCTION TO UPDATE THE SCORE AND THE LEVEL ON THE SCREEN
  screen.setCursor(80,300);
  screen.fillRect(80,300,150,20,ILI9341_BLACK);
  screen.setTextColor(ILI9341_WHITE);
  screen.setTextSize(2);
  screen.print("Score: ");
  screen.print(points);
  screen.setCursor(20,10);
  screen.fillRect(20,10,150,20,ILI9341_BLACK);
  screen.print("Level ");
  screen.print(level);
}

void gameOver(int points){
  // FUNCTION TO RUN THE GAME OVER SEQUENCE
  screen.fillRect(10,10,220,300,ILI9341_WHITE);
  screen.setCursor(50, 140);
  screen.setTextColor(ILI9341_RED);
  screen.setTextSize(3);
  screen.print("GAME OVER!");
  screen.setCursor(50,180);
  screen.setTextColor(ILI9341_BLACK);
  screen.print("SCORE: ");
  screen.print(points);

  int highScore = readHighScore();
  // Update the high score if applicable
  if ((points > highScore)) {
    screen.setCursor(20, 80);
    screen.setTextSize(2);
    screen.print("NEW HIGHSCORE!");
    writeHighScore(points);
    highScore = points;  // Update local high score variable
  }
  delay(1000);
  playSound("playGameOverSong");
  highscore();
  
  
}

void displayCountdown(unsigned int remainingTime) {
  // DISPLAYS THE COUNTDOWN TIMER ON THE SCREEN STARTING FROM LEVEL 3
    screen.fillRect(120, 10, 80, 20, ILI9341_BLACK);  // Clear previous countdown
    screen.setCursor(120, 10);
    screen.setTextColor(ILI9341_WHITE);
    screen.setTextSize(2);
    if (remainingTime > 0) {
        screen.print("Food: ");
        screen.print(remainingTime);
        screen.print("s");
    }else {
      screen.print("Missed!");
    }
}

int readHighScore() {
  // READ THE HIGHSCORE FROM THE EEPROM
    int highScore ;
    EEPROM.get(EEPROM_HIGH_SCORE_ADDRESS, highScore);
    return highScore;
}

void writeHighScore(int highScore) {
  // WRITE THE HIGHSCORE TO THE EEPROM
    EEPROM.put(EEPROM_HIGH_SCORE_ADDRESS, highScore);
}

void highscore(){
  // HIGHSCORE MODE FROM THE MENU 
  screen.fillScreen(ILI9341_BLACK);
  int highScore = readHighScore();
  screen.fillScreen(ILI9341_BLACK);
  screen.setCursor(50, 50);
  screen.setTextColor(ILI9341_WHITE);
  screen.setTextSize(2);
  screen.print("High Score: ");
  if (highScore < 0) {
    highScore = 0;
  }
  screen.print(highScore); 
  delay(1500);  // Display high score for 3 seconds
  screen.fillScreen(ILI9341_BLACK);
  menu();
}


void displayBackButton() {
  // BUTTON TO PRESS WHEN IN THE HIGHSCORE MENU
  screen.fillRect(10, 300, 80, 20, ILI9341_BLUE);  // Draw the button
  screen.setCursor(20, 305);
  screen.setTextColor(ILI9341_WHITE);
  screen.setTextSize(2);
  screen.print("BACK");
  while (!buttonPressed);  // Wait for button press
  delay(100);
  buttonPressed = false;  // Reset the button press flag
  screen.fillScreen(ILI9341_BLACK);
  menu();  // Navigate back to the menu
}

void playSound(char sound[]) {
  int hit;
  if (strcmp(sound, "playFoodEatenSong") == 0) hit = 1;
  else if (strcmp(sound, "redFoodDisplayRing") == 0) hit = 2;
  else if (strcmp(sound, "gameStartingMelody") == 0) hit = 3;
  else if (strcmp(sound, "playGameOverSong") == 0) hit = 4;
  else if (strcmp(sound, "redFoodEatingRing") == 0) hit = 5;
  else if (strcmp(sound, "gamePauseMusic") == 0) hit = 6;
  else if (strcmp(sound, "GamePauseReleaseMusic") == 0) hit = 7;
  else if (strcmp(sound, "clickSound") == 0) hit = 8;

  switch (hit) {
    case 1:
      tone(BUZZER, 523, 200);
      delay(50);
      tone(BUZZER, 784, 200);
      delay(50);
      break;
    case 2:
      tone(BUZZER, 10, 100);
      break;
    case 3: {
      int melody[] = {900, 800, 700, 600, 500, 400, 300, 200, 100};
      for (int i = 0; i < 9; i++) {
        tone(BUZZER, melody[i]);
        delay(50);
        noTone(BUZZER);
        delay(50);
      }
      break;
    }
    case 4:
      tone(BUZZER, 523, 200);
      delay(50);
      tone(BUZZER, 392, 200);
      delay(50);
      tone(BUZZER, 330, 200);
      delay(50);
      tone(BUZZER, 293, 200);
      delay(50);
      tone(BUZZER, 261.6, 400);
      delay(50);
      break;
    case 5:
      tone(BUZZER, 6000, 400);
      delay(50);
      break;
    case 6:
      tone(BUZZER, 9000, 10);
      break;
    case 7:
      tone(BUZZER, 10000, 10);
      break;
    case 8:
      tone(BUZZER, 1000, 10);
      break;
    default:
      // Optionally handle if no valid sound string was passed
      break;
  }
}