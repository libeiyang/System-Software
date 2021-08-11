/**
 * \author Beiyang Li
 */


#include "datamgr.h"


struct data{
    int sensor_ID;
    int room_ID;
    sensor_value_t running_avg;
    sensor_ts_t  last_modified;
    sensor_value_t* avg_buffer;
    int counter;
};

typedef struct data* data_t;

data_t data;
data_t finder;
int* map_buff;//temp value
dplist_t* map_list = NULL;


void* element_t_copy(void* element);
void element_t_free(void** element);
int element_t_compare(void* x, void* y);

void unlock_deadMutexLock_datamgr(void *arg)
{
    sbuffer_t* sbuffer;
    sbuffer = (sbuffer_t*) arg;
    pthread_mutex_unlock(&(sbuffer->lock));
}

void unlock_deadSemLock_datamgr(void *arg)
{
    sbuffer_t* sbuffer;
    sbuffer = (sbuffer_t*) arg;
    sem_post(&(sbuffer->numInBuffer_stor));
}

/**
 *  This method holds the core functionality of your datamgr. It takes in 2 file pointers to the sensor files and parses them.
 *  When the method finishes all data should be in the internal pointer list and all log messages should be printed to stderr.
 *  \param fp_sensor_map file pointer to the map file
 *  \param fp_sensor_data file pointer to the binary data file
 */
void datamgr_parse_sensor_files(FILE *fp_sensor_map, sbuffer_t *bf_sensor_data)
{
    time_t now;

    map_list = dpl_create(&element_t_copy,&element_t_free,&element_t_compare);
    data = (data_t)malloc(sizeof(struct data));
    char* send_buffer;

    map_buff = (int*)malloc(2*sizeof(int));

    atexit(datamgr_free);

    if(fp_sensor_map != NULL)
    {
        while(1)
        {
            if(fscanf(fp_sensor_map, "%d", map_buff) == EOF)
                break;
            if(fscanf(fp_sensor_map, "%d", map_buff+1) == EOF)
                break;
            data->room_ID = *map_buff;
            data->sensor_ID = *(map_buff+1);
            data->running_avg = 0;
            data->counter = 0;
            data->last_modified = 0;
            dpl_insert_sorted(map_list,data,true);

        }
    }
    else{//no sensor map
        time(&now);
        asprintf(&send_buffer,"Error when opening sensor_map file at %ld\n",now);
        write_to_log(send_buffer,bf_sensor_data->fifo_lock);
    }

    fclose(fp_sensor_map);
    //room id and sensor id insert finished

    //start to check shared buffer
    sensor_data_t* temp;
    finder = (data_t)malloc(sizeof (struct data));
    if(bf_sensor_data != NULL)
    {
        data_t dummy;
        dplist_node_t* dummy_node;
        sbuffer_node_t* buffer_node;

        pthread_cleanup_push(unlock_deadSemLock_datamgr,bf_sensor_data);
        sem_wait(&(bf_sensor_data->numInBuffer_data));
        pthread_cleanup_pop(0);
        buffer_node=bf_sensor_data->head;//read from head

        while (1)
        {
            temp = &(buffer_node->data);
            finder->sensor_ID = temp->id;
            dummy_node = dpl_get_reference_of_element(map_list,finder);
            //first check sensor ID in the map list
            if(dummy_node==NULL)
            {
                fprintf(stderr,"invalid sensor ID: %d\n",temp->id);
                asprintf(&send_buffer,"Received sensor data at %ld from invalid sensor ID: %d\n", temp->ts, temp->id);
                write_to_log(send_buffer,bf_sensor_data->fifo_lock);
            }
            else//valid sensor ID
            {
                dummy = (data_t)dpl_get_element_at_reference(map_list,dummy_node);
                dummy->last_modified = temp->ts;
                *((dummy->avg_buffer)+dummy->counter) = temp->value;
                dummy->counter++;

                if(dummy->counter == RUN_AVG_LENGTH)
                {
                    dummy->counter = 0;
                    if(dummy->running_avg == 0)
                        dummy->running_avg = 1;
                }
                if(dummy->running_avg != 0)//means we can calculate avg
                {
                    sensor_value_t avg = 0;
                    for(int i = 0;i<RUN_AVG_LENGTH;i++)
                        avg = avg + *(dummy->avg_buffer+i);
                    dummy->running_avg = avg/RUN_AVG_LENGTH;

                    if ((dummy->running_avg) > SET_MAX_TEMP) {
                        fprintf(stderr, "WARNING:High Temperature %2.2f Reported by sensor %d in room %d at %ld \n",dummy->running_avg,dummy->sensor_ID,dummy->room_ID,(long)(dummy->last_modified));
                        fflush(stderr);
                        asprintf(&send_buffer,"%ld The sensor node with %d reports it’s too hot (running avg temperature = %f) \n",dummy->last_modified,dummy->sensor_ID,dummy->running_avg);
                        write_to_log(send_buffer,bf_sensor_data->fifo_lock);
                    }
                    else if ((dummy->running_avg) < SET_MIN_TEMP) {
                        fprintf(stderr, "WARNING:Low Temperature %2.2f Reported by sensor %d in room %d at %ld \n",dummy->running_avg, dummy->sensor_ID, dummy->room_ID,(long)(dummy->last_modified));
                        fflush(stderr);
                        asprintf(&send_buffer,"%ld The sensor node with %d reports it’s too cold (running avg temperature = %f) \n",dummy->last_modified,dummy->sensor_ID,dummy->running_avg);
                        write_to_log(send_buffer,bf_sensor_data->fifo_lock);
                    }
                }


            }

            pthread_cleanup_push(unlock_deadSemLock_datamgr,bf_sensor_data);
            sem_wait(&(bf_sensor_data->numInBuffer_data));//wait producer(connmgr)
            pthread_cleanup_pop(0);
            pthread_cleanup_push(unlock_deadMutexLock_datamgr,bf_sensor_data);
            buffer_node = sbuffer_remove(bf_sensor_data,buffer_node);
            pthread_cleanup_pop(0);

        }
    }
}

