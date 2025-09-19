#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H


#include <cstddef>
#include<string>
#include<sys/socket.h>
#include<unistd.h>
#include<sstream>

#include "details.h"
using namespace std;


void* client_handler(void* arg);

#endif