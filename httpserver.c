#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <unistd.h> // write
#include <string.h> // memset
#include <stdlib.h> // atoi
#include <stdbool.h> // true, false
#include <errno.h>
#include <ctype.h>
#include <getopt.h> // parse the command line
#include <pthread.h> // threads library
#include <signal.h> 
#include "queue.h" //queue
#include <semaphore.h> 

#define BUFFER_SIZE 512

pthread_mutex_t mutex;
sem_t empty_sem;
sem_t full_sem;

//init logfile stuff
int log_FD = -1;
bool log_bool = false;
sem_t log_sem;
int log_entries = 0;
int log_errors = 0;

//[init queue]
int head, tail, element;
int job_queue[1000];

void readWriteFD(int in_FD, int out_FD, int content_len){
	char *buf = (char *)malloc(32000); //allocate a buffer of 32kb
	ssize_t sz_rd = -1, sz_wr = -1;
	int bytes = 0; //initialize numBytes read/written
	//printf("- writing\n");

	while(bytes != content_len) { //while there's still bytes to read
		sz_rd = read(in_FD, buf, 32000);
		bytes += (int)sz_rd; //count the bytes read (for response message purposes)
		//printf("- bytes = %d\n", bytes);
		sz_wr = write(out_FD, buf, sz_rd); //print user input to standard output
	}
	//printf("- done writing\n");

	free(buf); //free mem
}

//module to write to logfile upon successful processing of a request
void rdWrLogFile(int log_FD, int data_FD, char* request, char* resource_name, int content_len){
	log_entries += 1;

	//log entry header
	char log_header[4000] = "";
	int log_header_bytes = sprintf(log_header, "%s /%s length %d", request, resource_name, content_len);
	write(log_FD, log_header, log_header_bytes);
	memset(log_header, 0, sizeof(log_header));
	
	if(strcmp(request, "HEAD") != 0){ //only add data if necessary 
		int counter = 0;
		
		while(counter != content_len){
			unsigned char chunk[1600] = "";
			char converted_chunk[16000] = "";
			char this_byte[100] = "";
			ssize_t chunk_size = read(data_FD, chunk, 1600); //read max 1600 bytes from the file
			for(int i = 0; i < (int)chunk_size; i++){ //for every byte of the chunk
				if(i%20 == 0){ //if were on the last byte of a logfile line
					sprintf(this_byte, "\n%08d %02x", i + counter, chunk[i]); //write a new line, the counter, space, converted byte
					strcat(converted_chunk, this_byte);
				} 
				else{
					sprintf(this_byte, " %02x", chunk[i]);
					strcat(converted_chunk, this_byte);
				}
				memset(this_byte, 0, strlen(this_byte));
			}
			//printf("%s", converted_chunk);
			counter += (int)chunk_size;
			write(log_FD, converted_chunk, strlen(converted_chunk));
			memset(chunk, 0, sizeof(chunk));
			memset(converted_chunk, 0, sizeof(converted_chunk));
		}
		
	}
	

	//"========\n" to denote ending
	char log_end[20] = "";
	int log_end_size = sprintf(log_end, "\n========\n");
	write(log_FD, log_end, strlen(log_end));
	memset(log_end, 0, sizeof(log_end));

}

void rdWrLogFileError(int log_FD, char* request, char* resource_name, int response_code){
	log_entries += 1;
	log_errors += 1;

	//write the log entry header in the specified format
	char log_header[4000] = "";
	int log_header_bytes = sprintf(log_header, "FAIL: %s /%s HTTP/1.1 --- response %d\n", request, resource_name, response_code);
	//printf("LOGHEADER: %s\n", log_header);
	write(log_FD, log_header, log_header_bytes);
	memset(log_header, 0, sizeof(log_header));

	//write "========\n" to denote ending
	char log_end[12];
	int log_end_size = sprintf(log_end, "========\n");
	write(log_FD, log_end, strlen(log_end));
	memset(log_end, 0, sizeof(log_end));

}

int validateRequest(char* request, char* resource_name, char* protocol){
	//if request != GET, PUT, or HEAD; return 400;
	if(strcmp(request, "GET") != 0){} 
	else if(strcmp(request, "PUT") != 0){} 
	else if(strcmp(request, "HEAD") != 0){
		return 400;
	}

	if (resource_name[0] == '/') { // "/" does not count towards 27 char limit
		memmove(resource_name, resource_name + 1, strlen(resource_name));
	}

	if(strlen(resource_name) > 27){
		//printf("%s", resource_name);
		return 400;
	}
	
	for(size_t i=0; i<strlen(resource_name); i++){
		if( isalnum((int)(resource_name[i])) ){} //alphanumeric?
		else if( (int)resource_name[i] == 45){} //'-'?
		else if( (int)resource_name[i] == 95){} //'_'?
		else{return 400;}
	}

	if(strcmp(protocol, "HTTP/1.1") != 0){
		return 400;
	}

	return 1;
}

