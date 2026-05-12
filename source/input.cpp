#include "input.h"
#include "chat.h"
#include "net.h"
#include "storage.h"

int selectionMenuIndex = 0;
int authMenuIndex = 0;

void handle_button_down(const SDL_ControllerButtonEvent& e)
{
    if (currentTextSendType == type_none) {
        if (scene == CHAT) {
            if (e.button == SDL_CONTROLLER_BUTTON_A) {
                if (connectionLost) {
                    ReconnectToTCPServer();
                }
                else {
                    currentTextSendType = type_message;
                    SDL_WiiUSetSWKBDHintText("Say something...");
                    SDL_StartTextInput();
                }
            }
            else if (e.button == SDL_CONTROLLER_BUTTON_B) {
                scene = ROOMS_LIST;
            }
        }
        else if (scene == SELECTION_MENU) {
            if (e.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            
                selectionMenuIndex--;
            
                if (selectionMenuIndex < 0)
                    selectionMenuIndex = 1;
            }
        
            else if (e.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            
                selectionMenuIndex++;
            
                if (selectionMenuIndex > 1)
                    selectionMenuIndex = 0;
            }
        
            else if (e.button == SDL_CONTROLLER_BUTTON_A) {
            
                switch (selectionMenuIndex) {
                
                    case 0:
                        scene = SIGN_UP;
                        authMenuIndex = 0;
                        break;
                
                    case 1:
                        scene = SIGN_IN;
                        authMenuIndex = 0;
                        break;
                }
            }
        }
        else if (scene == SIGN_UP || scene == SIGN_IN) {
            if (e.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            
                authMenuIndex--;
            
                if (authMenuIndex < 0)
                    authMenuIndex = 2;
            }
        
            else if (e.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            
                authMenuIndex++;
            
                if (authMenuIndex > 2)
                    authMenuIndex = 0;
            }
        
            else if (e.button == SDL_CONTROLLER_BUTTON_A) {
            
                switch (authMenuIndex) {
                
                    case 0:
                        currentTextSendType = type_username;
                        SDL_WiiUSetSWKBDInitialText(username.c_str());
                        SDL_WiiUSetSWKBDHintText("Enter a username...");
                        SDL_StartTextInput();
                        authMenuIndex = 0;
                        break;
                
                    case 1:
                        currentTextSendType = type_password;
                        SDL_WiiUSetSWKBDInitialText(password.c_str());
                        SDL_WiiUSetSWKBDHintText("Enter a password...");
                        SDL_WiiUSetSWKBDPasswordMode(SDL_WIIU_SWKBD_PASSWORD_MODE_HIDE);
                        SDL_StartTextInput();
                        break;
                
                    case 2:
                        if (scene == SIGN_UP && create_account(username.c_str(), password.c_str())) {
                            SaveLogin(username.c_str(), password.c_str());

                            fetch_rooms();
                            scene = ROOMS_LIST;
                        } else if (scene == SIGN_IN && login_account(username.c_str(), password.c_str())) {
                            SaveLogin(username.c_str(), password.c_str());

                            fetch_rooms();
                            scene = ROOMS_LIST;
                        } else {
                            scene = SELECTION_MENU;
                        }
                        break;
                }
            }
            else if (e.button == SDL_CONTROLLER_BUTTON_B) {
                scene = SELECTION_MENU;
            }
        }
        else if (scene == ROOMS_LIST) {
            if (e.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            
                if (selectedRoom > 0)
                    selectedRoom--;
            }
        
            else if (e.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            
                if (selectedRoom < roomCount - 1)
                    selectedRoom++;
            }
        
            else if (e.button == SDL_CONTROLLER_BUTTON_A) {
            
                currentRoom = rooms[selectedRoom].name;
            
                scene = CHAT;
            }
        
            else if (e.button == SDL_CONTROLLER_BUTTON_B) {
            
                scene = SELECTION_MENU;
            }
        }
    }
}

void handle_event(const SDL_Event& event)
{
    switch (event.type) {
        case SDL_CONTROLLERDEVICEADDED:
            SDL_GameControllerOpen(event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (auto ctrlr = SDL_GameControllerFromInstanceID(event.cdevice.which))
                SDL_GameControllerClose(ctrlr);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            handle_button_down(event.cbutton);
            break;
    }
}
