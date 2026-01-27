CXX = g++
CC = gcc
CXXFLAGS = -std=c++11 -Wall -Wextra -O2
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lcurl -lssl -lcrypto -lpthread -ldl

# Source files
CPP_SOURCES = supplierbuyer.cpp CivetServer.cpp
C_SOURCES = civetweb.c sqlite3.c mongoose.c shell.c

# Object files
CPP_OBJECTS = $(CPP_SOURCES:.cpp=.o)
C_OBJECTS = $(C_SOURCES:.c=.o)
OBJECTS = $(CPP_OBJECTS) $(C_OBJECTS)

# Executable
TARGET = supplierbuyer

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean