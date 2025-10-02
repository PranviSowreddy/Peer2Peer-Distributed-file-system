# Compiler and flags
CXX      = g++
# CXXFLAGS = -std=c++17 -Wall -pthread -g # -g flag adds debugging symbols
CXXFLAGS = -std=c++17 -Wall -pthread -g -I/opt/homebrew/opt/openssl/include
# On Linux, you might just need -lssl -lcrypto
# On macOS with Homebrew, we need to specify paths
LDFLAGS_SERVER = -lpthread
# Add Homebrew's OpenSSL path for libraries and link them
LDFLAGS_CLIENT = -L/opt/homebrew/opt/openssl/lib -lreadline -lssl -lcrypto

# --- Directories ---
SERVER_DIR = server
CLIENT_DIR = client

# --- Source Files ---
# This tells the Makefile to find ALL .cpp files in the server directory
TRACKER_SRCS = $(wildcard $(SERVER_DIR)/*.cpp)
CLIENT_SRCS  = $(wildcard $(CLIENT_DIR)/*.cpp)

# --- Object Files (automatically generated from source files) ---
TRACKER_OBJS = $(TRACKER_SRCS:.cpp=.o)
CLIENT_OBJS  = $(CLIENT_SRCS:.cpp=.o)

# --- Targets ---
TRACKER_TARGET = tracker
CLIENT_TARGET  = p2p_client
# CLIENT_TARGET  = client

# --- Default Target ---
all: $(TRACKER_TARGET) $(CLIENT_TARGET)

# --- Build Rules ---

# Rule to link the tracker executable from all its object files
$(TRACKER_TARGET): $(TRACKER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS_SERVER)

# Rule to link the client executable
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS_CLIENT)

# Generic rule to compile any .cpp file into a .o file
# This is how each of your .cpp files becomes a .o file
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- Convenience Run Targets ---
# IMPORTANT: You must create a 'tracker_info.txt' file for these to work.
run-tracker0: $(TRACKER_TARGET)
	@echo "--- Starting Tracker 1 (./tracker tracker_info.txt 1) ---"
	./$(TRACKER_TARGET) tracker_info.txt 0

run-tracker1: $(TRACKER_TARGET)
	@echo "--- Starting Tracker 2 (./tracker tracker_info.txt 2) ---"
	./$(TRACKER_TARGET) tracker_info.txt 1

run-client: $(CLIENT_TARGET)
	@echo "--- Starting Client (./client/client tracker_info.txt) ---"
	./$(CLIENT_TARGET) tracker_info.txt 127.0.0.1:8083

# --- Clean Rule ---
clean:
	rm -f $(TRACKER_TARGET) $(CLIENT_TARGET) $(SERVER_DIR)/*.o $(CLIENT_DIR)/*.o

