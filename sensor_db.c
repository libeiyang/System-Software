#include "sensor_db.h"

sqlite3 *db=NULL;

void unlock_deadMutexLock_stormgr(void *arg);
void unlock_deadSemLock_stormgr(void *arg);

/**
 * Make a connection to the database server
 * Create (open) a database with name DB_NAME having 1 table named TABLE_NAME  
 * \param clear_up_flag if the table existed, clear up the existing data when clear_up_flag is set to 1
 * \return the connection for success, NULL if an error occurs
 */
DBCONN *init_connection(char clear_up_flag, sbuffer_t* buffer)
{
    time_t now;
    int len;
    char *send_buffer;
    char *zErrMsg =NULL;
    // char **azResult=NULL; //二维数组存放结果
    /* 打开数据库 */
    len = sqlite3_open(TO_STRING(DB_NAME),&db);
    if( len )
    {
        time(&now);

        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));//sqlite3_errmsg(db)用以获得数据库打开错误码的英文描述。
        asprintf(&send_buffer,"%ld Unable to connect to SQL server\n",now);
        write_to_log(send_buffer,buffer->fifo_lock);
        sqlite3_close(db);
        return NULL;//NULL if error occurs
     }

    //table
    char sql_query[200];
	strcpy(sql_query, 	"CREATE TABLE IF NOT EXISTS "                             );
	strcat(sql_query, 	TO_STRING(TABLE_NAME)                               );
	strcat(sql_query, 	"(id INTEGER PRIMARY KEY AUTOINCREMENT, sensor_id INT, sensor_value DECIMAL(4,2), timestamp TIMESTAMP);" );

    if(sqlite3_exec(db,sql_query,NULL,NULL,&zErrMsg))
    {
        time(&now);
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        asprintf(&send_buffer,"%ld Can't create a new table\n",now);
        write_to_log(send_buffer,buffer->fifo_lock);
		sqlite3_free(zErrMsg);
		sqlite3_close(db);
		return NULL;
	}
    else{
        if(clear_up_flag==1)
        {
            strcpy(sql_query, 	"DELETE FROM "  									);
            strcat(sql_query, 	TO_STRING(TABLE_NAME)												);
            strcat(sql_query, 	";" );
            //a new sql query, need to exec again
            if(sqlite3_exec(db,sql_query,NULL,NULL,&zErrMsg))
            {
                time(&now);
                fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
                asprintf(&send_buffer,"%ld Unable to connect to SQL server\n",now);
                write_to_log(send_buffer,buffer->fifo_lock);
                sqlite3_free(zErrMsg);
                sqlite3_close(db);
                return NULL;
            }
            time(&now);
            asprintf(&send_buffer,"%ld New table created successfully\n",now);
            write_to_log(send_buffer,buffer->fifo_lock);
        }
	}
    sqlite3_free(zErrMsg);
    return db;

}

/**
 * Disconnect from the database server
 * \param conn pointer to the current connection
 */
void disconnect(DBCONN *conn)
{
    sqlite3_close(conn);
}

/**
 * Write an INSERT query to insert a single sensor measurement
 * \param conn pointer to the current connection
 * \param id the sensor id
 * \param value the measurement value
 * \param ts the measurement timestamp
 * \return zero for success, and non-zero if an error occurs
 */
int insert_sensor(DBCONN *conn, sensor_id_t id, sensor_value_t value, sensor_ts_t ts)
{
    char *zErrMsg =NULL;
    //get the value of id out and convert it into a string
	char id_string[10];
	char value_string[10];
    char ts_string[20];

	snprintf(id_string,sizeof(id_string),"%u",id);
	snprintf(value_string,sizeof(value_string),"%3.2f",value);
	snprintf(ts_string,sizeof(ts_string),"%ld",ts);

    /*插入数据  */
    //char *zErrMsg =NULL;
    char sql_query[200];
    //"INSERT INTO "TO_STRING(TABLE_NAME)" (sensor_id, sensor_value, timestamp) VALUES(("id_string"), ("value_string"),( "ts_string"));
    strcpy(sql_query, 	"INSERT INTO "                             );
	strcat(sql_query, 	TO_STRING(TABLE_NAME)                               );
	strcat(sql_query, 	" (sensor_id, sensor_value, timestamp) VALUES(("   );
	strcat(sql_query, 	id_string  									);
	strcat(sql_query, 	"), ("												);
    strcat(sql_query, 	value_string  									);
	strcat(sql_query, 	"), ("												);
    strcat(sql_query, 	ts_string  									);
	strcat(sql_query, 	"));"												);
    //printf("%s\n",sql_query);

    if(sqlite3_exec(conn,sql_query,NULL,NULL,&zErrMsg))
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(conn));
        sqlite3_free(zErrMsg);
        return -1;
    }
    sqlite3_free(zErrMsg);
    return 0;
    
}

