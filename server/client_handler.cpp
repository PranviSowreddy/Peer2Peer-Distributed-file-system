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
        ssize_t r=recv(socket_fd,buff,sizeof(buff)-1,0);
        if(r<=0)
        {
            perror("read");
            break;
        }
        buff[r]='\0';
        stringstream ss(buff);
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
            else {
            response="Not logged in";
            }
        }

        response+="\n";
        send(socket_fd,response.c_str(),response.size(),0);

    }
    close(socket_fd);
    return nullptr;

}