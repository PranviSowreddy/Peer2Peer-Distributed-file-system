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
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <algorithm>
#include <random>

using namespace std;

// --- Globals ---
vector<pair<string, int>> trackers;
atomic<int> current_tracker_idx(0);
// This is the corrected line 27
static const size_t PIECE_SIZE = 512 * 1024; // As specified in the PDF
// static const size_t PIECE_SIZE = 512 * 1024; [cite_start]// As specified in the PDF [cite: 34]

// --- FIX: ADD GLOBALS FOR LOGIN STATE ---
static mutex g_credentials_mtx;
static string g_currentUser;
static string g_currentPassword;
static int g_peer_port = 0;

// --- Structs for Local File Seeding and Download Tracking ---
struct LocalFile {
    string path;
    size_t file_size;
};
static mutex local_files_mtx;
static unordered_map<string, LocalFile> local_seeding_files; // Key: group_id:filename

struct DownloadState {
    string group_id;
    string file_name;
    atomic<int> completed_pieces;
    int total_pieces;
    atomic<bool> is_complete;

    DownloadState(string gid, string fname, int t_pieces)
        : group_id(gid), file_name(fname), completed_pieces(0), total_pieces(t_pieces), is_complete(false) {}

    DownloadState(const DownloadState& other)
        : group_id(other.group_id), file_name(other.file_name), 
          completed_pieces(other.completed_pieces.load()), 
          total_pieces(other.total_pieces), is_complete(other.is_complete.load()) {}
};
static mutex downloads_mtx;
static unordered_map<string, DownloadState> ongoing_downloads; // Key: group_id:filename

// --- Hashing and Network Utilities ---
// ... (This section is unchanged) ...
string bytes_to_hex(const unsigned char* d, size_t n) {
    static const char hex[] = "0123456789abcdef";
    string s; s.reserve(n * 2);
    for (size_t i = 0; i < n; i++) {
        s.push_back(hex[(d[i] >> 4) & 0xF]);
        s.push_back(hex[d[i] & 0xF]);
    }
    return s;
}

string sha1_hex_of_buffer(const void* data, size_t len) {
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(static_cast<const unsigned char*>(data), len, digest);
    return bytes_to_hex(digest, SHA_DIGEST_LENGTH);
}

bool compute_file_hashes(const string& path, size_t& file_size, string& whole_sha, vector<string>& piece_sha) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) { perror("open"); return false; }
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); perror("fstat"); return false; }
    file_size = st.st_size;

    SHA_CTX whole_ctx;
    SHA1_Init(&whole_ctx);
    piece_sha.clear();
    vector<char> buf(65536);
    size_t total_read = 0;

    while (total_read < file_size) {
        SHA_CTX piece_ctx;
        SHA1_Init(&piece_ctx);
        size_t piece_to_read = std::min((size_t)PIECE_SIZE, (size_t)(file_size - total_read));
        size_t piece_read = 0;
        
        lseek(fd, total_read, SEEK_SET);

        while (piece_read < piece_to_read) {
            ssize_t r = read(fd, buf.data(), std::min((size_t)buf.size(), (size_t)(piece_to_read - piece_read)));
            if (r <= 0) { close(fd); return false; }
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


// --- Peer-to-Peer Listener (Seeder) Logic ---
// ... (This section is unchanged) ...
void handle_peer_connection(int cfd) {
    char buffer[1024];
    ssize_t n = recv(cfd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        close(cfd);
        return;
    }
    buffer[n] = '\0';
    
    stringstream ss(buffer);
    string command, group_id, filename;
    int piece_idx;
    ss >> command >> group_id >> filename >> piece_idx;

    if (command != "GET_PIECE" || group_id.empty() || filename.empty() || piece_idx < 0) {
        close(cfd);
        return;
    }

    string key = group_id + ":" + filename;
    LocalFile file_info;
    {
        lock_guard<mutex> lock(local_files_mtx);
        if (!local_seeding_files.count(key)) {
            close(cfd);
            return;
        }
        file_info = local_seeding_files[key];
    }
    
    off_t offset = (off_t)piece_idx * PIECE_SIZE;
    if (offset >= (off_t)file_info.file_size) {
        close(cfd);
        return;
    }

    int file_fd = open(file_info.path.c_str(), O_RDONLY);
    if (file_fd < 0) {
        close(cfd);
        return;
    }

    size_t piece_len = std::min((size_t)PIECE_SIZE, (size_t)(file_info.file_size - offset));
    vector<char> piece_data(piece_len);
    
    if (pread(file_fd, piece_data.data(), piece_len, offset) != (ssize_t)piece_len) {
        close(file_fd);
        close(cfd);
        return;
    }
    close(file_fd);
    
    uint32_t net_len = htonl(piece_len);
    send(cfd, &net_len, sizeof(net_len), 0);
    send(cfd, piece_data.data(), piece_len, 0);
    
    close(cfd);
}

void peer_listener_thread(int listen_port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("peer socket");
        return;
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("peer bind");
        close(server_fd);
        return;
    }
    if (listen(server_fd, 10) < 0) {
        perror("peer listen");
        close(server_fd);
        return;
    }
    cout << "[PEER] Listening for other clients on port " << listen_port << "\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            perror("peer accept");
            continue;
        }
        thread(handle_peer_connection, client_fd).detach();
    }
    close(server_fd);
}

