
#include "connmgr.h"



#define CONNMGR_ERR_HANDLER(condition,err_code)    \
  do {						            \
            if ((condition)) {   \
                DEBUG_PRINTF(#condition " failed\n");    \
            pthread_exit("");   \
            }                                 \
        } while(0)
struct sock{
    tcpsock_t* addr;
    int id;   //sock id
    int status;      //0 open  1 closed
    long lastActive;
    int sensorId;
};
typedef struct sock sock_t;       //used in connmgr contains tcpsock and some useful information

sock_t* connmgr;  //store socks
tcpsock_t * server;
struct pollfd * num;
int conn_counter;

//used to free all memory while exiting.e
void freeAtExit();
void * element_copy(void * src_element);

void connmgr_listen(int port_number,sbuffer_t* sbuffer)
{

    connmgr=(sock_t*)malloc(sizeof(sock_t));
    tcpsock_t *client;
    sensor_data_t data;
    sensor_data_t *dummy_data;
    char* send_buffer;  //fifo write buffer
    int bytes, result;
    long timeNow;
    long lastActive;
    num=(struct pollfd*)malloc(sizeof(struct pollfd));
    //CONNMGR_ERR_HANDLER(num==NULL,CONNMGR_MEMORY_ERROR);
    conn_counter=0;
    atexit(freeAtExit);

    printf("connect manager is starting to listen\n");
    tcp_passive_open(&server,port_number);
    //CONNMGR_ERR_HANDLER(result!=TCP_NO_ERROR,CONNMGR_SOCKOP_ERROR);

    connmgr->addr=server;
    connmgr->id=0; //add server sock in the connmgr
    connmgr->status=0;
    connmgr->sensorId=0;
    time(&connmgr->lastActive);
    num->fd=server->sd;
    num->events=POLLIN;
    while(1)
    {
        int det=poll(num,conn_counter+1,-1);

        if((num->revents&POLLIN)==POLLIN)  //check if there is connection  socket detector
        {
            //the first element in connmgr is server which act as employee
            tcp_wait_for_connection(server,&client);
            //CONNMGR_ERR_HANDLER(result!=TCP_NO_ERROR,CONNMGR_SOCKOP_ERROR);

            conn_counter++;
            sock_t* temp_connmgr;
            temp_connmgr= (sock_t*) realloc(connmgr,(1+conn_counter)*sizeof(sock_t));
            //free num upon failure
            //CONNMGR_ERR_HANDLER(temp_connmgr==NULL,CONNMGR_MEMORY_ERROR);
            connmgr=temp_connmgr;

            temp_connmgr=NULL;

            (connmgr+conn_counter)->addr=client;
            (connmgr+conn_counter)->id=conn_counter; //add client sock in the connmgr
            (connmgr+conn_counter)->status=0;
            (connmgr+conn_counter)->sensorId=0;
            (connmgr+conn_counter)->lastActive=0;

            printf("Incoming  client %d connection\n",conn_counter);
            fflush(stdout);
            struct pollfd* temp_num;
            temp_num=(struct pollfd*)realloc(num,(1+conn_counter)* sizeof(struct pollfd));
            //free num upon failure
            //CONNMGR_ERR_HANDLER(temp_num==NULL,CONNMGR_MEMORY_ERROR);
            num=temp_num;
            temp_num=NULL;
            (num+conn_counter)->fd=(connmgr+conn_counter)->addr->sd;      //add new poll detection
            (num+conn_counter)->events=POLLIN;
        }
        for(int i=1;i<=conn_counter;i++)            //detect if i can receive data
        {
            if((connmgr+i)->status) ////0 open  1 closed
            {            //if already closed
                continue;
            }

            if((((num+i)->revents&POLLIN)==POLLIN)|POLLERR)
            {
                // read sensor ID
                bytes = sizeof(data.id);
                result = tcp_receive((connmgr+i)->addr,(void *)&data.id,&bytes);
                if ((result==TCP_NO_ERROR) && bytes)
                {
                    if((connmgr+i)->sensorId==0)
                    {
                        (connmgr+i)->sensorId=data.id;
                    }
                    // read temperature
                    bytes = sizeof(data.value);
                    result = tcp_receive((connmgr+i)->addr,(void *)&data.value,&bytes);
                    if ((result==TCP_NO_ERROR) && bytes)
                    {
                        // read timestamp
                        bytes = sizeof(data.ts);
                        result = tcp_receive((connmgr+i)->addr, (void *)&data.ts,&bytes);
                        if ((result==TCP_NO_ERROR) && bytes)
                        {
                            if((connmgr+i)->lastActive==0){
                                (connmgr+i)->lastActive=(long int)data.ts;
                                asprintf(&send_buffer,"%ld A sensor node with %d has opened a new connection\n",data.ts,(connmgr+i)->sensorId);
                                write_to_log(send_buffer,sbuffer->fifo_lock);

                            }
                            if((long int)data.ts-(connmgr+i)->lastActive>TIMEOUT)
                            {
                                result=tcp_close(&((connmgr+i)->addr));
                                //CONNMGR_ERR_HANDLER(result!=TCP_NO_ERROR,CONNMGR_SOCKOP_ERROR);
                                (connmgr+i)->status=1;
                                printf("Timeout ! Connection with clint %d is closed by server\n", i);
                                asprintf(&send_buffer,"%ld The sensor node with %d is closed by server (Timeout)\n",data.ts,(connmgr+i)->sensorId);
                                write_to_log(send_buffer,sbuffer->fifo_lock);
                                fflush(stdout);
                                time(&lastActive);
                                continue;
                            }
                            (connmgr+i)->lastActive=(long int)data.ts;
                            printf("sensor id = %" PRIu16 " - temperature = %g - timestamp = %ld\n", data.id, data.value, (long int)data.ts);
                            //write in the buffer
                            dummy_data=(sensor_data_t*)malloc(sizeof(sensor_data_t));
                            *dummy_data=data;
                            sbuffer_insert(sbuffer,dummy_data);
                            free(dummy_data);
                            sem_post(&(sbuffer->numInBuffer_stor));
                            sem_post(&(sbuffer->numInBuffer_data));
                            time(&lastActive);
                            continue;
                        }
                    }

                }

                if (result==TCP_CONNECTION_CLOSED)
                {
                    time(&lastActive);
                    printf("Peer has closed connection\n");
                    asprintf(&send_buffer,"%ld A sensor node with %d has closed the connection\n",lastActive,(connmgr+i)->sensorId);
                    write_to_log(send_buffer,sbuffer->fifo_lock);
                    time(&lastActive);
                }

                else
                {
                    time(&lastActive);
                    printf("Error occurred on connection to peer\n");
                    asprintf(&send_buffer,"%ld Error occurred on connection to sensor %d\n",lastActive,(connmgr+i)->sensorId);
                    write_to_log(send_buffer,sbuffer->fifo_lock);

                }

                tcp_close(&((connmgr+i)->addr));
                //CONNMGR_ERR_HANDLER(result!=TCP_NO_ERROR,CONNMGR_SOCKOP_ERROR);

                (connmgr+i)->status=1;
                if(--det<0)
                    break;// break out for loop, Because you don't have to go through it all, just go through socket which is POLLIN
            }

        }

        time(&timeNow);
        if(timeNow-lastActive>TIMEOUT)
        {
            printf("Timeout! server closed \n");
            fflush(stdout);                //exit after timeout when there is no sensor connecting to server
            pthread_exit(EXIT_SUCCESS);
        }
    }
}

void freeAtExit()
{
    if(num!=NULL)
    {
        free(num);
    }

    if(server!=NULL)
        free(server);
    if(connmgr!=NULL)
    {

        for(int i=1;i<conn_counter+1;i++)
        {
            if((connmgr+i)->status==0)
                tcp_close(&((connmgr+i)->addr));
        }
        free(connmgr);
    }
    fprintf(stderr,"all cleared from connmgr\n");
}

