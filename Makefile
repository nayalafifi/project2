# Define the C compiler
CC = gcc

CFLAGS = -Wall -Wextra -g -std=c99

# Define the source files
SERVER_SRC = code/server.c
CLIENT_SRC = code/client.c

# Define the target executables
SERVER_TARGET = server/server
CLIENT_TARGET = client/client

# Default target: build both server and client
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Rule to build the server executable
$(SERVER_TARGET): $(SERVER_SRC)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@

# Rule to build the client executable
$(CLIENT_TARGET): $(CLIENT_SRC)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@


clean:
	rm -rf server/$(notdir $(SERVER_TARGET)) client/$(notdir $(CLIENT_TARGET)) # Remove executables from target directories
	rm -rf server/*.dSYM client/*.dSYM
