#include <iostream>
#include <filesystem>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <map>
#include <mutex>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// Namespace imports
using json = nlohmann::json;
using namespace std;

// Function declarations
void show_menu();
void dynamic_spinner(const string& message, const string& color);
void show_command_prompt();
void save_session(const vector<json>& messages);
void display_history(const vector<json>& messages);
void load_session();
void display_prompts(const map<string, string>& prompts);
void edit_prompt(map<string, string>& prompts);
void save_prompt(const string& name, const string& content);

// Configuration files and constants
const string CONFIG_FILE = "config.json";
const string PROMPT_DIR = "prompts/";
const string SESSION_FILE = "session_history.json";

// ANSI color codes for enhanced UI
const string COLOR_RESET = "\033[0m";
const string COLOR_RED = "\033[31m";
const string COLOR_GREEN = "\033[32m";
const string COLOR_BLUE = "\033[34m";
const string COLOR_YELLOW = "\033[33m";
const string COLOR_CYAN = "\033[36m";
const string COLOR_GRADIENT_1 = "\033[38;5;199m";
const string COLOR_GRADIENT_2 = "\033[38;5;198m";
const string COLOR_GRADIENT_3 = "\033[38;5;197m";
const string COLOR_HIGHLIGHT = "\033[38;5;219m";
const string COLOR_SUCCESS = "\033[38;5;156m";
const string COLOR_ALERT = "\033[38;5;209m";
const string COLOR_ERROR = "\033[38;5;196m";

// Search engine options
enum class SearchEngine {
    GOOGLE,
    BING,
    DUCKDUCKGO
};

// Configuration structure
struct Config {
    string server_url = "http://127.0.0.1:9003/v1/chat/completions";   // Replace with your backedn URL
    int max_tokens = 1000;
    double temperature = 0.7;
    bool debug_mode = false;
    SearchEngine search_engine = SearchEngine::DUCKDUCKGO;
    bool nsfw_mode = true;
    bool stream = true;  // Enable streaming by default

    // Default constructor
    Config() = default;

    // Move constructor
    Config(Config&&) = default;

    // Delete copy constructor and assignment operator
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};
// Thread-safe output
mutex output_mutex;

void thread_safe_output(const string& message) {
    lock_guard<mutex> guard(output_mutex);
    cout << message << endl;
}

// Curl response helper
size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t total_size = size * nmemb;
    output->append((char*)contents, total_size);
    return total_size;
}

