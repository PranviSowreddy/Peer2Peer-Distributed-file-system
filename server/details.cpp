#include "details.h"
#include <sstream>

// --- Definitions of Global State Variables ---
map<string, string> userDetails;
map<string, Group> groupDetails;
map<string, FileInfo> fileDetails;
map<string, string> client_addresses;
pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- Helper to create a consistent key for the file map ---
string make_file_key(const string& group_id, const string& filename) {
    return group_id + ":" + filename;
}

// --- User and Group Management Functions (with authorization fixes) ---

string create_user(const string& username, const string& password) {
    pthread_mutex_lock(&state_mutex);
    if (userDetails.count(username)) {
        pthread_mutex_unlock(&state_mutex);
        return "User already exists.";
    }
    userDetails[username] = password;
    pthread_mutex_unlock(&state_mutex);
    return "User created successfully.";
}

string login(const string& username, const string& password, const string& client_addr) {
    pthread_mutex_lock(&state_mutex);
    if (!userDetails.count(username) || userDetails[username] != password) {
        pthread_mutex_unlock(&state_mutex);
        return "Login failed: Invalid credentials.";
    }
    // Store the client's public-facing address on successful login
    client_addresses[username] = client_addr;
    pthread_mutex_unlock(&state_mutex);
    return "Login successful.";
}

string create_group(const string& groupId, const string& userId) {
    pthread_mutex_lock(&state_mutex);
    if (groupDetails.count(groupId)) {
        pthread_mutex_unlock(&state_mutex);
        return "Group already exists.";
    }
    Group g;
    g.owner = userId;
    g.members.insert(userId);
    groupDetails[groupId] = g;
    pthread_mutex_unlock(&state_mutex);
    return "Group created successfully.";
}

string join_group(const string& groupId, const string& userId) {
    pthread_mutex_lock(&state_mutex);
    if (!groupDetails.count(groupId)) {
        pthread_mutex_unlock(&state_mutex);
        return "Group not found.";
    }
    if (groupDetails[groupId].members.count(userId)) {
        pthread_mutex_unlock(&state_mutex);
        return "You are already a member of this group.";
    }
    groupDetails[groupId].pendRequests.insert(userId);
    pthread_mutex_unlock(&state_mutex);
    return "Join request sent.";
}

string accept_request(const string& groupId, const string& userId_to_accept, const string& ownerId) {
    pthread_mutex_lock(&state_mutex);
    if (!groupDetails.count(groupId)) {
        pthread_mutex_unlock(&state_mutex);
        return "Group not found.";
    }
    // Authorization check: Only the owner can accept requests.
    if (groupDetails[groupId].owner != ownerId) {
        pthread_mutex_unlock(&state_mutex);
        return "Error: Only the group owner can accept requests.";
    }
    if (!groupDetails[groupId].pendRequests.count(userId_to_accept)) {
        pthread_mutex_unlock(&state_mutex);
        return "No pending request from this user.";
    }
    groupDetails[groupId].pendRequests.erase(userId_to_accept);
    groupDetails[groupId].members.insert(userId_to_accept);
    pthread_mutex_unlock(&state_mutex);
    return "User request accepted.";
}

// --- File Management Functions (FIXED and FULLY IMPLEMENTED) ---

string upload_file(const string& group_id, const string& filename, size_t file_size, const string& whole_sha1, const vector<string>& piece_hashes, const string& uploader_id) {
    pthread_mutex_lock(&state_mutex);

    // Authorization check
    if (!groupDetails.count(group_id) || !groupDetails[group_id].members.count(uploader_id)) {
        pthread_mutex_unlock(&state_mutex);
        return "Upload failed: You are not a member of this group.";
    }

    string key = make_file_key(group_id, filename);
    
    // If the file is new, create all its metadata.
    if (!fileDetails.count(key)) {
        FileInfo new_file;
        new_file.group_id = group_id;
        new_file.owner_id = uploader_id;
        new_file.filename = filename;
        new_file.file_size = file_size;
        new_file.whole_file_sha1 = whole_sha1;
        new_file.piece_hashes = piece_hashes;
        fileDetails[key] = new_file;
    }

    // Add the uploader as a seeder for this file.
    fileDetails[key].seeders.insert(uploader_id);

    pthread_mutex_unlock(&state_mutex);
    return "File uploaded and shared successfully.";
}

string list_files(const string& group_id, const string& userId) {
    pthread_mutex_lock(&state_mutex);
    
    // Authorization check
    if (!groupDetails.count(group_id) || !groupDetails[group_id].members.count(userId)) {
        pthread_mutex_unlock(&state_mutex);
        return "Cannot list files: You are not a member of this group.";
    }

    stringstream ss;
    for (const auto& pair : fileDetails) {
        if (pair.second.group_id == group_id) {
            ss << pair.second.filename << "\n";
        }
    }
    pthread_mutex_unlock(&state_mutex);
    string result = ss.str();
    return result.empty() ? "No files in this group." : result;
}

string get_file(const string& group_id, const string& filename, const string& userId) {
    pthread_mutex_lock(&state_mutex);

    // Authorization check
    if (!groupDetails.count(group_id) || !groupDetails[group_id].members.count(userId)) {
        pthread_mutex_unlock(&state_mutex);
        return "Download failed: You are not a member of this group.";
    }

    string key = make_file_key(group_id, filename);
    if (!fileDetails.count(key)) {
        pthread_mutex_unlock(&state_mutex);
        return "File not found in this group.";
    }

    const FileInfo& info = fileDetails[key];
    stringstream ss;
    ss << "FILEINFO " << info.file_size << " " << info.whole_file_sha1 << " " << info.piece_hashes.size();
    for (const auto& hash : info.piece_hashes) {
        ss << " " << hash;
    }

    ss << " SEEDERS";
    for (const auto& seeder_id : info.seeders) {
        if (client_addresses.count(seeder_id)) {
            // Append the IP:PORT of each seeder
            ss << " " << client_addresses[seeder_id];
        }
    }

    pthread_mutex_unlock(&state_mutex);
    return ss.str();
}

// --- Other Management Functions ---

string leave_group(const string&, const string&) { return "Not implemented."; }

string list_groups() { 
    pthread_mutex_lock(&state_mutex);
    stringstream ss;
    for(const auto &it:groupDetails) ss << it.first << "\n";
    pthread_mutex_unlock(&state_mutex);
    string out=ss.str();
    return out.empty() ? "No groups found." : out;
}

string list_requests(const string& groupId, const string& userId) {
    pthread_mutex_lock(&state_mutex);
    if (!groupDetails.count(groupId)) {
        pthread_mutex_unlock(&state_mutex);
        return "Group not found.";
    }
    if (groupDetails[groupId].owner != userId) {
        pthread_mutex_unlock(&state_mutex);
        return "Error: Only the group owner can view requests.";
    }
    stringstream ss;
    for (const auto& req : groupDetails[groupId].pendRequests) {
        ss << req << "\n";
    }
    pthread_mutex_unlock(&state_mutex);
    string result = ss.str();
    return result.empty() ? "No pending requests." : result;
}

string logout(const string& userId) { 
    pthread_mutex_lock(&state_mutex);
    client_addresses.erase(userId);
    pthread_mutex_unlock(&state_mutex);
    return "Logout successful.";
}

