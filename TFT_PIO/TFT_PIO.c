
#include <stdio.h> //The standard C library
#include <stdlib.h> //C stdlib
#include "pico/stdlib.h" //Standard library for Pico
#include <math.h> //The standard math library
#include "hardware/gpio.h" //The hardware GPIO library
#include "pico/time.h" //The pico time library
#include "hardware/irq.h" //The hardware interrupt library
#include "hardware/pwm.h" //The hardware PWM library
#include "hardware/pio.h" //The hardware PIO library
#include "TFTMaster.h" //The TFT Master library

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "TFTMaster.h"

// Define cell size (each snake segment and food cell will be CELL_SIZE x CELL_SIZE pixels)
#define CELL_SIZE 4
#define GRID_WIDTH  (ILI9340_TFTWIDTH / CELL_SIZE)
#define GRID_HEIGHT (ILI9340_TFTHEIGHT / CELL_SIZE)
#define MAX_SNAKE_LENGTH (GRID_WIDTH * GRID_HEIGHT)

// Define GPIO pins for the direction buttons (adjust as needed for your wiring)
#define BUTTON_UP     7
#define BUTTON_LEFT   8
#define BUTTON_RIGHT  6
#define BUTTON_DOWN   9

// Point structure to represent positions on the grid
typedef struct {
    int x;
    int y;
} Point;

// Enum for the four movement directions
typedef enum {
    UP, DOWN, LEFT, RIGHT
} Direction;

// Global game variables
Point snake[MAX_SNAKE_LENGTH];  // Array holding positions of the snake segments
int snake_length;               // Current length of the snake
Direction current_direction;    // The snake's current moving direction
Point food;                     // Position of the food

// Function prototypes
void init_game();
void spawn_food();
void draw_food();
void process_input();

int main() {
    // Initialize standard I/O and the TFT display.
    stdio_init_all();
    tft_init_hw();
    tft_begin();
    tft_setRotation(0);
    tft_fillScreen(ILI9340_BLACK);
    
    // Initialize the button pins as inputs with internal pull-ups.
    gpio_init(BUTTON_UP);
    gpio_set_dir(BUTTON_UP, GPIO_IN);
    gpio_pull_up(BUTTON_UP);
    
    gpio_init(BUTTON_DOWN);
    gpio_set_dir(BUTTON_DOWN, GPIO_IN);
    gpio_pull_up(BUTTON_DOWN);
    
    gpio_init(BUTTON_LEFT);
    gpio_set_dir(BUTTON_LEFT, GPIO_IN);
    gpio_pull_up(BUTTON_LEFT);
    
    gpio_init(BUTTON_RIGHT);
    gpio_set_dir(BUTTON_RIGHT, GPIO_IN);
    gpio_pull_up(BUTTON_RIGHT);
    
    // Seed the random number generator (using the Pico's uptime for a seed).
    srand(to_ms_since_boot(get_absolute_time()));
    
    // Start the game (initialize the snake, food, and game board)
    init_game();
    
    while (1) {
        // Check user input to change the snake's direction.
        process_input();
        
        // Compute the new head position based on the current direction.
        Point new_head = snake[0];
        switch (current_direction) {
            case UP:    new_head.y -= 1; break;
            case DOWN:  new_head.y += 1; break;
            case LEFT:  new_head.x -= 1; break;
            case RIGHT: new_head.x += 1; break;
        }
        
        // Check for collision with the boundaries.
        if (new_head.x < 0 || new_head.x >= GRID_WIDTH ||
            new_head.y < 0 || new_head.y >= GRID_HEIGHT) {
            // Restart game on wall collision.
            init_game();
            sleep_ms(500);
            continue;
        }
        
        // Check for collision with itself.
        for (int i = 0; i < snake_length; i++) {
            if (snake[i].x == new_head.x && snake[i].y == new_head.y) {
                init_game();
                sleep_ms(500);
                continue;
            }
        }
        
        // Determine if the snake has eaten food.
        bool ate_food = (new_head.x == food.x && new_head.y == food.y);
        
        // If the snake did not eat food, erase its tail from the display.
        if (!ate_food) {
            Point tail = snake[snake_length - 1];
            tft_fillRect(tail.x * CELL_SIZE, tail.y * CELL_SIZE, CELL_SIZE, CELL_SIZE, ILI9340_BLACK);
        } else {
            // Grow the snake if there's room.
            if (snake_length < MAX_SNAKE_LENGTH) {
                snake_length++;
            }
        }
        
        // Update the snake's body by shifting segments.
        for (int i = snake_length - 1; i > 0; i--) {
            snake[i] = snake[i - 1];
        }
        snake[0] = new_head;
        
        // Draw the new head segment.
        tft_fillRect(new_head.x * CELL_SIZE, new_head.y * CELL_SIZE, CELL_SIZE, CELL_SIZE, ILI9340_GREEN);
        
        // If food was eaten, spawn new food and draw it on screen.
        if (ate_food) {
            spawn_food();
            draw_food();
        }
        
        // Delay to control game speed (adjust as needed).
        sleep_ms(200);
    }
    
    return 0;
}

// Initialize or restart the game.
void init_game() {
    // Clear the screen.
    tft_fillScreen(ILI9340_BLACK);
    
    // Set initial snake length and starting position (center of the grid).
    snake_length = 3;
    int start_x = GRID_WIDTH / 2;
    int start_y = GRID_HEIGHT / 2;
    for (int i = 0; i < snake_length; i++) {
        snake[i].x = start_x - i;  // Snake initially oriented horizontally.
        snake[i].y = start_y;
        tft_fillRect(snake[i].x * CELL_SIZE, snake[i].y * CELL_SIZE, CELL_SIZE, CELL_SIZE, ILI9340_GREEN);
    }
    
    // Start moving to the right.
    current_direction = RIGHT;
    
    // Generate and draw the first food item.
    spawn_food();
    draw_food();
}

// Spawn food at a random grid location not occupied by the snake.
void spawn_food() {
    bool valid = false;
    while (!valid) {
        food.x = rand() % GRID_WIDTH;
        food.y = rand() % GRID_HEIGHT;
        valid = true;
        // Check the new food location against all snake segments.
        for (int i = 0; i < snake_length; i++) {
            if (snake[i].x == food.x && snake[i].y == food.y) {
                valid = false;
                break;
            }
        }
    }
}

// Draw the food on the TFT display.
void draw_food() {
    tft_fillRect(food.x * CELL_SIZE, food.y * CELL_SIZE, CELL_SIZE, CELL_SIZE, ILI9340_RED);
}

// Read GPIO button inputs and update the snake's movement direction.
// This code assumes active low buttons (pressed = 0) and prevents reversal.
void process_input() {
    if (!gpio_get(BUTTON_UP) && current_direction != DOWN) {
        current_direction = UP;
    } else if (!gpio_get(BUTTON_DOWN) && current_direction != UP) {
        current_direction = DOWN;
    } else if (!gpio_get(BUTTON_LEFT) && current_direction != RIGHT) {
        current_direction = LEFT;
    } else if (!gpio_get(BUTTON_RIGHT) && current_direction != LEFT) {
        current_direction = RIGHT;
    }
}
