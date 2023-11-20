#include <iostream>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <atomic>
#include <functional>

// SDL Headers
#include <SDL.h>
#include <SDL2_gfxPrimitives.h>
#include <SDL_mixer.h>

using namespace std;

// For window dimensions
const int WIDTH = 800;
const int HEIGHT = 600;

// Global atomic variables for race states and winner ID
atomic<bool> raceFinished(false);
atomic<bool> raceStarted(false);
atomic<int> winnerId(-1);

// Class creation for race cars
class Racer
{
    float percentageCompleted = 0.0f;
    function<float(float)> easingFunction; // Easing function
    int id;                                // Racer ID

    // Variables for winner spinning effect
    float rotationAngle = 0.0f;
    bool isSpinning = false;

public:
    SDL_Texture *sprite;
    SDL_Rect rect;

    // Constructor
    Racer(SDL_Renderer *renderer, const string &imagePath, int x, int y, function<float(float)> easingFunc, int racerId)
        : easingFunction(easingFunc), id(racerId)
    {
        // Loading car image and creating texture
        SDL_Surface *surface = SDL_LoadBMP(imagePath.c_str());
        if (!surface)
        {
            cerr << "Car load fail" << SDL_GetError() << endl;
            sprite = nullptr;
            return;
        }
        sprite = SDL_CreateTextureFromSurface(renderer, surface);

        // Initial positions and size
        rect.x = x;
        rect.y = y;
        rect.w = surface->w;
        rect.h = surface->h;

        SDL_FreeSurface(surface);
    }

    // Getter for percentage
    float getPercentageCompleted() const
    {
        return percentageCompleted;
    }

    // Racers position update
    void update(float delta)
    {
        // Race completed check
        if (raceFinished.load() || percentageCompleted >= 1.0f)
        {
            return;
        }

        // Update the completion percentage and calculate the eased percentage
        percentageCompleted += delta;
        percentageCompleted = min(percentageCompleted, 1.0f);
        float easedPercentage = easingFunction(percentageCompleted);
        rect.x = static_cast<int>(easedPercentage * (WIDTH - rect.w));

        // Syncing the eased percentage at end with percentage complete to make visuals accurate aka fixing my broken code
        if (easedPercentage >= 1.0f)
        {
            percentageCompleted = 1.0f;
        }
    }

    // Winning spin effect functions
    void startSpinning()
    {
        isSpinning = true;
    }

    void updateRotation(float rotateIncrement)
    {
        if (isSpinning)
        {
            rotationAngle += rotateIncrement;
            if (rotationAngle >= 360.0f)
            {
                rotationAngle -= 360.0f;
            }
        }
    }

    // Descturor for racer
    ~Racer()
    {
        if (sprite)
        {
            SDL_DestroyTexture(sprite);
        }
    }

    // For drawing racer
    void draw(SDL_Renderer *renderer)
    {
        if (isSpinning)
        {
            SDL_Point center = {rect.w / 2, rect.h / 2};
            SDL_RenderCopyEx(renderer, sprite, NULL, &rect, rotationAngle, &center, SDL_FLIP_NONE);
        }
        else
        {
            SDL_RenderCopy(renderer, sprite, NULL, &rect);
        }
    }
};

// Class for confetti particle
class ConfettiParticle
{
public:
    SDL_Rect rect;
    SDL_Color color;
    // Constructor to initialize particle with position size and random RGB
    ConfettiParticle(int x, int y, int size)
    {
        rect.x = x;
        rect.y = y;
        rect.w = size;
        rect.h = size;

        color.r = rand() % 256;
        color.g = rand() % 256;
        color.b = rand() % 256;
        color.a = 255;
    }

    // Update position of particle with a downward movement
    void update()
    {
        rect.y += 1;
        // Reset to top at a random X pos when they go off the screen
        if (rect.y > HEIGHT)
        {
            rect.y = 0;
            rect.x = rand() % WIDTH;
        }
    }

    // Render particle as rectangle
    void draw(SDL_Renderer *renderer)
    {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer, &rect);
    }
};

vector<ConfettiParticle> confetti; // Collection of confettis!
bool displayConfetti = false;

