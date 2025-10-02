#include <cstddef>
#include<string>
#include<sys/socket.h>
#include<unistd.h>
#include<sstream>
#include "sync.h"
#include "client_handler.h"
#include "details.h"
using namespace std;

pthread_mutex_t sync_mutex=PTHREAD_MUTEX_INITIALIZER;   


void* client_handler(void* arg)
{
    int socket_fd=*(int*)arg;
    delete (int*)arg;

    char buff[1024];
    string current_user;
    while(1)
    {
        // Use a loop to handle partial reads, common in stream sockets
        string client_request;
        bool done_reading = false;
        
        while (!done_reading) {
            ssize_t r = recv(socket_fd, buff, sizeof(buff) - 1, 0);
            if (r <= 0) {
                perror("recv");
                goto exit_handler; // Break out of the handler
            }
            buff[r] = '\0';
            client_request += buff;

            // Check for the newline terminator
            if (client_request.back() == '\n') {
                done_reading = true;
                client_request.pop_back(); // Remove the trailing newline
            } else if (r < sizeof(buff) - 1) {
                // If we didn't fill the buffer, assume the sender stopped. 
                // This is a common simplification in command-response.
                done_reading = true;
            }
        }
        
        if (client_request.empty()) continue;

        stringstream ss(client_request);
        string command;
        ss>>command;
        string response="Unknown command ";

        if(command=="create_user")
        {
            string userName,password;
            ss>>userName>>password;
            response=create_user(userName,password);

            string peerSync="SYNC|create_user|"+userName+"|"+password+"|";
            send_sync(peerSync);
        }
        else if(command=="login")
        {
            string userName,password;
            ss>>userName>>password;
            response=login(userName,password);
            current_user=userName;
        }
        else if(command=="create_group")
        {
            string groupId;
            ss>>groupId;
            if(current_user.empty())
            {
                response="Login required";
            }
            else{
            response=create_group(groupId,current_user);
            string peerSync="SYNC|create_group|"+groupId+"|"+current_user+"|";
            send_sync(peerSync);
            }
        }
        else if(command=="join_group")
        {
            string groupId;
            ss>>groupId;
            if(current_user.empty())
            {
                response="Login required";
            }
            else{
            response=join_group(groupId,current_user);
            string peerSync="SYNC|join_group|"+groupId+"|"+current_user+"|";
            send_sync(peerSync);
            }
        }
        else if(command=="list_groups")
        {
            response=list_groups();
        }
        else if(command=="list_requests")
        {
            string groupId;
            ss>>groupId;
            response=list_requests(groupId);
        }
        else if(command=="accept_request")
        {
            string groupId,userId;
            ss>>groupId>>userId;
            response=accept_request(groupId,userId);
            string peerSync="SYNC|accept_request|"+groupId+"|"+userId+"|";
            send_sync(peerSync);
        }
        else if(command=="leave_group")
        {
            string groupId;
            ss>>groupId;
            if(current_user.empty())
            {
                response="Login required";
            }
            else{
            response=leave_group(groupId,current_user);
            string peerSync="SYNC|leave_group|"+groupId+"|"+current_user+"|";
            send_sync(peerSync);
            }

        }
        else if(command=="logout")
        {
            if(!current_user.empty())
            {
                response=logout(current_user);
                current_user.clear();
            }
            else
                response = "Not logged in";
        }
       else if (command == "upload_file")
        {
            // Expected: upload_file <group> <filename> <filesize> <whole_sha1> <num_pieces> <piece_sha1_1> ... <piece_sha1_n> SEEDERS <num_seeders> <seeder1> <seeder2> ...
            if (current_user.empty())
            {
                response = "Login required";
            }
            else
            {
                string groupId, filename, whole_sha1;
                size_t fileSize;
                int num_pieces;
                ss >> groupId >> filename >> fileSize >> whole_sha1 >> num_pieces;
                
                if (groupId.empty() || filename.empty() || whole_sha1.empty() || num_pieces <= 0)
                {
                    response = "ERR usage: upload_file <group> <filename> <filesize> <whole_sha1> <num_pieces> <piece_sha1 ...> SEEDERS <num_seeders> <seeder ...>";
                }
                else
                {
                    vector<string> pieceShaKeys(num_pieces);
                    for (int i = 0; i < num_pieces; ++i)
                        ss >> pieceShaKeys[i];

                    string seeders_label;
                    ss >> seeders_label; // should be "SEEDERS"
                    int num_seeders;
                    ss >> num_seeders;
                    vector<uint64_t> seeders(num_seeders);
                    for (int i = 0; i < num_seeders; ++i)
                        ss >> seeders[i];

                    response = upload_file(groupId, filename, whole_sha1, pieceShaKeys, fileSize, seeders);

                    // Optional: sync to other peers
                    string peerSync = "SYNC|upload_file|" + groupId + "|" + filename + "|" + whole_sha1 + "|";
                    send_sync(peerSync);
                }
            }
        }

        else if(command=="list_files")
        {
             string groupId;
            ss >> groupId;
            response = list_files(groupId);
        }
        else if (command == "get_file")
        {
            string groupId, filename;
            ss >> groupId >> filename;
            if (current_user.empty())
            {
                response = "Login required";
            }
            else
            {
                // NOTE: get_file response is a long string with FILEINFO + seeders, 
                // but no raw file data is sent from the tracker
                response = get_file(groupId, filename); 
            }
        }
        
        response += "\n";
        send(socket_fd, response.c_str(), response.size(), 0);
    }

exit_handler:
    close(socket_fd);
    return nullptr;

}