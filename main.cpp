#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 1024;

std::mutex logMutex;

class HTTPServer {
public:
  HTTPServer() : serverSocket(-1) {}

  ~HTTPServer() {
    if (serverSocket != -1) {
      close(serverSocket);
    }
  }

  void startServer() {
    createServerSocket();
    if (serverSocket == -1) {
      log("Error creating server socket");
      return;
    }

    bindServerSocket();
    if (listen(serverSocket, 10) == -1) {
      log("Error listening on server socket");
      return;
    }

    log("Server started. Listening on port " + std::to_string(PORT));

    while (true) {
      acceptClient();
    }
  }

private:
  int serverSocket;

  void createServerSocket() { serverSocket = socket(AF_INET, SOCK_STREAM, 0); }

  void bindServerSocket() {
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);
    if (bind(serverSocket, (struct sockaddr *)&serverAddr,
             sizeof(serverAddr)) == -1) {
      log("Error binding server socket");
      close(serverSocket);
      exit(EXIT_FAILURE); // Exit on critical error
    }
  }

  void acceptClient() {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    int clientSocket =
        accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (clientSocket == -1) {
      log("Error accepting connection");
      return;
    }

    // Handle client in a separate thread
    std::thread clientThread(&HTTPServer::handleRequest, this, clientSocket);
    clientThread.detach(); // Detach the thread, so it runs independently
  }

  void handleRequest(int clientSocket) {
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (bytesRead <= 0) {
      log("Error receiving data from client");
      close(clientSocket);
      return;
    }
    std::string httpRequest(buffer, bytesRead);

    if (httpRequest.substr(0, 3) == "GET") {
      handleGET(clientSocket, httpRequest);
    } else if (httpRequest.substr(0, 4) == "POST") {
      size_t pos = httpRequest.find("\r\n\r\n");
      if (pos != std::string::npos) {
        std::string postData = httpRequest.substr(pos + 4);
        handlePOST(clientSocket, postData);
      } else {
        close(clientSocket);
      }
    } else {
      // Handle unsupported methods with a "501 Not Implemented" response
      std::string response = "HTTP/1.1 501 Not Implemented\r\n\r\n";
      send(clientSocket, response.c_str(), response.size(), 0);
      close(clientSocket);
    }
  }

  void handleGET(int clientSocket, const std::string &httpRequest) {
    size_t start = httpRequest.find(" ");
    size_t end = httpRequest.find(" ", start + 1);
    std::string requestedFile = httpRequest.substr(start + 1, end - start - 1);

    // Replace any "%20" in the requested file path with spaces
    std::replace(requestedFile.begin(), requestedFile.end(), '%', ' ');
    std::replace(requestedFile.begin(), requestedFile.end(), '2', ' ');

    if (requestedFile == "/") {
      requestedFile = "/index.html"; // Serve index.html for root path
    }

    std::ifstream file("www" + requestedFile);
    if (!file.is_open()) {
      std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";
      send(clientSocket, response.c_str(), response.size(), 0);
      close(clientSocket);
      return;
    }

    std::ostringstream contentStream;
    contentStream << file.rdbuf();
    file.close();
    std::string content = contentStream.str();

    std::string response =
        "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(content.size()) +
        "\r\n\r\n" + content;
    send(clientSocket, response.c_str(), response.size(), 0);
    close(clientSocket);
  }

  // Inside the HTTPServer class
  void handlePOST(int clientSocket, const std::string &postData) {
    // Parse the POST data to extract name, email, and message
    std::string name, email, message;
    size_t pos_name = postData.find("name=");
    if (pos_name != std::string::npos) {
      size_t end_pos_name = postData.find("&", pos_name);
      if (end_pos_name != std::string::npos) {
        name =
            postData.substr(pos_name + 5, end_pos_name - pos_name -
                                              5); // 5 is the length of "name="
      }
    }

    // Save the submission to a file
    std::ofstream submissionFile("submissions.txt", std::ios::app);
    if (submissionFile.is_open()) {
      submissionFile << "Name: " << name << "  USER OK  " << std::endl;
      submissionFile.close();
      // Send a response indicating success
      std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
      send(clientSocket, response.c_str(), response.size(), 0);
      close(clientSocket);
      // Log the successful submission
      log("Form submission saved: Name - " + name);
    } else {
      // Failed to open the file, send a 500 Internal Server Error response
      std::string response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
      send(clientSocket, response.c_str(), response.size(), 0);
      close(clientSocket);
      // Log the error
      log("Error opening submission file for writing");
    }
  }

  void log(const std::string &message) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << message << std::endl;
    // Additional: Log to a file for auditing purposes
    std::ofstream logFile("server.log", std::ios::app);
    if (logFile.is_open()) {
      logFile << message << std::endl;
      logFile.close();
    }
  }
};

int main() {
  HTTPServer server;
  server.startServer();
  return 0;
}