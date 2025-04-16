#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include <math.h>
#include "hardware/gpio.h"
#include "pico/time.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "TFTMaster.h"
#include <string.h>
#include <pico/multicore.h>
#include "pt_cornell_rp2040_v1_3.h"


// Snake Arena 
#define CELL_SIZE 6
#define GRID_WIDTH  (ILI9340_TFTWIDTH / CELL_SIZE)
#define GRID_HEIGHT (ILI9340_TFTHEIGHT / CELL_SIZE)
#define MAX_SNAKE_LENGTH (GRID_WIDTH * GRID_HEIGHT)

// Define GPIO pins for the direction buttons (adjust as needed for your wiring)
#define BUTTON_UP     7
#define BUTTON_LEFT   8
#define BUTTON_RIGHT  6
#define BUTTON_DOWN   9

// Sound Design 
#define SPEAKER_PIN   19    

// Point structure to represent positions on the grid
typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    uint32_t freq;      // in Hz; use 0 for a rest
    uint32_t dur_ms;    // duration in milliseconds
} note_t;

// Enum for the four movement directions.
typedef enum {
    UP, DOWN, LEFT, RIGHT
} Direction;

// Enum for the overall game states.
typedef enum {
    STATE_START,   // Welcome screen
    STATE_PLAYING, // Game in progress
    STATE_END      // Game over screen
} GameState;

static GameState game_state;    // Game state variable
static int score = 0;           // Private variable to track the score

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
void run_game();
void start_screen();
void end_screen();
bool any_button_pressed();

// Helper: Return true if any button is pressed.
bool any_button_pressed() {
    return (!gpio_get(BUTTON_UP) || !gpio_get(BUTTON_DOWN) ||
            !gpio_get(BUTTON_LEFT) || !gpio_get(BUTTON_RIGHT));
}

// Display the start screen.
void start_screen() {
    tft_fillScreen(ILI9340_BLACK);
    tft_setTextColor(ILI9340_WHITE);
    tft_setTextSize(4);
    // Centered title. Adjust cursor positions as needed.
    tft_setCursor(ILI9340_TFTWIDTH/ 2 - 75, ILI9340_TFTHEIGHT / 2 - 80);
    tft_writeString("Snake Game");
}

// Display the game over screen along with the final score.
void end_screen() {
    char score_text[32];
    tft_fillScreen(ILI9340_BLACK);
    tft_setTextColor(ILI9340_WHITE);
    tft_setTextSize(4);
    tft_setCursor(ILI9340_TFTWIDTH/ 2 - 65, ILI9340_TFTHEIGHT / 2 - 80);
    tft_writeString("Game Over");
    tft_setTextSize(3);
    // Format the final score as a string.
    sprintf(score_text, "Score: %d", score);
    tft_setCursor(ILI9340_TFTWIDTH/ 2 - 30, ILI9340_TFTHEIGHT / 2 - 40);
    tft_writeString(score_text);
}

