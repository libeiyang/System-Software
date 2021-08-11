//
// Created by beiyang
//

#define _GNU_SOURCE
#include "connmgr.h"
#include "datamgr.h"
#include "sensor_db.h"
#include "sbuffer.h"

#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>


#define CHECK_FILE_OPEN(fp) 								\
		do {												\
			if ( (fp) == NULL )								\
			{												\
				perror("File open failed");					\
				exit( EXIT_FAILURE );						\
			}												\
		} while(0)

#define CHECK_FILE_CLOSE(err) 								\
		do {												\
			if ( (err) == -1 )								\
			{												\
				perror("File close failed");				\
				exit( EXIT_FAILURE );						\
			}												\
		} while(0)

void *data_manager_thread(void *arg);
void *storage_manager_thread(void *arg);
void *conn_manager_thread(void *arg);

void process_log();
void process_main(pid_t pid,int portNr);
void freeSbufferExit();

sbuffer_t * buffer;
pid_t pid_signal;

pthread_t thread_data_mgr;
pthread_t thread_storage_mgr;
pthread_t thread_conn_mgr;

void handler()
{
    printf("log process exits\n");
}


int main(int argc, char *argv[])
{
    if (argc!=2)
    {
        printf("Sensor system: Invalid input arguments! Please input port!\n");
        exit(EXIT_SUCCESS);
    }
    int portNr;
    portNr = atoi(argv[1]);
    //sbuffer_init(&buffer);//init sbuffer in process_main
    if(access(FIFO_NAME, F_OK) ==-1 )
    {
        int ret = mkfifo(FIFO_NAME, FIFO_MODE);
        if(ret==-1)
        {
            perror("mkfifo error:");
            exit(1);
        }
        printf("Create a new fifo.\n");
    }

    signal(SIGCHLD,handler);
    pid_t pid = fork();
    if(pid == 0)//child process
    {
        process_log();
    }
    else
    {
        process_main(pid,portNr);
    }
}

void process_log()
{
    FILE* log_fifo;
    FILE* log_file;
    char* str_result;
    char recv_buffer[256];


    log_fifo = fopen(FIFO_NAME, "r");//
    CHECK_FILE_OPEN(log_fifo);
    log_file = fopen(LOG_FILE_NAME, "a");//log
    CHECK_FILE_OPEN(log_file);

    time_t time_now;
    time(&time_now);

    fprintf(log_file, "%ld: Gateway log start record now!\n",(long)time_now);
    fflush(log_file);
    CHECK_FILE_CLOSE(fclose(log_file));
    CHECK_FILE_CLOSE(fclose(log_fifo));

    int Number = 1;

    //read FIFO
    while(1)
    {
        log_fifo = fopen(FIFO_NAME, "r");
        CHECK_FILE_OPEN(log_fifo);
        do {
            str_result = fgets(recv_buffer, 256, log_fifo);

            if(str_result != NULL)
            {
                log_file = fopen(LOG_FILE_NAME, "a");//log
                CHECK_FILE_OPEN(log_file);
                fprintf(log_file, "No.%d %s\n", Number, recv_buffer);
                Number++;
                CHECK_FILE_CLOSE(fclose(log_file));
            }
        } while (str_result != NULL);
        CHECK_FILE_CLOSE(fclose(log_fifo));
    }


    exit(EXIT_SUCCESS);
}

void process_main(pid_t pid,int portNr)
{


    if(sbuffer_init(&buffer)!=0)
    {
        printf("Couldn't initialize sbuffer!\n");
        exit(EXIT_FAILURE);
    }

    pid_signal=pid;
    atexit(freeSbufferExit);
    
    pthread_create(&thread_conn_mgr,NULL,conn_manager_thread,&portNr);
    pthread_create(&thread_data_mgr,NULL,data_manager_thread,NULL);
    pthread_create(&thread_storage_mgr,NULL,storage_manager_thread,NULL);
    pthread_join(thread_conn_mgr,NULL);
    pthread_cancel(thread_data_mgr);
    pthread_cancel(thread_storage_mgr);
    pthread_join(thread_storage_mgr,NULL);
    pthread_join(thread_data_mgr,NULL);
    printf("main profess finished!\n");

    exit(EXIT_SUCCESS);//atExit will be called
    //kill(pid,SIGCONT);



}

void freeSbufferExit()
{
    kill(pid_signal,SIGKILL);   //kill process log
    sbuffer_free(&buffer);
    printf("sbuffer is clear!\n");
}

void *data_manager_thread(void *arg)
{
    FILE* room_sensor_map;
    room_sensor_map = fopen("room_sensor.map", "r");
    printf("Start data manager thread.\n");
    datamgr_parse_sensor_files(room_sensor_map, buffer);
    //printf("We can reach here.\n");
    datamgr_free();
    printf("Exit data manager thread.\n");
    return NULL;
}
void disconnectDB (void* arg)
{
    DBCONN* db;
    db=(DBCONN*) arg;
    disconnect(db);
}
void *storage_manager_thread(void *arg)
{
    DBCONN* db;
    char* bufferToFIFO;
    time_t now;

    for(int i=0;i<3;i++)
    {
        db=init_connection(1,buffer);
        if(db!=NULL)
        {
            time(&now);
            printf("Database is connected successfully!\n");
            asprintf(&bufferToFIFO,"%ld A connection to the database server is made!\n",now);
            write_to_log(bufferToFIFO,buffer->fifo_lock);
            break;
        }
        else
        {
            printf("Error: Cannot connect to database. (Attempt: %d)\n",i);
            sleep(5);
        }
    }
    if(db==NULL)
    {
        time(&now);
        asprintf(&bufferToFIFO,"%ld Error: Cannot connect to database finally.\n",now);
        write_to_log(bufferToFIFO,buffer->fifo_lock);
        exit(EXIT_FAILURE);exit(EXIT_FAILURE);
    }

    pthread_cleanup_push(disconnectDB,db);
    insert_sensor_from_buffer(db,buffer) ;
    disconnect(db);
    pthread_cleanup_pop(0);

    time(&now);
    asprintf(&bufferToFIFO,"%ld Connection to SQL server lost\n",now);
    write_to_log(bufferToFIFO,buffer->fifo_lock);
    printf("Thread exit :storage_manager_thread exit!\n");
    return NULL;
}
void *conn_manager_thread(void *arg)
{
    connmgr_listen(*(int*) arg, buffer);
    //connmgr_free();
    return NULL;
}

