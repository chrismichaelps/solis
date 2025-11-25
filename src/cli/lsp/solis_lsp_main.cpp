// Solis Programming Language - LSP Server Entry Point
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/lsp/lsp.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

// Minimal JSON parsing/generation
namespace json {

std::string escape(const std::string& s) {
  std::string result;
  for (char c : s) {
    if (c == '"')
      result += "\\\"";
    else if (c == '\\')
      result += "\\\\";
    else if (c == '\n')
      result += "\\n";
    else if (c == '\r')
      result += "\\r";
    else if (c == '\t')
      result += "\\t";
    else
      result += c;
  }
  return result;
}

}  // namespace json

using namespace solis;
using namespace solis::lsp;

class LSPServer {
private:
  LanguageServer server_;
  bool running_;

  // JSON parsing utilities
  std::string extractString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos)
      return "";

    size_t colonPos = keyPos + searchKey.length();
    size_t startQuote = json.find('"', colonPos);
    if (startQuote == std::string::npos)
      return "";

    size_t endQuote = startQuote + 1;
    while (endQuote < json.length()) {
      if (json[endQuote] == '"' && json[endQuote - 1] != '\\')
        break;
      endQuote++;
    }

    if (endQuote >= json.length())
      return "";
    return json.substr(startQuote + 1, endQuote - (startQuote + 1));
  }

  int extractInt(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos)
      return 0;

    size_t colonPos = keyPos + searchKey.length();
    size_t startVal = colonPos;
    while (startVal < json.length() && (json[startVal] == ' ' || json[startVal] == '\t')) {
      startVal++;
    }

    if (startVal >= json.length())
      return 0;

    size_t endVal = startVal;
    while (endVal < json.length() && (isdigit(json[endVal]) || json[endVal] == '-')) {
      endVal++;
    }

    try {
      return std::stoi(json.substr(startVal, endVal - startVal));
    } catch (...) {
      return 0;
    }
  }

  std::string extractUri(const std::string& json) { return extractString(json, "uri"); }

  std::string extractText(const std::string& json) {
    // Find "text": in the JSON
    size_t textPos = json.find("\"text\":");
    if (textPos == std::string::npos)
      return "";

    size_t colonPos = textPos + 7;  // length of "text":
    size_t startQuote = json.find('"', colonPos);
    if (startQuote == std::string::npos)
      return "";

    // Unescape the text content
    std::string text;
    for (size_t i = startQuote + 1; i < json.length(); i++) {
      if (json[i] == '"' && (i == startQuote + 1 || json[i - 1] != '\\'))
        break;
      if (json[i] == '\\' && i + 1 < json.length()) {
        i++;
        if (json[i] == 'n')
          text += '\n';
        else if (json[i] == 'r')
          text += '\r';
        else if (json[i] == 't')
          text += '\t';
        else if (json[i] == '"')
          text += '"';
        else if (json[i] == '\\')
          text += '\\';
        else
          text += json[i];
      } else {
        text += json[i];
      }
    }

    return text;
  }

  Position extractPosition(const std::string& json) {
    Position pos{0, 0};

    // Find "position": { in the JSON
    size_t posPos = json.find("\"position\":");
    if (posPos == std::string::npos) {
      // Try without position wrapper (e.g., in hover/definition requests)
      pos.line = extractInt(json, "line");
      pos.character = extractInt(json, "character");
    } else {
      // Extract from position object
      std::string positionSection = json.substr(posPos);
      size_t linePos = positionSection.find("\"line\":");
      size_t charPos = positionSection.find("\"character\":");

      if (linePos != std::string::npos) {
        size_t colonPos = linePos + 7;
        size_t startVal = colonPos;
        while (startVal < positionSection.length() && !isdigit(positionSection[startVal]))
          startVal++;
        size_t endVal = startVal;
        while (endVal < positionSection.length() && isdigit(positionSection[endVal]))
          endVal++;
        try {
          pos.line = std::stoi(positionSection.substr(startVal, endVal - startVal));
        } catch (...) {
        }
      }

      if (charPos != std::string::npos) {
        size_t colonPos = charPos + 12;
        size_t startVal = colonPos;
        while (startVal < positionSection.length() && !isdigit(positionSection[startVal]))
          startVal++;
        size_t endVal = startVal;
        while (endVal < positionSection.length() && isdigit(positionSection[endVal]))
          endVal++;
        try {
          pos.character = std::stoi(positionSection.substr(startVal, endVal - startVal));
        } catch (...) {
        }
      }
    }

    return pos;
  }

