#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/*constants*/
#define SOCKET_TIMEOUT_IN_MICROSECONDS 5000
#define SOCKET_PASSWORD           "a1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz&'-<>?=!@#$^*()_+/*-+{}[]|:;"
#define BUFFER_SIZE               4096

/*globals*/
static char *s_serverBindingAddress = NULL;
static int s_serverBindingPortFromUser = 0;
static int s_serverForwardPortFromMachine = 0;
static int s_serverMachinePort = 0;

/*forward declaration*/
static int server();
static int client();

/**
 * @brief printUsage
 */
static void printUsage()
{
    printf("Usage:\n");
    printf("  ./PortForwarding server <SERVER IP ADDRESS> <SERVER USER BIND PORT> <SERVER MACHINE BIND PORT> <MACHINE PORT> \n");
    printf("  ./PortForwarding client <SERVER IP ADDRESS> <SERVER USER BIND PORT> <SERVER MACHINE BIND PORT> <MACHINE PORT> \n");
}

/**
 * @brief server
 * @return
 */
int main(int argc, char *argv[])
{
    // we need 1 argument
    if (argc != 6)
    {
        printUsage();
        return 1;
    }

    // init globals
    s_serverBindingAddress = argv[2];
    s_serverBindingPortFromUser = atoi(argv[3]);
    s_serverForwardPortFromMachine = atoi(argv[4]);
    s_serverMachinePort = atoi(argv[5]);

    if (*s_serverBindingAddress == '\0'     ||
        s_serverBindingPortFromUser == 0    ||
        s_serverForwardPortFromMachine == 0 ||
        s_serverMachinePort == 0)
    {
        printUsage();
        return 1;
    }

    // is it a server
    if (strcmp(argv[1], "server") == 0)
    {
        return server();
    }

    // is it a client
    if (strcmp(argv[1], "client") == 0)
    {
        return client();
    }

    // print usage and exit
    printUsage();
    return 1;
}

