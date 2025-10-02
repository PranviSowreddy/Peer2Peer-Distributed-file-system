#include "client_handler.h"
#include "details.h"
#include "sync.h"
#include <unistd.h>
#include <sys/socket.h>
#include <vector>
#include <arpa/inet.h>

void* client_handler(void* arg) {
    int socket_fd = *(int*)arg;
    delete (int*)arg;

    // Get client address for login
    sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    getpeername(socket_fd, (struct sockaddr*)&client_addr, &len);
    string client_ip = inet_ntoa(client_addr.sin_addr);
    int client_port = ntohs(client_addr.sin_port);
    string client_addr_str = client_ip + ":" + to_string(client_port);

    char buff[4096];
    string current_user;

    while (true) {
        ssize_t r = recv(socket_fd, buff, sizeof(buff) - 1, 0);
        if (r <= 0) {
            break; // Client disconnected
        }
        buff[r] = '\0';
        string client_request(buff);

        // Remove trailing newline if it exists
        if (!client_request.empty() && client_request.back() == '\n') {
            client_request.pop_back();
        }

        stringstream ss(client_request);
        string command;
        ss >> command;
        string response = "Unknown command.";

        if (command == "create_user") {
            string userName, password;
            ss >> userName >> password;
            response = create_user(userName, password);
            // Example of how sync would be used
            // send_sync("SYNC|create_user|" + userName + "|" + password);
        } else if (command == "login") {
            string userName, password;
            ss >> userName >> password;
            response = login(userName, password, client_addr_str);
            if (response.find("successful") != string::npos) {
                current_user = userName;
            }
        } else if (!current_user.empty()) { // Commands requiring login
            if (command == "create_group") {
                string groupId;
                ss >> groupId;
                response = create_group(groupId, current_user);
            } else if (command == "join_group") {
                string groupId;
                ss >> groupId;
                response = join_group(groupId, current_user);
            } else if (command == "list_groups") {
                response = list_groups();
            } else if (command == "list_requests") {
                string groupId;
                ss >> groupId;
                response = list_requests(groupId, current_user);
            } else if (command == "accept_request") {
                string groupId, userId;
                ss >> groupId >> userId;
                response = accept_request(groupId, userId, current_user);
            } else if (command == "upload_file") {
                string group_id, filename, whole_sha1;
                size_t file_size;
                int num_pieces;
                ss >> group_id >> filename >> file_size >> whole_sha1 >> num_pieces;
                
                vector<string> piece_hashes;
                if(num_pieces > 0){
                    piece_hashes.resize(num_pieces);
                    for(int i = 0; i < num_pieces; ++i) {
                        ss >> piece_hashes[i];
                    }
                }
                response = upload_file(group_id, filename, file_size, whole_sha1, piece_hashes, current_user);

            } else if (command == "list_files") {
                string groupId;
                ss >> groupId;
                response = list_files(groupId, current_user);
            } else if (command == "get_file") {
                string groupId, filename;
                ss >> groupId >> filename;
                response = get_file(groupId, filename, current_user);
            }
            else if (command == "logout") {
                response = logout(current_user);
                current_user.clear();
            }
        } else {
            response = "Login required.";
        }
        
        response += "\n";
        send(socket_fd, response.c_str(), response.size(), 0);
    }

    if (!current_user.empty()) {
        logout(current_user); // Logout user on disconnect
    }
    close(socket_fd);
    return nullptr;
}
