# Peer-to-Peer Distributed File Sharing System  
### AOS Assignment 3 – Interim Submission  

---

##  Overview  

The goal is to implement a **peer-to-peer distributed file sharing system** with redundancy using **two trackers** and multiple clients.  

For the interim submission, the following parts have been implemented:  

- **Tracker system** with synchronization between two trackers.  
- **User and group management commands** as described in the assignment.  

---

## Implemented Features  

### Tracker Synchronization  
- Two trackers run in parallel, each listening on:  
  - **Client Port** → for client connections.  
  - **Sync Port** → for tracker-to-tracker synchronization.  
- Synchronization is done using TCP sockets.  
- Whenever a state-changing command is executed (e.g., `create_user`, `create_group`), the information is **propagated to the peer tracker** using `send_sync()`.  
- Both incoming and outgoing sync connections are maintained.  
- Trackers remain consistent so that clients can connect to either tracker.  

---

### User Commands  
- `create_user <user_id> <password>`  
  Registers a new user and syncs the information across trackers.  
- `login <user_id> <password>`  
  Authenticates a user.  
- `logout`  
  Ends the user session.  

---

### Group Commands  
- `create_group <group_id>`  
  Creates a new group. Owner is the creator.  
- `join_group <group_id>`  
  Requests to join a group.  
- `leave_group <group_id>`  
  Leaves a group.  
- `list_groups`  
  Lists all existing groups.  
- `list_requests <group_id>`  
  Shows pending join requests (only for group owner).  
- `accept_request <group_id> <user_id>`  
  Accepts a join request into the group.  

All group operations are also synchronized between trackers.

---
