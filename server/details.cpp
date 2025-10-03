// details.cpp
#include "details.h"
#include <pthread.h>
#include <sstream>

map<string,string> userDetails;
map<string,Group> groupDetails;
map<string, FileInfo> fileDetails;
map<string,string> client_addresses;

pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

string make_file_key(const string& group_id, const string& filename)
{
    return group_id + ":" + filename;
}

string create_user(const string& username, const string& password)
{
    pthread_mutex_lock(&state_mutex);
    if (userDetails.find(username) != userDetails.end())
    {
        pthread_mutex_unlock(&state_mutex);
        return "User already exists try logging in";
    }
    userDetails[username] = password;
    pthread_mutex_unlock(&state_mutex);
    return "User with " + username + " created";
}

// NOTE: updated login signature to accept client_addr (IP:PORT)
string login(const string& username, const string& password, const string& client_addr)
{
    pthread_mutex_lock(&state_mutex);
    auto it = userDetails.find(username);
    if (it == userDetails.end())
    {
        pthread_mutex_unlock(&state_mutex);
        return "User not found";
    }
    if (it->second != password)
    {
        pthread_mutex_unlock(&state_mutex);
        return "Wrong password";
    }
    // store client's public address for seeder discovery
    client_addresses[username] = client_addr;
    pthread_mutex_unlock(&state_mutex);
    return "Ok logged in successful";
}

// Overload/backwards-compat: keep two-arg login if some code calls it
string login(const string& username, const string& password)
{
    // delegate to new login without client_addr (empty)
    return login(username, password, "");
}

string create_group(const string& groupId, const string& userId)
{
    pthread_mutex_lock(&state_mutex);
    if (groupDetails.find(groupId) != groupDetails.end())
    {
        pthread_mutex_unlock(&state_mutex);
        return "Group already exists";
    }
    Group g;
    g.owner = userId;
    g.members.insert(userId);
    groupDetails[groupId] = g;
    pthread_mutex_unlock(&state_mutex);
    return "Group " + groupId + " created";
}

string join_group(const string& groupId, const string& userId)
{
    pthread_mutex_lock(&state_mutex);
    auto it = groupDetails.find(groupId);
    if (it == groupDetails.end())
    {
        pthread_mutex_unlock(&state_mutex);
        return "group with " + groupId + " is not found";
    }
    // if already member, return message
    if (it->second.members.count(userId)) {
        pthread_mutex_unlock(&state_mutex);
        return "Already a member";
    }
    it->second.pendRequests.insert(userId);
    pthread_mutex_unlock(&state_mutex);
    return "Join request submitted";
}

string leave_group(const string& groupId, const string& userId)
{
    pthread_mutex_lock(&state_mutex);
    auto it = groupDetails.find(groupId);
    if (it == groupDetails.end())
    {
        pthread_mutex_unlock(&state_mutex);
        return "group not found";
    }
    it->second.members.erase(userId);
    if (it->second.owner == userId)
    {
        if (!it->second.members.empty())
        {
            it->second.owner = *it->second.members.begin();
        }
        else {
            groupDetails.erase(it);
            pthread_mutex_unlock(&state_mutex);
            return "user left and group removed";
        }
    }
    pthread_mutex_unlock(&state_mutex);
    return "user " + userId + " left group";
}

string list_groups()
{
    pthread_mutex_lock(&state_mutex);
    stringstream ss;
    for (const auto &it : groupDetails)
    {
        ss << it.first << "\n";
    }
    pthread_mutex_unlock(&state_mutex);
    string out = ss.str();
    if (out.empty())
    {
        return "No groups found";
    }
    return out;
}

// list_requests requires owner authorization; updated signature to include ownerId
string list_requests(const string& groupId, const string& ownerId)
{
    pthread_mutex_lock(&state_mutex);
    auto it = groupDetails.find(groupId);

    if (it == groupDetails.end())
    {
        pthread_mutex_unlock(&state_mutex);
        return "group not found";
    }
    if (it->second.owner != ownerId)
    {
        pthread_mutex_unlock(&state_mutex);
        return "Error: Only group owner can view requests";
    }

    stringstream ss;
    for (const auto &r : it->second.pendRequests)
    {
        ss << r << "\n";
    }
    pthread_mutex_unlock(&state_mutex);
    string out = ss.str();
    if (out.empty())
    {
        return "No pending requests found";
    }
    return out;
}

