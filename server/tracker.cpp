#include <cstddef>
#include <cstdio>
#include <iostream>
#include <pthread.h>
#include <signal.h>

#include <sys/_types/_socklen_t.h>
#include <sys/_types/_ssize_t.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "client_handler.h"
#include "sync.h"

using namespace std;

int main(int argc,char* argv[])
{
    if(argc!=3)
    {
        cerr<<"Usage: ./tracker tracker_info.txt tracker_id\n";
        return 1;
    }

    // Ignore SIGPIPE to avoid tracker crashing on client disconnect
    signal(SIGPIPE, SIG_IGN);

    string file=argv[1];
    int tracker_id=stoi(argv[2]);

    start_sync(file,tracker_id);

    int listen_port=get_client_port(tracker_id);
    if(listen_port<=0)
    {
        cerr<<"[TRACKER] Invalid tracker id\n";
        return 1;
    }

    int server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0)
    {
        perror("socket");
        return 1;
    }

    int opt=1;
    setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(listen_port);
    addr.sin_addr.s_addr=INADDR_ANY;

    if(::bind(server_fd,(struct sockaddr*)&addr,sizeof(addr))<0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if(listen(server_fd,5)<0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    cout<<"[TRACKER] Listening on port for clients "<<listen_port<<endl;

    while(1)
    {
        sockaddr_in caddr;
        socklen_t len=sizeof(caddr);
        int client_fd=accept(server_fd,(struct sockaddr*)&caddr,&len);

        if(client_fd<0)
        {
            if(errno == EINTR) continue; // retry if interrupted
            perror("accept");
            continue;
        }

        int* pclient=new int(client_fd);
        pthread_t id;
        pthread_create(&id,nullptr,client_handler,pclient);
        pthread_detach(id);
    }

    close(server_fd);
    return 0;
}
