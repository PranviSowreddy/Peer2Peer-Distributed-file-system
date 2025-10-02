#include <cstddef>
#include <cstdio>
#include<iostream>
#include <pthread.h>
#include<sstream>
#include <sys/_types/_socklen_t.h>
#include <sys/_types/_ssize_t.h>
#include <sys/fcntl.h>
#include<vector>
#include<unistd.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>

#include "details.h"
#include "sync.h"

using namespace std;

struct Tracker
{
    string ip;
    int syncPort;
    int clientPort;
};

static vector<Tracker>g_trackers;
static pthread_mutex_t sync_mutex=PTHREAD_MUTEX_INITIALIZER;
static int peer_fd_in=-1;
static int peer_fd_out=-1;
static int self_id=-1;

static void read_input(const string& file)
{
    g_trackers.clear();
    int fd=open(file.c_str(),O_RDONLY);
    if(fd<0)
    {
        perror("open");
        return;
    }
    char buff[1024];
    int n=read(fd,buff,sizeof(buff)-1);
    if(n<=0)
    {
        if(n==-1)
        perror("read");
        close(fd);
        return;
    }
    buff[n]='\0';
    close(fd);

    stringstream ss(buff);
    string line;
    while(getline(ss,line))
    {
        if(line.empty())
        continue;
        string ip;
        int syncPort,clientPort;
        stringstream ss(line);
        ss>>ip>>syncPort>>clientPort;
        if(!ss.fail())
        g_trackers.push_back({ip,syncPort,clientPort});
    }
}

int get_client_port(int trackerId)
{
    if(trackerId<0 || trackerId>=(int)g_trackers.size())
    return -1;
    return g_trackers[trackerId].clientPort;
}

void send_sync(const string& msg)
{
    pthread_mutex_lock(&sync_mutex);
    string new_msg=msg;
    if(new_msg.empty() || new_msg.back()!='\n')
    new_msg.push_back('\n');

    if(peer_fd_in!=-1)
    {
        ssize_t n=send(peer_fd_in,new_msg.c_str(),new_msg.size(),0);
        if(n<=0){
        cerr<<"[SYNC] Failed to send (incoming)\n";
        close(peer_fd_in);
        peer_fd_in=-1;
        }
    }
    if(peer_fd_out!=-1)
    {
        ssize_t n=send(peer_fd_out,new_msg.c_str(),new_msg.size(),0);
        if(n<=0){
        cerr<<"[SYNC] Failed to send (outgoing)\n";
        close(peer_fd_out);
        peer_fd_out=-1;
        }
    }
     pthread_mutex_unlock(&sync_mutex);
}