// accept_request now requires ownerId to check authorization
string accept_request(const string& groupId, const string& userId, const string& ownerId)
{
    pthread_mutex_lock(&state_mutex);
    auto it = groupDetails.find(groupId);
    if (it == groupDetails.end())
    {
        pthread_mutex_unlock(&state_mutex);
        return "group not found";
    }
    if (it->second.owner != ownerId)
    {
        pthread_mutex_unlock(&state_mutex);
        return "Error: Only the group owner can accept requests.";
    }
    if (it->second.pendRequests.find(userId) == it->second.pendRequests.end())
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
    // Do not erase credentials on logout; only remove client address
    // If you want to remove user entirely, revert to userDetails.erase
    client_addresses.erase(userId);
    pthread_mutex_unlock(&state_mutex);
    return "User " + userId + " logged out";
}

string upload_file(const string &group_id, const string &filename, size_t file_size, const string &whole_sha1, const vector<string> &piece_hashes, const string &uploader_id)
{
    pthread_mutex_lock(&state_mutex);
    if (!groupDetails.count(group_id) || !groupDetails[group_id].members.count(uploader_id))
    {
        pthread_mutex_unlock(&state_mutex);
        return "Upload_failed : not member of grp";
    }

    string key = make_file_key(group_id, filename);
    if (!fileDetails.count(key))
    {
        FileInfo new_file;
        new_file.group_id = group_id;
        new_file.owner_id = uploader_id;
        new_file.filename = filename;
        new_file.file_size = file_size;
        new_file.whole_file_sha1 = whole_sha1;
        new_file.piece_hashes = piece_hashes;
        fileDetails[key] = new_file;
    }

    fileDetails[key].seeders.insert(uploader_id);
    pthread_mutex_unlock(&state_mutex);
    return "File uploaded and shared successfully";
}

string list_files(const string &group_id, const string &userId)
{
    pthread_mutex_lock(&state_mutex);
    if (!groupDetails.count(group_id) || !groupDetails[group_id].members.count(userId))
    {
        pthread_mutex_unlock(&state_mutex);
        return "Cant fetch list files : not member of grp";
    }

    stringstream ss;
    for (const auto& pair : fileDetails)
    {
        if (pair.second.group_id == group_id)
        {
            ss << pair.second.filename << "\n";
        }
    }
    pthread_mutex_unlock(&state_mutex);
    string result = ss.str();
    return result.empty() ? "No files in the group" : result;
}

string get_file(const string &group_id, const string &filename, const string &userId)
{
    pthread_mutex_lock(&state_mutex);
    if (!groupDetails.count(group_id) || !groupDetails[group_id].members.count(userId))
    {
        pthread_mutex_unlock(&state_mutex);
        return "Cant fetch file : not member of grp";
    }
    string key = make_file_key(group_id, filename);
    if (!fileDetails.count(key))
    {
        pthread_mutex_unlock(&state_mutex);
        return "File not found in grp";
    }

    const FileInfo& info = fileDetails[key];
    stringstream ss;
    ss << "FILEINFO " << info.file_size << " " << info.whole_file_sha1 << " " << info.piece_hashes.size();
    for (const auto& hash : info.piece_hashes)
    {
        ss << " " << hash;
    }

    ss << " SEEDERS";
    for (const auto& seeder_id : info.seeders)
    {
        if (client_addresses.count(seeder_id))
        {
            ss << " " << client_addresses[seeder_id];
        }
    }
    pthread_mutex_unlock(&state_mutex);
    return ss.str();
}

string stop_share(const string &group_id, const string &filename, const string &userId)
{
    pthread_mutex_lock(&state_mutex);
    if (!groupDetails.count(group_id) || !groupDetails[group_id].members.count(userId))
    {
        pthread_mutex_unlock(&state_mutex);
        return "Cant stop share: not member of grp";
    }
    string key = make_file_key(group_id, filename);
    if (!fileDetails.count(key))
    {
        pthread_mutex_unlock(&state_mutex);
        return "File not found in grp";
    }

    fileDetails[key].seeders.erase(userId);

    // Optional cleanup: remove file record if no seeders remain
    if (fileDetails[key].seeders.empty()) {
        fileDetails.erase(key);
    }

    pthread_mutex_unlock(&state_mutex);
    return "Stopped sharing file " + filename;
}
