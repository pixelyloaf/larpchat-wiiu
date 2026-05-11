#include <whb/proc.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>
#include <romfs-wiiu.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_syswm.h>

#include "font.h"
#include "chat.h"
#include "image.h"
#include "net.h"
#include "input.h"
#include "storage.h"

// Used in multiple files, so declared here
// -------------------------
std::string clientVersion = "6.0";

std::string username = "";
std::string password = "";

textSendType currentTextSendType = type_none;

std::string currentRoom = "general";

Scene scene = SELECTION_MENU;

int fontSize = 48;
int maxWidth = 1920 - 40;

int sock = ConnectToTCPServer();

bool connectionLost = false;

SDL_Window *tvWindow = NULL;
SDL_Window *drcWindow = NULL;
SDL_Renderer *tvRenderer = NULL;
SDL_Renderer *drcRenderer = NULL;

// TV colors
SDL_Color tvBackgroundColor = {0, 0, 0, 255};
SDL_Color tvTextColor = {255, 255, 255, 255};

// DRC colors
SDL_Color drcBackgroundColor = {0, 0, 0, 255};
SDL_Color drcTextColor = {255, 255, 255, 255};
// -------------------------

// -----------------------
// Main
// -----------------------
int main(int argc, char **argv)
{
    WHBProcInit();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO);
    romfsInit();
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);

    mkdir("fs:/vol/external01/wiiu/apps/aurorachatforWiiU", 0777);
    mkdir("fs:/vol/external01/wiiu/apps/aurorachatforWiiU/avatars", 0777);

    std::string serverResponse = "";
    std::string failedReason = "";

    // Keyboard Text Input Buffer
    std::string textBuffer = "";

    char input[512] = "";

    // Initialize audio to stop loading screen music from playing
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16;
    want.channels = 2;
    want.samples = 4096;
    want.callback = nullptr;

    SDL_OpenAudio(&want, &have);
    SDL_PauseAudio(0);

    // Set vsync hint before creating windows
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    // TV Window (primary display)
    tvWindow = SDL_CreateWindow("TV", 
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        1280, 720,  // Use 720p resolution
        SDL_WINDOW_FULLSCREEN | SDL_WINDOW_WIIU_TV_ONLY);
    if (tvWindow) {
        tvRenderer = SDL_CreateRenderer(tvWindow, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    }

    // GamePad Window
    drcWindow = SDL_CreateWindow("DRC",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        854, 480,  // Native GamePad resolution
        SDL_WINDOW_WIIU_GAMEPAD_ONLY | SDL_WINDOW_WIIU_PREVENT_SWAP);
    if (drcWindow) {
        drcRenderer = SDL_CreateRenderer(drcWindow, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    }

    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

    if (LoadLogin(username, password)) {
        if (login_account(username.c_str(), password.c_str())) {
            fetch_rooms();
            scene = ROOMS_LIST;
        } else {
            scene = SELECTION_MENU;
        }
    } else {
        scene = SELECTION_MENU;
    }

    // Background texture
    SDL_Texture* bgTexture = LoadImage(tvRenderer, "romfs:/res/bg.png");
    SDL_Texture* bgTextureDRC = LoadImage(drcRenderer, "romfs:/res/bg.png");

    LoadAvatars();

    SDL_Texture* systemAvatar = LoadImage(tvRenderer, "romfs:/res/system.png");
    AddChatLine(
        tvRenderer,
        "System",
        "Welcome!",
        systemAvatar,
        fontSize,
        fontSize,
        tvTextColor,
        tvTextColor,
        maxWidth
    );

    Uint32 lastTicks = 0;
    const int AXIS_DEADZONE = 8000;  // deadzone for joystick
    const float MAX_SPEED = 1000.0f;  // pixels per second when stick is fully pushed

    SDL_Event event;
    SDL_GameController* gController = nullptr;

    if (SDL_NumJoysticks() > 0) {
        if (SDL_IsGameController(0)) {
            gController = SDL_GameControllerOpen(0);
        }
    }

    int Keyboard_Event;
    SDL_WiiUSysWMEventType Keyboard_Ok = SDL_WIIU_SYSWM_SWKBD_OK_FINISH_EVENT;
    SDL_WiiUSysWMEventType Keyboard_Cancel = SDL_WIIU_SYSWM_SWKBD_CANCEL_EVENT;

    const char* selectionMenu[] = {
        "Create Account",
        "Log In"
    };

    const char* signUpMenu[] = {
        "Enter a username",
        "Enter a password",
        "Create account"
    };

    const char* signInMenu[] = {
        "Enter your username",
        "Enter your password",
        "Log In"
    };

    lastTicks = SDL_GetTicks();
    while (WHBProcIsRunning()) {
        if (scene == CHAT) {
            Uint32 now = SDL_GetTicks();
            float deltaSec = (now - lastTicks) / 1000.0f;
            lastTicks = now;

            if (gController) {
                // ANALOG STICK
                Sint16 axisY = SDL_GameControllerGetAxis(gController, SDL_CONTROLLER_AXIS_LEFTY);

                if (axisY > AXIS_DEADZONE || axisY < -AXIS_DEADZONE) {
                    float norm = axisY / 32767.0f;
                    float move = norm * MAX_SPEED * deltaSec;
                    chatPosY -= (int)move;
                }

                // D-PAD
                if (SDL_GameControllerGetButton(gController, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
                    chatPosY += (int)(MAX_SPEED * deltaSec);
                }

                if (SDL_GameControllerGetButton(gController, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
                    chatPosY -= (int)(MAX_SPEED * deltaSec);
                }
            }
        }

        while (SDL_PollEvent(&event)) {
            handle_event(event);

            if (event.type == SDL_TEXTINPUT)
                textBuffer += event.text.text;

            if (event.type == SDL_SYSWMEVENT) {
                Keyboard_Event = event.syswm.msg->msg.wiiu.event;
                if (Keyboard_Event == Keyboard_Ok || Keyboard_Event == Keyboard_Cancel) {
                    if (Keyboard_Event == Keyboard_Ok) {
                        if (currentTextSendType == type_message && !textBuffer.empty()) {
                            strncpy(input, textBuffer.c_str(), sizeof(input) - 1);
                            input[sizeof(input) - 1] = '\0';
                            send_chat(currentRoom, input);
                        }
                        else if (currentTextSendType == type_username) {
                            username = textBuffer;
                        }
                        else if (currentTextSendType == type_password) {
                            password = textBuffer;
                        }
                    }
                    textBuffer.clear();
                    currentTextSendType = type_none;
                    SDL_StopTextInput();
                }
            }
        }

        // Handle incoming messages
        TryReceive(&sock, tvRenderer, fontSize, tvTextColor, maxWidth);

        // Render TV Screen
        if (tvRenderer) {
            SDL_RenderClear(tvRenderer);

            // Draw background image
            if (bgTexture) {
                SDL_RenderCopy(tvRenderer, bgTexture, NULL, NULL);
            } else {
                SDL_SetRenderDrawColor(tvRenderer,
                    tvBackgroundColor.r,
                    tvBackgroundColor.g,
                    tvBackgroundColor.b,
                    tvBackgroundColor.a);
                SDL_RenderClear(tvRenderer);
            }

            if (scene == SIGN_UP || scene == SIGN_IN) {
                const int authMenuCount = 3;
                        
                for (int i = 0; i < authMenuCount; i++) {
                    // Highlight selected item
                    if (authMenuIndex == i) {
                    
                        SDL_Rect highlightRect = {
                            0,
                            180 + (60 * i),
                            1920,
                            56
                        };
                    
                        SDL_SetRenderDrawBlendMode(
                            tvRenderer,
                            SDL_BLENDMODE_BLEND
                        );
                    
                        SDL_SetRenderDrawColor(
                            tvRenderer,
                            0, 0, 0, 180
                        );
                    
                        SDL_RenderFillRect(
                            tvRenderer,
                            &highlightRect
                        );
                    }

                    if (scene == SIGN_UP) {
                        DrawText(
                            tvRenderer,
                            signUpMenu[i],
                            40,
                            180 + (60 * i),
                            48,
                            drcTextColor
                        );
                    }
                    else {
                        DrawText(
                            tvRenderer,
                            signInMenu[i],
                            40,
                            180 + (60 * i),
                            48,
                            drcTextColor
                        );
                    }
                }
            }

            if (scene == SELECTION_MENU) {
                DrawText(tvRenderer, "Account Setup", 450, 50, 128, tvTextColor);

                DrawText(tvRenderer, "Move: ↑/↓", 20, 930, 64, tvTextColor);
                DrawText(tvRenderer, "Select: Ⓐ", 20, 1000, 64, tvTextColor);

                const int selectionMenuCount = 2;
                        
                for (int i = 0; i < selectionMenuCount; i++) {
                    // Highlight selected item
                    if (selectionMenuIndex == i) {
                    
                        SDL_Rect highlightRect = {
                            0,
                            180 + (60 * i),
                            1920,
                            56
                        };
                    
                        SDL_SetRenderDrawBlendMode(
                            tvRenderer,
                            SDL_BLENDMODE_BLEND
                        );
                    
                        SDL_SetRenderDrawColor(
                            tvRenderer,
                            0, 0, 0, 180
                        );
                    
                        SDL_RenderFillRect(
                            tvRenderer,
                            &highlightRect
                        );
                    }
                
                    DrawText(
                        tvRenderer,
                        selectionMenu[i],
                        40,
                        180 + (60 * i),
                        48,
                        tvTextColor
                    );
                }
            }
            else if (scene == SIGN_UP) {
                DrawText(tvRenderer, "Create Account", 450, 50, 128, tvTextColor);

                DrawText(tvRenderer, "Move: ↑/↓", 20, 930, 64, tvTextColor);
                DrawText(tvRenderer, "Select: Ⓐ", 20, 1000, 64, tvTextColor);
            }
            else if (scene == SIGN_IN) {
                DrawText(tvRenderer, "Logging In", 550, 50, 128, tvTextColor);

                DrawText(tvRenderer, "Move: ↑/↓", 20, 930, 64, tvTextColor);
                DrawText(tvRenderer, "Select: Ⓐ", 20, 1000, 64, tvTextColor);
            }
            else if (scene == ROOMS_LIST) {
                DrawText(tvRenderer, "Rooms", 700, 50, 128, tvTextColor);

                DrawText(tvRenderer, "Move: ↑/↓", 20, 860, 64, tvTextColor);
                DrawText(tvRenderer, "Log out: Ⓑ", 20, 930, 64, tvTextColor);
                DrawText(tvRenderer, "Select: Ⓐ", 20, 1000, 64, tvTextColor);

                for (int i = 0; i < roomCount; i++) {

                    // Highlight selected room
                    if (selectedRoom == i) {

                        SDL_Rect highlightRect = {
                            0,
                            180 + (60 * i),
                            1920,
                            56
                        };

                        SDL_SetRenderDrawBlendMode(
                            tvRenderer,
                            SDL_BLENDMODE_BLEND
                        );

                        SDL_SetRenderDrawColor(
                            tvRenderer,
                            0, 0, 0, 180
                        );

                        SDL_RenderFillRect(
                            tvRenderer,
                            &highlightRect
                        );
                    }

                    DrawText(
                        tvRenderer,
                        rooms[i].name,
                        40,
                        180 + (60 * i),
                        48,
                        tvTextColor
                    );
                }
            }
            else if (scene == CHAT) {
                DrawChatBuffer(tvRenderer, 40, 40);

                DrawText(tvRenderer, "Move: ↑/↓", 20, 860, 64, tvTextColor);
                DrawText(tvRenderer, "Leave: Ⓑ", 20, 930, 64, tvTextColor);
                DrawText(tvRenderer, "Select: Ⓐ", 20, 1000, 64, tvTextColor);
            }
            SDL_RenderPresent(tvRenderer);
        }

        // Render DRC (GamePad) Screen
        if (drcRenderer) {
            SDL_RenderClear(drcRenderer);

            // Draw background image
            if (bgTextureDRC) {
                SDL_RenderCopy(drcRenderer, bgTextureDRC, NULL, NULL);
            } else {
                SDL_SetRenderDrawColor(drcRenderer,
                    drcBackgroundColor.r,
                    drcBackgroundColor.g,
                    drcBackgroundColor.b,
                    drcBackgroundColor.a);
                SDL_RenderClear(drcRenderer);
            }

            if (scene == SELECTION_MENU) {
                const int selectionMenuCount = 2;
                        
                for (int i = 0; i < selectionMenuCount; i++) {
                    // Highlight selected item
                    if (selectionMenuIndex == i) {
                    
                        SDL_Rect highlightRect = {
                            0,
                            40 * i,
                            854,
                            40
                        };
                    
                        SDL_SetRenderDrawBlendMode(
                            drcRenderer,
                            SDL_BLENDMODE_BLEND
                        );
                    
                        SDL_SetRenderDrawColor(
                            drcRenderer,
                            0, 0, 0, 180
                        );
                    
                        SDL_RenderFillRect(
                            drcRenderer,
                            &highlightRect
                        );
                    }
                
                    DrawText(
                        drcRenderer,
                        selectionMenu[i],
                        20,
                        40 * i + 4,
                        32,
                        drcTextColor
                    );
                }
            }
            else if (scene == SIGN_UP || scene == SIGN_IN) {
                const int authMenuCount = 3;
                        
                for (int i = 0; i < authMenuCount; i++) {
                    // Highlight selected item
                    if (authMenuIndex == i) {
                    
                        SDL_Rect highlightRect = {
                            0,
                            40 * i,
                            854,
                            40
                        };
                    
                        SDL_SetRenderDrawBlendMode(
                            drcRenderer,
                            SDL_BLENDMODE_BLEND
                        );
                    
                        SDL_SetRenderDrawColor(
                            drcRenderer,
                            0, 0, 0, 180
                        );
                    
                        SDL_RenderFillRect(
                            drcRenderer,
                            &highlightRect
                        );
                    }

                    if (scene == SIGN_UP) {
                        DrawText(
                            drcRenderer,
                            signUpMenu[i],
                            20,
                            40 * i + 4,
                            32,
                            drcTextColor
                        );
                    }
                    else {
                        DrawText(
                            drcRenderer,
                            signInMenu[i],
                            20,
                            40 * i + 4,
                            32,
                            drcTextColor
                        );
                    }
                }
            }
            else if (scene == ROOMS_LIST) {
                DrawText(
                    drcRenderer,
                    ("Logged in as: " + username).c_str(),
                    10,
                    420,
                    48,
                    drcTextColor
                );

                for (int i = 0; i < roomCount; i++) {
                
                    // Highlight selected room
                    if (selectedRoom == i) {
                    
                        SDL_Rect highlightRect = {
                            0,
                            40 * i,
                            854,
                            40
                        };
                    
                        SDL_SetRenderDrawBlendMode(
                            drcRenderer,
                            SDL_BLENDMODE_BLEND
                        );
                    
                        SDL_SetRenderDrawColor(
                            drcRenderer,
                            0, 0, 0, 180
                        );
                    
                        SDL_RenderFillRect(
                            drcRenderer,
                            &highlightRect
                        );
                    }
                
                    DrawText(
                        drcRenderer,
                        rooms[i].name,
                        20,
                        40 * i + 4,
                        32,
                        drcTextColor
                    );
                }
            }
            else if (scene == CHAT) {
                DrawText(
                    drcRenderer,
                    currentRoom.c_str(),
                    20,
                    20,
                    64,
                    drcTextColor
                );

                DrawText(
                    drcRenderer,
                    ("Logged in as: " + username).c_str(),
                    10,
                    380,
                    48,
                    drcTextColor
                );

                DrawText(
                    drcRenderer,
                    "Press A to type a message...",
                    10,
                    440,
                    32,
                    drcTextColor
                );
            }
            SDL_RenderPresent(drcRenderer);
        }
    }

    if (sock >= 0) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }

    if (gController)
        SDL_GameControllerClose(gController);

    if (bgTexture)
        SDL_DestroyTexture(bgTexture);
    if (bgTextureDRC)
        SDL_DestroyTexture(bgTextureDRC);

    if (systemAvatar)
        SDL_DestroyTexture(systemAvatar);

    if (drcRenderer)
        SDL_DestroyRenderer(drcRenderer);
    if (drcWindow)
        SDL_DestroyWindow(drcWindow);
    if (tvRenderer)
        SDL_DestroyRenderer(tvRenderer);
    if (tvWindow)
        SDL_DestroyWindow(tvWindow);

    DestroyAvatars();
    IMG_Quit();
    FreeFonts();
    TTF_Quit();
    romfsExit();
    SDL_CloseAudio();
    SDL_Quit();
    WHBProcShutdown();
    return 0;
}
