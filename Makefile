CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread

SRC_DIR = server
OBJS = $(SRC_DIR)/tracker.o $(SRC_DIR)/sync.o $(SRC_DIR)/client_handler.o $(SRC_DIR)/details.o

TARGET = tracker

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(SRC_DIR)/*.o $(TARGET)