// --- Peer-to-Peer Downloader Logic ---
// ... (This section is unchanged) ...
bool download_piece_from_seeder(const string& seeder_addr, const string& group, const string& filename, int idx, vector<char>& out_data) {
    size_t colon_pos = seeder_addr.find(':');
    if (colon_pos == string::npos) return false;

    string ip = seeder_addr.substr(0, colon_pos);
    int port = stoi(seeder_addr.substr(colon_pos + 1));

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock_fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock_fd);
        return false;
    }

    string request = "GET_PIECE " + group + " " + filename + " " + to_string(idx);
    send(sock_fd, request.c_str(), request.length(), 0);

    uint32_t net_len;
    if (recv(sock_fd, &net_len, sizeof(net_len), 0) != sizeof(net_len)) {
        close(sock_fd);
        return false;
    }
    uint32_t piece_len = ntohl(net_len);
    if (piece_len == 0 || piece_len > PIECE_SIZE) {
        close(sock_fd);
        return false;
    }

    out_data.resize(piece_len);
    ssize_t total_read = 0;
    while(total_read < piece_len) {
        ssize_t n = recv(sock_fd, out_data.data() + total_read, piece_len - total_read, 0);
        if (n <= 0) {
            close(sock_fd);
            return false;
        }
        total_read += n;
    }
    
    close(sock_fd);
    return true;
}


// --- High-Level Command Implementations ---
// ... (connect_to_tracker and do_upload are unchanged) ...
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
    
    // Register file for local seeding
    string key = group + ":" + filename;
    {
        lock_guard<mutex> lock(local_files_mtx);
        local_seeding_files[key] = {path, file_size};
    }
}