// Initialize or restart the game.
void init_game() {
    // Reset the score.
    score = 0;
    
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
// Process button inputs to update the snake's movement direction.
// Prevents reversal of direction.
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

///////////////////////////////////////////////////////////////////////////////////////////////////
//Speaker
///////////////////////////////////////////////////////////////////////////////////////////////////
// play a square‑wave of given freq (Hz) for duration_ms
void play_tone(uint pin, uint32_t freq, uint32_t duration_ms) {
    // 1) route the pin to PWM hardware
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);

    // 2) choose a clock divider so that (sys_clk / divider) / (wrap+1) = freq
    //    sys_clk is 125 MHz by default on Pico
    //    Let's pick divider = 125 so 1 MHz pwm_clk
    pwm_set_clkdiv(slice, 125.0f);

    // 3) compute wrap so pwm_freq = 1 MHz / (wrap + 1) → wrap = 1 MHz/freq – 1
    uint32_t wrap = 1000000 / freq - 1;
    pwm_set_wrap(slice, wrap);

    // 4) 50% duty cycle → level = wrap/2
    pwm_set_chan_level(slice, pwm_gpio_to_channel(pin), wrap/2);

    // 5) enable PWM
    pwm_set_enabled(slice, true);

    // 6) hold for the requested duration
    sleep_ms(duration_ms);

    // 7) stop PWM and clean up
    pwm_set_enabled(slice, false);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_put(pin, 0);
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////

































static GameState last_state = -1;
// The screen‐manager thread on core 1
static PT_THREAD (screen_pt(struct pt *pt)){
    PT_BEGIN(pt);

    while (1) {
        // Wait for a state change
        PT_WAIT_UNTIL(pt, game_state != last_state);

        // Clear and draw static content for the new state
        tft_fillScreen(ILI9340_BLACK);
        tft_setTextColor(ILI9340_WHITE);
        tft_setRotation(1);
        if (game_state == STATE_START) {
            start_screen();
        }
        else if (game_state == STATE_END) {
            end_screen();
        }

        last_state = game_state;

        // Now blink the “press any button” line until state changes

        tft_setTextSize(2);
        while (game_state == last_state) {
            if (game_state == STATE_START){
                tft_fillRect(ILI9340_TFTWIDTH / 2 -  60, ILI9340_TFTHEIGHT / 2-30, 300, 50, ILI9340_BLACK);
                sleep_ms(1000);
                tft_setCursor(ILI9340_TFTWIDTH / 2 -  55, ILI9340_TFTHEIGHT / 2-20);
                tft_writeString("Press any button");
                tft_setCursor(ILI9340_TFTWIDTH / 2 - 10, ILI9340_TFTHEIGHT / 2);
                tft_writeString("to start");
                sleep_ms(1000);
            }
            else if (game_state == STATE_END){
                tft_fillRect(ILI9340_TFTWIDTH / 2 -  60, ILI9340_TFTHEIGHT / 2, 300, 50, ILI9340_BLACK);
                sleep_ms(1000);
                tft_setCursor(ILI9340_TFTWIDTH/ 2 - 55, ILI9340_TFTHEIGHT / 2 + 5);
                tft_writeString("Press any button");
                tft_setCursor(ILI9340_TFTWIDTH/ 2 - 15, ILI9340_TFTHEIGHT / 2 + 30);
                tft_writeString("to restart");
                sleep_ms(1000);
            }
            
        }
        
    }

    PT_END(pt);
}

// --------------------------------------------------------------------
// Protothread: game logic (input, snake movement, collision, drawing)
// --------------------------------------------------------------------
static PT_THREAD(game_thread(struct pt *pt)) {
    PT_BEGIN(pt);
    tft_setRotation(0);
    while (1) {
        // Wait for start state
        PT_WAIT_UNTIL(pt, game_state == STATE_START);
        // Wait for button press to begin
        PT_WAIT_UNTIL(pt, any_button_pressed());
        sleep_ms(100);    // debounce

        init_game();               // initialize snake + food
        game_state = STATE_PLAYING;

        // Game loop
        while (game_state == STATE_PLAYING) {
            process_input();
            // compute new head
            Point nh = snake[0];
            switch (current_direction) {
                case UP:    nh.y--; break;
                case DOWN:  nh.y++; break;
                case LEFT:  nh.x--; break;
                case RIGHT: nh.x++; break;
            }
            // boundary collision
            if (nh.x < 0 || nh.x >= GRID_WIDTH || nh.y < 0 || nh.y >= GRID_HEIGHT) {
                game_state = STATE_END;
                break;
            }
            // self-collision
            for (int i = 0; i < snake_length; i++) {
                if (snake[i].x == nh.x && snake[i].y == nh.y) {
                    game_state = STATE_END;
                    break;
                }
            }
            if (game_state != STATE_PLAYING) break;

            // eat?
            bool ate = (nh.x==food.x && nh.y==food.y);
            if (ate) {
                score++;
                if (snake_length < MAX_SNAKE_LENGTH) snake_length++;
            } else {
                // erase tail
                Point tail = snake[snake_length-1];
                tft_fillRect(tail.x*CELL_SIZE, tail.y*CELL_SIZE, CELL_SIZE, CELL_SIZE, ILI9340_BLACK);
            }
            // shift body
            for (int i = snake_length-1; i>0; i--) snake[i] = snake[i-1];
            snake[0] = nh;
            // draw head
            tft_fillRect(nh.x*CELL_SIZE, nh.y*CELL_SIZE, CELL_SIZE, CELL_SIZE, ILI9340_GREEN);
            // spawn + draw food
            if (ate) {
                spawn_food();
                draw_food();
            }
            sleep_ms(200);
        }
        // STATE_END: wait for button to restart
        PT_WAIT_UNTIL(pt, any_button_pressed());
        game_state = STATE_START;
    }
    PT_END(pt);
}

// Core 1 entry
void core1_entry() {
    pt_add_thread(screen_pt) ;
    pt_schedule_start ;
}



int main() {
    // Initialize standard I/O and the TFT display.
    stdio_init_all();
    tft_init_hw();
    tft_begin();
    tft_fillScreen(ILI9340_BLACK);
    
    // Initialize button pins as inputs with internal pull-ups.
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
    
    // Seed the random number generator.
    srand(to_ms_since_boot(get_absolute_time()));
    // Start with the start screen.
    game_state = STATE_START;
    multicore_reset_core1();
    multicore_launch_core1(core1_entry);
    pt_add_thread(game_thread);
    pt_schedule_start;
}