// Generating confetti
void generateConfetti()
{
    confetti.clear(); // Clear existing confetti particles if any

    // Generating particles at random positions
    for (int i = 0; i < 100; ++i)
    {
        confetti.emplace_back(rand() % WIDTH, rand() % HEIGHT / 2, 5);
    }
    displayConfetti = true;
}

// Function to handle racer behavior
void raceFunction(Racer *racer, int racerId)
{
    while (!raceFinished.load())
    {
        // Check if the race has started, pause the threads for a moment
        if (!raceStarted.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Contiously updating the progress by a linear small amount
        racer->update(0.001f);

        // Check if the racer has completed the race
        if (racer->getPercentageCompleted() >= 1)
        {
            /*
                Double check if finished
                Store Winner ID, mark race finished, create confetti, end thread
            */
            if (!raceFinished.load())
            {
                winnerId.store(racerId);
                raceFinished.store(true);
                generateConfetti();
                break;
            }
        }
        // another brief pause before update
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

/*
    Collection of ease functions for smooth visual updates to racer across track
*/

float constrain(float x, float low, float high)
{
    return std::min(high, std::max(low, x));
}

/*
    Ease functions using sine
    Ease in sine has a smoother start
    Ease out sine has a smoother end
*/
float ease_in_sine(float percent)
{
    percent = constrain(percent, 0.0, 1.0);
    return 1 - cos((percent * M_PI) / 2);
}

float ease_out_sine(float percent)
{
    percent = constrain(percent, 0.0, 1.0);
    return sin((percent * M_PI) / 2);
}

/*
    Ease function with a reverse start followed by acceleration
    Planned to implement another backwards movement close to end but..... no luck
*/

float ease_in_out_back(float percent)
{
    percent = constrain(percent, 0.0, 1.0);

    // For overshoots
    const float c1 = 1.70158;
    const float c2 = c1 * 1.525;
    float result;

    if (percent < 0.5)
    {
        // Backwards easing
        result = 0.5 * (pow(2 * percent, 2) * ((c2 + 1) * 2 * percent - c2));
    }
    else
    {
        float p = 2 * percent - 2;
        result = 0.5 * (pow(p, 2) * ((c2 + 1) * p + c2) + 2);
    }

    return min(result, 1.0f);
}

// Basic exponential ease in and out
float ease_in_out_exponential(float percent)
{
    percent = constrain(percent, 0.0, 1.0);

    if (percent == 0.0 || percent == 1.0)
    {
        return percent;
    }

    if (percent < 0.5)
    {
        return 0.5 * pow(2, 20 * percent - 10);
    }
    else
    {
        return 0.5 * (-pow(2, -20 * percent + 10) + 2);
    }
}

int main(int argc, char *argv[])
{
    srand(time(nullptr));

    /*
        Initializing SDL, creating window, renderer, BG images
    */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) < 0)
    {
        cerr << "SDL_Init() failed... " << SDL_GetError() << endl;
        return EXIT_FAILURE;
    }

    SDL_Window *window = SDL_CreateWindow("Prog 5: Gracana2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if (!window)
    {
        cerr << "SDL_CreateWindow() failed... " << SDL_GetError() << endl;
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        cerr << "SDL_CreateRenderer() failed... " << SDL_GetError() << endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Surface *backgroundSurface = SDL_LoadBMP("background.bmp");
    if (!backgroundSurface)
    {
        cerr << "SDL_LoadBMP() failed... " << SDL_GetError() << endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Texture *backgroundTextureWinner = SDL_CreateTextureFromSurface(renderer, SDL_LoadBMP("winnerbackground.bmp"));
    SDL_Texture *backgroundTexture = SDL_CreateTextureFromSurface(renderer, backgroundSurface);
    SDL_FreeSurface(backgroundSurface);

    // Loading audio
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    Mix_Music *raceMusic = Mix_LoadMUS("music.wav");
    Mix_Music *startSound = Mix_LoadMUS("startsound.wav");
    Mix_Chunk *winnerSound = Mix_LoadWAV("endsound.wav");
    Mix_VolumeChunk(winnerSound, MIX_MAX_VOLUME / 2);

    /*
        Created a vector to store the different easing functions
        Used shuffle to rearrange them for easy random assignment
    */
    vector<function<float(float)>> easingFunctions = {
        ease_in_sine,
        ease_out_sine,
        ease_in_out_back,
        ease_in_out_exponential

    };

    random_shuffle(easingFunctions.begin(), easingFunctions.end());

    // Create racers with the randomly assigned easing functions
    Racer yellowCar(renderer, "yellowcar.bmp", 5, 125, easingFunctions[0], 0);
    Racer whiteCar(renderer, "whitecar.bmp", 5, 210, easingFunctions[1], 1);
    Racer redCar(renderer, "redcar.bmp", 5, 305, easingFunctions[2], 2);
    Racer blueCar(renderer, "bluecar.bmp", 5, 395, easingFunctions[3], 3);

    // Creating the seperate threads for the racers
    thread yellowCarThread(raceFunction, &yellowCar, 0);
    thread whiteCarThread(raceFunction, &whiteCar, 1);
    thread redCarThread(raceFunction, &redCar, 2);
    thread blueCarThread(raceFunction, &blueCar, 3);

    bool quit = false;
    SDL_Event event;
    while (!quit)
    {

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                quit = true;
            }
            else if (event.type == SDL_KEYDOWN)
            {
                if (event.key.keysym.sym == SDLK_SPACE && !raceStarted.load())
                {
                    // Play starting sound, wait till finished, play racing music
                    Mix_PlayMusic(startSound, 1);
                    while (Mix_PlayingMusic())
                    {
                        SDL_Delay(100);
                    }

                    raceStarted.store(true);
                    Mix_PlayMusic(raceMusic, -1);
                }
            }
            else if (event.key.keysym.sym == SDLK_ESCAPE)
            {
                quit = true;
            }
        }

        // Clear screen and render
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, backgroundTexture, NULL, NULL);

        // Pre-race text
        if (!raceStarted.load())
        {
            string displayText = "Press Space to Start";
            int textX = 350;
            int textY = HEIGHT - 50;

            stringColor(renderer, textX, textY, displayText.c_str(), 0xFF000000);
        }

        if (!raceFinished.load())
        {
            // Draw all cars if the race is not finished
            yellowCar.draw(renderer);
            whiteCar.draw(renderer);
            redCar.draw(renderer);
            blueCar.draw(renderer);
        }
        else if (raceFinished.load())
        {
            // Find the winner
            Racer *winner;
            switch (winnerId.load())
            {
            case 0:
                winner = &yellowCar;
                break;
            case 1:
                winner = &whiteCar;
                break;
            case 2:
                winner = &redCar;
                break;
            case 3:
                winner = &blueCar;
                break;
            }

            SDL_RenderCopy(renderer, backgroundTextureWinner, NULL, NULL); // Winner BG

            // Update and draw each confetti particle
            if (displayConfetti)
            {
                for (auto &particle : confetti)
                {
                    particle.update();
                    particle.draw(renderer);
                }
            };

            // Stop race music and play the winner sound
            Mix_HaltMusic();
            Mix_PlayChannel(-1, winnerSound, 0);

            // Center the winner
            winner->rect.x = (WIDTH - winner->rect.w) / 2;
            winner->rect.y = (HEIGHT - winner->rect.h) / 2;
            winner->startSpinning();      // SPIN!
            winner->updateRotation(0.5f); // Rotation speed

            // ESC to exit text
            string exitText = "Press ESC to Exit";
            int exitTextX = (WIDTH / 2);                          // Center horizontal (not really)
            int exitTextY = winner->rect.y + winner->rect.h + 50; // 50 px under car

            // Render
            stringColor(renderer, exitTextX, exitTextY, exitText.c_str(), 0xFFFFFFFF);
            winner->draw(renderer);
        }

        SDL_RenderPresent(renderer);
    }

    // Cleanup everything
    yellowCarThread.join();
    whiteCarThread.join();
    redCarThread.join();
    blueCarThread.join();
    Mix_FreeMusic(raceMusic);
    Mix_FreeMusic(startSound);
    Mix_FreeChunk(winnerSound);
    Mix_CloseAudio();
    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(backgroundTextureWinner);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}