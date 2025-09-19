#include <pthread.h>
#include<string>
#include<set>
#include<map>
#include<iostream>
#include<sstream>
#include <sys/_pthread/_pthread_mutex_t.h>

using namespace std;
struct Group
{
    string owner;
    set<string>members;
    set<string>pendRequests;
};

static map<string,string>userDetails;
static map<string,Group>groupDetails;
static pthread_mutex_t state_mutex=PTHREAD_MUTEX_INITIALIZER;

string create_user(const string& username,const string& password)
{
    pthread_mutex_lock (&state_mutex);
    if(userDetails.find(username)!=userDetails.end())
    {
        pthread_mutex_unlock (&state_mutex);
        return "User already exists try logging in";
    }
        userDetails[username]= password;
        pthread_mutex_unlock(&state_mutex);
        return "User with "+username+" created";
}

string login(const string& username,const string& password)
{
    pthread_mutex_lock (&state_mutex);
    auto it=userDetails.find(username);
    if(it==userDetails.end())
    {
        pthread_mutex_unlock (&state_mutex);
        return "User not found";
    }
    if(it->second!=password)
    {
        pthread_mutex_unlock (&state_mutex);
        return "Wrong password";
    }
    pthread_mutex_unlock (&state_mutex);
    return "Ok logged in ";
}

string create_group(const string& groupId,const string& userId)
{
    pthread_mutex_lock (&state_mutex);
    if(groupDetails.find(groupId)!=groupDetails.end())
    {
        pthread_mutex_unlock (&state_mutex);
        return "Group already exists";
    }
    Group g;
    g.owner=userId;
    g.members.insert(userId);
    groupDetails[groupId]=g;
    pthread_mutex_unlock (&state_mutex);
    return "Group "+groupId+" created"; 
}

string join_group(const string& groupId,const string& userId)
{
    pthread_mutex_lock(&state_mutex);
    auto it=groupDetails.find(groupId);
    if(it==groupDetails.end())
    {
        pthread_mutex_unlock (&state_mutex);
        return "group with "+groupId+" is not found";
    }
    it->second.pendRequests.insert(userId);
    pthread_mutex_unlock (&state_mutex);
    return "Join request submitted";
}

string leave_group(const string& groupId,const string& userId)
{
    pthread_mutex_lock(&state_mutex);
    auto it=groupDetails.find(groupId);
    if(it==groupDetails.end())
    {
        pthread_mutex_unlock (&state_mutex);
        return "group not found";
    }
    it->second.members.erase(userId);
    if(it->second.owner==userId)
    {
        if(!it->second.members.empty())
        {
            it->second.owner=*it->second.members.begin();
        }
        else {
            groupDetails.erase(it);
            pthread_mutex_unlock (&state_mutex);

        }
    }
    pthread_mutex_unlock (&state_mutex);
    return "user "+userId+" left group";
}
string list_groups()
{
    pthread_mutex_lock(&state_mutex);
    stringstream ss;
    for(const auto &it:groupDetails)
    {
        ss<<it.first<<"\n";
    }
    pthread_mutex_unlock(&state_mutex);
    string out=ss.str();
    if(out.empty())
    {
        return "No groups found";
    }
    return out;
}

string list_requests(const string& groupId)
{
    pthread_mutex_lock(&state_mutex);
    auto it=groupDetails.find(groupId);
    
    if(it==groupDetails.end())
    {
        pthread_mutex_unlock (&state_mutex);
        return "group not found";
    }
    stringstream ss;
    for(const auto &r:it->second.pendRequests)
    {
        ss<<r<<"\n";
    }
    pthread_mutex_unlock (&state_mutex);
    string out=ss.str();
    if(out.empty())
    {
        return "No pending requests found";
    }
    return out;
}

string accept_request(const string& groupId,const string& userId)
{
    pthread_mutex_lock(&state_mutex);
    auto it=groupDetails.find(groupId);
    if(it==groupDetails.end())
    {
        pthread_mutex_unlock(&state_mutex);
        return "group not found";
    }
    if(it->second.pendRequests.find(userId)==it->second.pendRequests.end())
    {
        pthread_mutex_unlock(&state_mutex);
        return "Pending request is not found";
    }
    it->second.pendRequests.erase(userId);
    it->second.members.insert(userId);
    pthread_mutex_unlock(&state_mutex);
    return "OK request accepted";

}

string logout(const string& userId)
{
    pthread_mutex_lock(&state_mutex);
    userDetails.erase(userId);
    pthread_mutex_unlock(&state_mutex);
    return "User "+userId+" logged out ";
}


