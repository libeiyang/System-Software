/**
 * \author Beiyang Li
 */


#include "sbuffer.h"


pthread_mutex_t sbuffer_mutex;



int sbuffer_init(sbuffer_t **buffer)
{
    *buffer = malloc(sizeof(sbuffer_t));
    if (*buffer == NULL) return SBUFFER_FAILURE;
    (*buffer)->head = NULL;
    (*buffer)->tail = NULL;

    //add a lock
    sem_init(&((*buffer)->numInBuffer_data),0,0);
    sem_init(&((*buffer)->numInBuffer_stor),0,0);
    pthread_mutex_init(&((*buffer)->lock),NULL);
    pthread_mutex_init(&((*buffer)->fifo_lock),NULL);

    return SBUFFER_SUCCESS;
}

int sbuffer_free(sbuffer_t **buffer)
{
    if ((buffer == NULL) || (*buffer == NULL))
        return SBUFFER_FAILURE;

    while ((*buffer)->head)
    {
        sbuffer_node_t *dummy;
        dummy = (*buffer)->head;
        (*buffer)->head = (*buffer)->head->next;
        free(dummy);
    }
    sem_destroy(&((*buffer)->numInBuffer_data));
    sem_destroy(&((*buffer)->numInBuffer_stor));
    pthread_mutex_destroy(&((*buffer)->lock));
    pthread_mutex_destroy(&((*buffer)->fifo_lock));

    free(*buffer);
    *buffer = NULL;

    return SBUFFER_SUCCESS;
}
/**
 * Removes the first sensor data in 'buffer' (at the 'head') and returns this sensor data as '*data'
 * If 'buffer' is empty, the function doesn't block until new sensor data becomes available but returns SBUFFER_NO_DATA
 * \param buffer a pointer to the buffer that is used
 * \param data a pointer to pre-allocated sensor_data_t space, the data will be copied into this structure. No new memory is allocated for 'data' in this function.
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
sbuffer_node_t* sbuffer_remove(sbuffer_t * buffer,sbuffer_node_t* buffer_node)
{
    if (buffer == NULL) return NULL;
    if (buffer->head == NULL) return NULL;
    pthread_mutex_lock(&(buffer->lock));

    if((buffer_node->nrOfRead)++==1)
    {	//second to ascess
        pthread_mutex_unlock(&(buffer->lock));
        if (buffer->head == buffer->tail) // buffer has only one node
        {
            buffer->head = buffer->tail = NULL;
        }
        else  // buffer has many nodes empty
        {
            buffer->head = buffer->head->next;
        }
        free(buffer_node);
        return buffer->head;
        //last one to access the data, so no lock needed
    }
    else
    {	//first to ascess

        buffer_node=buffer_node->next;
        pthread_mutex_unlock(&(buffer->lock));
        return buffer_node;
    }
}
/**
 * Inserts the sensor data in 'data' at the end of 'buffer' (at the 'tail')
 * \param buffer a pointer to the buffer that is used
 * \param data a pointer to sensor_data_t data, that will be copied into the buffer
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
*/
int sbuffer_insert(sbuffer_t *buffer, sensor_data_t *data) {
    //pthread_mutex_lock(&sbuffer_mutex);

    sbuffer_node_t *dummy;
    if (buffer == NULL)
        return SBUFFER_FAILURE;
    dummy = malloc(sizeof(sbuffer_node_t));
    if (dummy == NULL)
        return SBUFFER_FAILURE;
    dummy->data = *data;
    dummy->next = NULL;
    dummy->nrOfRead = 0;
    if (buffer->tail == NULL)
    {
        buffer->tail = dummy;
        buffer->head = dummy;
    }

    else //buffer not empty
    {
        buffer->tail->next = dummy;
        buffer->tail = buffer->tail->next;
    }
    //pthread_mutex_unlock(&sbuffer_mutex);
    return SBUFFER_SUCCESS;
}

void write_to_log(char * buffer, pthread_mutex_t mutex){
    pthread_mutex_lock(&mutex);

    FILE* fifo_file;
    fifo_file = fopen(FIFO_NAME, "w");

    if(fputs(buffer, fifo_file)==EOF)
    {
        fprintf(stderr, "Error when write data to fifo.\n");
        fclose(fifo_file);
        exit(EXIT_FAILURE);
    }
    free(buffer);
    fflush(stdout);
    fclose(fifo_file);
    //flush();

    //printf("Message send from sbuffer: %s\n", buffer);
    pthread_mutex_unlock(&mutex);
    return ;
}
