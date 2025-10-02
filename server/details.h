#ifndef DETAILS_H
#define DETAILS_H

#include <string>
#include <vector>
#include <set>
#include <map>
#include <pthread.h>

using namespace std;

// --- Data Structures for Tracker State ---

struct Group {
    string owner;
    set<string> members;
    set<string> pendRequests;
};

struct FileInfo {
    string group_id;
    string owner_id;
    string filename;
    string whole_file_sha1;
    vector<string> piece_hashes;
    size_t file_size;
    set<string> seeders; // A set of user_ids who have this file
};

// --- Global State Variables ---
extern map<string, string> userDetails;
extern map<string, Group> groupDetails;
extern map<string, FileInfo> fileDetails;
extern map<string, string> client_addresses; // Maps logged-in user_id to their IP:Port
extern pthread_mutex_t state_mutex;

// --- Function Declarations for Tracker Logic ---

// User & Group Management
string create_user(const string& username, const string& password);
string login(const string& username, const string& password, const string& client_addr);
string create_group(const string& groupId, const string& userId);
string join_group(const string& groupId, const string& userId);
string leave_group(const string& groupId, const string& userId);
string list_groups();
string list_requests(const string& groupId, const string& userId);
string accept_request(const string& groupId, const string& userId_to_accept, const string& ownerId);
string logout(const string& userId);

// File Management
string upload_file(const string& group_id, const string& filename, size_t file_size, const string& whole_sha1, const vector<string>& piece_hashes, const string& uploader_id);
string list_files(const string& group_id, const string& userId);
string get_file(const string& group_id, const string& filename, const string& userId);

#endif

