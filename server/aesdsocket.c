#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>

#define PORT 9000

// Flag set by signal handler to request graceful exit
static volatile sig_atomic_t exit_requested = 0;

// signal_handler: called on SIGINT or SIGTERM
//   sig = signal number received (unused, but required by sigaction API)
static void signal_handler(int sig)
{
    (void)sig; // suppress unused parameter warning
    exit_requested = 1;
}

int main(int argc, char *argv[])
{
    // Parse optional -d flag to run as a daemon
    int daemon_mode = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0)
        daemon_mode = 1;
    // openlog(ident, option, facility): initialize syslog
    //   ident    = "aesdsocket": prefix added to each log message
    //   option   = LOG_PID     : include process ID in each message
    //   facility = LOG_USER    : general user-level messages
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // sigaction(signum, act, oldact): register a signal handler
    //   signum = SIGINT/SIGTERM : signals to intercept (Ctrl+C and kill)
    //   act.sa_handler          : function called when signal is received
    //   oldact = NULL           : don't save the previous handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int socket_fd;

    // Step 1: Create a socket
    // socket(domain, type, protocol)
    //   domain   = AF_INET    : IPv4 address family
    //   type     = SOCK_STREAM: TCP (reliable, connection-oriented stream)
    //   protocol = 0          : auto-select protocol for the given type (TCP)
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Create socket failed");
        closelog();
        return -1;
    }

    // Step 2: Bind the socket to an address and port
    struct sockaddr_in server_addr, client_addr;
    // memset(ptr, value, size): zero out the struct to clear any padding/garbage bytes
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;    // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0: accept on all network interfaces
    server_addr.sin_port        = htons(PORT); // htons(): host to network byte order (big-endian)

    // bind(sockfd, addr, addrlen)
    //   sockfd  = socket_fd              : the socket to bind
    //   addr    = (struct sockaddr *)... : pointer to address struct (cast from sockaddr_in)
    //   addrlen = sizeof(server_addr)    : size of the address struct
    if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(socket_fd);
        closelog();
        return -1;
    }

    // Step 3: Listen for incoming connections
    // listen(sockfd, backlog)
    //   sockfd  = socket_fd: the bound socket to listen on
    //   backlog = 5        : max number of pending connections in the queue
    if (listen(socket_fd, 5) < 0) {
        perror("Listen failed");
        close(socket_fd);
        closelog();
        return -1;
    }

    // Step 4: Daemonize if -d was specified.
    // Fork here — after bind/listen succeed — so the parent can report errors before exiting.
    if (daemon_mode) {
        // fork(): create a child process
        //   parent gets child PID (> 0) → exits immediately
        //   child  gets 0             → continues as the daemon
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            close(socket_fd);
            closelog();
            return -1;
        }
        if (pid > 0) {
            // Parent: exit cleanly so the shell prompt returns
            close(socket_fd);
            closelog();
            return 0;
        }

        // Child: become a session leader to detach from the terminal
        // setsid(): create a new session; child becomes session leader with no controlling terminal
        if (setsid() < 0) {
            perror("setsid failed");
            close(socket_fd);
            closelog();
            return -1;
        }

        // Redirect stdin, stdout, stderr to /dev/null so daemon has no terminal I/O
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
    }

    // Step 5: Accept connections in a loop until SIGINT or SIGTERM is received
    while (!exit_requested) {
        socklen_t client_addr_len = sizeof(client_addr);

        // accept(sockfd, addr, addrlen)
        //   sockfd  = socket_fd        : the listening socket
        //   addr    = &client_addr     : filled with the client's IP and port by the kernel
        //   addrlen = &client_addr_len : in: buffer size; out: actual address size written
        //   returns a new fd for the connection; -1 if interrupted by a signal
        int client_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            if (exit_requested) break; // accept() interrupted by SIGINT/SIGTERM
            perror("Accept failed");
            continue;
        }

        // syslog(priority, format, ...): write a message to the system log
        //   LOG_INFO: informational severity level
        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        // Step 5: Receive data; append each newline-terminated packet to file.
        // dup(fd): duplicate client_fd so fdopen owns the copy and client_fd stays open for send()
        int dup_fd = dup(client_fd);
        if (dup_fd < 0) {
            perror("dup failed");
            close(client_fd);
            continue;
        }

        // fdopen(fd, mode): wrap a file descriptor into a buffered FILE* stream
        //   fd   = dup_fd: duplicate socket fd (fdopen takes ownership — fclose closes it)
        //   mode = "r"   : open for reading
        FILE *sock_stream = fdopen(dup_fd, "r");
        if (!sock_stream) {
            perror("fdopen failed");
            close(dup_fd);
            close(client_fd);
            continue;
        }

        // getline(lineptr, n, stream): read one line including '\n' from stream
        //   lineptr = &line      : malloc'd buffer, grown automatically via realloc
        //   n       = &len       : current buffer capacity, updated on each realloc
        //   stream  = sock_stream: FILE* to read from
        //   returns bytes read; -1 on EOF or error
        char *line = NULL; // getline allocates this buffer
        size_t len  = 0;   // getline sets and updates this capacity

        while (getline(&line, &len, sock_stream) != -1) {
            // fopen(path, "a"): open for appending; creates the file if it doesn't exist
            FILE *file = fopen("/var/tmp/aesdsocketdata", "a");
            if (!file) {
                perror("fopen failed");
                break;
            }
            // fputs(str, stream): write the line (including '\n') to the file
            fputs(line, file);
            fclose(file);

            // Step 6: Send full file content back to client immediately after each complete packet.
            // fopen(path, "r"): open file for reading
            FILE *rfile = fopen("/var/tmp/aesdsocketdata", "r");
            if (!rfile) {
                perror("fopen for read failed");
                break;
            }
            // fseek(file, offset, whence): move the file position pointer
            //   offset = 0        : no offset
            //   whence = SEEK_END : start from the end of the file
            // ftell(file): return current position = total file size in bytes
            fseek(rfile, 0, SEEK_END);
            long file_size = ftell(rfile);
            // rewind(file): reset file position pointer back to the beginning for reading
            rewind(rfile);

            // malloc(size): allocate file_size bytes on the heap to hold the file content
            char *file_content = malloc(file_size);
            if (!file_content) {
                perror("malloc failed");
                fclose(rfile);
                break;
            }
            // fread(ptr, size, nmemb, stream): read file content into buffer
            //   ptr    = file_content : destination buffer
            //   size   = 1            : read 1 byte per element
            //   nmemb  = file_size    : number of elements (total bytes) to read
            //   stream = rfile        : source FILE* stream
            fread(file_content, 1, file_size, rfile);
            fclose(rfile);

            // send(sockfd, buf, len, flags): transmit data to the client
            //   sockfd = client_fd    : connected client socket
            //   buf    = file_content : data buffer to send
            //   len    = file_size    : number of bytes to send
            //   flags  = 0            : normal blocking send, no special options
            send(client_fd, file_content, file_size, 0);
            free(file_content);
        }

        free(line);          // free buffer allocated by getline
        fclose(sock_stream); // closes dup_fd; client_fd remains open

        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
    }

    // Graceful shutdown: log exit message, clean up resources, delete data file
    syslog(LOG_INFO, "Caught signal, exiting");
    close(socket_fd);
    // unlink(path): delete the file from the filesystem
    unlink("/var/tmp/aesdsocketdata");
    closelog();

    return 0;
}