static void* sync_listen(void* arg)
{ 

    int listen_port=g_trackers[self_id].syncPort;
    int server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0)
    {
        perror("socket");
        return nullptr;
    }
    int opt=1;
    setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(listen_port);
    //use inet_pton later
    addr.sin_addr.s_addr=INADDR_ANY;

    if(::bind(server_fd,(struct sockaddr*)&addr,sizeof(addr))<0)
    {
        perror("bind");
        close(server_fd);
        return nullptr;
    }

    if(listen(server_fd,5)<0)
    {
        perror("listen");
        close(server_fd);
        return nullptr;
    }
    cout<<"[SYNC] Listening on port "<<listen_port<<endl;

    while(1){
        sockaddr_in peerAddr{};
        socklen_t len=sizeof(peerAddr);
        int conn=accept(server_fd,(struct sockaddr*)&peerAddr,&len);
        if(conn<0)
        {
        perror("accept");
        sleep(1);
        continue;
        }

        cout<<"[SYNC] Connection established (incoming) "<<endl;

        pthread_mutex_lock(&sync_mutex);
        if(peer_fd_in!=-1 && peer_fd_in!=conn)
        {
            close(peer_fd_in);
        }
        peer_fd_in=conn;
        pthread_mutex_unlock(&sync_mutex);

        string rem;
        char buff[1024];
        while(1)
        {
            ssize_t r=recv(conn,buff,sizeof(buff)-1,0);
            if(r<=0)
            {
                cout<<"[SYNC] incoming connection closed"<<endl;
                close(conn);
                pthread_mutex_lock(&sync_mutex);
                peer_fd_in=-1;
                pthread_mutex_unlock(&sync_mutex);
                break;
            }
            buff[r]='\0';
            rem.append(buff,r);

            size_t pos;
            while((pos=rem.find('\n'))!=string::npos)
            {
                string command=rem.substr(0,pos);
                rem.erase(0,pos+1);
                if(command.empty())
                continue;

                stringstream ss(command);
                string tag,txt;
                getline(ss,tag,'|');
                if(tag!="SYNC")
                continue;
                getline(ss,txt,'|');
                if(txt=="create_user")
                {
                    string user,password;
                    getline(ss,user,'|');
                    getline(ss,password,'|');
                    create_user(user,password);
                }
                else if(txt=="create_group")
                {
                    string groupId,userId;
                    getline(ss,groupId,'|');
                    getline(ss,userId,'|');
                    create_group(groupId,userId);
                }
                else if(txt=="join_group")
                {
                    string groupId,userId;
                    getline(ss,groupId,'|');
                    getline(ss,userId,'|');
                    join_group(groupId,userId);
                }
                else if(txt=="leave_group")
                {
                    string groupId,userId;
                    getline(ss,groupId,'|');
                    getline(ss,userId,'|');
                    leave_group(groupId,userId);
                }
                else if(txt=="accept_request")
                {
                    string groupId,userId;
                    getline(ss,groupId,'|');
                    getline(ss,userId,'|');
                    
                    // --- FIX STARTS HERE ---
                    // The function needs the owner's ID for its authorization check.
                    // For a sync operation, we can look it up and provide it.
                    pthread_mutex_lock(&state_mutex);
                    string ownerId = groupDetails.count(groupId) ? groupDetails[groupId].owner : "";
                    pthread_mutex_unlock(&state_mutex);

                    if (!ownerId.empty()) {
                        accept_request(groupId, userId, ownerId);
                    }
                    else {
                    cerr<<"[SYNC] unknown command: "<<txt<<"\n";
                    }
                }
            }
        }
    }
    close(server_fd);
    return nullptr;
}

static void* sync_accept(void* arg)
{
    int peer_id=1-self_id;
    string ipAddr=g_trackers[peer_id].ip;
    int accept_port=g_trackers[peer_id].syncPort;

    while(1)
    {
        int sock_fd=socket(AF_INET,SOCK_STREAM,0);
        if(sock_fd<0)
        {
            perror("socket");
            return nullptr;
        }
        sockaddr_in addr{};
        addr.sin_family=AF_INET;
        addr.sin_port=htons(accept_port);
        inet_pton(AF_INET,ipAddr.c_str(),&addr.sin_addr);

        if(connect(sock_fd,(struct sockaddr*)&addr,sizeof(addr))==0)
        {
            pthread_mutex_lock(&sync_mutex);
            peer_fd_out=sock_fd;
            pthread_mutex_unlock(&sync_mutex);
            cout<<"[SYNC] Connection established (outgoing)"<<endl;
            return nullptr;

        }
        close(sock_fd);
        sleep(2);

    }
    return nullptr;
}

void start_sync(const string&file,int trackerId)
{
    read_input(file);
    if(g_trackers.empty())
    {
        cerr<<"[SYNC] Tracker info file missing or empty: "<<file<<endl;
        exit(1);
    }
    self_id=trackerId;
    if(self_id<0 || self_id>=(int)g_trackers.size())
    {
        cerr<<"[SYNC] Invalid tracker_id "<<self_id<<endl;
        exit(1);
    }
   
    pthread_t listener_thread,connector_thread;
    pthread_create(&listener_thread,nullptr,sync_listen,nullptr);
    pthread_detach(listener_thread);
    pthread_create(&connector_thread,nullptr,sync_accept,nullptr);
    pthread_detach(connector_thread);
}
