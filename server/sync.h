#ifndef SYNC_H
#define SYNC_H

#include<string>

using namespace std;
void start_sync(const string&file,int trackerId);
void send_sync(const string& msg);
int get_client_port(int trackerId);

#endif