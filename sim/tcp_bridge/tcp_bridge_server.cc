#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "shield/sim_controller.h"

using namespace ns3;

// ===================================================================
// Minimal JSON helpers – no library dependency
// ===================================================================

static std::string GetJsonStringValue(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();

    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) ++pos;
    if (pos >= json.length() || json[pos] != '"') return "";
    ++pos;

    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

static std::array<int, 14> ParseStepActions(const std::string& json)
{
    std::array<int, 14> actions{};
    std::string search = "\"actions\":[";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return actions;
    pos += search.length();

    for (uint32_t i = 0; i < 14; ++i)
    {
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        size_t end = json.find_first_of(",]", pos);
        if (end == std::string::npos) break;
        std::string num = json.substr(pos, end - pos);
        if (!num.empty())
            actions[i] = std::stoi(num);
        pos = end + 1;
        if (json[end] == ']') break;
    }
    return actions;
}

static std::string SerializeObs(const std::vector<std::vector<double>>& obs)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << "[";
    for (uint32_t i = 0; i < obs.size(); ++i)
    {
        if (i > 0) oss << ",";
        oss << "[";
        for (uint32_t j = 0; j < obs[i].size(); ++j)
        {
            if (j > 0) oss << ",";
            oss << obs[i][j];
        }
        oss << "]";
    }
    oss << "]";
    return oss.str();
}

// ===================================================================
// Idle timeout constant
// ===================================================================
constexpr int IDLE_TIMEOUT_SECONDS = 300;

// ===================================================================
// Read one newline-delimited line.
// Returns: 1 = line received, 0 = EOF, -1 = error/timeout.
// ===================================================================
static int ReadLine(int fd, std::string& out)
{
    out.clear();
    char ch;
    ssize_t n;
    while ((n = read(fd, &ch, 1)) > 0)
    {
        if (ch == '\n')
            return 1;
        out += ch;
    }

    if (n == 0)
        return 0;

    return -1;
}

// ===================================================================
// Serve one client. Returns true when server should continue accepting.
// ===================================================================
static bool ServeClient(int client_fd)
{
    SimController controller;
    bool hasBeenReset = false;
    bool running = true;

    while (running)
    {
        std::string line;
        int status = ReadLine(client_fd, line);

        if (status == 0)
        {
            std::cout << "Client disconnected unexpectedly (EOF).\n";
            break;
        }
        else if (status < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                std::cout << "Client disconnected unexpectedly (idle timeout).\n";
            else
                std::cout << "Client disconnected unexpectedly (socket error: "
                          << strerror(errno) << ").\n";
            break;
        }

        if (line.empty())
            continue;

        std::string cmd = GetJsonStringValue(line, "cmd");
        std::cout << "Received: " << cmd << std::endl;

        if (cmd == "reset")
        {
            auto obs = controller.Reset();
            hasBeenReset = true;
            std::string response = "{\"obs\":" + SerializeObs(obs) + "}\n";
            write(client_fd, response.c_str(), response.length());
            std::cout << "Sent reset response." << std::endl;
        }
        else if (cmd == "step")
        {
            if (!hasBeenReset)
            {
                std::string err = "{\"error\":\"step called before reset\"}\n";
                write(client_fd, err.c_str(), err.length());
                std::cout << "Sent error: step before reset." << std::endl;
                continue;
            }

            auto actionsArr = ParseStepActions(line);
            std::vector<int> actionsVec(actionsArr.begin(), actionsArr.end());
            auto result = controller.StepOnce(actionsVec);

            std::string response = "{\"obs\":" + SerializeObs(result.obs) +
                                   ",\"reward\":" + std::to_string(result.reward) +
                                   ",\"done\":" + (result.done ? "true" : "false") + "}\n";
            write(client_fd, response.c_str(), response.length());
            std::cout << "Step reward=" << result.reward << " done=" << result.done << std::endl;

            if (result.done)
                hasBeenReset = false;
        }
        else if (cmd == "close")
        {
            std::string response = "{\"status\":\"closing\"}\n";
            write(client_fd, response.c_str(), response.length());
            running = false;
        }
        else
        {
            std::string response = "{\"error\":\"unknown command\"}\n";
            write(client_fd, response.c_str(), response.length());
        }
    }

    close(client_fd);
    return true;
}

// ===================================================================
// Main server
// ===================================================================
int main(int argc, char* argv[])
{
    int port = 9999;

    // Simple command-line port parsing
    if (argc > 1)
    {
        if (std::string(argv[1]) == "--port")
        {
            if (argc < 3)
            {
                std::cerr << "Usage: " << argv[0] << " [--port PORT]\n";
                return EXIT_FAILURE;
            }
            port = std::atoi(argv[2]);
            if (port <= 0 || port > 65535)
            {
                std::cerr << "Invalid port: " << argv[2] << "\n";
                return EXIT_FAILURE;
            }
        }
        else
        {
            std::cerr << "Unknown argument: " << argv[1] << "\n";
            return EXIT_FAILURE;
        }
    }

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        return EXIT_FAILURE;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        return EXIT_FAILURE;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        return EXIT_FAILURE;
    }

    std::cout << "TCP bridge server listening on port " << port << std::endl;

    bool keepRunning = true;
    while (keepRunning)
    {
        socklen_t addrlen = sizeof(address);
        int client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (client_fd < 0)
        {
            perror("accept");
            break;
        }

        struct timeval tv;
        tv.tv_sec = IDLE_TIMEOUT_SECONDS;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::cout << "Client connected." << std::endl;
        keepRunning = ServeClient(client_fd);
    }

    close(server_fd);
    std::cout << "Server shut down." << std::endl;
    return 0;
}