/**
 * Write an INSERT query to insert all sensor measurements available in the file 'sensor_data'
 * \param conn pointer to the current connection
 * \param sensor_data a file pointer to binary file containing sensor data
 * \return zero for success, and non-zero if an error occurs
 */
int insert_sensor_from_buffer(DBCONN* conn, sbuffer_t* sensor_data)
{
    //read data from sbuffer

    if(sensor_data!=NULL)
    {
        sbuffer_node_t* buffer_node;
        pthread_cleanup_push(unlock_deadSemLock_stormgr,sensor_data);
        sem_wait(&(sensor_data->numInBuffer_stor));
        pthread_cleanup_pop(0);
        buffer_node=sensor_data->head;

        while(1)
        {
            sensor_data_t* temp;
            temp=&(buffer_node->data);
            insert_sensor(conn, temp->id, temp->value, temp->ts);
            pthread_cleanup_push(unlock_deadSemLock_stormgr,sensor_data);
            sem_wait(&(sensor_data->numInBuffer_stor));  //wait for connmgr adding the next one
            pthread_cleanup_pop(0);
            pthread_cleanup_push(unlock_deadMutexLock_stormgr,sensor_data);
            buffer_node = sbuffer_remove(sensor_data,buffer_node);
            pthread_cleanup_pop(0);
        }
    }
    else
        return 0;


    fprintf(stdout, "success insert from sbuffer!");
    return 0;

}

/**
  * Write a SELECT query to select all sensor measurements in the table 
  * The callback function is applied to every row in the result
  * \param conn pointer to the current connection
  * \param f function pointer to the callback method that will handle the result set
  * \return zero for success, and non-zero if an error occurs
  */
int find_sensor_all(DBCONN *conn, callback_t f)
{
    char *zErrMsg =NULL;
    char sql_query[200];
    //"SELECT * FROM" TO_STRING(TABLE_NAME);
    strcpy(sql_query, 	"SELECT * FROM "                             );
	strcat(sql_query, 	TO_STRING(TABLE_NAME)                               );
    strcat(sql_query, 	";"                             );
    //printf("%s\n",sql_query);

    if(sqlite3_exec(conn,sql_query,f,NULL,&zErrMsg))
    {
        fprintf(stderr, "Can't get all sensor: %s\n", sqlite3_errmsg(conn));
        sqlite3_free(zErrMsg);
        return -1;
    }
    sqlite3_free(zErrMsg);
    fprintf(stdout, "Get all sensor successfully\n");
    return 0;
}

/**
 * Write a SELECT query to return all sensor measurements having a temperature of 'value'
 * The callback function is applied to every row in the result
 * \param conn pointer to the current connection
 * \param value the value to be queried
 * \param f function pointer to the callback method that will handle the result set
 * \return zero for success, and non-zero if an error occurs
 */
int find_sensor_by_value(DBCONN *conn, sensor_value_t value, callback_t f)
{
    char *zErrMsg =NULL;
    char sql_query[200];
    char value_string[20];
	sprintf(value_string," %3.2f",value);

    //"SELECT * FROM "TO_STRING(TABLE_NAME)" WHERE sensor_value ="TO_STRING(value);
    strcpy(sql_query, 	"SELECT * FROM "                             );
	strcat(sql_query, 	TO_STRING(TABLE_NAME)                               );
    strcat(sql_query, 	" WHERE sensor_value ="                               );
    strcat(sql_query, 	value_string                              );
    strcat(sql_query, 	";"                             );
    //printf("%s\n",sql_query);

    if(sqlite3_exec(conn,sql_query,f,NULL,&zErrMsg))
    {
        fprintf(stderr, "Can't find sensor by value: %s\n", sqlite3_errmsg(conn));
        sqlite3_free(zErrMsg);
        return -1;
    }
    sqlite3_free(zErrMsg);
    fprintf(stdout, "Find sensor by value successfully\n");
    return 0;
}

