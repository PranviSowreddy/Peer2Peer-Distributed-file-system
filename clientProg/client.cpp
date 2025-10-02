#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <readline/readline.h>
#include <readline/history.h>

using namespace std;

// --- Globals ---
vector<pair<string, int>> trackers;
atomic<int> current_tracker_idx(0);
static const size_t PIECE_SIZE = 512 * 1024;

// --- Hashing and Network Utilities ---

string bytes_to_hex(const unsigned char* d, size_t n) {
    static const char hex[] = "0123456789abcdef";
    string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; i++) {
        s.push_back(hex[(d[i] >> 4) & 0xF]);
        s.push_back(hex[d[i] & 0xF]);
    }
    return s;
}

// Correctly hashes a file piece-by-piece and for the whole file.
bool compute_file_hashes(const string& path, size_t& file_size, string& whole_sha, vector<string>& piece_sha) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        perror("open");
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        perror("fstat");
        return false;
    }
    file_size = st.st_size;

    SHA_CTX whole_ctx;
    SHA1_Init(&whole_ctx);
    piece_sha.clear();
    vector<char> buf(65536);
    size_t total_read = 0;

    while (total_read < file_size) {
        SHA_CTX piece_ctx;
        SHA1_Init(&piece_ctx);
        size_t piece_to_read = min((size_t)PIECE_SIZE, file_size - total_read);
        size_t piece_read = 0;
        
        lseek(fd, total_read, SEEK_SET); // Position file descriptor for the start of the piece

        while (piece_read < piece_to_read) {
            ssize_t r = read(fd, buf.data(), min(buf.size(), piece_to_read - piece_read));
            if (r <= 0) {
                close(fd);
                return false;
            }
            SHA1_Update(&piece_ctx, (const unsigned char*)buf.data(), r);
            SHA1_Update(&whole_ctx, (const unsigned char*)buf.data(), r);
            piece_read += r;
        }
        unsigned char pd[SHA_DIGEST_LENGTH];
        SHA1_Final(pd, &piece_ctx);
        piece_sha.push_back(bytes_to_hex(pd, SHA_DIGEST_LENGTH));
        total_read += piece_to_read;
    }

    unsigned char wd[SHA_DIGEST_LENGTH];
    SHA1_Final(wd, &whole_ctx);
    whole_sha = bytes_to_hex(wd, SHA_DIGEST_LENGTH);
    close(fd);
    return true;
}

// --- Main Program Logic ---

int connect_to_tracker() {
    for (size_t i = 0; i < trackers.size(); ++i) {
        int idx = (current_tracker_idx.load() + i) % trackers.size();
        const auto& tracker_addr = trackers[idx];

        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) continue;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(tracker_addr.second);
        inet_pton(AF_INET, tracker_addr.first.c_str(), &addr.sin_addr);

        if (connect(sock_fd, (sockaddr*)&addr, sizeof(addr)) == 0) {
            cout << "[CLIENT] Connected to tracker " << tracker_addr.first << ":" << tracker_addr.second << "\n";
            current_tracker_idx.store(idx);
            return sock_fd;
        }
        close(sock_fd);
    }
    return -1;
}

// This function builds and sends the complex upload_file command.
void do_upload(int sock, const string& group, const string& path) {
    size_t file_size;
    string whole_sha;
    vector<string> piece_sha;

    cout << "[CLIENT] Calculating hashes for " << path << "...\n";
    if (!compute_file_hashes(path, file_size, whole_sha, piece_sha)) {
        cerr << "[CLIENT] Error: Could not process file.\n";
        return;
    }
    cout << "[CLIENT] Hash calculation complete.\n";
    
    size_t last_slash = path.find_last_of("/\\");
    string filename = (last_slash == string::npos) ? path : path.substr(last_slash + 1);

    stringstream command;
    command << "upload_file " << group << " " << filename << " " << file_size << " " << whole_sha << " " << piece_sha.size();
    for(const auto& hash : piece_sha) {
        command << " " << hash;
    }
    command << "\n";

    string cmd_str = command.str();
    send(sock, cmd_str.c_str(), cmd_str.length(), 0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: ./client <tracker_info_file> [client_listen_port]\n";
        return 1;
    }

    ifstream infile(argv[1]);
    if (!infile) {
        cerr << "Error: Cannot open tracker info file: " << argv[1] << endl;
        return 1;
    }
    string line;
    while(getline(infile, line)) {
        size_t pos = line.find(':');
        if (pos != string::npos) {
            try {
                trackers.push_back({line.substr(0, pos), stoi(line.substr(pos + 1))});
            } catch (const std::exception& e) {
                 cerr << "Warning: Skipping invalid line in tracker file: " << line << endl;
            }
        }
    }
    if (trackers.empty()) {
        cerr << "[CLIENT] No valid trackers found in file.\n";
        return 1;
    }

    int sock = connect_to_tracker();
    if (sock == -1) {
        cerr << "[CLIENT] Failed to connect to any tracker.\n";
        return 1;
    }

    char* input;
    while ((input = readline("$ ")) != nullptr) {
        string line_str(input);
        free(input);
        if (line_str.empty()) continue;
        add_history(line_str.c_str());
        if (line_str == "quit" || line_str == "exit") break;
        
        stringstream ss(line_str);
        string command;
        ss >> command;

        if (command == "upload_file") {
            string group, path;
            ss >> group >> path;
            if (group.empty() || path.empty()) {
                cout << "Usage: upload_file <group_id> <file_path>\n";
            } else {
                do_upload(sock, group, path);
            }
        } else {
             line_str += "\n";
             send(sock, line_str.c_str(), line_str.length(), 0);
        }

        char buff[4096];
        ssize_t n = recv(sock, buff, sizeof(buff) - 1, 0);
        if (n <= 0) {
            cout << "[CLIENT] Connection to tracker lost. Attempting to reconnect...\n";
            close(sock);
            sock = connect_to_tracker();
            if (sock == -1) {
                cerr << "[CLIENT] Reconnect failed. Exiting.\n";
                break;
            }
            continue;
        }
        buff[n] = '\0';
        cout << "[SERVER] " << buff;
    }

    if (sock != -1) close(sock);
    cout << "[CLIENT] Exiting.\n";
    return 0;
}