int processGET(int socket_FD, char* resource_name){
	//printf("SOCKET: %d, RECNAME: %s\n", socket_FD, resource_name);
	if((strcmp(resource_name, "healthcheck") == 0) && (log_FD != -1)){
		//printf("INSIDE HEALTHCHECK COND\n");
		//printf("NUMERRORS: %d, NUMENTRIES: %d", log_errors, log_entries);
		char healthchecc[100] = "";
		//calculate the size
		char str_errors[10] = "";
		sprintf(str_errors, "%d\n", log_errors);
		char str_entries[10] = "";
		sprintf(str_entries, "%d", log_entries);
		int len = strlen(str_errors) + strlen(str_entries);

		int healthcheck_size = sprintf(healthchecc, "HTTP/1.1 200 OK\r\nContent-length: %d\r\n\r\n%d\n%d", len, log_errors, log_entries);
		send(socket_FD, healthchecc, healthcheck_size, 0);
		memset(healthchecc, 0, sizeof(healthchecc));
		
		// sem_wait(&log_sem);

		// //log entry header
		// char log_header[4000] = "";
		// int log_header_bytes = sprintf(log_header, "GET /healthcheck length %d\n", len);
		// write(log_FD, log_header, log_header_bytes);
		// memset(log_header, 0, sizeof(log_header));

		// //health check body (log errors and entries)
		// char health_body[40] = ""; 
		// sprintf(health_body, "%s%s", str_errors, str_entries); //make health body
		
		// unsigned char health_hex[100] = ""; //make buffer for the hex line
		// unsigned char health_byte[20] = ""; //buffer for conversion
		// sprintf(health_hex, "%08d", 0); //write counter to the buffer
		// strcat(health_hex, health_byte);
		// for(int i=0; i<strlen(health_body); i++){ //for each byte
		// 	sprintf(health_byte, " %02x", health_body[i]); //convert to hex and write to buffer
		// 	strcat(health_hex, health_byte);
		// }
		// write(log_FD, health_hex, strlen(health_hex));
		// memset(health_byte, 0, sizeof(health_byte));
		// memset(health_body, 0, sizeof(health_body));
		// memset(health_hex, 0, sizeof(health_hex));

		// //"========\n" to denote ending
		// char log_end[20] = "";
		// int log_end_size = sprintf(log_end, "\n========\n");
		// write(log_FD, log_end, strlen(log_end));
		// memset(log_end, 0, sizeof(log_end));

		// log_entries += 1; // update counter after

		// sem_post(&log_sem);
		return 1;
	}

	int this_file = open(resource_name, O_RDONLY, S_IRWXU); //open the specified file
	
	if(this_file == -1){ //if (this_file == -1){ return 404 error}
		//send msg to client
		char response_msg[4000] = "";
		int bytesToWrite = sprintf(response_msg, "HTTP/1.1 404 File Not Found\r\nContent-Length: 0\r\n\r\n");
		send(socket_FD, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

		close(socket_FD);

		//write error to logfile
		if(log_FD != -1){
			sem_wait(&log_sem);
			rdWrLogFileError(log_FD, "GET", resource_name, 404);
			sem_post(&log_sem);
		}
		return -1;
	} else if(errno == EACCES){ //else if errno == 13{ return 403 error}
		//send msg to client
		char response_msg[4000] = "";
		int bytesToWrite = sprintf(response_msg, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
		send(socket_FD, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

		close(socket_FD);
		
		//write error to logfile
		if(log_FD != -1){
			sem_wait(&log_sem);
			rdWrLogFileError(log_FD, "GET", resource_name, 403);
			sem_post(&log_sem);
		}

		return -1;
	} else{ //else handle the file
		//get content length of file
		struct stat sb;
		fstat(this_file, &sb);
		int content_len = sb.st_size;

		//send msg to client
		char response_msg[4000] = "";
        int bytesToWrite = sprintf(response_msg, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", content_len);
        send(socket_FD, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

		readWriteFD(this_file, socket_FD, content_len);

		close(socket_FD);

		close(this_file);
		this_file = open(resource_name, O_RDWR, S_IRWXU);

		if(log_FD != -1){
			sem_wait(&log_sem);
			rdWrLogFile(log_FD, this_file, "GET", resource_name, content_len);
			sem_post(&log_sem);
		}

		close(this_file);
		
		return 1;
	}
    
}

int processHEAD(int socket_FD, char* resource_name){
	int this_file = open(resource_name, O_RDONLY, S_IRWXU); //open() specified file

	if(this_file == -1){
		//send msg to client
		char response_msg[4000] = "";
		int bytesToWrite = sprintf(response_msg, "HTTP/1.1 404 File Not Found\r\nContent-Length: 0\r\n\r\n");
		send(socket_FD, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

		close(socket_FD);

		//write error to logfile
		if(log_FD != -1){
			sem_wait(&log_sem);
			rdWrLogFileError(log_FD, "HEAD", resource_name, 404);
			sem_post(&log_sem);
		}

		return -1;
	} else {
		//get content length of file
		struct stat sb;
		fstat(this_file, &sb);
		int content_len = sb.st_size;

		//send msg to client
		char response_msg[4000] = "";
		int bytesToWrite = sprintf(response_msg, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", content_len);
		send(socket_FD, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

		close(socket_FD);

		close(this_file);
		this_file = open(resource_name, O_RDWR, S_IRWXU);

		if(log_FD != -1){
			sem_wait(&log_sem);
			rdWrLogFile(log_FD, this_file, "HEAD", resource_name, content_len);
			sem_post(&log_sem);
		}

		return 1;
	}
}

int processPUT(int socket_FD, char* resource_name, int content_len){
	int this_file = open(resource_name, O_RDWR, S_IRWXU); //open() specified file
	//printf("- 1st open() attempt: %d\n", this_file);

	if(this_file == -1){ //if (file did not already exist)
		this_file = creat(resource_name, S_IRWXU); //create the file
		//printf("- we made the file: %d\n", this_file); 

		//read/write
		readWriteFD(socket_FD, this_file, content_len);

		//send msg to client
		char response_msg[4000] = "";
		int bytesToWrite = sprintf(response_msg, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");
		send(socket_FD, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

	} else{ //else (file already existed)
		//printf("- file opened\n");

		readWriteFD(socket_FD, this_file, content_len);

		//send msg to client
		char response_msg[4000] = "";
		int bytesToWrite = sprintf(response_msg, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
		send(socket_FD, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

	}

	close(socket_FD);
	
	close(this_file);
	this_file = open(resource_name, O_RDWR, S_IRWXU);

	/* [if log file is specified, write to it] */
	if(log_FD != -1){
		sem_wait(&log_sem);
		rdWrLogFile(log_FD, this_file, "PUT", resource_name, content_len);
		sem_post(&log_sem);
	}

	close(this_file);

	return 1; //return 1 (to let the main() know that we did the job)
}

void processRequest(int client_sockd){
	//[get the header]
	uint8_t buff[BUFFER_SIZE + 1];
	ssize_t bytes = recv(client_sockd, buff, BUFFER_SIZE, 0);
	buff[bytes] = 0; // null terminate
	//printf("[+] received %ld bytes from client\n[+] response: \n", bytes);
	//write(STDOUT_FILENO, buff, bytes);

	//[process header of message]

	//parse first line of header
	char request[bytes], resource_name[bytes], protocol[bytes];
	sscanf(buff, "%s %s %s", request, resource_name, protocol); //get the 3 parts
	//printf("REQUEST FOR %d: %s %s %s \n", client_sockd, request, resource_name, protocol);

	//make sure request is valid
	int stat = -1;
	if(validateRequest(request, resource_name, protocol) == 400){
		char response_msg[4000] = "";
		int bytesToWrite = sprintf(response_msg, "HTTP/1.1 400 Bad request\r\nContent-Length: 0\r\n\r\n");
		send(client_sockd, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

		close(client_sockd);

		//write error to logfile
		if(log_bool == true){
			sem_wait(&log_sem);
			rdWrLogFileError(log_FD, "GET", resource_name, 400);
			sem_post(&log_sem);
		}
	}

	//special cases for healthcheck file
	else if((strcmp(resource_name, "healthcheck") == 0) && (strcmp(request, "GET") != 0)){ //if trying to put/head a healthcheck, return 403
		//send msg to client
		char response_msg[4000] = "";
		int bytesToWrite = sprintf(response_msg, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
		send(client_sockd, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

		close(client_sockd);

		//write error to logfile
		if(log_FD != -1){

			sem_wait(&log_sem);
			rdWrLogFileError(log_FD, "GET", resource_name, 403);
			sem_post(&log_sem);
		}
	} else if((strcmp(resource_name, "healthcheck") == 0) && (log_FD == -1) && (strcmp(request, "GET") == 0)){ //if get healthcheck and logfile is not enabled, send 404
		//send msg to client
		char response_msg[4000] = "";
		int bytesToWrite = sprintf(response_msg, "HTTP/1.1 404 File Not Found\r\nContent-Length: 0\r\n\r\n");
		send(client_sockd, response_msg, bytesToWrite, 0);
		memset(response_msg, 0, sizeof(response_msg));

		//write error to logfile
		if(log_FD != -1){

			sem_wait(&log_sem);
			rdWrLogFileError(log_FD, "HEAD", resource_name, 404);
			sem_post(&log_sem);
		}
	}

	//[otherwise, process request accordingly]
	else if(strcmp(request, "GET") == 0){
		stat = processGET(client_sockd, resource_name);
	} else if(strcmp(request, "PUT") == 0){
		//get content length from the header
		char *token = strtok_r(buff, "\r\n", &buff);
		int content_len = -1;
		while( sscanf(token, "Content-Length: %d", &content_len) == 0){
			token = strtok_r(NULL, "\r\n", &buff);
		}
		//process the request
		stat = processPUT(client_sockd, resource_name, content_len);
	} else if(strcmp(request, "HEAD") == 0){ 
		stat = processHEAD(client_sockd, resource_name);
	}
	
}

void *threadProcess(){    
    while(true){ //threads will continuously work
		
		sem_wait(&full_sem);
		pthread_mutex_lock(&mutex); //lock the resource
		int this_job = dequeue(job_queue, &head); //get a job from the queue
		pthread_mutex_unlock(&mutex); //unlock
		sem_post(&empty_sem); //TALK TO EM

		processRequest(this_job); //process the job
  
    }
}


int main(int argc, char** argv) {

	int n_threads = 4; //default number of threads

    //use getopt to parse the command line
	int opt;
    while ((opt = getopt(argc, argv, "N:l:")) != -1 ){
        switch (opt) {
            case 'N': //if '-N' is specified, change n_threads to initialize
                n_threads = atoi(optarg);
				//printf("NUMTHREADS: %d\n", n_threads);
                break;

            case 'l':
                if((log_FD = open(optarg, O_RDWR | O_TRUNC, S_IRWXU)) == -1){ //if specified logfile doesnt exist
                    log_FD = creat(optarg, S_IRWXU); //create the logfile
                }
                //close(log_FD);
                log_bool = true;
                break;

            case '?': //continue upon undesired flags
                break; 

            default: 
                //printf("the args: %s, %s", argv[optind], argv[optind+1]);
                break;
        }
    } 

    /*
        Create sockaddr_in with server information
    */
   	//printf("port num: %s\n", argv[optind]);
    char* port = argv[optind]; //use the specified port

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(server_addr);

    /*
        Create server socket
    */
    int server_sockd = socket(AF_INET, SOCK_STREAM, 0);

    // Need to check if server_sockd < 0, meaning an error
    if (server_sockd < 0) {
        perror("socket");
    }

    /*
        Configure server socket
    */
    int enable = 1;

    /*
        This allows you to avoid: 'Bind: Address Already in Use' error
    */
    int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    /*
        Bind server address to socket that is open
    */
    ret = bind(server_sockd, (struct sockaddr *) &server_addr, addrlen);

    /*
        Listen for incoming connections
    */
    ret = listen(server_sockd, SOMAXCONN); // 5 should be enough, if not use SOMAXCONN

    if (ret < 0) {
        return 1;
    }

	//[init the queue]
	init(&head, &tail);
	//printf("made queue\n");

	if(log_bool == true){ //initialize log sem if logging was indicated
	    sem_init(&log_sem, 0, 1);
	}
	
	//init locks and sems for worker/dispatch threads
	pthread_mutex_init(&mutex, NULL);
	sem_init(&full_sem, 0, 0);
	sem_init(&empty_sem, 0, 1000);

	//[make the threadpool]
    pthread_t thread_pool[n_threads]; //initialize thread pool
    for(long i=0; i<n_threads; i++){ //create n-threads in the thread pool
        pthread_create(&thread_pool[i], NULL, &threadProcess, NULL); //MAKESURE FN THREADJOB MAKES THE THREAD SLEEP INITIALLY
    }

    /*
        Connecting with a client
    */
    struct sockaddr client_addr;
    socklen_t client_addrlen = sizeof(client_addr);

	while (true) {
		//printf("[+] server is waiting...\n");

		int client_sockd = accept(server_sockd, &client_addr, &client_addrlen);
		// Remember errors happen
		//printf("Client FD: %d\n", client_sockd);

		//if buffer is full, wait

		sem_wait(&empty_sem); 
		pthread_mutex_lock(&mutex); //lock the resources
		enqueue(job_queue, &tail, client_sockd); //add to queue
		pthread_mutex_unlock(&mutex); //unlock
		sem_post(&full_sem); //TALK TO THESE THREADS QUE
	}
}