/**
 * Write a SELECT query to return all sensor measurements of which the temperature exceeds 'value'
 * The callback function is applied to every row in the result
 * \param conn pointer to the current connection
 * \param value the value to be queried
 * \param f function pointer to the callback method that will handle the result set
 * \return zero for success, and non-zero if an error occurs
 */
int find_sensor_exceed_value(DBCONN *conn, sensor_value_t value, callback_t f)
{
    char *zErrMsg =NULL;
    char sql_query[200];
    char value_string[20];
	sprintf(value_string," %3.2f",value);

    //"SELECT * FROM " TO_STRING(TABLE_NAME) "WHERE sensor_value >" TO_STRING(value);
    strcpy(sql_query, 	"SELECT * FROM "                             );
	strcat(sql_query, 	TO_STRING(TABLE_NAME)                               );
    strcat(sql_query, 	" WHERE sensor_value >"                               );
    strcat(sql_query, 	value_string                              );
    strcat(sql_query, 	";"                             );
    //printf("%s\n",sql_query);

    if(sqlite3_exec(conn,sql_query,f,NULL,&zErrMsg))
    {
        fprintf(stderr, "Can't find sensor exceed value: %s\n", sqlite3_errmsg(conn));
        sqlite3_free(zErrMsg);
        return -1;
    }
    sqlite3_free(zErrMsg);
    fprintf(stdout, "Find sensor exceed value successfully\n");
    return 0;
}

/**
 * Write a SELECT query to return all sensor measurements having a timestamp 'ts'
 * The callback function is applied to every row in the result
 * \param conn pointer to the current connection
 * \param ts the timestamp to be queried
 * \param f function pointer to the callback method that will handle the result set
 * \return zero for success, and non-zero if an error occurs
 */
int find_sensor_by_timestamp(DBCONN *conn, sensor_ts_t ts, callback_t f)
{
    char *zErrMsg =NULL;
    char sql_query[200];
    char ts_string[20];
	sprintf(ts_string," %ld",ts);

    //"SELECT * FROM" TO_STRING(TABLE_NAME) "WHERE timestamp =" TO_STRING(ts);
    strcpy(sql_query, 	"SELECT * FROM "                             );
	strcat(sql_query, 	TO_STRING(TABLE_NAME)                               );
    strcat(sql_query, 	" WHERE timestamp ="                               );
    strcat(sql_query, 	ts_string                              );
    strcat(sql_query, 	";"                             );
    //printf("%s\n",sql_query);

    if(sqlite3_exec(conn,sql_query,f,NULL,&zErrMsg))
    {
        fprintf(stderr, "Can't find sensor by timestamp: %s\n", sqlite3_errmsg(conn));
        sqlite3_free(zErrMsg);
        return -1;
    }
    sqlite3_free(zErrMsg);
    fprintf(stdout, "Find sensor by timestamp successfully\n");
    return 0;
}
int find_sensor_after_timestamp(DBCONN *conn, sensor_ts_t ts, callback_t f)
{
    char *zErrMsg =NULL;
    char sql_query[200];
    char ts_string[20];
	sprintf(ts_string," %ld",ts);

    //"SELECT * FROM" TO_STRING(TABLE_NAME) "WHERE timestamp =" TO_STRING(ts);
    strcpy(sql_query, 	"SELECT * FROM "                             );
	strcat(sql_query, 	TO_STRING(TABLE_NAME)                               );
    strcat(sql_query, 	" WHERE timestamp >"                               );
    strcat(sql_query, 	ts_string                              );
    strcat(sql_query, 	";"                             );
    //printf("%s\n",sql_query);

    if(sqlite3_exec(conn,sql_query,f,NULL,&zErrMsg))
    {
        fprintf(stderr, "Can't find sensor by timestamp: %s\n", sqlite3_errmsg(conn));
        sqlite3_free(zErrMsg);
        return -1;
    }
    sqlite3_free(zErrMsg);
    fprintf(stdout, "Find sensor by timestamp successfully\n");
    return 0;
}
void unlock_deadMutexLock_stormgr(void *arg)   //clean up if pthread is canceled at cancel point pthread_mutex_lock
{
    sbuffer_t * sbuffer;
    sbuffer=(sbuffer_t*) arg;
    pthread_mutex_unlock(&(sbuffer->lock));
}
void unlock_deadSemLock_stormgr(void *arg)    //clean up if pthread is canceled at cancel point sem_wait
{
    sbuffer_t * sbuffer;
    sbuffer=(sbuffer_t*) arg;
    sem_post(&(sbuffer->numInBuffer_stor));
}