// client_handler.cpp
#include <cstddef>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>
#include "sync.h"
#include <arpa/inet.h>
#include <vector>
#include "client_handler.h"
#include "details.h"
using namespace std;

// NOTE: sync_mutex is defined/used in sync.cpp; don't redefine here.
// pthread_mutex_t sync_mutex=PTHREAD_MUTEX_INITIALIZER;   

void* client_handler(void* arg)
{
    int socket_fd = *(int*)arg;
    delete (int*)arg;

    sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    if (getpeername(socket_fd, (struct sockaddr*)&client_addr, &len) < 0) {
        perror("getpeername");
    }
    string client_ip = inet_ntoa(client_addr.sin_addr);

    char buff[4096];
    string current_user;

    while (1)
    {
        ssize_t r = recv(socket_fd, buff, sizeof(buff) - 1, 0);
        if (r <= 0)
        {
            // client disconnected or error
            if (r < 0) perror("recv");
            break;
        }
        buff[r] = '\0';
        string client_request(buff);

        // remove trailing newline if present
        if (!client_request.empty() && client_request.back() == '\n') {
            client_request.pop_back();
        }

        // use client_request for parsing (not raw buff)
        stringstream ss(client_request);
        string command;
        ss >> command;
        string response = "Unknown command";

        if (command == "create_user")
        {
            string userName, password;
            ss >> userName >> password;
            response = create_user(userName, password);

            // sync to peer trackers
            string peerSync = "SYNC|create_user|" + userName + "|" + password + "|";
            send_sync(peerSync);
        }
        else if (command == "login")
        {
            string userName, password, listen_port_str;
            ss >> userName >> password >> listen_port_str;
            string client_listen_addr = client_ip + ":" + listen_port_str;
            response = login(userName, password, client_listen_addr);
            if (response.find("successful") != string::npos || response.find("Ok logged in") != string::npos) {
                current_user = userName;
            }

            // sync login to other trackers (so they know client address)
            string peerSync = "SYNC|login|" + userName + "|" + client_listen_addr + "|";
            send_sync(peerSync);
        }
        else if (command == "create_group")
        {
            string groupId;
            ss >> groupId;
            if (current_user.empty())
            {
                response = "Login required";
            }
            else
            {
                response = create_group(groupId, current_user);
                string peerSync = "SYNC|create_group|" + groupId + "|" + current_user + "|";
                send_sync(peerSync);
            }
        }
        else if (command == "join_group")
        {
            string groupId;
            ss >> groupId;
            if (current_user.empty())
            {
                response = "Login required";
            }
            else
            {
                response = join_group(groupId, current_user);
                string peerSync = "SYNC|join_group|" + groupId + "|" + current_user + "|";
                send_sync(peerSync);
            }
        }
        else if (command == "list_groups")
        {
            response = list_groups();
        }
        else if (command == "list_requests")
        {
            string groupId;
            ss >> groupId;
            // list_requests requires owner authorization
            response = list_requests(groupId, current_user);
        }
        else if (command == "accept_request")
        {
            string groupId, userId;
            ss >> groupId >> userId;
            response = accept_request(groupId, userId, current_user);
            if (response.find("accepted") != string::npos || response.find("OK") != string::npos) {
                string peerSync = "SYNC|accept_request|" + groupId + "|" + userId + "|";
                send_sync(peerSync);
            }
        }
        else if (command == "leave_group")
        {
            string groupId;
            ss >> groupId;
            if (current_user.empty())
            {
                response = "Login required";
            }
            else
            {
                response = leave_group(groupId, current_user);
                string peerSync = "SYNC|leave_group|" + groupId + "|" + current_user + "|";
                send_sync(peerSync);
            }
        }
        else if (command == "logout")
        {
            if (!current_user.empty())
            {
                response = logout(current_user);
                // sync logout
                string peerSync = "SYNC|logout|" + current_user + "|";
                send_sync(peerSync);

                current_user.clear();
            }
            else {
                response = "Not logged in";
            }
        }
        else if (command == "upload_file")
        {
            // require login to upload
            if (current_user.empty()) {
                response = "Login required";
            } else {
                string group_id, filename, whole_sha1;
                size_t file_size;
                int num_pieces;
                ss >> group_id >> filename >> file_size >> whole_sha1 >> num_pieces;

                vector<string> piece_hashes;
                if (num_pieces > 0) {
                    piece_hashes.resize(num_pieces);
                    for (int i = 0; i < num_pieces; ++i) {
                        ss >> piece_hashes[i];
                    }
                }

                response = upload_file(group_id, filename, file_size, whole_sha1, piece_hashes, current_user);

                // build sync message containing piece hashes and seeders (seeder ids)
                string key = make_file_key(group_id, filename);
                // it's possible fileDetails[key] now exists
                FileInfo info;
                bool haveInfo = false;
                {
                    // access protected map (but upload_file already locked/unlocked)
                    // we still check presence without locking long-term
                    pthread_mutex_lock(&state_mutex);
                    if (fileDetails.count(key)) {
                        info = fileDetails[key];
                        haveInfo = true;
                    }
                    pthread_mutex_unlock(&state_mutex);
                }

                if (haveInfo) {
                    string peerSync = "SYNC|upload_file|" + group_id + "|" + filename + "|" + to_string(info.file_size) + "|" + info.whole_file_sha1 + "|";
                    // piece hashes
                    for (const auto &ph : info.piece_hashes) {
                        peerSync += ph + "|";
                    }
                    // seeders (ids)
                    peerSync += "SEEDERS|";
                    for (const auto &seeder : info.seeders) {
                        // store seeder id (username). receiver can map to client_addresses if known.
                        peerSync += seeder + "|";
                    }
                    send_sync(peerSync);
                }
            }
        }
        else if (command == "list_files")
        {
            string groupId;
            ss >> groupId;
            response = list_files(groupId, current_user);
        }
        else if (command == "get_file")
        {
            string groupId, filename;
            ss >> groupId >> filename;
            response = get_file(groupId, filename, current_user);
        }
        else if (command == "stop_share")
        {
            string groupId, filename;
            ss >> groupId >> filename;
            response = stop_share(groupId, filename, current_user);
            // optionally sync stop_share
            string peerSync = "SYNC|stop_sharing|" + groupId + "|" + filename + "|" + current_user + "|";
            send_sync(peerSync);
        }

        response += "\n";
        send(socket_fd, response.c_str(), response.size(), 0);
    }

    // on disconnect, if user logged in, remove entry (logout)
    if (!current_user.empty()) {
        logout(current_user);
        string peerSync = "SYNC|logout|" + current_user + "|";
        send_sync(peerSync);
        current_user.clear();
    }

    close(socket_fd);
    return nullptr;
}
