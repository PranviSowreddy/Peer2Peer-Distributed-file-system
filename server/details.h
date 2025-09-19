#ifndef DETAILS_H
#define DETAILS_H


#include<string>
#include<set>
#include<map>
#include<iostream>
#include <sys/_pthread/_pthread_mutex_t.h>

using namespace std;
struct Group
{
    string owner;
    set<string>members;
};

extern map<string,string>userDetails;
extern map<string,Group>groupDetails;

extern pthread_mutex_t state_mutex;

string create_user(const string& username,const string& password);
string login(const string& username,const string& password);
string create_group(const string& groupId,const string& userId);
string join_group(const string& groupId,const string& userId);
string leave_group(const string& groupId,const string& userId);
string list_groups();
string list_requests(const string& groupId);
string accept_request(const string& groupId,const string& userId);
string logout(const string& userId);

#endif


