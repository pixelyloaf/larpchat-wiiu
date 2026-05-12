#include "net.h"
#include "chat.h"
#include "image.h"

std::string authToken = "";
std::string authError = "";

bool showResponse = false;

SDL_Texture* discordAvatar = nullptr;
SDL_Texture* defaultAvatar = nullptr;

void LoadAvatars()
{
    discordAvatar = LoadImage(tvRenderer, "romfs:/res/discord.png");
    defaultAvatar = LoadImage(tvRenderer, "romfs:/res/default.png");
}

void DestroyAvatars()
{
    if (discordAvatar)
        SDL_DestroyTexture(discordAvatar);

    if (defaultAvatar)
        SDL_DestroyTexture(defaultAvatar);
}

static bool SetNonBlocking(int sock)
{
    if (sock < 0) return false;
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) flags = 0;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) != -1;
}

int ConnectToTCPServer()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT_TCP);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        close(sock);
        return -1;
    }

    SetNonBlocking(sock);
    return sock;
}

void ReconnectToTCPServer()
{
    int newSock = ConnectToTCPServer();

    if (newSock >= 0) {
        sock = newSock;
        connectionLost = false;
    }
}

int ConnectToHTTPServer()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT_HTTP);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        close(sock);
        return -1;
    }

    return sock;
}

void TryReceive(int *sock, SDL_Renderer* renderer, int fontSize, SDL_Color textColor, int maxWidth)
{
    if (*sock < 0) return;

    static std::string pending;
    char buf[512];

    while (true)
    {
        ssize_t r = recv(*sock, buf, sizeof(buf), 0);

        if (r > 0)
        {
            pending.append(buf, r);

            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos)
            {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);

                std::vector<std::string> parts;

                size_t start = 0;
                size_t end;

                while ((end = line.find('|', start)) != std::string::npos)
                {
                    parts.push_back(line.substr(start, end - start));
                    start = end + 1;
                }

                parts.push_back(line.substr(start));

                std::string username;
                std::string message;
                std::string room;

                if (parts.size() >= 3)
                {
                    username = parts[0];
                    message  = parts[1];
                    room     = parts[2];

                    SDL_Texture* avatar;

                    if (username == "auroracross")
                    {
                        avatar = discordAvatar;
                    }
                    else
                    {
                        avatar = defaultAvatar;
                    }

                    AddChatLine(
                        renderer,
                        username,
                        message,
                        room,
                        avatar,
                        fontSize,
                        fontSize,
                        textColor,
                        textColor,
                        maxWidth
                    );
                }
                else
                {
                    SDL_Log("Malformed message: %s", line.c_str());
                }
            }
        }
        else if (r == 0)
        {
            close(*sock);
            *sock = -1;
            pending.clear();

            connectionLost = true;
            break;
        }
        else
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                break;

            close(*sock);
            *sock = -1;
            pending.clear();

            connectionLost = true;
            break;
        }
    }
}

std::string send_post_request(const std::string& endpoint, const std::string& body)
{
    int sock = ConnectToHTTPServer();

    if (sock < 0)
        return "";

    std::string request =
        "POST " + endpoint + " HTTP/1.1\r\n"
        "Host: " + std::string(SERVER_IP) + ":" + std::to_string(SERVER_PORT_HTTP) + "\r\n"
        "User-Agent: aurorachat Wii U\r\n"
        "Content-Type: text/plain\r\n"
        "auth: " + authToken + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    send(sock, request.c_str(), request.size(), 0);

    std::string response;

    char buffer[1024];

    int r;

    while ((r = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, r);
    }

    close(sock);

    size_t bodyPos = response.find("\r\n\r\n");

    if (bodyPos != std::string::npos) {
        response = response.substr(bodyPos + 4);
    }

    return response;
}

bool create_account(const std::string& username, const std::string& password)
{
    std::string body = username + "|" + password;

    std::string response = send_post_request("/api/signup", body);

    authError = response;

    if (response.empty())
        return false;

    // trim whitespace/newlines
    while (!response.empty() &&
        (response.back() == '\n' ||
         response.back() == '\r' ||
         response.back() == ' '))
    {
        response.pop_back();
    }

    // reject errors
    if (response == "ERR_MISSING_INPUT" ||
        response == "ERR_USER_USED" ||
        response == "ERR_BANNED")
    {
        showResponse = true;
        return false;
    }

    showResponse = false;
    authToken = response;

    return true;
}

bool login_account(const std::string& username, const std::string& password)
{
    std::string body = username + "|" + password;

    std::string response = send_post_request("/api/login", body);

    authError = response;

    if (response.empty())
        return false;

    // trim whitespace/newlines
    while (!response.empty() &&
        (response.back() == '\n' ||
         response.back() == '\r' ||
         response.back() == ' '))
    {
        response.pop_back();
    }

    // reject errors
    if (response == "ERR_WRONG_PASS" ||
        response == "ERR_BANNED")
    {
        showResponse = true;
        return false;
    }

    showResponse = false;
    authToken = response;

    return true;
}

void append_room(const char* name, const char* desc)
{
    if (roomCount < 100) {
        strncpy(rooms[roomCount].name, name, sizeof(rooms[roomCount].name));
        strncpy(rooms[roomCount].description, desc, sizeof(rooms[roomCount].description));
        roomCount++;
    }
}

void fetch_rooms()
{
    roomCount = 0;

    std::string response =
        send_post_request("/api/rooms", "");

    if (response.empty())
        return;

    char buf[2048];

    strncpy(buf, response.c_str(), sizeof(buf) - 1);

    buf[sizeof(buf) - 1] = '\0';

    char* countStr = strtok(buf, "|");

    if (!countStr)
        return;

    int roomsToAdd = atoi(countStr);

    for (int i = 0; i < roomsToAdd; i++) {

        char* roomName = strtok(NULL, "|");

        if (roomName) {
            append_room(roomName, "");
        }
    }
}

void send_chat(const std::string& room, const std::string& message)
{
    std::string body =
        message + "|" + room + "|";

    send_post_request("/api/chat", body);
}