/**
 * This method should be called to clean up the datamgr, and to free all used memory.
 * After this, any call to datamgr_get_room_id, datamgr_get_avg, datamgr_get_last_modified or datamgr_get_total_sensors will not return a valid result
 */
void datamgr_free()
{
    if (map_list!=NULL)
    {
        dpl_free(&map_list, true);
    }
    if(finder!=NULL)
    {
        free(finder);
    }
    if(data!=NULL)
    {
        free(data);
    }
    if(map_buff!=NULL)
    {
        free(map_buff);
    }
    printf("all cleared from data mgr\n");
}

/**
 * Gets the room ID for a certain sensor ID
 * Use ERROR_HANDLER() if sensor_id is invalid
 * \param sensor_id the sensor id to look for
 * \return the corresponding room id
 */
uint16_t datamgr_get_room_id(sensor_id_t sensor_id);

/**
 * Gets the running AVG of a certain senor ID (if less then RUN_AVG_LENGTH measurements are recorded the avg is 0)
 * Use ERROR_HANDLER() if sensor_id is invalid
 * \param sensor_id the sensor id to look for
 * \return the running AVG of the given sensor
 */
sensor_value_t datamgr_get_avg(sensor_id_t sensor_id);

/**
 * Returns the time of the last reading for a certain sensor ID
 * Use ERROR_HANDLER() if sensor_id is invalid
 * \param sensor_id the sensor id to look for
 * \return the last modified timestamp for the given sensor
 */
time_t datamgr_get_last_modified(sensor_id_t sensor_id);

/**
 *  Return the total amount of unique sensor ID's recorded by the datamgr
 *  \return the total amount of sensors
 */
int datamgr_get_total_sensors();

void* element_t_copy(void* element)
{
    data_t temp, ele;
    ele = (data_t)element;
    temp = (data_t)malloc(sizeof(struct data));
    assert(temp != NULL);
    temp->last_modified = ele->last_modified;
    temp->room_ID = ele->room_ID;
    temp->running_avg = ele->running_avg;
    temp->sensor_ID = ele->sensor_ID;
    temp->counter = ele->counter;
    temp->avg_buffer = (sensor_value_t*)malloc(RUN_AVG_LENGTH * sizeof(sensor_value_t));
    return temp;
}
void element_t_free(void** element)
{
    free(((data_t)(*element))->avg_buffer);
    free(*element);
}
int element_t_compare(void* x0, void* y0)
{
    data_t x, y;
    x = (data_t) x0;
    y = (data_t) y0;

    if ((int)(x->sensor_ID) > (int)(y->sensor_ID)) {
        return 1;
    }
    else if ((int)(x->sensor_ID) < (int)(y->sensor_ID)) {
        return -1;
    }
    else {
        return 0;
    }

}