/*** SERVER ***/
static int server()
{
    //create server socket from user
    int serverSocketFromUser = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketFromUser == -1)
    {
        printf("Failed to create server socket from user\n");
        return 1;
    }

    //create server socket from machine
    int serverSocketFromMachine = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketFromMachine == -1)
    {
        printf("Failed to create server socket from machine\n");
        return 1;
    }

    //re use the port
    int reuse = 1;
    if (setsockopt(serverSocketFromUser, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == -1)
    {
        printf("Failed to set server socket from user reuse\n");
        return 1;
    }
    
    if (setsockopt(serverSocketFromMachine, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == -1)
    {
        printf("Failed to set server socket from machine reuse\n");
        return 1;
    }

    //set up server address structure from user
    struct sockaddr_in serverAddressFromUser;
    memset(&serverAddressFromUser, 0, sizeof(serverAddressFromUser));
    serverAddressFromUser.sin_family = AF_INET;
    serverAddressFromUser.sin_port = htons(s_serverBindingPortFromUser);
#if 1
    serverAddressFromUser.sin_addr.s_addr = INADDR_ANY; //custome need to open tunnel for any incomming address
#else
    serverAddressFromUser.sin_addr.s_addr = inet_addr(s_serverBindingAddress);
#endif

    //set up server address structure from machine
    struct sockaddr_in serverAddressFromMachine;
    memset(&serverAddressFromMachine, 0, sizeof(serverAddressFromMachine));
    serverAddressFromMachine.sin_family = AF_INET;
    serverAddressFromMachine.sin_port = htons(s_serverForwardPortFromMachine);
    serverAddressFromMachine.sin_addr.s_addr = inet_addr(s_serverBindingAddress);

    //bind server socket from user
    if (bind(serverSocketFromUser, (struct sockaddr *)&serverAddressFromUser, sizeof(serverAddressFromUser)) == -1)
    {
        printf("Failed to bind server socket from user\n");
        return 1;
    }

    //bind server socket from machine
    if (bind(serverSocketFromMachine, (struct sockaddr *)&serverAddressFromMachine, sizeof(serverAddressFromMachine)) == -1)
    {
        printf("Failed to bind server socket from machine\n");
        return 1;
    }

    //listen on the server socket from user
    if (listen(serverSocketFromUser, 1) == -1)
    {
        printf("Failed to listen on server socket from user\n");
        return 1;
    }

    //listen on the server socket from machine
    if (listen(serverSocketFromMachine, 1) == -1)
    {
        printf("Failed to listen on server socket from machine\n");
        return 1;
    }

    // loop forever
    while (1)
    {
        //sleep 2 seconds
        sleep(2);

        //accept a client from machine
        struct sockaddr_in clientAddressFromMachine;
        socklen_t clientAddressLengthFromMachine = sizeof(clientAddressFromMachine);
        int clientSocketFromMachine = accept(serverSocketFromMachine, (struct sockaddr *)&clientAddressFromMachine, &clientAddressLengthFromMachine);
        if (clientSocketFromMachine == -1)
        {
            printf("Failed to accept client socket from machine\n");
            continue;
        }

        //log
        printf("New connection from machine with IP address %s\n", inet_ntoa(clientAddressFromMachine.sin_addr));

        //set the client socket from machine into non-blocking mode
        struct timeval timeoutFromMachine;
        timeoutFromMachine.tv_sec = 0;
        timeoutFromMachine.tv_usec = SOCKET_TIMEOUT_IN_MICROSECONDS;
        if (setsockopt(clientSocketFromMachine, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeoutFromMachine, sizeof(timeoutFromMachine)) < 0)
        {
            printf("Failed to set client socket from machine timeout\n");
            return 1;
        }

        //log
        printf("Checking password from machine\n");

        //sleep of 200 ms to give time to the client to send the password
        usleep(200000);

        //receive from the client the password
        char passwordFromMachine[1024];
        memset(passwordFromMachine, 0, sizeof(passwordFromMachine));
        int passwordFromMachineLength = recv(clientSocketFromMachine, passwordFromMachine, sizeof(passwordFromMachine), 0);
        if (passwordFromMachineLength == -1)
        {
            printf("Failed to receive password from machine\n");
            close(clientSocketFromMachine);
            continue;
        }

        //check the password
        if (memcmp(passwordFromMachine, SOCKET_PASSWORD, strlen(SOCKET_PASSWORD)) != 0)
        {
            printf("Wrong password from machine\n");
            close(clientSocketFromMachine);
            continue;
        }

        //log
        printf("Password from machine is correct\n");

        //accept a client from user
        struct sockaddr_in clientAddressFromUser;
        socklen_t clientAddressLengthFromUser = sizeof(clientAddressFromUser);
        int clientSocketFromUser = accept(serverSocketFromUser, (struct sockaddr *)&clientAddressFromUser, &clientAddressLengthFromUser);
        if (clientSocketFromUser == -1)
        {
            printf("Failed to accept client socket from user\n");
            close(clientSocketFromMachine);
            continue;
        }

        //log
        printf("New connection from user with IP address %s\n", inet_ntoa(clientAddressFromUser.sin_addr));

        //set the client socket from user into non-blocking mode
        struct timeval timeoutFromUser;
        timeoutFromUser.tv_sec = 0;
        timeoutFromUser.tv_usec = SOCKET_TIMEOUT_IN_MICROSECONDS;
        if (setsockopt(clientSocketFromUser, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeoutFromUser, sizeof(timeoutFromUser)) < 0)
        {
            printf("Failed to set client socket from user timeout\n");
            close(clientSocketFromMachine);
            close(clientSocketFromUser);
            continue;
        }

        //log
        printf("Checking password from user\n");

        //receive from the client the password
#if 0
        char passwordFromUser[1024];
        memset(passwordFromUser, 0, sizeof(passwordFromUser));
        int passwordFromUserLength = recv(clientSocketFromUser, passwordFromUser, sizeof(passwordFromUser), 0);
        if (passwordFromUserLength == -1)
        {
            printf("Failed to receive password from user\n");
            close(clientSocketFromMachine);
            close(clientSocketFromUser);
            continue;
        }

        //check the password
        if (memcmp(passwordFromUser, SOCKET_PASSWORD, strlen(SOCKET_PASSWORD)) != 0)
        {
            printf("Wrong password from user\n");
            close(clientSocketFromMachine);
            close(clientSocketFromUser);
            continue;
        }

        //log
        printf("Password from user is correct\n");
#endif
        //create the buffer
        const int bufferSize = BUFFER_SIZE;
        char* buffer = (char*)malloc(bufferSize);
        
        //log
        printf("Forwarding from machine to user and from user to machine started\n");

        //enter in forwarding loop from machine to user and from user to machine
        while (1)
        {
            //from machine to use
            int readFromMachine = recv(clientSocketFromMachine, buffer, bufferSize, 0);
            if (readFromMachine == 0) //client disconnected
            {
                break;
            }
            else if (readFromMachine > 0)
            {
                send(clientSocketFromUser, buffer, readFromMachine, 0);
            }

            //from user to machine
            int readFromUser = recv(clientSocketFromUser, buffer, bufferSize, 0);
            if (readFromUser == 0) //client disconnected
            {
                break;
            }
            else if (readFromUser > 0)
            {
                send(clientSocketFromMachine, buffer, readFromUser, 0);
            }

            //sleep 5 ms
            usleep(5000);
        }
        
        //close the sockets
        close(clientSocketFromMachine);
        close(clientSocketFromUser);

        //free the buffer
        free(buffer);

        //log 
        printf("Forwarding from machine to user and from user to machine ended\n");
    }
 
    //close the sockets
    close(serverSocketFromMachine);
    close(serverSocketFromUser);

    //return
    return 0;
}

/*** CLIENT ***/
static int client()
{
    //enter the loop event
    while (1)
    {
        //sleep 2 seconds
        sleep(2);

        //connect to the server Machine side
        int serverSocketFromMachine = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocketFromMachine == -1)
        {
            printf("Failed to create server socket from machine\n");
            continue;
        }

        //set up socket address for Machine side
        struct sockaddr_in socketAddressFromMachine;
        memset(&socketAddressFromMachine, 0, sizeof(socketAddressFromMachine));
        socketAddressFromMachine.sin_family = AF_INET;
        socketAddressFromMachine.sin_port = htons(s_serverForwardPortFromMachine);
        socketAddressFromMachine.sin_addr.s_addr = inet_addr(s_serverBindingAddress);

        //connect to the server Machine side
        if (connect(serverSocketFromMachine, (struct sockaddr *)&socketAddressFromMachine, sizeof(socketAddressFromMachine)) == -1)
        {
            printf("Failed to connect to server socket from machine\n");
            close(serverSocketFromMachine);
            continue;
        }

        //log
        printf("Connected to server socket from machine\n");

        //set the socket to non-blocking mode
        struct timeval timeoutFromMachine;
        timeoutFromMachine.tv_sec = 0;
        timeoutFromMachine.tv_usec = SOCKET_TIMEOUT_IN_MICROSECONDS;
        if (setsockopt(serverSocketFromMachine, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeoutFromMachine, sizeof(timeoutFromMachine)) < 0)
        {
            printf("Failed to set server socket from machine timeout\n");
            close(serverSocketFromMachine);
            continue;
        }

        //send the password
        if (send(serverSocketFromMachine, SOCKET_PASSWORD, strlen(SOCKET_PASSWORD), 0) == -1)
        {
            printf("Failed to send password from machine\n");
            close(serverSocketFromMachine);
            continue;
        }

        //create the buffer
        const int bufferSize = BUFFER_SIZE;
        char* buffer = (char*)malloc(bufferSize);

        //wait the first message from the server
        int readFromMachine = recv(serverSocketFromMachine, buffer, bufferSize, 0);
        while (readFromMachine <= 0)
        {
            if (readFromMachine == 0) //server disconnected
            {
                break;
            }

            //sleep 50 ms
            usleep(50000);

            //try again 
            readFromMachine = recv(serverSocketFromMachine, buffer, bufferSize, 0);
        }

        if (readFromMachine <= 0)
        {
            //free the buffer
            free(buffer);

            //close the socket
            close(serverSocketFromMachine);

            //continue
            continue;
        }

        //log
        printf("First message received\n");

        //connect to local server
        int localSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (localSocket == -1)
        {
            printf("Failed to create local socket\n");
            free(buffer);
            close(serverSocketFromMachine);
            continue;
        }

        //set up socket address for local server
        struct sockaddr_in localSocketAddress;
        memset(&localSocketAddress, 0, sizeof(localSocketAddress));
        localSocketAddress.sin_family = AF_INET;
        localSocketAddress.sin_port = htons(s_serverMachinePort);
        localSocketAddress.sin_addr.s_addr = inet_addr("127.0.0.1");

        //connect to local server
        if (connect(localSocket, (struct sockaddr *)&localSocketAddress, sizeof(localSocketAddress)) == -1)
        {
            printf("Failed to connect to local socket\n");
            free(buffer);
            close(serverSocketFromMachine);
            close(localSocket);
            continue;
        }

        //log
        printf("Connected to local socket\n");

        //set the socket to non-blocking mode
        struct timeval timeoutFromLocal;
        timeoutFromLocal.tv_sec = 0;
        timeoutFromLocal.tv_usec = SOCKET_TIMEOUT_IN_MICROSECONDS;
        if (setsockopt(localSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeoutFromLocal, sizeof(timeoutFromLocal)) < 0)
        {
            printf("Failed to set local socket timeout\n");
            free(buffer);
            close(serverSocketFromMachine);
            close(localSocket);
            continue;
        }

        //send the first message to local server
        if (send(localSocket, buffer, readFromMachine, 0) == -1)
        {
            printf("Failed to send first message to local socket\n");
            free(buffer);
            close(serverSocketFromMachine);
            close(localSocket);
            continue;
        }

        //start forwarding from machine to local and from local to machine
        while (1)
        {
            //from machine to use
            int readFromMachine = recv(serverSocketFromMachine, buffer, bufferSize, 0);
            if (readFromMachine == 0) //client disconnected
            {
                break;
            }
            else if (readFromMachine > 0)
            {
                send(localSocket, buffer, readFromMachine, 0);
            }

            //from user to machine
            int readFromLocal = recv(localSocket, buffer, bufferSize, 0);
            if (readFromLocal == 0) //client disconnected
            {
                break;
            }
            else if (readFromLocal > 0)
            {
                send(serverSocketFromMachine, buffer, readFromLocal, 0);
            }

            //sleep 5 ms
            usleep(5000);
        }

        //close the sockets
        close(serverSocketFromMachine);
        close(localSocket);

        //free the buffer
        free(buffer);

        //log
        printf("Forwarding from machine to local and from local to machine ended\n");
    }
}
