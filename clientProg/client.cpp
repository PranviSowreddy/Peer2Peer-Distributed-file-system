
#include <_stdio.h>
#include <cstddef>
#include<iostream>
#include <netinet/in.h>
#include <pthread.h>
#include<sstream>
#include<fcntl.h>
#include <sys/_types/_ssize_t.h>
#include <sys/socket.h>
#include<unistd.h>
#include<arpa/inet.h>
using namespace std;

struct Tracker
{
    string ip;
    int clientPort;
};


void read_input(const string& file,vector<Tracker>&trackers)
{
    trackers.clear();
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
        trackers.push_back({ip,clientPort});
    }
}

int connect_to_trackers(const Tracker&t)
{
    int sock_fd=socket(AF_INET,SOCK_STREAM,0);
    if(sock_fd<0)
    {
        perror("socket");
        return -1;
    }

    sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_port=htons(t.clientPort);
    if(inet_pton(AF_INET,t.ip.c_str(),&addr.sin_addr)<=0)
    {
        perror("inet_pton");
        close(sock_fd);
        return -1;
    }

    if(connect(sock_fd,(sockaddr*)&addr,sizeof(addr))<0)
    {
        perror("connect");
        close(sock_fd);
        return -1;
    }
    cout<<"[CLIENT] connected to tracker "<<t.ip<<":"<<t.clientPort<<"\n";
    return sock_fd;
}


int main(int argc,char* argv[])
{
    if(argc!=3)
    {
        cerr<<"Usage ./client IP:PORT  tracker_info.txt";
        return 1;
    }
    string ipPort=argv[1];
    string file=argv[2];
    ssize_t pos=ipPort.find(":");
    if(pos==string::npos)
    {
        cerr<<"Invalid format. Use <IP>:<PORT>\n";
        return 1;
    }
    string ipAddr=ipPort.substr(0,pos);
    int port=stoi(ipPort.substr(pos+1));


    vector<Tracker>trackers;
    read_input(argv[2],trackers);

    int active=0;
    int sock=connect_to_trackers(trackers[active]);
    if(sock==-1)
    {
        cerr<<"Failed to connect to tracker 0\n";
        return 1;
    }

    string line;
    char buff[1024];

    while(1)
    {
        cout<<"$";
        if(!getline(cin,line))
        break;
        if(line.empty())
        continue;
        if(line=="quit")
        break;
        line+="\n";

        if(send(sock,line.c_str(),line.size(),0)<0)
        {
            perror("[CLIENT] send failed");
            close(sock);

            if(active==0)
            {
                cout<<"[CLIENT] switching to tracker 1\n";
                active=1;
                sock=connect_to_trackers(trackers[active]);
                if(sock==-1)
                {
                    cerr<<"[CLIENT] unable to connect to tracker 1 \n";
                    break;
                }
                continue;
            }
            else {
                cerr<<"[CLIENT] both trackers failed \n";
                break;
            }
        }
        ssize_t n=recv(sock,buff,sizeof(buff)-1,0);
        if(n<=0)
        {
            cerr<<"[CLIENT] connection closed by tracker\n";
            close(sock);
            if(active==0)
            {
                cout<<"[CLIENT] switching to tracker 1\n";
                active=1;
                sock=connect_to_trackers(trackers[active]);
                if(sock==-1)
                {
                    cerr<<"[CLIENT] unable to connect to tracker 1 \n";
                    break;
                }
                continue;
            }
            else {
                cerr<<"[CLIENT] both trackers failed \n";
                break;
            }
        }
        buff[n]='\0';
        cout<<"[SERVER] "<<buff;
    }
    
    if(sock!=-1)
    close(sock);
    cout<<"[CLIENT] exiting\n";
    return 0;
    
}