// Spinner helper for progress indication
bool spinner_running = false;
void spinner(const string& message) {
    const vector<string> frames = {"|", "/", "-", "\\"};
    int frame = 0;
    while (spinner_running) {
        cout << "\r" << COLOR_CYAN << message << " " << frames[frame] << COLOR_RESET << flush;
        frame = (frame + 1) % frames.size();
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    cout << "\r" << string(message.size() + 3, ' ') << "\r"; // Clear the spinner line
}

// JSON utilities
json load_json_file(const string& file_path) {
    ifstream file(file_path);
    if (!file.is_open()) return {};
    json data;
    file >> data;
    return data;
}

void save_json_file(const string& file_path, const json& data) {
    ofstream file(file_path);
    if (!file.is_open()) {
        cerr << COLOR_RED << "Error: Unable to save file " << file_path << COLOR_RESET << endl;
        return;
    }
    file << data.dump(4);
    file.close();
}

// Load and save configuration
Config load_config() {
    json config_data = load_json_file(CONFIG_FILE);
    Config config;
    if (!config_data.is_null()) {
        config.server_url = config_data.value("server_url", config.server_url);
        config.max_tokens = config_data.value("max_tokens", config.max_tokens);
        config.temperature = config_data.value("temperature", config.temperature);
        config.debug_mode = config_data.value("debug_mode", config.debug_mode);
        config.search_engine = static_cast<SearchEngine>(config_data.value("search_engine", static_cast<int>(config.search_engine)));
    } else {
        // Create a JSON object from Config struct
        json config_json = {
            {"server_url", config.server_url},
            {"max_tokens", config.max_tokens},
            {"temperature", config.temperature},
            {"debug_mode", config.debug_mode},
            {"search_engine", static_cast<int>(config.search_engine)}
        };
        save_json_file(CONFIG_FILE, config_json);
    }
    return config;
}
// Enhanced AI query with llama, focusing on NSFW content
string query_ai(const Config& config, const vector<json>& messages) {
    CURL* curl = curl_easy_init();
    CURLcode res;
    string response_string;

    if (!curl) {
        cerr << COLOR_RED << "Error: CURL initialization failed" << COLOR_RESET << endl;
        return {};
    }

    json payload = {
        {"model", "llama"},
        {"messages", messages},
        {"max_tokens", config.max_tokens},
        {"temperature", config.temperature},
        {"nsfw_mode", true} // Ensuring NSFW mode is enabled
    };

    string payload_str = payload.dump();

    curl_easy_setopt(curl, CURLOPT_URL, config.server_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

    // Improved spinner functionality with dynamic_spinner
    thread spinner_thread(dynamic_spinner, "Processing your query...", COLOR_CYAN);
    spinner_running = true;
    res = curl_easy_perform(curl);
    spinner_running = false;
    spinner_thread.join();

    if (res != CURLE_OK) {
        cerr << COLOR_RED << "Error: " << curl_easy_strerror(res) << COLOR_RESET << endl;
    } else {
        cout << COLOR_GREEN << "Query successful. Analyzing results..." << COLOR_RESET << endl;
    }

    curl_easy_cleanup(curl);
    return response_string;
}

// URL encoding function
string url_encode(const string& value) {
    CURL *curl = curl_easy_init();
    string encoded;
    if (curl) {
        char* output = curl_easy_escape(curl, value.c_str(), value.length());
        if (output) {
            encoded = output;
            curl_free(output);
        }
        curl_easy_cleanup(curl);
    }
    return encoded;
}

// Enhanced status display with timestamp
void display_status(const string& message, const string& color, const string& icon) {
    auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    auto timestamp = put_time(localtime(&now), "%Y-%m-%d %H:%M:%S");
    cout << color << "[" << timestamp << "] " << icon << " " << message << COLOR_RESET << endl;
}

// Function to generate a border
string generate_border(int length, char border_char = '=', const string& color = COLOR_RESET) {
    return color + string(length, border_char) + COLOR_RESET;
}


// Di// Function to display search results with pagination
void display_results(const vector<string>& results) {
    cout << generate_border(30) << endl;
    if (results.empty()) {
        cout << COLOR_RED << "No results found." << COLOR_RESET << endl;
    } else {
        cout << COLOR_YELLOW << "Displaying top results:" << COLOR_RESET << endl;
        for (size_t i = 0; i < results.size(); i++) {
            cout << COLOR_GRADIENT_2 << " " << (i + 1) << ". " << COLOR_RESET;
            cout << results[i] << endl;
        }
    }
    cout << generate_border(30) << endl;
}

// Function to show the welcome screen and available commands
void show_welcome_screen() {
    cout << "\033[2J\033[1;1H";  // Clear screen and move to top-left corner
    cout << generate_border(50, '=', COLOR_GRADIENT_1) << endl;  // Top border
    cout << COLOR_GRADIENT_1 << "    * Enhanced AI Terminal v2.0 *" << COLOR_RESET << endl;  // Version info
    cout << COLOR_GRADIENT_2 << "        Ghostintheshell  " << COLOR_RESET << endl;  // Companion message
    cout << COLOR_GRADIENT_3 << "    Build: " << __DATE__ << " " << __TIME__ << COLOR_RESET << endl;  // Build info
    cout << generate_border(50, '=', COLOR_GRADIENT_1) << endl;  // Bottom border
    show_menu();  // Show available commands
}

// Enhanced Interactive Menu Display
void show_menu() {
    std::cout << COLOR_HIGHLIGHT << "\nAvailable Commands:\n" << COLOR_RESET;
    std::cout << COLOR_GRADIENT_2 << " ├─ search:<query>   " << COLOR_RESET << "Search the web for information\n";
    std::cout << COLOR_GRADIENT_2 << " ├─ chat:<question>   " << COLOR_RESET << "Engage in conversation with the AI\n";
    std::cout << COLOR_GRADIENT_2 << " ├─ nsfw:<on/off>    " << COLOR_RESET << "Toggle NSFW content filtering\n";
    std::cout << COLOR_GRADIENT_2 << " ├─ help             " << COLOR_RESET << "Display detailed help information\n";
    std::cout << COLOR_GRADIENT_2 << " ├─ clear            " << COLOR_RESET << "Clear the terminal screen\n";
    std::cout << COLOR_GRADIENT_2 << " ├─ settings         " << COLOR_RESET << "Configure AI and search settings\n";
    std::cout << COLOR_GRADIENT_2 << " └─ exit             " << COLOR_RESET << "Terminate the program\n";
}

void handle_settings(Config& config) {
    cout << COLOR_GRADIENT_1 << "\nSettings Menu" << COLOR_RESET << endl;
    cout << generate_border(30) << endl;
    cout << COLOR_GRADIENT_2 << "1. NSFW Mode: " << (config.nsfw_mode ? "ON" : "OFF") << COLOR_RESET << endl;
    cout << COLOR_GRADIENT_2 << "2. Temperature: " << config.temperature << COLOR_RESET << endl;
    cout << COLOR_GRADIENT_2 << "3. Max Tokens: " << config.max_tokens << COLOR_RESET << endl;
    cout << COLOR_GRADIENT_2 << "4. Debug Mode: " << (config.debug_mode ? "ON" : "OFF") << COLOR_RESET << endl;
    cout << generate_border(30) << endl;
    
    cout << "Enter setting number to change (or 'x' to exit): ";
    string choice;
    getline(cin, choice);
    
    // Start spinner while processing settings
    thread spinner_thread(spinner, "Processing your settings...");
    spinner_running = true;

    if (choice == "1") {
        config.nsfw_mode = !config.nsfw_mode;
        cout << COLOR_SUCCESS << "NSFW Mode: " << (config.nsfw_mode ? "Enabled" : "Disabled") << COLOR_RESET << endl;
    } else if (choice == "2") {
        cout << "Enter new temperature (0.1-1.0): ";
        cin >> config.temperature;
        cin.ignore();
    } else if (choice == "3") {
        cout << "Enter new max tokens (100-2000): ";
        cin >> config.max_tokens;
        cin.ignore();
    } else if (choice == "4") {
        config.debug_mode = !config.debug_mode;
        cout << COLOR_SUCCESS << "Debug Mode: " << (config.debug_mode ? "Enabled" : "Disabled") << COLOR_RESET << endl;
    } else if (choice == "x") {
        cout << "Exiting settings menu." << endl;
        spinner_running = false; // Stop spinner before exiting
        spinner_thread.join(); // Wait for spinner thread to finish
        return; // Exit the settings menu
    } else {
        cout << COLOR_ALERT << "Invalid choice. Please try again." << COLOR_RESET << endl;
    }

    // Save the updated configuration
    save_json_file(CONFIG_FILE, {
        {"nsfw_mode", config.nsfw_mode},
        {"temperature", config.temperature},
        {"max_tokens", config.max_tokens},
        {"debug_mode", config.debug_mode}
    });

    // Stop spinner after processing
    spinner_running = false;
    spinner_thread.join(); // Wait for spinner thread to finish
}

// Help command logic
void handle_help() {
    cout << COLOR_GRADIENT_1 << "\nHelp Menu" << COLOR_RESET << endl;
    cout << generate_border(30) << endl;
    cout << COLOR_GRADIENT_2 << "1. search:<query>   " << COLOR_RESET << "Search the web for information.\n";
    cout << COLOR_GRADIENT_2 << "2. chat:<question>   " << COLOR_RESET << "Engage in conversation with the AI.\n";
    cout << COLOR_GRADIENT_2 << "3. nsfw:<on/off>    " << COLOR_RESET << "Toggle NSFW content filtering.\n";
    cout << COLOR_GRADIENT_2 << "4. help             " << COLOR_RESET << "Display this help information.\n";
    cout << COLOR_GRADIENT_2 << "5. clear            " << COLOR_RESET << "Clear the terminal screen.\n";
    cout << COLOR_GRADIENT_2 << "6. settings         " << COLOR_RESET << "Configure AI and search settings.\n";
    cout << COLOR_GRADIENT_2 << "7. exit             " << COLOR_RESET << "Terminate the program.\n";
    cout << generate_border(30) << endl;
}

string web_search_with_selection(const string& query) {
    CURL* curl = curl_easy_init();
    string response;

    // Load configuration to check NSFW mode
    Config config = load_config();
    string safe_search = config.nsfw_mode ? "off" : "on"; // Set based on user preference

    display_status("Starting Web Search: " + query, COLOR_HIGHLIGHT, "ℹ");

    spinner_running = true;
    thread spinner_thread(dynamic_spinner, "Searching web resources", COLOR_GRADIENT_1);

    if (curl) {
        string encoded_query = url_encode(query);
        string search_url = "https://api.duckduckgo.com/?q=" + encoded_query + 
                          "&format=json&safesearch=" + safe_search; // Use user preference for safe search

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, search_url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

        CURLcode res = curl_easy_perform(curl);
        spinner_running = false;
        spinner_thread.join();

        if (res == CURLE_OK) {
            try {
                json results = json::parse(response);
                if (results.contains("RelatedTopics") && results["RelatedTopics"].is_array()) {
                    if (results["RelatedTopics"].empty()) {
                        cout << COLOR_RED << "No related topics found for your search." << COLOR_RESET << endl;
                        return "No results."; // Return early if no results
                    }

                    cout << COLOR_SUCCESS << "\n📚 NSFW Search Results" << COLOR_RESET << endl;
                    cout << generate_border(30) << endl;

                    vector<string> search_results;
                    for (int i = 0; i < min(10, (int)results["RelatedTopics"].size()); i++) {
                        if (results["RelatedTopics"][i].contains("Text")) {
                            search_results.push_back(results["RelatedTopics"][i]["Text"]);
                        } else {
                            search_results.push_back("No text available for this result.");
                        }
                    }

                    display_results(search_results);

                    cout << COLOR_HIGHLIGHT << "Select result (1-10): " << COLOR_RESET;

                    int selection;
                    while (!(cin >> selection) || selection < 1 || selection > 10) {
                        cin.clear();
                        cin.ignore(numeric_limits<streamsize>::max(), '\n');
                        cout << COLOR_ALERT << "Please enter a number between 1 and 10: " << COLOR_RESET;
                    }
                    cin.ignore();

                    curl_slist_free_all(headers);
                    curl_easy_cleanup(curl);

                    display_status("NSFW Search completed successfully", COLOR_SUCCESS, "✓");
                    return results["RelatedTopics"][selection - 1]["Text"];
                } else {
                    cout << COLOR_RED << "Unexpected JSON structure." << COLOR_RESET << endl;
                }
            } catch (const json::parse_error& e) {
                cerr << COLOR_RED << "JSON parsing error: " << e.what() << COLOR_RESET << endl;
            } catch (const json::type_error& e) {
                cerr << COLOR_RED << "Type error: " << e.what() << COLOR_RESET << endl;
            }
        } else {
            cerr << COLOR_RED << "CURL error: " << curl_easy_strerror(res) << COLOR_RESET << endl;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return "Unable to complete search";
}

// Main interactive agent function
void interactive_agent_enhanced() {
    Config config = load_config();
    vector<json> messages = {
        {{"role", "system"}, {"content", "You are a powerful AI assistant with advanced capabilities."}}
    };
    bool debug_mode = false;
    show_welcome_screen();  // Show welcome screen only once
    
    while (true) {
        show_command_prompt();
        string input;
        getline(cin, input);
        
        // Enhanced command processing
        if (input == "clear") {
            cout << "\033[2J\033[1;1H";
            show_welcome_screen();  // Show welcome screen again after clearing
            continue;
        }
        
        if (input == "settings") {
            handle_settings(config); // Call the settings handler
            continue;
        }
        
        if (input == "help") {
            handle_help(); // Call the help handler
            continue;
        }
        
        if (input.find("search:") == 0) {
            string query = input.substr(7);
            string result = web_search_with_selection(query);
            messages.push_back({{"role", "system"}, {"content", "Search result: " + result}});
            // Auto-analyze search results
            messages.push_back({{"role", "user"}, {"content", "Please analyze this search result and provide insights."}});
            string response = query_ai(config, messages);
            // Ensure response is valid
            if (!response.empty()) {
                json response_json = json::parse(response);
                string reply = response_json["choices"][0]["message"]["content"];
                cout << COLOR_CYAN << "Analysis >>> " << reply << COLOR_RESET << endl;
            }
            continue;
        }
        
        if (input == "exit") {
            save_session(messages); // Auto-save on exit
            display_status("Session saved. Goodbye!", COLOR_GRADIENT_1, "👋");
            break;
        }
        
        // Enhanced NSFW command processing
        if (input.find("nsfw:") == 0) {
            string mode = input.substr(5);
            if (mode == "on") {
                config.nsfw_mode = true;
                cout << COLOR_SUCCESS << "NSFW Mode: Enabled" << COLOR_RESET << endl;
            } else if (mode == "off") {
                config.nsfw_mode = false;
                cout << COLOR_SUCCESS << "NSFW Mode: Disabled" << COLOR_RESET << endl;
            } else {
                cout << COLOR_ALERT << "Invalid NSFW command. Use 'nsfw:on' or 'nsfw:off'." << COLOR_RESET << endl;
            }
            continue;
        }
        
        // Enhanced chat processing
        if (!input.empty() && input != "help") {
            messages.push_back({{"role", "user"}, {"content", input}});
            string response = query_ai(config, messages);
            json response_json = json::parse(response);
            string reply = response_json["choices"][0]["message"]["content"];
            
            if (debug_mode) {
                cout << COLOR_YELLOW << "Token count: " << response_json["usage"]["total_tokens"] << COLOR_RESET << endl;
            }
            
            messages.push_back({{"role", "assistant"}, {"content", reply}});
            cout << COLOR_CYAN << "AI >>> " << reply << COLOR_RESET << endl;
        } else {
            cout << COLOR_ALERT << "Invalid command. Type 'help' for a list of available commands." << COLOR_RESET << endl;
        }
    }
}

// Function to load prompts
std::map<std::string, std::string> load_prompts() {
    const std::string PROMPT_DIR = "prompts/";

    if (!std::filesystem::exists(PROMPT_DIR)) {
        std::cerr << "Error: Prompt directory not found." << std::endl;
        return {};
    }
    std::map<std::string, std::string> prompts;
    
    // Add default prompt regardless of directory
    prompts["default"] = "You are a helpful AI assistant.";
    
    // Only try to load from directory if it exists
    if (std::filesystem::exists(PROMPT_DIR)) {
        for (const auto& entry : std::filesystem::directory_iterator(PROMPT_DIR)) {
            if (entry.path().extension() == ".json") {
                json prompt_data = load_json_file(entry.path().string());
                std::string name = entry.path().stem().string();
                prompts[name] = prompt_data.value("content", "");
            }
        }
    }
    return prompts;
}

void display_prompts(const map<string, string>& prompts) {
    cout << COLOR_YELLOW << "Available Prompts:" << COLOR_RESET << endl;
    for (const auto& prompt : prompts) {
        cout << COLOR_GREEN << "  " << prompt.first << COLOR_RESET << endl;
    }
    cout << COLOR_BLUE << "Use 'prompt:load:<name>' to switch prompts." << COLOR_RESET << endl;
}
string execute_command(const string& command) {
    string output;
    stringstream formatted_output;
    
    cout << "\n⚡ Executing Command ⚡" << endl;
    cout << "╔════════════════════╗" << endl;
    cout << "│ " << command << endl;
    cout << "╟────────────────────╢" << endl;
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        cerr << "Error: Failed to execute command" << endl;
        cout << "╚════════════════════╝" << endl;
        return "Command execution failed";
    }
    
    try {
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
            formatted_output << "│ " << buffer;
        }
        
        int status = pclose(pipe);
        if (status != 0) {
            formatted_output << "│ Warning: Command exited with status " << status << endl;
        }
        
        cout << formatted_output.str();
        cout << "╚════════════════════╝" << endl;
        
        return output;
    } catch (const exception& e) {
        pclose(pipe);
        cerr << "Error during command execution: " << e.what() << endl;
        cout << "╚════════════════════╝" << endl;
        return "Command execution error";
    }
}
// load prompt 
void edit_prompt(map<string, string>& prompts) {
    cout << COLOR_YELLOW << "Available Prompts:" << COLOR_RESET << endl;
    for (const auto& prompt : prompts) {
        cout << COLOR_GREEN << "  " << prompt.first << COLOR_RESET << endl;
    }
    cout << COLOR_BLUE << "Use 'prompt:load:<name>' to load a prompt." << COLOR_RESET << endl;

    cout << "Enter the name of the prompt you want to edit: ";
    string name;
    cin >> name;

    if (prompts.find(name) != prompts.end()) {
        cout << "Enter the new content for the prompt: ";
        string new_content;
        cin >> new_content;
        prompts[name] = new_content;
        cout << COLOR_GREEN << "Prompt updated successfully." << COLOR_RESET << endl;
    } else {
        cout << COLOR_RED << "Prompt not found." << COLOR_RESET << endl;
    }
}

void save_prompt(const string& name, const string& content) {
    json prompt_data = {
        {"role", "system"},
        {"content", content}
    };
    save_json_file(PROMPT_DIR + "/" + name + ".json", prompt_data);
    cout << COLOR_GREEN << "Prompt saved successfully." << COLOR_RESET << endl;
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    interactive_agent_enhanced();
    curl_global_cleanup();
    return 0;
}

void display_history(const vector<json>& messages) {
    cout << COLOR_YELLOW << "History:" << COLOR_RESET << endl;
    for (const auto& message : messages) {
        cout << COLOR_GREEN << "  " << message["role"] << ": " << message["content"] << COLOR_RESET << endl;
    }
}

void save_session(const vector<json>& messages) {
    save_json_file(SESSION_FILE, messages);
}

void load_session() {
    if (filesystem::exists(SESSION_FILE)) {
        vector<json> messages = load_json_file(SESSION_FILE);
        for (const auto& message : messages) {
            cout << COLOR_GREEN << "  " << message["role"] << ": " << message["content"] << COLOR_RESET << endl;
        }
    } else {
        cout << COLOR_RED << "No saved session found." << COLOR_RESET << endl;
    }
}

// Function for dynamic spinner
void dynamic_spinner(const string& message, const string& color) {
    const vector<string> frames = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    int frame = 0;
    while (spinner_running) {
        cout << "\r" << color << " " << frames[frame] << " " << message << "..." << COLOR_RESET << flush;
        frame = (frame + 1) % frames.size();
        this_thread::sleep_for(chrono::milliseconds(80));
    }
    cout << "\r" << string(message.length() + 10, ' ') << "\r";  // Clean up spinner line
}

// Function to show a command prompt
void show_command_prompt() {
    cout << COLOR_GRADIENT_1 << "\n┌─────[" << COLOR_RESET;
    cout << COLOR_GRADIENT_2 << "Ghostintheshell" << COLOR_RESET;
    cout << COLOR_GRADIENT_1 << "]─[" << COLOR_RESET;
    cout << COLOR_GRADIENT_2 << "Command" << COLOR_RESET;
    cout << COLOR_GRADIENT_1 << "]" << COLOR_RESET;
    cout << COLOR_GRADIENT_3 << "───────" << COLOR_RESET;
    cout << COLOR_GRADIENT_1 << "\n└→ " << COLOR_HIGHLIGHT;
}