// --- FIX: UPDATED do_download FUNCTION ---
void do_download(const string& group, const string& filename, const string& dest_path) {
    thread([=]() {
        // Step 1: Create a new connection for this download task.
        int tracker_sock = connect_to_tracker();
        if (tracker_sock == -1) {
            cerr << "[DOWNLOAD] Failed to connect to tracker for download task.\n";
            return;
        }

        // Step 2: Read stored credentials and log in on the new connection.
        string user, pass;
        int port;
        {
            lock_guard<mutex> lock(g_credentials_mtx);
            user = g_currentUser;
            pass = g_currentPassword;
            port = g_peer_port;
        }

        if (user.empty()) {
            cout << "[DOWNLOAD] Error: Login required.\n";
            close(tracker_sock);
            return;
        }

        string login_cmd = "login " + user + " " + pass + " " + to_string(port) + "\n";
        send(tracker_sock, login_cmd.c_str(), login_cmd.length(), 0);
        char login_buff[1024];
        ssize_t n_login = recv(tracker_sock, login_buff, sizeof(login_buff) - 1, 0);
        if (n_login <= 0) {
            cerr << "[DOWNLOAD] Login failed for background task.\n";
            close(tracker_sock);
            return;
        }
        login_buff[n_login] = '\0';
        if (string(login_buff).find("successful") == string::npos) {
            cerr << "[DOWNLOAD] Login failed for background task: " << login_buff;
            close(tracker_sock);
            return;
        }
        
        // Step 3: Now that we are logged in, get the file info.
        string get_cmd = "get_file " + group + " " + filename + "\n";
        send(tracker_sock, get_cmd.c_str(), get_cmd.length(), 0);
        char buff[8192];
        ssize_t n = recv(tracker_sock, buff, sizeof(buff) - 1, 0);
        
        string logout_cmd = "logout\n";
        send(tracker_sock, logout_cmd.c_str(), logout_cmd.length(), 0);
        close(tracker_sock);

        if (n <= 0) {
            cerr << "[DOWNLOAD] Failed to get file info from tracker.\n";
            return;
        }
        buff[n] = '\0';
        
        string response(buff);
        stringstream ss(response);
        string command;
        ss >> command;
        if (command != "FILEINFO") {
            cerr << "[DOWNLOAD] Error: " << response;
            return;
        }

        size_t file_size;
        string whole_sha;
        int num_pieces;
        ss >> file_size >> whole_sha >> num_pieces;

        vector<string> piece_hashes(num_pieces);
        for (int i = 0; i < num_pieces; ++i) ss >> piece_hashes[i];

        string seeders_tag;
        ss >> seeders_tag;
        vector<string> seeders;
        if (seeders_tag == "SEEDERS") {
            string seeder_addr;
            while (ss >> seeder_addr) {
                seeders.push_back(seeder_addr);
            }
        }
        if (seeders.empty()) {
            cerr << "[DOWNLOAD] No seeders available for this file.\n";
            return;
        }

        cout << "[DOWNLOAD] Starting download for " << filename << " (" << file_size << " bytes, " << num_pieces << " pieces)\n";

        string part_path = dest_path + ".part";
        int out_fd = open(part_path.c_str(), O_RDWR | O_CREAT, 0644);
        if (out_fd < 0) {
            perror("open part file");
            return;
        }
        if (ftruncate(out_fd, file_size) < 0) {
            perror("ftruncate part file");
            close(out_fd);
            return;
        }

        string dl_key = group + ":" + filename;
        {
            lock_guard<mutex> lock(downloads_mtx);
            ongoing_downloads.emplace(piecewise_construct, make_tuple(dl_key), make_tuple(group, filename, num_pieces));
        }

        vector<atomic<int>> piece_status(num_pieces);
        atomic<int> completed_count(0);

        auto worker_lambda = [&]() {
            while (completed_count.load() < num_pieces) {
                int piece_idx = -1;
                for (int i = 0; i < num_pieces; ++i) {
                    int expected = 0;
                    if (piece_status[i].compare_exchange_strong(expected, 1)) {
                        piece_idx = i;
                        break;
                    }
                }

                if (piece_idx == -1) {
                    this_thread::sleep_for(chrono::milliseconds(100));
                    continue;
                }

                bool piece_ok = false;
                vector<string> shuffled_seeders = seeders;
                random_device rd;
                mt19937 g(rd());
                shuffle(shuffled_seeders.begin(), shuffled_seeders.end(), g);

                for (const auto& seeder : shuffled_seeders) {
                    vector<char> piece_data;
                    if (download_piece_from_seeder(seeder, group, filename, piece_idx, piece_data)) {
                        if (sha1_hex_of_buffer(piece_data.data(), piece_data.size()) == piece_hashes[piece_idx]) {
                            off_t offset = (off_t)piece_idx * PIECE_SIZE;
                            if (pwrite(out_fd, piece_data.data(), piece_data.size(), offset) == (ssize_t)piece_data.size()) {
                                piece_status[piece_idx] = 2;
                                completed_count++;
                                {
                                    lock_guard<mutex> lock(downloads_mtx);
                                    ongoing_downloads.at(dl_key).completed_pieces++;
                                }
                                piece_ok = true;
                                break;
                            }
                        }
                    }
                }

                if (!piece_ok) {
                    piece_status[piece_idx] = 0;
                }
            }
        };
        
        unsigned num_workers = min((unsigned)seeders.size(), (unsigned)max(4, (int)thread::hardware_concurrency()));
        num_workers = min(num_workers, (unsigned)num_pieces);
        vector<thread> workers;
        for (unsigned i = 0; i < num_workers; ++i) {
            workers.emplace_back(worker_lambda);
        }
        for (auto& t : workers) {
            t.join();
        }

        if (completed_count.load() != num_pieces) {
            cerr << "\n[DOWNLOAD] Download failed for " << filename << ". Could not retrieve all pieces.\n";
            close(out_fd);
            unlink(part_path.c_str());
            return;
        }
        
        string final_hash;
        size_t final_size;
        vector<string> ignored_pieces;
        if (!compute_file_hashes(part_path, final_size, final_hash, ignored_pieces) || final_hash != whole_sha) {
             cerr << "\n[DOWNLOAD] Final hash verification failed for " << filename << ". Deleting corrupt file.\n";
             close(out_fd);
             unlink(part_path.c_str());
             return;
        }

        close(out_fd);
        if (rename(part_path.c_str(), dest_path.c_str()) != 0) {
            perror("rename failed");
            unlink(part_path.c_str());
            return;
        }

        cout << "\n[DOWNLOAD] Download complete and verified: " << dest_path << "\n";
        {
            lock_guard<mutex> lock(downloads_mtx);
            ongoing_downloads.at(dl_key).is_complete = true;
        }
        
        string key = group + ":" + filename;
        {
            lock_guard<mutex> lock(local_files_mtx);
            local_seeding_files[key] = {dest_path, file_size};
        }
        
        int final_tracker_sock = connect_to_tracker();
        if(final_tracker_sock != -1) {
            string final_login_cmd = "login " + user + " " + pass + " " + to_string(port) + "\n";
            send(final_tracker_sock, final_login_cmd.c_str(), final_login_cmd.length(), 0);
            char final_login_buff[1024];
            recv(final_tracker_sock, final_login_buff, sizeof(final_login_buff) - 1, 0);
            
            do_upload(final_tracker_sock, group, dest_path);
            
            send(final_tracker_sock, logout_cmd.c_str(), logout_cmd.length(), 0);
            close(final_tracker_sock);
        }
    }).detach();
}

