#include<err.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<unistd.h>

#include <errno.h>
#include <ctype.h>
#include <getopt.h> // parse the command line
#include <pthread.h> // threads library
#include <semaphore.h> 
#include <signal.h> 

#include "queue.h" //queue

pthread_mutex_t mutex;
sem_t empty_sem;
sem_t full_sem;

int n_parallel = 4; //number of parallel connections to be maintained; default at 4
int interval_HC = 5; //number of requests to be handled before we send a healthcheck; default at 5
int serverports[32][3];
int portcounter = 0;

//[init queue]
int head, tail, element;
int job_queue[1000];


/*
 * client_connect takes a port number and establishes a connection as a client.
 * connectport: port number of server to connect to
 * returns: valid socket if successful, -1 otherwise
 */
int client_connect(uint16_t connectport) {
    int connfd;
    struct sockaddr_in servaddr;

    connfd=socket(AF_INET,SOCK_STREAM,0);
    if (connfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);

    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(connectport);

    /* For this assignment the IP address can be fixed */
    inet_pton(AF_INET,"127.0.0.1",&(servaddr.sin_addr));

    if(connect(connfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0)
        return -1;
    return connfd;
}

/*
 * server_listen takes a port number and creates a socket to listen on 
 * that port.
 * port: the port number to receive connections
 * returns: valid socket if successful, -1 otherwise
 */
int server_listen(int port) {
    int listenfd;
    int enable = 1;
    struct sockaddr_in servaddr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
        return -1;
    if (bind(listenfd, (struct sockaddr*) &servaddr, sizeof servaddr) < 0)
        return -1;
    if (listen(listenfd, 500) < 0)
        return -1;
    return listenfd;
}

/*
 * bridge_connections send up to 100 bytes from fromfd to tofd
 * fromfd, tofd: valid sockets
 * returns: number of bytes sent, 0 if connection closed, -1 on error
 */
int bridge_connections(int fromfd, int tofd) {
    char recvline[1500];
    int n = recv(fromfd, recvline, 100, 0);
    if (n < 0) {
        printf("connection error receiving\n");
        return -1;
    } else if (n == 0) {
        printf("receiving connection ended\n");
        return 0;
    }
    recvline[n] = '\0';
    //printf("%s", recvline);
    //sleep(1);
    n = send(tofd, recvline, n, 0);
    if (n < 0) {
        printf("connection error sending\n");
        return -1;
    } else if (n == 0) {
        printf("sending connection ended\n");
        return 0;
    }
    return n;
}

/*
 * bridge_loop forwards all messages between both sockets until the connection
 * is interrupted. It also prints a message if both channels are idle.
 * sockfd1, sockfd2: valid sockets
 */
void bridge_loop(int sockfd1, int sockfd2) {
    fd_set set;
    struct timeval timeout;

    int fromfd, tofd;
    while(1) {
        // set for select usage must be initialized before each select call
        // set manages which file descriptors are being watched
        FD_ZERO (&set);
        FD_SET (sockfd1, &set);
        FD_SET (sockfd2, &set);

        // same for timeout
        // max time waiting, 5 seconds, 0 microseconds
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        // select return the number of file descriptors ready for reading in set
        switch (select(FD_SETSIZE, &set, NULL, NULL, &timeout)) {
            case -1:
                printf("error during select, exiting\n");
                return;
            case 0:
                printf("both channels are idle, waiting again\n");
                continue;
            default:
                if (FD_ISSET(sockfd1, &set)) {
                    fromfd = sockfd1;
                    tofd = sockfd2;
                } else if (FD_ISSET(sockfd2, &set)) {
                    fromfd = sockfd2;
                    tofd = sockfd1;
                } else {
                    printf("this should be unreachable\n");
                    return;
                }
        }
        if (bridge_connections(fromfd, tofd) <= 0)
            return;
    }
}

/*
    update port info by retrieving healthcheck, getting number of errors and entries sent
    takes in a (32x3) array of ports
    * sends a healthcheck to all servers, updating a (32x3) array of ports, errors, and entries
    * returns (32x3) array
*/
int* health_probe(int index){
    //printf(" - HEALTHCHECK TO: %d\n", serverports[index][0]);

    int serverfd = client_connect(serverports[index][0]); //connect to the server in question

    static int portinfo[3]; //make int array of size 2, to store healthcheck info from this port

    //send a healthcheck to the i-th server's FD
    //printf(" - SENDING HEALTHCHECK\n");
    char healthcheck[100] = "";
    int szof_healthcheck = sprintf(healthcheck, "GET /healthcheck HTTP/1.1\r\n\r\n");
    send(serverfd, healthcheck, szof_healthcheck, 0);

    //recv the response
    char recvline[1500] = "";
    // int n = recv(serverfd, recvline, 100, 0);
    int n = 0;
    int sz_recv = 0;
    while( (sz_recv = recv(serverfd, recvline, 100, 0)) != 0){
        n += sz_recv;
    }
    if (n < 0) {
        printf("error recieving from port %d\n", serverports[index][0]);
        return -1;
    } else if (n == 0) {
        printf("receiving connection ended\n");
        return 0;
    }
    recvline[n] = '\0';
    //printf("%s\n", recvline);

    int length, num_errors, num_entries;
    int nscan = sscanf(recvline, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n%d\n%d", &length, &num_errors, &num_entries);
    if(nscan < 1){
        //
    }
    //printf(" - HEALTHCHECK INFO: %d errors, %d entries\n", num_errors, num_entries);

    //use retrieved info to update serverports[i][1, 2]
    portinfo[0] = num_errors;
    portinfo[1] = num_entries;

    return portinfo;
}

/*
    Search thru the global 2d array of ports for 
    * a server w/ 0 entries
    * a server w/ least number of entries
    * or least number of entries AND least number of errors
    return the index of the selected port
*/
int find_server(){
    //find the server w the min number of requests received to send this one to 
    int assigned_index = 0; //initialize the index of the port we're gonna use
    for(int i=1; i<portcounter; i++){ //for each port
        if(serverports[i][2] == 0){ //if a server has had 0 requests
            assigned_index = i; //assign this index to a variable
            break; //use it immediately
        } else if(serverports[i][2] < serverports[assigned_index][2]){ //else if a server is a current min
            assigned_index = i; //assign this index to a variable
        }else if(serverports[i][2] == serverports[assigned_index][2]){ // if 2 servers have the same number of requests
            //use the one w the least amount of errors
            if(serverports[i][1] < serverports[assigned_index][1]){ //if the ith server has less errors then use it 
                assigned_index = i; //assign this index to a variable
            }
        }
    }
    return assigned_index;
}

/*
    * each worker thread gets a job
    * finds the best server to use
    * then sends the job to that server
*/
void *worker_thread(){
    while(1){
       // printf("WORKERTHREAD WAIT\n");

        sem_wait(&full_sem);
		pthread_mutex_lock(&mutex); //lock the resource
		
        int job_FD = dequeue(job_queue, &head); //get a job from the queue (job_queue shared)
        int server_index = find_server(); //find the best server (serverports also shared)
        serverports[server_index][2] += 1; //update the number of entries
        //printf("JOB %d SENT TO SERVER %d\n", job_FD, serverports[server_index][0]); 
		
        pthread_mutex_unlock(&mutex); //unlock
        sem_post(&empty_sem); //TALK TO EM

        int serverfd = client_connect(serverports[server_index][0]);//open the connection to this server
        
        bridge_loop(job_FD, serverfd); //bridge a connection b/w this request and the selected server
        //printf("DONE WITH JOB %d\n", job_FD);
        close(job_FD); //once done, close the accepted FD

    }
}


int main(int argc,char **argv) {
    int connfd, listenfd, acceptfd;
    uint16_t connectport, listenport;

    //init locks and sems for worker/dispatch threads
	pthread_mutex_init(&mutex, NULL);
	sem_init(&full_sem, 0, 0);
	sem_init(&empty_sem, 0, 1000);

    init(&head, &tail); //init queue

    if (argc < 3) {
        printf("missing arguments: usage %s port_to_connect port_to_listen", argv[0]);
        return 1;
    }

    //use getopt to parse the command line
	int opt; 
    while ((opt = getopt(argc, argv, "N:R:")) != -1 ){
        switch (opt) {
            case 'N': //if '-N' is specified, change number of connections to maintain
                n_parallel = atoi(optarg);
                break;

            case 'R':
                interval_HC = atoi(optarg) + 1; //set number of requests b/w each healthcheck
                break;

            case '?': //continue upon undesired flags
                break; 

            default:
                break;
        }
    }

    listenport = atoi(argv[optind]); //loadbalancer listens from first port
    optind += 1;

    for(; optind < argc; optind++){ //get all the ports from the command line
        serverports[portcounter][0] = atoi(argv[optind]); //first col the port number
        serverports[portcounter][1] = 0; //second col is the number of errors
        serverports[portcounter][2] = 0; //third col is the number of requests processed
        //printf("THIS PORT ADDED TO ARRAY: %d\n", serverports[portcounter][0]);
        portcounter += 1;
    }

    //[make the threadpool]
    pthread_t thread_pool[portcounter]; //initialize thread pool
    for(long i=0; i<portcounter; i++){ //create n-threads in the thread pool
        pthread_create(&thread_pool[i], NULL, &worker_thread, NULL); 
    }

    if ((listenfd = server_listen(listenport)) < 0) //listens for incoming connections, given first port
        err(1, "failed listening");

    int interval_counter = 0;
    while(1){ //continuously accept connections (and add them to a queue)
        
        if ((acceptfd = accept(listenfd, NULL, NULL)) < 0) //accept a connection received from the first port
            err(1, "failed accepting");
        //printf("RECEIVED JoB %d\n", acceptfd);
	    
        interval_counter += 1; //update number of requests received by load balancer, for interval HC purposes
        //printf(" - received %d requests so far\n", interval_counter);

        if(interval_counter % interval_HC == 0){ //do a global healthcheck and update ports of the servers if needed
            //printf("DOING GLOBAL HEALTHCHECK\n");
            int *updated_portinfo;
            for(int i=0; i<portcounter; i++){ //check all servers 
                //printf("HEALTHCHECKING PORT %d\n", serverports[i][0]);
                updated_portinfo = health_probe(i); //get updated errors and entries for the ith server
                //printf(" - UPDATED INFO: err=%d, en=%d\n", updated_portinfo[0], updated_portinfo[1]);
                serverports[i][1] = updated_portinfo[0]; //update errors
                serverports[i][2] = updated_portinfo[1]; //update entries
            }
        }
        
        sem_wait(&empty_sem); 
	pthread_mutex_lock(&mutex); //lock the resources

	enqueue(job_queue, &tail, acceptfd); //add to queue
        //printf(" - %d added to job queue\n", acceptfd);
        
	pthread_mutex_unlock(&mutex); //unlock
	sem_post(&full_sem);
    }
}