public:
  LSPServer()
      : running_(true) {}

  void run() {
    // Log startup to file for debugging
    std::ofstream startupLog("/tmp/solis-lsp-startup.log", std::ios::app);
    startupLog << "=== LSP Server Starting ===" << std::endl;
    startupLog << "Time: " << time(nullptr) << std::endl;
    startupLog << "PID: " << getpid() << std::endl;
    startupLog.flush();

    std::cerr << "[LSP] Solis Language Server starting..." << std::endl;
    std::string line;

    while (running_) {
      // Read headers
      int contentLength = -1;

      while (std::getline(std::cin, line)) {
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }

        if (line.empty()) {
          // Empty line marks end of headers
          break;
        }

        if (line.find("Content-Length:") == 0) {
          try {
            contentLength = std::stoi(line.substr(15));
            std::cerr << "[LSP] Content-Length: " << contentLength << std::endl;
          } catch (...) {
            std::cerr << "[LSP] Error parsing Content-Length" << std::endl;
          }
        }
      }

      if (contentLength <= 0) {
        std::cerr << "[LSP] Invalid or missing Content-Length, exiting" << std::endl;
        break;
      }

      // Read message body
      std::string message(contentLength, '\0');
      if (!std::cin.read(&message[0], contentLength)) {
        std::cerr << "[LSP] Error reading message body" << std::endl;
        break;
      }

      std::cerr << "[LSP] Received message: " << message.substr(0, 100) << "..." << std::endl;
      handleMessage(message);
    }

    std::cerr << "[LSP] Server shutting down" << std::endl;
  }

  void handleMessage(const std::string& message) {
    std::cerr << "[LSP] Handling message..." << std::endl;

    // Extract request ID from JSON message
    int requestId = 1;
    size_t idPos = message.find("\"id\":");
    if (idPos != std::string::npos) {
      size_t numStart = idPos + 5;
      while (numStart < message.length() && !isdigit(message[numStart]))
        numStart++;
      if (numStart < message.length()) {
        requestId = std::stoi(message.substr(numStart));
      }
    }

    if (message.find("initialize") != std::string::npos &&
        message.find("method") != std::string::npos) {
      std::cerr << "[LSP] Initialize request, ID=" << requestId << std::endl;
      std::ostringstream response;
      response << R"({"jsonrpc":"2.0","id":)" << requestId << R"(,"result":{)";
      response << R"("capabilities":{)";
      response << R"("textDocumentSync":1,)";
      response << R"("completionProvider":{"triggerCharacters":["."," ","\n"]},)";
      response << R"("hoverProvider":true,)";
      response << R"("definitionProvider":true)";
      response << R"(}}})";
      sendResponse(response.str());
      std::cerr << "[LSP] Sent initialize response" << std::endl;
    } else if (message.find("initialized") != std::string::npos &&
               message.find("method") != std::string::npos) {
      std::cerr << "[LSP] Initialized notification (no response needed)" << std::endl;
      // No response needed for initialized notification
    } else if (message.find("textDocument/didOpen") != std::string::npos) {
      std::cerr << "[LSP] didOpen" << std::endl;
      handleDidOpen(message);
    } else if (message.find("textDocument/didChange") != std::string::npos) {
      std::cerr << "[LSP] didChange" << std::endl;
      handleDidChange(message);
    } else if (message.find("textDocument/completion") != std::string::npos) {
      std::cerr << "[LSP] completion request, ID=" << requestId << std::endl;
      handleCompletion(message, requestId);
    } else if (message.find("textDocument/hover") != std::string::npos) {
      std::cerr << "[LSP] hover request, ID=" << requestId << std::endl;
      handleHover(message, requestId);
    } else if (message.find("textDocument/didClose") != std::string::npos) {
      std::cerr << "[LSP] didClose" << std::endl;
      handleDidClose(message);
    } else if (message.find("textDocument/definition") != std::string::npos) {
      std::cerr << "[LSP] definition request, ID=" << requestId << std::endl;
      handleDefinition(message, requestId);
    } else if (message.find("shutdown") != std::string::npos &&
               message.find("method") != std::string::npos) {
      std::cerr << "[LSP] shutdown request" << std::endl;
      std::ostringstream response;
      response << R"({"jsonrpc":"2.0","id":)" << requestId << R"(,"result":null})";
      sendResponse(response.str());
      running_ = false;
    } else if (message.find("\"method\":\"$/setTrace\"") != std::string::npos ||
               message.find("\"method\":\"$/cancelRequest\"") != std::string::npos) {
      std::cerr << "[LSP] Ignoring notification (no response needed)" << std::endl;
      // These are notifications, no response needed
    } else {
      std::cerr << "[LSP] Unknown method in message" << std::endl;
    }
  }

  void handleDidOpen(const std::string& message) {
    std::string uri = extractUri(message);
    std::string text = extractText(message);

    std::cerr << "[LSP] Opening document: " << uri << std::endl;
    server_.didOpen(uri, text, 1);

    // Send diagnostics
    auto diags = server_.getDiagnostics(uri);
    publishDiagnostics(uri, diags);
  }

  void handleDidChange(const std::string& message) {
    std::string uri = extractUri(message);
    std::string text = extractText(message);

    std::cerr << "[LSP] Updating document: " << uri << std::endl;
    server_.didChange(uri, text, 2);

    auto diags = server_.getDiagnostics(uri);
    publishDiagnostics(uri, diags);
  }

  void handleCompletion(const std::string& message, int requestId) {
    std::string uri = extractUri(message);
    Position pos = extractPosition(message);

    std::cerr << "[LSP] Getting completions for " << uri << " at (" << pos.line << ","
              << pos.character << ")" << std::endl;
    auto items = server_.getCompletions(uri, pos);
    std::cerr << "[LSP] Got " << items.size() << " completion items" << std::endl;

    std::ostringstream response;
    response << R"({"jsonrpc":"2.0","id":)" << requestId << R"(,"result":[)";

    for (size_t i = 0; i < items.size(); ++i) {
      if (i > 0)
        response << ",";
      response << R"({"label":")" << items[i].label << R"(",)"
               << R"("detail":")" << json::escape(items[i].detail) << R"(",)"
               << R"("kind":)" << items[i].kind << "}";
    }

    response << "]}";
    std::cerr << "[LSP] Sending completion response: " << response.str().substr(0, 200) << "..."
              << std::endl;
    sendResponse(response.str());
  }

  void handleHover(const std::string& message, int requestId) {
    std::string uri = extractUri(message);
    Position pos = extractPosition(message);

    std::cerr << "[LSP] Hover at " << uri << " (" << pos.line << "," << pos.character << ")"
              << std::endl;

    auto hover = server_.getHover(uri, pos);

    std::ostringstream response;
    if (!hover.contents.empty()) {
      response << R"({"jsonrpc":"2.0","id":)" << requestId << R"(,"result":{)";
      response << R"("contents":{"kind":"markdown","value":")" << json::escape(hover.contents)
               << R"("}})";
    } else {
      response << R"({"jsonrpc":"2.0","id":)" << requestId << R"(,"result":null})";
    }

    sendResponse(response.str());
  }

  void handleDidClose(const std::string& message) {
    std::string uri = extractUri(message);

    std::cerr << "[LSP] Closing document: " << uri << std::endl;
    server_.didClose(uri);
  }

  void handleDefinition(const std::string& message, int requestId) {
    std::string uri = extractUri(message);
    Position pos = extractPosition(message);

    std::cerr << "[LSP] Definition request at " << uri << " (" << pos.line << "," << pos.character
              << ")" << std::endl;

    auto location = server_.getDefinition(uri, pos);

    std::ostringstream response;
    if (!location.uri.empty() && location.range.start.line >= 0) {
      // Return valid location
      response << R"({"jsonrpc":"2.0","id":)" << requestId << R"(,"result":{)";
      response << R"("uri":")" << location.uri << R"(",)";
      response << R"("range":{"start":{"line":)" << location.range.start.line << R"(,"character":)"
               << location.range.start.character << "},";
      response << R"("end":{"line":)" << location.range.end.line << R"(,"character":)"
               << location.range.end.character << "}}}}";
    } else {
      // Return null if no definition found
      response << R"({"jsonrpc":"2.0","id":)" << requestId << R"(,"result":null})";
    }

    sendResponse(response.str());
  }

  void publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diags) {
    std::cerr << "[LSP] Publishing " << diags.size() << " diagnostics for " << uri << std::endl;
    std::ostringstream notification;
    notification << R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics",)";
    notification << R"("params":{"uri":")" << uri << R"(",)";
    notification << R"("diagnostics":[)";

    for (size_t i = 0; i < diags.size(); ++i) {
      if (i > 0)
        notification << ",";
      notification << "{";
      notification << R"("range":{"start":{"line":)" << diags[i].range.start.line
                   << R"(,"character":)" << diags[i].range.start.character << "},";
      notification << R"("end":{"line":)" << diags[i].range.end.line << R"(,"character":)"
                   << diags[i].range.end.character << "}},";
      notification << R"("severity":)" << diags[i].severity << ",";
      notification << R"("message":")" << json::escape(diags[i].message) << "\"";
      notification << "}";
    }

    notification << "]}}";
    sendNotification(notification.str());
  }

  void sendResponse(const std::string& response) {
    std::cout << "Content-Length: " << response.length() << "\r\n\r\n";
    std::cout << response << std::flush;
  }

  void sendNotification(const std::string& notification) {
    std::cout << "Content-Length: " << notification.length() << "\r\n\r\n";
    std::cout << notification << std::flush;
  }
};

int main() {
  LSPServer lspServer;
  lspServer.run();
  return 0;
}