void do_show_downloads() {
    lock_guard<mutex> lock(downloads_mtx);
    if (ongoing_downloads.empty()) {
        cout << "No downloads in progress or completed.\n";
        return;
    }
    for (const auto& pair : ongoing_downloads) {
        const auto& state = pair.second;
        if (state.is_complete) {
            cout << "[C] [" << state.group_id << "] " << state.file_name << "\n";
        } else {
            cout << "[D] [" << state.group_id << "] " << state.file_name 
                 << " (" << state.completed_pieces << "/" << state.total_pieces << " pieces)\n";
        }
    }
}

// --- Main Program Logic ---
int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: ./p2p_client <tracker_info_file> <client_listen_port>\n";
        return 1;
    }

    ifstream infile(argv[1]);
    if (!infile) {
        cerr << "Error: Cannot open tracker info file: " << argv[1] << endl;
        return 1;
    }
    string line;
    while(getline(infile, line)) {
        string ip;
        int port;
        stringstream ss(line);
        ss >> ip >> port >> port;
        if (!ss.fail()) {
            trackers.push_back({ip, port});
        }
    }
    if (trackers.empty()) {
        cerr << "[CLIENT] No valid trackers found in file.\n";
        return 1;
    }
    
    // --- FIX: STORE PEER PORT GLOBALLY ---
    g_peer_port = stoi(argv[2]);
    thread(peer_listener_thread, g_peer_port).detach();

    int sock = connect_to_tracker();
    if (sock == -1) {
        cerr << "[CLIENT] Failed to connect to any tracker.\n";
        return 1;
    }

    string current_pass_temp; // Temporary storage for password

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
        } else if (command == "download_file") {
            string group, filename, dest_path;
            ss >> group >> filename >> dest_path;
            if (group.empty() || filename.empty() || dest_path.empty()) {
                cout << "Usage: download_file <group_id> <file_name> <destination_path>\n";
            } else {
                do_download(group, filename, dest_path);
            }
            continue;
        } else if (command == "show_downloads") {
            do_show_downloads();
            continue;
        }
        else {
             // --- FIX: MODIFY LOGIN AND HANDLE LOGOUT ---
            if (command == "login") {
                string user, pass;
                ss >> user >> pass;
                current_pass_temp = pass;
                line_str = command + " " + user + " " + pass + " " + to_string(g_peer_port);
            } else if (command == "logout") {
                lock_guard<mutex> lock(g_credentials_mtx);
                g_currentUser.clear();
                g_currentPassword.clear();
            }
            line_str += "\n";
            send(sock, line_str.c_str(), line_str.length(), 0);
        }

        char resp_buff[8192];
        ssize_t n = recv(sock, resp_buff, sizeof(resp_buff) - 1, 0);
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
        resp_buff[n] = '\0';
        cout << "[SERVER] " << resp_buff;

        // --- FIX: STORE CREDENTIALS ON SUCCESSFUL LOGIN ---
        if (command == "login" && string(resp_buff).find("successful") != string::npos) {
            stringstream user_ss(line_str);
            string temp_cmd, user_str;
            user_ss >> temp_cmd >> user_str;
            lock_guard<mutex> lock(g_credentials_mtx);
            g_currentUser = user_str;
            g_currentPassword = current_pass_temp;
        }
    }

    if (sock != -1) close(sock);
    cout << "[CLIENT] Exiting.\n";
    return 0;
}