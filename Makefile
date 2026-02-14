CXX = g++
CC = gcc

CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -DOPENSSL_API_3_0
CFLAGS   = -Wall -Wextra -O2 -DOPENSSL_API_3_0

LDFLAGS = -lcurl -lssl -lcrypto -lpthread -ldl

CPP_SOURCES = supplierbuyer.cpp CivetServer.cpp
C_SOURCES   = civetweb.c sqlite3.c

CPP_OBJECTS = $(CPP_SOURCES:.cpp=.o)
C_OBJECTS   = $(C_SOURCES:.c=.o)
OBJECTS     = $(CPP_OBJECTS) $(C_OBJECTS)

TARGET = supplierbuyer-server

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
