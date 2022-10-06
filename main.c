/*
|-------------------------------------------------------------------|
| HCMC University of Technology                                     |
| Telecommunications Departments                                    |
| Gateway software for controlling the SLS                          |
| Version: 2.0                                                      |
| Author: sonvq@hcmut.edu.vn                                        |
| Date: 01/2019                                                     |
| - Support DB using MySQL                                          |
| - Discovery service                                               |                   |
| - compile: gcc -o main main.c $(mysql_config --libs --cflags)     |
|-------------------------------------------------------------------|

Topology description:

        |----------|     IPv6     |-----------|   IPv4/IPv6     |----------|
        | 6LoWPAN  |<------------>|  Gateway  |<--------------->| Client   |   
        | network  |   wireless   | + BR + DB |  wired/wireless | software |
        |----------|              |-----------|                 |----------|

*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/time.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <ifaddrs.h>

//#ifdef USING_SQL_SERVER
#include <mysql/mysql.h>
//#endif

//#include "sls.h"
//#include "sls_cli.h"     
#include "util.h"      



#define BUFSIZE 2048
#define MAXBUF  sizeof(cmd_struct_t)  


#define SERVICE_PORT            21234	         /* hard-coded port number */
#define REPORT_SERVER_PORT      21235            /* hard-coded port number */
#define REPORT_SERVER_IP		"127.0.0.1"


#define SHOW_FULL_DB            0

/* if using simulation: set to TRUE to reduce the sim time */
#define USING_LESS_RETRANS_CONF 1

#if (USING_LESS_RETRANS_CONF)
#define MAX_TIMEOUT             5           // seconds for a long chain topology 60 nodes: 5s
#define TIME_OUT                5           // seconds: recommend 5s
#define NUM_RETRANS_AUTHEN      2           // for authentication; default = 2
#define NUM_RETRANS_SET_KEY     2           // for setting application key: default = 2
#define NUM_RETRANS             2           // for commands: default = 2
#else
#define MAX_TIMEOUT             10          // seconds for a long chain topology 60 nodes: 10s
#define TIME_OUT                8           // seconds: recommend 8s
#define NUM_RETRANS_AUTHEN      5           // for authentication; default = 5
#define NUM_RETRANS_SET_KEY     5           // for setting application key: default = 5
#define NUM_RETRANS             5           // for commands: default = 5
#endif

#define clear() printf("\033[H\033[J")


static  struct  sockaddr_in6 rev_sin6;
static  int     rev_sin6len;
static  char    str_port[5];
        uint8_t payload[MAX_CMD_LEN];    

static  int     rev_bytes;
static  char    rev_buffer[MAXBUF];
static  char    dst_ipv6addr[40];
static  char    cmd[20];
static  char    arg[32];

static node_db_struct_t node_db;
static node_db_struct_t node_db_list[MAX_NUM_OF_NODE]; 

static env_struct_t env_db;

struct  pollfd fd;
int     res;

static  char    dst_ipv6addr_list[MAX_NUM_OF_NODE][40];

static  cmd_struct_t  tx_cmd, rx_reply, emergency_reply;
static  cmd_struct_t *cmdPtr;
static  char *p;

// bien cho server
//static  cmd_struct_t  pi_tx_cmd, pi_rx_reply;
static  cmd_struct_t *pi_cmdPtr;
static  char *pi_p; 

static  int node_id, num_of_node, timeout_val;

/*prototype definition */
static void prepare_cmd(int nodeid);
static int  read_node_list();
static void run_node_discovery();
static boolean authenticate_node(int node_id, uint32_t challenge_code, uint16_t challenge_res, int *res);
static int  ip6_send_cmd (int nodeid, int port, int retrans, boolean encryption_en);
static void init_main();
static boolean is_cmd_of_gw(cmd_struct_t cmd);
static void process_gw_cmd(cmd_struct_t cmd, int nodeid);


/* database functions */
static void finish_with_error(MYSQL *con);
static void get_db_row(MYSQL_ROW row, int i);
static int  execute_sql_cmd(char *sql);


static void show_sql_db();
static void show_local_db();
static void update1_sql_db();
static void update2_sql_db();
static void update_sql_db();
static void update1_sql_row(int nodeid);
static void update2_sql_row(int nodeid);
static void update3_sql_row(int nodeid);
static void update_sql_row(int nodeid);
static void update_sql_sensor(int nodeid);

static int  execute_broadcast_cmd(cmd_struct_t cmd, int val, int mode);
static int  execute_multicast_cmd(cmd_struct_t cmd);
static int  execute_broadcast_general_cmd(cmd_struct_t cmd, int mode);
static boolean is_node_valid(int node);
static boolean is_node_connected(int node);
static void auto_set_app_key();
static void set_node_app_key (int node_id);
static void run_reload_gw_fw();
static int  num_of_active_node();
static void reset_reply_data();
static void reset_node_cnt(int node_id);
static char* add_ipaddr(char *buf, int nodeid);

static void gen_app_key_for_node(int nodeid);

static void make_packet_for_node(cmd_struct_t *cmd, uint16_t nodeid, boolean encryption_en);
static boolean check_packet_for_node(cmd_struct_t *cmd, uint16_t nodeid,  boolean encryption_en);
static void reset_sequence(int nodeid);
static void update_sensor_data(int nodeid, env_struct_t env_db);
static void show_network_topo();
static void send_data_to_server(int node_id); 

static void check_border_router();
static void check_ip_interface();


struct timeval t0, t1;
time_t rawtime;
struct tm *timeinfo;

MYSQL *con;
char *sql_cmd; 

/* database infor */
static char sql_server_ipaddr[20] ="localhost";
static char sql_username[20]= "root";
static char sql_password[20]= "Son100480";
static char sql_db[20] = "sls_db";
static char sim_gw_ipv6[40] = "aaaa::c30c:0:0:1"; 


/*------------------------------------------------*/
void finish_with_error(MYSQL *con) {
#if (USING_SQL_SERVER_ENABLE==1)   
    fprintf(stderr, "%s\n", mysql_error(con));
    mysql_close(con);
    //exit(1);        
#endif
}


void show_network_topo() {
    printf("\n");
    printf("NETWORK TOPOLOGY \n");
    printf("\n");
    printf("    |----------|     IPv6     \033[1;35m|-----------|\033[0m      IPv4       |----------| \n");
    printf("    | 6LoWPAN  |<------------>\033[1;35m|  Gateway  |\033[0m<--------------->| Client   | \n");
    printf("    | network  |   wireless   \033[1;35m| + BR + DB |\033[0m  wired/wireless | software | \n");
    printf("    |----------|              \033[1;35m|-----------|\033[0m                 |----------| \n");
    printf("\n");
}

/*------------------------------------------------*/
#if (USING_SQL_SERVER_ENABLE==1) 
void get_db_row(MYSQL_ROW row, int i) {
    node_db_list[i].index       = atoi(row[0]);
    node_db_list[i].id          = atoi(row[1]);
    strcpy(node_db_list[i].ipv6_addr,row[2]);
    strcpy(node_db_list[i].app_key,row[15]);
    strcpy(dst_ipv6addr_list[node_db_list[i].id], node_db_list[i].ipv6_addr);

    /* if border router: always connected */
    if (i==0) {
        strcpy(node_db_list[i].connected,"Y");

    /*if using simulation, use default IPv6 of border router */    
#if (SIMULATION_BUILD_ENABLE)  
        strcpy(node_db_list[i].ipv6_addr, sim_gw_ipv6);
#endif     
    }        
}
#endif


/*------------------------------------------------*/
void gen_app_key_for_node(int nodeid) {
    uint8_t i;
    unsigned char byte_array[16];
    char *result;

    gen_random_key_128(byte_array);
    convert_array2str(byte_array,sizeof(byte_array),&result);
    strcpy(node_db_list[nodeid].app_key, result);
    //printf(" - Key for node %2d: \033[1;32m %s \033[0m\n", nodeid, node_db_list[nodeid].app_key);
}

/*------------------------------------------------*/
void init_main() {
    int i;

    timeout_val = TIME_OUT;
    strcpy(node_db_list[0].connected,"Y");
    node_db_list[0].authenticated = TRUE;
    node_db_list[0].encryption_mode = ENCRYPTION_MODE;

    printf("\n - GENERATE RANDOM KEYS (128 bits) FOR %d NODE(S) ", num_of_node );
    for (i=1; i<num_of_node; i++) {
        gen_app_key_for_node(i);        
        reset_sequence(i);        
        node_db_list[i].encryption_phase = FALSE;
        node_db_list[i].async_prr = 0;

        node_db_list[i].next_hop_addr[0]=0xfe;
        node_db_list[i].next_hop_addr[1]=0x80;
        node_db_list[i].next_hop_addr[2]=0x00;
        node_db_list[i].next_hop_addr[3]=0x00;
        node_db_list[i].next_hop_addr[4]=0x00;
        node_db_list[i].next_hop_addr[5]=0x00;
        node_db_list[i].next_hop_addr[6]=0x00;
        node_db_list[i].next_hop_addr[7]=0x00;
    }
    
    update_sql_db();

    printf("... DONE \n\n");
    //show_sql_db();
}


/*------------------------------------------------*/
void reset_node_cnt (int node_id) {
    node_db_list[node_id].async_seq = 0;
    node_db_list[node_id].num_emergency_msg = 0;
    node_db_list[node_id].async_prr = 0;
}

/*------------------------------------------------*/
void set_node_app_key (int node_id) {
    int res, j, i;
    unsigned char byte_array[16];

    tx_cmd.cmd = CMD_SET_APP_KEY;    
    tx_cmd.type = MSG_TYPE_HELLO;
    tx_cmd.err_code = 0;
    convert_str2array(node_db_list[node_id].app_key, byte_array, 16);

    printf("\n1. Set key for node \033[1;32m%d\033[0m (16 bytes), key = [", node_id);
    for (i = 0; i<16; i++) {printf("%02X", byte_array[i]);}
    printf("]\n");


    for (j = 0; j<16; j++) {
        tx_cmd.arg[j] = byte_array[j];
    }

    // asssign an APP-ID
    tx_cmd.arg[16] = node_id;

    // try NUM_RETRANS_SET_KEY times
    //res = ip6_send_cmd(node_id, SLS_NORMAL_PORT, NUM_RETRANS_SET_KEY, false);
    res = ip6_send_cmd(node_id, SLS_NORMAL_PORT, NUM_RETRANS_SET_KEY, node_db_list[node_id].encryption_phase);
    if (res == -1) {
        printf(" - ERROR: set_node_app_key process \n");
    } else if (res == 0) {
        printf(" - Set App Key for node \033[1;32m%d\033[0m [\033[1;32m%s\033[0m] ... failed \n", node_id, node_db_list[node_id].ipv6_addr); 
    } else { 
        node_db_list[node_id].encryption_phase = TRUE;
        printf(" - Set App Key for node \033[1;32m%d\033[0m [\033[1;32m%s\033[0m] ... successful, encryption_phase = %d \n", node_id, node_db_list[node_id].ipv6_addr, 
                                                                                    node_db_list[node_id].encryption_phase);   
    }
}

/*------------------------------------------------*/
void auto_set_app_key() {
    int res, i, j;
    
    printf("\n\033[1;32mIII. AUTO SET APP KEY PROCESS....\033[0m\n");
    for (i = 1; i < num_of_node; i++)
        if (is_node_connected(i))   set_node_app_key(i);
}


/*------------------------------------------------*/
void update1_sql_row(int nodeid) {
#if (USING_SQL_SERVER_ENABLE)
    char sql[400];
    int i;
    char *result;
    char buf[MAX_CMD_DATA_LEN];

    memcpy(buf, &node_db_list[nodeid].last_emergency_msg, 20);    
    convert_array2str(buf, sizeof(buf), &result);    

    if (is_node_connected(nodeid)) {
        sprintf(sql,"UPDATE sls_db SET connected='Y', num_req=%d, num_rep=%d, num_timeout=%d, last_cmd=%d, last_seen='%s', num_of_retrans=%d, rf_channel=%d, rssi=%d, lqi=%d, pan_id=%d, tx_power=%d, last_err_code=%d, num_emergency_msg=%d, last_emergency_msg='%s'  WHERE node_id=%d;", 
                node_db_list[nodeid].num_req, node_db_list[nodeid].num_rep, node_db_list[nodeid].num_timeout, node_db_list[nodeid].last_cmd, 
                node_db_list[nodeid].last_seen, node_db_list[nodeid].num_of_retrans, node_db_list[nodeid].channel_id, node_db_list[nodeid].rssi, node_db_list[nodeid].lqi, 
                node_db_list[nodeid].pan_id, node_db_list[nodeid].tx_power,node_db_list[nodeid].last_err_code, 
                node_db_list[nodeid].num_emergency_msg, result,
                nodeid);
    } else {
        sprintf(sql,"UPDATE sls_db SET connected='N', num_req=%d, num_rep=%d, num_timeout=%d, last_cmd=%d, last_seen='%s', num_of_retrans=%d, rf_channel=%d, rssi=%d, lqi=%d, pan_id=%d, tx_power=%d, last_err_code=%d, num_emergency_msg=%d, last_emergency_msg='%s'  WHERE node_id=%d;", 
                node_db_list[nodeid].num_req, node_db_list[nodeid].num_rep, node_db_list[nodeid].num_timeout, node_db_list[nodeid].last_cmd, 
                node_db_list[nodeid].last_seen, node_db_list[nodeid].num_of_retrans, node_db_list[nodeid].channel_id, node_db_list[nodeid].rssi, node_db_list[nodeid].lqi, 
                node_db_list[nodeid].pan_id, node_db_list[nodeid].tx_power,node_db_list[nodeid].last_err_code, 
                node_db_list[nodeid].num_emergency_msg, result, 
                nodeid);
    }    
    
    if (execute_sql_cmd(sql)!=0) {
        printf("Update SQL failed: sql_cmd = %s\n", sql);
    }    

    free(result);    
#endif
}

/*------------------------------------------------*/
void update2_sql_row(int nodeid) {
#if (USING_SQL_SERVER_ENABLE)   
    char sql[400];
    int i;

    if (is_node_connected(nodeid)) {
        sprintf(sql,"UPDATE sls_db SET connected='Y', next_hop_link_addr='%s',delay=%.2f,rdr=%.2f, authenticated=%d  WHERE node_id=%d;", 
                node_db_list[nodeid].next_hop_link_addr, node_db_list[nodeid].delay, node_db_list[nodeid].rdr, node_db_list[nodeid].authenticated,
                nodeid);
    } else {
        sprintf(sql,"UPDATE sls_db SET connected='N', next_hop_link_addr='%s',delay=%.2f,rdr=%.2f, authenticated=%d  WHERE node_id=%d;", 
                node_db_list[nodeid].next_hop_link_addr,node_db_list[nodeid].delay, node_db_list[nodeid].rdr, node_db_list[nodeid].authenticated,
                nodeid);
    }    
    
    if (execute_sql_cmd(sql)!=0){
        printf("Update SQL failed: sql_cmd = %s\n", sql);
    }    
    //free(result);    
#endif    
}

/*------------------------------------------------*/
void update3_sql_row(int nodeid) {
#if (USING_SQL_SERVER_ENABLE)
    char sql[400];
    int i;

    if (is_node_connected(nodeid)) {
        sprintf(sql,"UPDATE sls_db SET connected='Y', temperature=%.1f,light=%.0f, pressure=%.1f,humidity=%.2f  WHERE node_id=%d;", 
                node_db_list[nodeid].sensor_db.temperature, node_db_list[nodeid].sensor_db.light, node_db_list[nodeid].sensor_db.pressure,
                node_db_list[nodeid].sensor_db.humidity, nodeid);
    } else {
        sprintf(sql,"UPDATE sls_db SET connected='N', temperature=%.1f,light=%.0f, pressure=%.1f,humidity=%.2f  WHERE node_id=%d;", 
                node_db_list[nodeid].sensor_db.temperature, node_db_list[nodeid].sensor_db.light, node_db_list[nodeid].sensor_db.pressure,
                node_db_list[nodeid].sensor_db.humidity, nodeid);
    }    
    
    if (execute_sql_cmd(sql)!=0){
        printf("Update SQL failed: sql_cmd = %s\n", sql);
    }    
#endif    
}


/*------------------------------------------------*/
void update_sql_sensor(int nodeid) {
#if (USING_SQL_SERVER_ENABLE)    
    char sql[400];

    if (is_node_connected(nodeid)) {
        sprintf(sql,"UPDATE sls_db SET connected='Y', temperature=%.1f, light=%.0f, pressure=%.1f, humidity=%.2f, oil_voltage=%.2f, oil_current=%.2f, water_voltage=%.2f, water_current=%.2f, pressure_voltage=%.2f, pressure_current=%.2f WHERE node_id=%d;", 
                node_db_list[nodeid].sensor_db.temperature, node_db_list[nodeid].sensor_db.light,
                node_db_list[nodeid].sensor_db.pressure, node_db_list[nodeid].sensor_db.humidity,  
                node_db_list[nodeid].sensor_db.oil_voltage, node_db_list[nodeid].sensor_db.oil_current,
                node_db_list[nodeid].sensor_db.water_voltage, node_db_list[nodeid].sensor_db.water_current,
                node_db_list[nodeid].sensor_db.pressure_voltage, node_db_list[nodeid].sensor_db.pressure_current,
                nodeid);
    } else {
        sprintf(sql,"UPDATE sls_db SET connected='N', temperature=%.1f, light=%.0f, pressure=%.1f, humidity=%.2f, oil_voltage=%.2f, oil_current=%.2f, water_voltage=%.2f, water_current=%.2f, pressure_voltage=%.2f, pressure_current=%.2f WHERE node_id=%d;", 
                node_db_list[nodeid].sensor_db.temperature, node_db_list[nodeid].sensor_db.light,
                node_db_list[nodeid].sensor_db.pressure, node_db_list[nodeid].sensor_db.humidity,  
                node_db_list[nodeid].sensor_db.oil_voltage, node_db_list[nodeid].sensor_db.oil_current,
                node_db_list[nodeid].sensor_db.water_voltage, node_db_list[nodeid].sensor_db.water_current,
                node_db_list[nodeid].sensor_db.pressure_voltage, node_db_list[nodeid].sensor_db.pressure_current,
                nodeid);
    }        
    if (execute_sql_cmd(sql)!=0){
        printf("Update SQL failed: sql_cmd = %s\n", sql);
    }    
#endif    
}


/*------------------------------------------------*/
void update_sql_row(int nodeid) {
#if (USING_SQL_SERVER_ENABLE) 
    update1_sql_row(nodeid);
    update2_sql_row(nodeid);
    update3_sql_row(nodeid);
    update_sql_sensor(nodeid);    
#endif    
}

/*------------------------------------------------*/
void update1_sql_db() {
#if (USING_SQL_SERVER_ENABLE)
    char sql[400];
    int i;
    char *result;
    char buf[MAX_CMD_DATA_LEN];

    for (i=1; i<num_of_node; i++) {
        memcpy(buf, &node_db_list[i].last_emergency_msg, 20);    
        convert_array2str(buf, sizeof(buf), &result);    
        if (is_node_connected(i)) {
            sprintf(sql,"UPDATE sls_db SET connected='Y', num_req=%d, num_rep=%d, num_timeout=%d, last_cmd=%d, last_seen='%s', num_of_retrans=%d, rf_channel=%d, rssi=%d, lqi=%d, pan_id=%d, tx_power=%d, last_err_code=%d, num_emergency_msg=%d, last_emergency_msg='%s'  WHERE node_id=%d;", 
                node_db_list[i].num_req, node_db_list[i].num_rep, node_db_list[i].num_timeout, node_db_list[i].last_cmd, 
                node_db_list[i].last_seen, node_db_list[i].num_of_retrans, node_db_list[i].channel_id, node_db_list[i].rssi, node_db_list[i].lqi, 
                node_db_list[i].pan_id, node_db_list[i].tx_power,node_db_list[i].last_err_code, node_db_list[i].num_emergency_msg, result, i);
        } else {
            sprintf(sql,"UPDATE sls_db SET connected='N', num_req=%d, num_rep=%d, num_timeout=%d, last_cmd=%d, last_seen='%s', num_of_retrans=%d, rf_channel=%d, rssi=%d, lqi=%d, pan_id=%d, tx_power=%d, last_err_code=%d, num_emergency_msg=%d, last_emergency_msg='%s'  WHERE node_id=%d;", 
                node_db_list[i].num_req, node_db_list[i].num_rep, node_db_list[i].num_timeout, node_db_list[i].last_cmd, 
                node_db_list[i].last_seen, node_db_list[i].num_of_retrans, node_db_list[i].channel_id, node_db_list[i].rssi, node_db_list[i].lqi, 
                node_db_list[i].pan_id, node_db_list[i].tx_power,node_db_list[i].last_err_code, node_db_list[i].num_emergency_msg, result, i);
        }            
        if (execute_sql_cmd(sql)!=0){
            printf("Update SQL failed: sql_cmd = %s\n", sql);
        }    
    }
#endif    
}

/*------------------------------------------------*/
void update2_sql_db() {
#if (USING_SQL_SERVER_ENABLE) 
    char sql[400];
    int i;

    for (i=1; i<num_of_node; i++) {
        if (is_node_connected(i)) {
            //printf("node %d = 'Y' \n",i);
            sprintf(sql,"UPDATE sls_db SET connected='Y', next_hop_link_addr='%s', delay=%f, rdr=%f, app_key='%s'  WHERE node_id=%d;", 
                node_db_list[i].next_hop_link_addr,node_db_list[i].delay, node_db_list[i].rdr, node_db_list[i].app_key, i);
        } else {
            //printf("node %d = 'N' \n",i);
            sprintf(sql,"UPDATE sls_db SET connected='N', next_hop_link_addr='%s', delay=%f, rdr=%f, app_key='%s'  WHERE node_id=%d;", 
                node_db_list[i].next_hop_link_addr,node_db_list[i].delay, node_db_list[i].rdr, node_db_list[i].app_key, i);
        }    
        if (execute_sql_cmd(sql)!=0){
            printf("Update SQL failed: sql_cmd = %s\n", sql);
        }    
    }
#endif    
}

/*------------------------------------------------*/
void update_sql_db() {
#if (USING_SQL_SERVER_ENABLE)  
    update1_sql_db();
    update2_sql_db();  
#endif      
}

/*------------------------------------------------*/
void show_local_db() { 
    int i;
    printf("\nLOCAL DATABASE: table_size (bytes) =\033[1;35m %d\033[0m, node_size (bytes) =\033[1;35m %d\033[0m, num_of_nodes = \033[1;35m%d \033[0m\n", sizeof(node_db_list), sizeof(node_db_struct_t), num_of_node);
    printf("Border router IPv6: \033[1;32m%s \033[0m\n",node_db_list[0].ipv6_addr);
    
    if (SHOW_FULL_DB) {        
        printf("|----|--------------------------|--------|-----|-----|-----|-----|-----|-------------------|----|--------------|-------------------|-----|------|-------|------|-----|-----|------|-----|\n");
        printf("|node|       ipv6 address       |con/aut/| req_| rep_|time | last|retr_|    last_seen      |chan|RSSI/LQI/power|    emger_cnt      |err_c| next | delay | ctrl_|temp_|light|press |humid|\n");
        printf("| id |  (prefix: aaaa::0/64_    |llsec/en| uest| ly  |-out | cmd | ies |       time        |_nel|(dBm)/  /(dBm)|   /seq /a_prr     |(hex)|  hop |  (ms) | prr  | (ºC)|(lux)|(hPa) | (RH)|\n");
        printf("|----|--------------------------|--------|-----|-----|-----|-----|-----|-------------------|----|--------------|-------------------|-----|------|-------|------|-----|-----|------|-----|\n");
        for(i = 0; i < num_of_node; i++) {
            if (i>0) {
                printf("| %2d |\033[1;33m %24s \033[0m|\033[1;32m%2s/%1d/%1d/%1d\033[0m|%5d|%5d|%5d| 0x%02X|%5d| %17s |%4d|%4d/%5u/%3X|%5d(%5d)/\033[1;35m%6.02f\033[0m| 0x%02X| _%02x%02x|%7.1f|%6.2f|\033[1;35m%5.02f\033[0m|\033[1;35m%5d\033[0m|\033[1;35m%6.1f\033[0m|\033[1;35m%5.1f\033[0m|\n",node_db_list[i].id,
                    node_db_list[i].ipv6_addr, node_db_list[i].connected,node_db_list[i].authenticated, node_db_list[i].llsec, node_db_list[i].encryption_mode,node_db_list[i].num_req, 
                    node_db_list[i].num_rep, node_db_list[i].num_timeout, node_db_list[i].last_cmd, node_db_list[i].num_of_retrans,
                    node_db_list[i].last_seen, node_db_list[i].channel_id, node_db_list[i].rssi, node_db_list[i].lqi, node_db_list[i].tx_power, 
                    node_db_list[i].num_emergency_msg, node_db_list[i].async_seq, node_db_list[i].async_prr, node_db_list[i].last_err_code,
                    (uint8_t) node_db_list[i].next_hop_addr[14], (uint8_t)node_db_list[i].next_hop_addr[15], node_db_list[i].delay, node_db_list[i].rdr,
                    node_db_list[i].sensor_db.temperature, (uint16_t)node_db_list[i].sensor_db.light, node_db_list[i].sensor_db.pressure, node_db_list[i].sensor_db.humidity);  
                } 
            else {
                printf("| %2d |\033[1;33m %24s \033[0m|\033[1;32m*%1s/%1d/%1d/%1d\033[0m|%5d|%5d|%5d| 0x%02X|%5d| %17s |%4d|%4d/%5u/%3X|%5d(%5d)/\033[1;35m%6.02f\033[0m| 0x%02X| _%02x%02x|%7.1f|%6.2f|\033[1;35m%5.02f\033[0m|\033[1;35m%5d\033[0m|\033[1;35m%6.1f\033[0m|\033[1;35m%5.1f\033[0m|\n",node_db_list[i].id,
                    node_db_list[i].ipv6_addr, node_db_list[i].connected,node_db_list[i].authenticated, node_db_list[1].llsec, node_db_list[i].encryption_mode, node_db_list[i].num_req, 
                    node_db_list[i].num_rep, node_db_list[i].num_timeout, node_db_list[i].last_cmd, node_db_list[i].num_of_retrans,
                    node_db_list[i].last_seen, node_db_list[1].channel_id, node_db_list[i].rssi, node_db_list[i].lqi, node_db_list[i].tx_power, 
                    node_db_list[i].num_emergency_msg, node_db_list[i].async_seq, node_db_list[i].async_prr, node_db_list[i].last_err_code,
                    (uint8_t) node_db_list[i].next_hop_addr[14], (uint8_t) node_db_list[i].next_hop_addr[15], (double)0,node_db_list[i].rdr,
                    node_db_list[i].sensor_db.temperature, (uint16_t)node_db_list[i].sensor_db.light, node_db_list[i].sensor_db.pressure, node_db_list[i].sensor_db.humidity);
            }    
        }
        printf("|----|--------------------------|--------|-----|-----|-----|-----|-----|-------------------|----|--------------|-------------------|-----|------|-------|------|-----|-----|------|-----|\n");

    // show short table     
    } else {
        printf("|----|--------------------------|--------|-----|-----|-----|-----|-----|----|--------------|-------------------|-----|------|-------|------|\n");
        printf("|node|       ipv6 address       |con/aut/| req_| rep_|time | last|retr_|chan|RSSI/LQI/power|    emger_cnt/     |err_c| next | delay | ctrl |\n");
        printf("| id |  (prefix: aaaa::0/64)    |llsec/en| uest| ly  |-out | cmd | ies |_nel|(dBm)/  /(dBm)|   seq/async_prr   |(hex)|  hop |  (ms) | _prr |\n");
        printf("|----|--------------------------|--------|-----|-----|-----|-----|-----|----|--------------|-------------------|-----|------|-------|------|\n");
        for(i = 0; i < num_of_node; i++) {
            if (i>0) {
                printf("| %2d |\033[1;33m %24s \033[0m|\033[1;32m%2s/%1d/%1d/%1d\033[0m|%5d|%5d|%5d| 0x%02X|%5d|%4d|%4d/%5u/%3X|%5d(%5d)/\033[1;35m%6.02f\033[0m| 0x%02X| _%02x%02x|%7.1f|%6.2f|\n",node_db_list[i].id,
                    node_db_list[i].ipv6_addr, node_db_list[i].connected,node_db_list[i].authenticated, node_db_list[i].llsec, node_db_list[i].encryption_mode, node_db_list[i].num_req, 
                    node_db_list[i].num_rep, node_db_list[i].num_timeout, node_db_list[i].last_cmd, node_db_list[i].num_of_retrans,
                    node_db_list[i].channel_id, node_db_list[i].rssi, node_db_list[i].lqi, node_db_list[i].tx_power, 
                    node_db_list[i].num_emergency_msg, node_db_list[i].async_seq, node_db_list[i].async_prr, node_db_list[i].last_err_code,
                    (uint8_t) node_db_list[i].next_hop_addr[14], (uint8_t)node_db_list[i].next_hop_addr[15], node_db_list[i].delay, node_db_list[i].rdr);  
                } else {
                printf("| %2d |\033[1;33m %24s \033[0m|\033[1;32m*%1s/%1d/%1d/%1d\033[0m|%5d|%5d|%5d| 0x%02X|%5d|%4d|%4d/%5u/%3X|%5d(%5d)/\033[1;35m%6.02f\033[0m| 0x%02X| _%02x%02x|%7.1f|%6.2f|\n",node_db_list[i].id,
                    node_db_list[i].ipv6_addr, node_db_list[i].connected,node_db_list[i].authenticated, node_db_list[1].llsec, node_db_list[i].encryption_mode, node_db_list[i].num_req, 
                    node_db_list[i].num_rep, node_db_list[i].num_timeout, node_db_list[i].last_cmd, node_db_list[i].num_of_retrans,
                    node_db_list[1].channel_id, node_db_list[i].rssi, node_db_list[i].lqi, node_db_list[i].tx_power, 
                    node_db_list[i].num_emergency_msg, node_db_list[i].async_seq, node_db_list[i].async_prr, node_db_list[i].last_err_code,
                    (uint8_t) node_db_list[i].next_hop_addr[14], (uint8_t) node_db_list[i].next_hop_addr[15], (double)0,node_db_list[i].rdr);
            }    
        }
        printf("|----|--------------------------|--------|-----|-----|-----|-----|-----|----|--------------|-------------------|-----|------|-------|------|\n");
    }
}



/*------------------------------------------------*/
void show_sql_db() {
#if (USING_SQL_SERVER_ENABLE)       
    int i, num_fields;

    con = mysql_init(NULL);
    if (con == NULL) {
      finish_with_error(con);
    }    
    if (mysql_real_connect(con, sql_server_ipaddr, sql_username, sql_password, sql_db, 0, NULL, 0) == NULL) {
        finish_with_error(con);
    }  

    if (mysql_query(con, "SELECT * FROM sls_db")) {
        finish_with_error(con);
    }

    MYSQL_RES *result = mysql_store_result(con);
    if (result == NULL) {
        finish_with_error(con);
    }

    printf("\n");
    printf("|---------------------------------------------SQL DATABASE---------------------------------------------------|\n");
    num_fields = mysql_num_fields(result);

    MYSQL_ROW row;

    while ((row = mysql_fetch_row(result)))  { 
        for(i = 0; i < num_fields; i++) {
            if (i==0) {
                printf("| %3s | ", row[i] ? row[i] : "NULL");
            } else if ((i==2) || (i==8)) {
                printf("%24s | ", row[i] ? row[i] : "NULL");
            } else {
                printf("%4s | ", row[i] ? row[i] : "NULL");
            }    
        }
        printf("\n");
    }
    printf("|------------------------------------------------------------------------------------------------------------|\n");

    mysql_free_result(result);
    mysql_close(con);   
#endif    
}

/*------------------------------------------------*/
int read_node_list(){
    boolean sql_db_error;
    int     node, num_fields;
    char    ipv6_addr[50], buf[1000];   
    FILE *ptr_file;


    num_of_node = 0;
#if (USING_SQL_SERVER_ENABLE)       
    sql_db_error = FALSE;
    con = mysql_init(NULL);

    show_network_topo();

    printf("DATABASE: sls_db; MySQL client version: %s\n", mysql_get_client_info());
    if (con == NULL) {
        finish_with_error(con);
        sql_db_error = TRUE;
    }    
    if (mysql_real_connect(con, sql_server_ipaddr, sql_username,sql_password, sql_db,0,NULL,0) == NULL) {
        finish_with_error(con);
        sql_db_error = TRUE;
    }  
    if (mysql_query(con, "SELECT * FROM sls_db")) {
        finish_with_error(con);
        sql_db_error = TRUE;
    }
  
    MYSQL_RES *result = mysql_store_result(con);
    if (result == NULL) {
        finish_with_error(con);
        sql_db_error = TRUE;
    } else {
        //printf("Reading DB: sls_db......\n");
    }

    num_fields = mysql_num_fields(result);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)))  { 
        //for(int i = 0; i < num_fields; i++)
        //    printf("%s ", row[i] ? row[i] : "NULL");
        get_db_row(row, num_of_node);        
        num_of_node++;
    }
    
    mysql_free_result(result);
    mysql_close(con);   
    update_sql_db();
#else    
    sql_db_error = TRUE;
#endif    

    if (sql_db_error==FALSE) {
        printf("SQL-DB: successfully reading node infor from DB....\n");    
    } else {
        printf("SQL-DB error: reading node infor from config file....\n");    
        num_of_node =0;

#if (SIMULATION_BUILD_ENABLE)
        ptr_file =fopen("cooja_node.txt","r");
#else
        ptr_file =fopen("node_list.txt","r");        
#endif        

        if (!ptr_file)
            return 1;
        while (fgets(buf,1000, ptr_file)!=NULL) {
            sscanf(buf,"%d %s",&node, ipv6_addr);
            node_db_list[num_of_node].id = node;
            strcpy(node_db_list[node].ipv6_addr, ipv6_addr);
            //printf("node = %d,   ipv6 = %s\n",node, node_db_list[node].ipv6_addr);
            num_of_node++;
            }
        fclose(ptr_file);
    }
    printf("\n\033[1;32mI. READ NODE LIST... DONE. Num of nodes: %d  \033[0m\n",num_of_node);
    
    check_border_router();
    check_ip_interface();

    show_local_db();
    return 0;
}


/*------------------------------------------------*/
void check_border_router(){
    char pingCmd[100] = {'\0'};
    sprintf( pingCmd, "%s %s", "ping6 -q -c 2", node_db_list[0].ipv6_addr);
    printf( "Check border router:\033[1;33m %s \033[0m\n", node_db_list[0].ipv6_addr);
    system(pingCmd );
}

/*------------------------------------------------*/
void check_ip_interface(){
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    printf( "Check IP interfaces: \n");
    if (getifaddrs(&ifaddr) == -1)    {
        perror("getifaddrs");
        //exit(EXIT_FAILURE);
    }


    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;  

        s=getnameinfo(ifa->ifa_addr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if((strcmp(ifa->ifa_name,"ens33")==0)&&(ifa->ifa_addr->sa_family==AF_INET)) {
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                //exit(EXIT_FAILURE);
            }
            printf("- Interface: <\033[1;33m%s\033[0m> Address: \033[1;33m%s\033[0m \n",ifa->ifa_name, host);
        }

        if((strcmp(ifa->ifa_name,"lo")==0)&&(ifa->ifa_addr->sa_family==AF_INET)) {
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                //exit(EXIT_FAILURE);
            }
            printf("- Interface: <\033[1;33m%s\033[0m> Address: \033[1;33m%s\033[0m \n",ifa->ifa_name, host);
        }

    }

    freeifaddrs(ifaddr);
    //exit(EXIT_SUCCESS);
}


/*------------------------------------------------*/
boolean is_broadcast_command(cmd_struct_t cmd) {
    return cmd.len = 0xFF;
}

boolean is_multicast_command(cmd_struct_t cmd) {
    return cmd.len = 0xFE;
}

/*------------------------------------------------*/
boolean is_node_valid(int node) {
    int i;
    for (i=0; i<num_of_node; i++) {
        if (node==node_db_list[i].id)
            return TRUE;
    }
    return FALSE;
}

/*------------------------------------------------*/
boolean is_node_connected(int node) {
    if (node >0) {
        return (char)node_db_list[node].connected[0]=='Y';
    }else { // border router 
        return TRUE;
    }
}

/*------------------------------------------------*/
int execute_sql_cmd(char *sql) {

#if (USING_SQL_SERVER_ENABLE) 
    con = mysql_init(NULL);

    if (con == NULL) {
        finish_with_error(con);
        return 1;
    }    

    if (mysql_real_connect(con, sql_server_ipaddr, sql_username, sql_password, sql_db, 0, NULL, 0) == NULL) {
        finish_with_error(con);
        return 1;
    }  

    if (mysql_query(con, sql)) {
        finish_with_error(con);
        return 1;
    }

    mysql_close(con);   
    return 0;
#endif     
    return 0;
}

/*------------------------------------------------*/
static char * add_ipaddr(char *buf, int nodeid) {
  uint16_t a;
  unsigned int i;
  int f;
  char *p = buf;

    for(i = 0, f = 0; i < 16; i += 2) {
        a = (node_db_list[nodeid].next_hop_addr[i] << 8) + node_db_list[nodeid].next_hop_addr[i + 1];
        if(a == 0 && f >= 0) {
            if(f++ == 0) {
                p += sprintf(p, "::");
            }
        } else {
        if(f > 0) {
            f = -1;
        } else if(i > 0) {
            p += sprintf(p, ":");
        }
        p += sprintf(p, "%04x", a);
        }
    }
    return p;
}

/*------------------------------------------------*/
static void reset_sequence(int nodeid){
    node_db_list[nodeid].async_seq = 0;
    node_db_list[nodeid].cmd_seq = 0;
}


/*------------------------------------------------*/
boolean authenticate_node(int node_id, uint32_t challenge_code, uint16_t challenge_res, int *res) {
    boolean result;
    int response;
    uint32_t rx_res, code;

    //prepare_cmd(node_id);
    reset_sequence(node_id);

    result = FALSE;
    code = challenge_code;
    tx_cmd.type = MSG_TYPE_HELLO;
    tx_cmd.cmd = CMD_RF_AUTHENTICATE;
    tx_cmd.err_code = 0;
    tx_cmd.arg[0]= (code >> 8) & 0xFF;
    tx_cmd.arg[1]= code  & 0xFF;

    // try in NUM_RETRANS_AUTHEN times
    //response = ip6_send_cmd(node_id, SLS_NORMAL_PORT, NUM_RETRANS_AUTHEN, false);
    response = ip6_send_cmd(node_id, SLS_NORMAL_PORT, NUM_RETRANS_AUTHEN, node_db_list[node_id].encryption_phase);
    if (response == -1) {
        printf(" - ERROR: autheticating node %d \n", node_id);
    }
    else if (response == 0)   {
        printf(" - Timeout: autheticating node %d \n", node_id);
    }
    else {
        rx_res = (rx_reply.arg[0] <<8) | (rx_reply.arg[1]);
        printf(" - Receiving challenge response = 0x%04X \n", rx_res);
        result = (rx_res==challenge_res);
    }

    update_sql_row(node_id);    
    *res = response;
    return result;
}



/*------------------------------------------------*/
void run_node_discovery(){
    char sql[200];
    int res, i,j;
    uint16_t rssi_rx;
    char *p;
    char buf[100];
    boolean node_authenticated = FALSE;
    uint32_t rx_res;

    printf("\033[1;32m");
    printf("II. RUNNING DISCOVERY PROCESS.....\n");
    printf("\033[0m");
    for (i = 1; i < num_of_node; i++) {
        node_db_list[i].challenge_code = gen_random_num();
        node_db_list[i].challenge_code_res = hash(node_db_list[i].challenge_code);
        printf("\n1. Send challenge_code [0x%04X] to node \033[1;32m%d\033[0m \n",node_db_list[i].challenge_code, i );
        printf(" - Expected challenge_code_res = 0x%04X \n",node_db_list[i].challenge_code_res);

        gettimeofday(&t0, 0);
        node_authenticated = authenticate_node(i, node_db_list[i].challenge_code, node_db_list[i].challenge_code_res, &res);
        gettimeofday(&t1, 0);
        node_db_list[i].delay = timedifference_msec(t0, t1);
        printf(" - Roundtrip delay %.2f (ms)\n", node_db_list[i].delay);

        if (res == -1) {
            printf(" - ERROR: discovery process \n");
        } else if (res == 0)   {
            printf(" - Node %d [\033[1;32m%s\033[0m] unavailable\n", i, node_db_list[i].ipv6_addr);
            sprintf(sql,"UPDATE sls_db SET connected='N' WHERE node_id=%d;", i);
            if (execute_sql_cmd(sql)!=0) {
                printf("Update SQL failed: sql_cmd = %s\n", sql);
            }
        } else{
            sprintf(sql,"UPDATE sls_db SET connected='Y' WHERE node_id=%d;", i);
            if (execute_sql_cmd(sql)!=0) {
                printf("Update SQL failed: sql_cmd = %s\n", sql);
            }
            if (node_authenticated==TRUE) {
                node_db_list[i].authenticated = TRUE;
                //node_db_list[i].encryption_phase = node_db_list[i].authenticated;
                printf(" - Node %d authenticated, encryption_phase = %d \n", i, node_db_list[i].encryption_phase);

                // rx_reply
                node_db_list[i].channel_id = rx_reply.arg[2];

                rssi_rx = rx_reply.arg[3];
                node_db_list[i].rssi = rssi_rx-150;
                
                node_db_list[i].lqi = rx_reply.arg[4];
                node_db_list[i].tx_power = rx_reply.arg[5];
                node_db_list[i].pan_id = (rx_reply.arg[6] << 8) | (rx_reply.arg[7]);


                node_db_list[i].llsec = rx_reply.arg[8] & 0x07;
                node_db_list[i].encryption_mode = rx_reply.arg[9];

                printf(" - Security_en = %d, LLSEC level = %d, encryption_mode = %d \n", (rx_reply.arg[8]>>4), node_db_list[i].llsec, node_db_list[i].encryption_mode);
                // get last 8 bytes of next_hop
                for (j=0; j<8; j++) {
                    node_db_list[i].next_hop_addr[8+j] = rx_reply.arg[10+j];
                } 

                add_ipaddr(buf,i);
                strcpy(node_db_list[i].next_hop_link_addr, buf);
                printf(" - Node %d [\033[1;32m%s\033[0m] available, next-hop link-addr = [\033[1;32m%s\033[0m] \n", i, node_db_list[i].ipv6_addr, node_db_list[i].next_hop_link_addr);
            }
        }
    }
    update_sql_db();    
}


/*------------------------------------------------*/
void prepare_cmd(int nodeid) {
    node_db_list[nodeid].cmd_seq++;
    tx_cmd.sfd = SFD;
    tx_cmd.seq = node_db_list[nodeid].cmd_seq;
    printf("\n2. Prepare COMMAND [seq=\033[1;35m%d\033[0m] for node \033[1;32m%d\033[0m \n", tx_cmd.seq, nodeid);  
}



/*------------------------------------------------*/
boolean is_cmd_of_gw(cmd_struct_t cmd) {
    return (cmd.cmd==CMD_GET_GW_STATUS) ||
            (cmd.cmd==CMD_GW_HELLO) ||
            (cmd.cmd==CMD_GW_SHUTDOWN) ||
            (cmd.cmd==CMD_GW_TURN_ON_ALL) ||
            (cmd.cmd==CMD_GW_TURN_OFF_ALL) ||
            (cmd.cmd==CMD_GW_DIM_ALL) ||            
            (cmd.cmd==CMD_GW_SET_TIMEOUT) ||
            (cmd.cmd==CMD_GW_BROADCAST_CMD) ||            
            (cmd.cmd==CMD_GW_MULTICAST_CMD) ||
            (cmd.cmd==CMD_GW_GET_EMER_INFO) ||   

            (cmd.cmd==CMD_GW_TURN_ON_ODD) ||      
            (cmd.cmd==CMD_GW_TURN_ON_EVEN) ||      
            (cmd.cmd==CMD_GW_TURN_ON_ODD) ||      
            (cmd.cmd==CMD_GW_TURN_OFF_ODD) ||      
            (cmd.cmd==CMD_GW_DIM_ODD) ||        
            (cmd.cmd==CMD_GW_DIM_EVEN) ||        
                            
            (cmd.cmd==CMD_GW_RELOAD_FW);
}

/*------------------------------------------------*/
/* mode = 2: broadcast all
   mode = 1: odd
   mode = 0: even */
int execute_broadcast_cmd(cmd_struct_t cmd, int val, int mode) {
    int i, num_timeout, res, num_rep;
    uint16_t err_code;

    num_timeout=0;
    num_rep=0;
    printf(" - Executing broadcast commmand: subcmd = 0x%02X, arg = %d ...\n", cmd.cmd, val);

    err_code = ERR_NORMAL;
    for (i = 1; i < num_of_node; i++) {
        /* prepare tx_cmd to send to RF nodes*/
        tx_cmd.type = cmd.type;
        tx_cmd.cmd = cmd.cmd;     // CMD_LED_ON;
        tx_cmd.arg[0] = val;
        tx_cmd.err_code = 0;

        if (mode==ALL_NODE) {
            node_db_list[i].num_req++;
            //res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, FALSE);
            res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, node_db_list[i].encryption_phase);
            if (res == -1) {
                printf(" - ERROR: broadcast process \n");
                err_code = ERR_BROADCAST_CMD;
            } else if (res == 0)   {
                printf(" - Send CMD to node %d [%s] failed\n", i, node_db_list[i].ipv6_addr);
                num_timeout++;
                node_db_list[i].num_timeout++;
                err_code = ERR_TIME_OUT;
            } else{
                printf(" - Send CMD to node %d [%s] successful\n", i, node_db_list[i].ipv6_addr);   
                num_rep++;
                node_db_list[i].num_rep++;
                node_db_list[i].last_cmd = tx_cmd.cmd;            
                node_db_list[i].rdr = 100*node_db_list[i].num_rep/node_db_list[i].num_req;                        
            }
        }

        /* odd led */
        else if (mode==ODD_NODE) {
            if ((i % 2)==1) {
                node_db_list[i].num_req++;
                //res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, false);
                res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, node_db_list[i].encryption_phase);
                if (res == -1) {
                    printf(" - ERROR: broadcast process \n");
                    err_code = ERR_BROADCAST_CMD;
                } else if (res == 0)   {
                    printf(" - Send CMD to node %d [%s] failed\n", i, node_db_list[i].ipv6_addr);
                    num_timeout++;
                    node_db_list[i].num_timeout++;
                    err_code = ERR_TIME_OUT;
                } else {
                    printf(" - Send CMD to node %d [%s] successful\n", i, node_db_list[i].ipv6_addr);   
                    num_rep++;
                    node_db_list[i].num_rep++;
                    node_db_list[i].last_cmd = tx_cmd.cmd;            
                    node_db_list[i].rdr = 100*node_db_list[i].num_rep/node_db_list[i].num_req;                        
                }
            }   
        }
        /* even */
        else if (mode==EVEN_NODE) {
            if (((i % 2)==0) && (i!=0)){
                node_db_list[i].num_req++;
                //res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, false);
                res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, node_db_list[i].encryption_phase);
                if (res == -1) {
                    printf(" - ERROR: broadcast process \n");
                    err_code = ERR_BROADCAST_CMD;
                } else if (res == 0)   {
                    printf(" - Send CMD to node %d [%s] failed\n", i, node_db_list[i].ipv6_addr);
                    num_timeout++;
                    node_db_list[i].num_timeout++;
                    err_code = ERR_TIME_OUT;
                } else {
                    printf(" - Send CMD to node %d [%s] successful\n", i, node_db_list[i].ipv6_addr);   
                    num_rep++;
                    node_db_list[i].num_rep++;
                    node_db_list[i].last_cmd = tx_cmd.cmd;        
                    node_db_list[i].rdr = 100*node_db_list[i].num_rep/node_db_list[i].num_req;                        
                }
            }   

        } 
    }

    rx_reply.err_code = err_code;
    memset(&rx_reply.arg,0,MAX_CMD_DATA_LEN);   //reset data of reply
    rx_reply.arg[0] = num_of_node-1;            //except node 0
    rx_reply.arg[1] = num_rep;
    rx_reply.arg[2] = num_timeout;

    return 0;
}

/*------------------------------------------------*/
int execute_multicast_cmd(cmd_struct_t cmd) {
    uint8_t     num_multicast_node, max_data_of_cmd;
    uint8_t     i,j, num_timeout, res, num_rep, executed_node;
    uint16_t    err_code;
    uint8_t     multicast_cmd;

    /*  data = 28 bytes
        multicast_cmd = data[0];
        multicast_data = data[1..10]
        multicast_node = data [11..27] */
    max_data_of_cmd = 10;    
    num_multicast_node = cmd.len;
    if (num_multicast_node > (MAX_CMD_DATA_LEN-max_data_of_cmd-1) ) {
        printf("Invalid number of multicast nodes, max = %d....\n", MAX_CMD_DATA_LEN-max_data_of_cmd-1);
        return -1;
    }
    multicast_cmd = cmd.arg[0];
    num_timeout=0;
    num_rep=0;
    printf(" - Executing multicast command: subcmd = 0x%02X...\n", multicast_cmd);

    err_code = ERR_NORMAL;
    for (i = 0; i < num_multicast_node; i++) {
        /* prepare tx_cmd to send to RF nodes*/
        tx_cmd.type = cmd.type;
        tx_cmd.len = 0xFF;      // multi-cast or broadcast
        tx_cmd.cmd = multicast_cmd;     
        tx_cmd.err_code = 0;
        for (j=0; j<max_data_of_cmd; j++)
            tx_cmd.arg[j] = cmd.arg[j+1];

        executed_node = cmd.arg[i + max_data_of_cmd + 1];               //from arg[7] to...
        if (is_node_valid(executed_node)) {
            node_db_list[executed_node].num_req++;
            //res = ip6_send_cmd(executed_node, SLS_NORMAL_PORT, NUM_RETRANS, false);
            res = ip6_send_cmd(executed_node, SLS_NORMAL_PORT, NUM_RETRANS, node_db_list[executed_node].encryption_phase);
            if (res == -1) {
                printf(" - ERROR: broadcast process \n");
                err_code = ERR_MULTICAST_CMD;
            } else if (res == 0)   {
                printf(" - Send CMD to node %d [%s] failed\n", executed_node, node_db_list[executed_node].ipv6_addr);
                num_timeout++;
                node_db_list[executed_node].num_timeout++;
                err_code = ERR_TIME_OUT;
            } else {
                printf(" - Send CMD to node %d [%s] successful\n", executed_node, node_db_list[executed_node].ipv6_addr);   
                num_rep++;
                node_db_list[executed_node].num_rep++;
                node_db_list[executed_node].last_cmd = multicast_cmd;     
                node_db_list[executed_node].rdr = 100*node_db_list[executed_node].num_rep/node_db_list[executed_node].num_req;
            }
        }
    }

    rx_reply.err_code = err_code;
    memset(&rx_reply.arg,0,MAX_CMD_DATA_LEN);       //reset data of reply
    rx_reply.arg[0] = num_multicast_node;           //except node 0
    rx_reply.arg[1] = num_rep;
    rx_reply.arg[2] = num_timeout;
    return 0;
}

/*------------------------------------------------*/
int execute_broadcast_general_cmd(cmd_struct_t cmd, int mode) {
    int i,j, num_timeout, res, num_rep;
    uint16_t err_code;
    uint8_t broadcast_cmd;

    num_timeout=0;
    num_rep=0;
    broadcast_cmd = cmd.arg[0];
    err_code = ERR_NORMAL;    
    printf(" - Executing broadcast general command: subcmd = 0x%02X ...\n", broadcast_cmd);

    for (i = 1; i < num_of_node; i++) {
        /* prepare tx_cmd to send to RF nodes*/
        tx_cmd = cmd;
        tx_cmd.type = MSG_TYPE_REQ;
        tx_cmd.len = 0xFF;                  // multi-cast or broadcast
        tx_cmd.cmd = broadcast_cmd;     
        tx_cmd.err_code = 0;
        for (j = 0; j < (MAX_CMD_DATA_LEN-1); j++) {    
            tx_cmd.arg[j] = cmd.arg[j+1];
        }

        if (mode==2) {
            node_db_list[i].num_req++;
            //res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, false);
            res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, node_db_list[i].encryption_phase);
            if (res == -1) {
                printf(" - ERROR: broadcast process \n");
                err_code = ERR_BROADCAST_CMD;
            } else if (res == 0)   {
                printf(" - Send CMD to node %d [%s] failed\n", i, node_db_list[i].ipv6_addr);
                num_timeout++;
                node_db_list[i].num_timeout++;
                err_code = ERR_TIME_OUT;
            } else {
                printf(" - Send CMD to node %d [%s] successful\n", i, node_db_list[i].ipv6_addr);   
                num_rep++;
                node_db_list[i].num_rep++;
                node_db_list[i].last_cmd = tx_cmd.cmd;            
                node_db_list[i].rdr = 100*node_db_list[i].num_rep/node_db_list[i].num_req;
            }
        }

        /* odd */
        else if (mode==1){
            if ((i%2)==1) {
                node_db_list[i].num_req++;
                //res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, false);
                res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, node_db_list[i].encryption_phase);
                if (res == -1) {
                    printf(" - ERROR: broadcast process \n");
                    err_code = ERR_BROADCAST_CMD;
                } else if (res == 0)   {
                    printf(" - Send CMD to node %d [%s] failed\n", i, node_db_list[i].ipv6_addr);
                    num_timeout++;
                    node_db_list[i].num_timeout++;
                    err_code = ERR_TIME_OUT;
                } else {
                    printf(" - Send CMD to node %d [%s] successful\n", i, node_db_list[i].ipv6_addr);   
                    num_rep++;
                    node_db_list[i].num_rep++;
                    node_db_list[i].last_cmd = tx_cmd.cmd;   
                    node_db_list[i].rdr = 100*node_db_list[i].num_rep/node_db_list[i].num_req;                             
                }
            }
        }
        /* even */
        else if (mode==0){
            if (((i % 2)==0) && (i!=0)){
                node_db_list[i].num_req++;
                //res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, false);
                res = ip6_send_cmd(i, SLS_NORMAL_PORT, NUM_RETRANS, node_db_list[i].encryption_phase);
                if (res == -1) {
                    printf(" - ERROR: broadcast process \n");
                    err_code = ERR_BROADCAST_CMD;
                } else if (res == 0)   {
                    printf(" - Send CMD to node %d [%s] failed\n", i, node_db_list[i].ipv6_addr);
                    num_timeout++;
                    node_db_list[i].num_timeout++;
                    err_code = ERR_TIME_OUT;
                } else {
                    printf(" - Send CMD to node %d [%s] successful\n", i, node_db_list[i].ipv6_addr);   
                    num_rep++;
                    node_db_list[i].num_rep++;
                    node_db_list[i].last_cmd = tx_cmd.cmd;      
                    node_db_list[i].rdr = 100*node_db_list[i].num_rep/node_db_list[i].num_req;                          
                }
            }
        }
    }

    rx_reply.err_code = err_code;
    memset(&rx_reply.arg,0,MAX_CMD_DATA_LEN);   //reset data of reply
    rx_reply.arg[0] = num_of_node-1; //except node 0
    rx_reply.arg[1] = num_rep;
    rx_reply.arg[2] = num_timeout;

    return 0;
}

/*------------------------------------------------*/
void process_gw_cmd(cmd_struct_t cmd, int nodeid) {
    switch (cmd.cmd) {
        case CMD_GW_HELLO:
            rx_reply.type = MSG_TYPE_REP;
            break;
        
        case CMD_GW_SET_TIMEOUT:
            rx_reply.type = MSG_TYPE_REP;
            timeout_val = (*pi_cmdPtr).arg[0];
            break;
        
        case CMD_GET_GW_STATUS:
            rx_reply.type = MSG_TYPE_REP;
            break;
        
        case CMD_GW_SHUTDOWN:
            rx_reply.type = MSG_TYPE_REP;
            break;
        
        case CMD_GW_TURN_ON_ALL:
            cmd.cmd = CMD_LED_DIM;
            execute_broadcast_cmd(cmd, 100, ALL_NODE);
            rx_reply.type = MSG_TYPE_REP;
            //execute_broadcasd_cmd(CMD_RF_LED_ON,0);
            break;

        case CMD_GW_TURN_ON_ODD:
            cmd.cmd = CMD_LED_DIM;
            execute_broadcast_cmd(cmd, 100, ODD_NODE);
            rx_reply.type = MSG_TYPE_REP;
            //execute_broadcasd_cmd(CMD_RF_LED_ON,170,1);
            break;
        
        case CMD_GW_TURN_ON_EVEN:
            cmd.cmd = CMD_LED_DIM;
            execute_broadcast_cmd(cmd, 100, EVEN_NODE);
            rx_reply.type = MSG_TYPE_REP;
            //execute_broadcasd_cmd(CMD_RF_LED_ON,170,0);
            break;

        case CMD_GW_TURN_OFF_ALL:
            cmd.cmd = CMD_LED_DIM;
            execute_broadcast_cmd(cmd, 170, ALL_NODE);
            rx_reply.type = MSG_TYPE_REP;
            //execute_broadcasd_cmd(CMD_RF_LED_OFF, 0);
            break;
        
        case CMD_GW_TURN_OFF_ODD:
            cmd.cmd = CMD_LED_DIM;
            execute_broadcast_cmd(cmd, 170, ODD_NODE);
            rx_reply.type = MSG_TYPE_REP;
            //execute_broadcasd_cmd(CMD_RF_LED_ON,0);
            break;
        
        case CMD_GW_TURN_OFF_EVEN:
            cmd.cmd = CMD_LED_DIM;            
            execute_broadcast_cmd(cmd, 170, EVEN_NODE);
            rx_reply.type = MSG_TYPE_REP;
            //execute_broadcasd_cmd(CMD_RF_LED_ON,0);
            break;

        case CMD_GW_DIM_ALL:
            cmd.cmd = CMD_LED_DIM;
            execute_broadcast_cmd(cmd, cmd.arg[0], ALL_NODE);
            rx_reply.type = MSG_TYPE_REP;
            //execute_broadcasd_cmd(CMD_RF_LED_DIM, cmd.arg[0]);
            break;

        case CMD_GW_DIM_ODD:
            cmd.cmd = CMD_LED_DIM;
            execute_broadcast_cmd(cmd, cmd.arg[0], ODD_NODE);
            rx_reply.type = MSG_TYPE_REP;
            //execute_broadcasd_cmd(CMD_RF_LED_DIM, cmd.arg[0]);
            break;

        case CMD_GW_DIM_EVEN:
            cmd.cmd = CMD_LED_DIM;
            execute_broadcast_cmd(cmd, cmd.arg[0], EVEN_NODE);
            rx_reply.type = MSG_TYPE_REP;
            //execute_broadcasd_cmd(CMD_RF_LED_DIM, cmd.arg[0]);
            break;

        case CMD_GW_MULTICAST_CMD:
            execute_multicast_cmd(cmd);
            rx_reply.type = MSG_TYPE_REP;
            break;

        case CMD_GW_BROADCAST_CMD:
            execute_broadcast_general_cmd(cmd, 2);
            rx_reply.type = MSG_TYPE_REP;
            break;

        case CMD_GW_GET_EMER_INFO:
            memcpy(&rx_reply.arg, &node_db_list[nodeid].last_emergency_msg, MAX_CMD_DATA_LEN);
            rx_reply.type = MSG_TYPE_REP;
            break;            

        case CMD_GW_RELOAD_FW:
            run_reload_gw_fw();
            rx_reply.type = MSG_TYPE_REP;
            reset_reply_data();
            rx_reply.arg[0]= num_of_active_node();
            break;            
    }
    //rx_reply.type = MSG_TYPE_REP;
}


/*------------------------------------------------*/
void reset_reply_data(){
    int i;
    for (i=0; i<MAX_CMD_DATA_LEN; i++) {
        rx_reply.arg[i]=0;
    }
}

/*------------------------------------------------*/
int num_of_active_node(){    
    int i, num_active_node;

    num_active_node = 0;
    for (i=1; i<num_of_node; i++) {
        if ((char)node_db_list[i].connected[0]=='Y') {
            num_active_node++;
        }
    }
    return num_active_node;
}

/*------------------------------------------------*/
int find_node(char *ip_addr) {
    int i;
    for (i=1; i<num_of_node; i++) {
        if (strcmp(node_db_list[i].ipv6_addr,ip_addr)==0) {
            return node_db_list[i].id;
        }
    }
    return -1;
}


/*------------------------------------------------*/
static void run_reload_gw_fw(){
    clear();

    read_node_list();
    
    init_main();
    
    run_node_discovery();

#ifdef AUTO_SET_APP_KEY
    auto_set_app_key();
#endif

    update_sql_db();
    show_local_db();    
}


//-------------------------------------------------------------------------------------------
static void update_sensor_data(int nodeid, env_struct_t env_db){
    uint8_t H, L;
    uint32_t temp;

//#ifdef SLS_USING_CC2538DK
    if (CC2538DK_HAS_SENSOR==TRUE) {
        node_db_list[nodeid].sensor_db.temperature = (float) env_db.temp;
        node_db_list[nodeid].sensor_db.temperature = node_db_list[nodeid].sensor_db.temperature /10;

        node_db_list[nodeid].sensor_db.pressure = (float) env_db.pressure;
        node_db_list[nodeid].sensor_db.pressure = node_db_list[nodeid].sensor_db.pressure /10;

        node_db_list[nodeid].sensor_db.light = (float) env_db.light;

        H = (uint8_t)(env_db.humidity >> 8);
        L = (uint8_t)(env_db.humidity & 0xFF);
        temp = ((uint32_t)H << 8) + (L & 0xFC);
        temp = (((temp) * 15625L) >> 13) - 6000;

        node_db_list[nodeid].sensor_db.humidity = (float)(temp*1.0);
        node_db_list[nodeid].sensor_db.humidity = node_db_list[nodeid].sensor_db.humidity/1000;

        printf("   + New async packet, extract data and update DB: \n");
        if (SHOW_FULL_DB == FALSE) {
            printf("     ++ Temperature =\033[1;33m %.1f (ºC)\033[0m, ", node_db_list[nodeid].sensor_db.temperature );
            printf("Light =\033[1;33m %.0f (lux)\033[0m, ", node_db_list[nodeid].sensor_db.light);
            printf("Pressure =\033[1;33m %.1f (hPa)\033[0m, ", node_db_list[nodeid].sensor_db.pressure);
            printf("Humidity =\033[1;33m %.2f (RH)\033[0m \n", node_db_list[nodeid].sensor_db.humidity);
        }
    }
//#endif        
}


void process_arduino_data(int emergency_node, cmd_struct_t emergency_reply) {
    uint16_t temp16;
    float temp_f;
    int i;
    
    printf("   + \033[1;35mUART data\033[0m (%d bytes): [", 9);
    for (i=0; i<9; i++)
        printf("\033[0;33m%02X \033[0m",emergency_reply.arg[i]);     
    printf("]\n");

    temp16 = emergency_reply.arg[0];
    temp16 = (temp16 << 8) | emergency_reply.arg[1];
    temp_f = (float)temp16;
    node_db_list[emergency_node].sensor_db.oil_voltage = temp_f/100;

    temp_f = emergency_reply.arg[2];
    node_db_list[emergency_node].sensor_db.oil_current = temp_f/10;


    temp16 = emergency_reply.arg[3];
    temp16 = (temp16 << 8) | emergency_reply.arg[4];
    temp_f = (float)temp16;
    node_db_list[emergency_node].sensor_db.water_voltage = temp_f/100;

    temp_f = emergency_reply.arg[5];
    node_db_list[emergency_node].sensor_db.water_current = temp_f/10;

    temp16 = emergency_reply.arg[6];
    temp16 = (temp16 << 8) | emergency_reply.arg[7];
    temp_f = (float)temp16;
    node_db_list[emergency_node].sensor_db.pressure_voltage = temp_f/100;

    temp_f = emergency_reply.arg[8];
    node_db_list[emergency_node].sensor_db.pressure_current = temp_f/10;

    printf("     + Oil      voltage = \033[0;33m%.2f\033[0m (mV); current = \033[0;33m%.2f (mA)\033[0m \n", node_db_list[emergency_node].sensor_db.oil_voltage, node_db_list[emergency_node].sensor_db.oil_current );
    printf("     + Water    voltage = \033[0;33m%.2f\033[0m (mV); current = \033[0;33m%.2f (mA)\033[0m \n", node_db_list[emergency_node].sensor_db.water_voltage, node_db_list[emergency_node].sensor_db.water_current );
    printf("     + Pressure voltage = \033[0;33m%.2f\033[0m (mV); current = \033[0;33m%.2f (mA)\033[0m \n", node_db_list[emergency_node].sensor_db.pressure_voltage, node_db_list[emergency_node].sensor_db.pressure_current );
}

//-------------------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    int res, i, j, result;
    int option = 1;

    struct sockaddr_in pi_myaddr;	                      /* our address */
	struct sockaddr_in pi_remaddr;	                    /* remote address */
	socklen_t pi_addrlen = sizeof(pi_remaddr);		   /* length of addresses */
	int pi_recvlen;			                            /* # bytes received */
	int pi_fd = 0, connfd = 0;				                         /* our socket */
	int pi_msgcnt = 0;			                          /* count # of messages we received */
	unsigned char pi_buf[BUFSIZE];	                        /* receive buffer */
    char buffer[MAXBUF];

    int emergency_node;
    int emergency_sock;
    int emergency_status;
    struct sockaddr_in6 sin6;
    int sin6len;
    int timeout = 0.5;      //200ms

    char str_time[80];

    uint16_t rssi_rx, old_seq;
    char buf[100];
    boolean node_authenticated = FALSE;
    uint32_t rx_res, temp;
    uint8_t H, L;


    clear();
	/* create a UDP socket */
    //if ((pi_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	if ((pi_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("ERROR: cannot create socket\n");
		//return 0;
	}
    setsockopt(pi_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

	/* bind the socket to any valid IP address and a specific port */
	memset((char *)&pi_myaddr, 0, sizeof(pi_myaddr));
	pi_myaddr.sin_family = AF_INET;
	pi_myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	pi_myaddr.sin_port = htons(SERVICE_PORT);

	if (bind(pi_fd, (struct sockaddr *)&pi_myaddr, sizeof(pi_myaddr)) < 0) {
		perror("ERROR: bind failed");
		return 0;
	}
    
    listen(pi_fd, 3); 
    
    /* read node list*/
    read_node_list();
    init_main();


    /* should wait approximately 5-10s here for a 60-node chain topology */
    /* running discovery */
    run_node_discovery();

#ifdef AUTO_SET_APP_KEY
    auto_set_app_key();
#endif

    update_sql_db();
    show_local_db();


    // setting UDP/IPv6 socket for emergency msg
    emergency_sock = socket(PF_INET6, SOCK_DGRAM,0);
    sin6len = sizeof(struct sockaddr_in6);
    memset(&sin6, 0, sin6len);
    /* just use the first address returned in the structure */
    sin6.sin6_port = htons(SLS_EMERGENCY_PORT);
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr = in6addr_any;
    emergency_status = bind(emergency_sock, (struct sockaddr *)&sin6, sin6len);
    if (-1 == emergency_status)
        perror("bind"), exit(1);

    emergency_status = getsockname(emergency_sock, (struct sockaddr *)&sin6, &sin6len);


    // main loop
    printf("\033[1;32m \nIV. GATEWAY WAITING on PORT %d for COMMANDS, and PORT %d for ASYNC MSG \033[0m \n", SERVICE_PORT, SLS_EMERGENCY_PORT);
    for (;;) {    
        // waiting for EMERGENCY msg
        fd.fd = emergency_sock;
        fd.events = POLLIN;
        res = poll(&fd, 1, timeout*1000); 
        if (res >0) {
            emergency_status = recvfrom(emergency_sock, buffer, MAXBUF, 0,(struct sockaddr *)&sin6, &sin6len);
            if (emergency_status>0) {
                //p = (char *) (&buffer); 
                cmdPtr = (cmd_struct_t *)((char *) (&buffer));
                emergency_reply = *cmdPtr;
                
                inet_ntop(AF_INET6, &sin6.sin6_addr, buffer, sizeof(buffer));
                result = find_node(buffer);
                if (result == -1) {
                    printf(" - Got a msg from node [%s]: not found in DB \n", buffer);
                    // can add to DB here                    
                } else {
                    emergency_node = result;

                    // check CRC here
                    //check_packet_for_node(&emergency_reply, emergency_node, false);
                    check_packet_for_node(&emergency_reply, emergency_node, node_db_list[emergency_node].encryption_phase);

                    if (emergency_reply.type == MSG_TYPE_ASYNC) {   
                        time(&rawtime );
                        timeinfo = localtime(&rawtime);
                        strftime(str_time,80,"%x-%H:%M:%S", timeinfo);
                        strcpy (node_db_list[emergency_node].last_seen, str_time);
                        strcpy(node_db_list[emergency_node].connected,"Y");


                        printf(" - [\033[1;33m%s\033[0m] Got an emergency msg (\033[1;32m%d\033[0m bytes) from node \033[1;32m%d\033[0m [\033[1;32m%s\033[0m] \n", str_time, emergency_status, emergency_node, buffer);
                        printf("   + Cmd =\033[1;35m 0x%02X\033[0m, type =\033[1;35m 0x%02X\033[0m,  seq =\033[1;35m %d \033[0m\n", emergency_reply.cmd, emergency_reply.type, emergency_reply.seq); 
                    

                        if (emergency_reply.cmd == ASYNC_MSG_SENT) { // read sensor data here
                            printf("   + \033[1;35mEmergency data\033[0m (\033[1;32m%d\033[0m bytes): [",MAX_CMD_DATA_LEN);
                            for (i=0; i<MAX_CMD_DATA_LEN; i++)
                                printf("%02X",emergency_reply.arg[i]);     
                            printf("]\n");


                            if (node_db_list[emergency_node].async_seq >= emergency_reply.seq){
                                printf("   + \033[1;33mDuplicate async packet\033[0m: last_seq = \033[1;35m%d\033[0m, curr_seq = \033[1;35m%d\033[0m ...discard \n",node_db_list[emergency_node].async_seq,
                                            emergency_reply.seq);
                            }

                            // extract sensor data
                            if (node_db_list[emergency_node].async_seq < emergency_reply.seq) {                                
                                memcpy(&env_db, emergency_reply.arg, sizeof(env_db));
                                //get rssi
                                //node_db_list[emergency_node].rssi = rx_reply.arg[22];

                                // update sensor data and print them
                                update_sensor_data(emergency_node, env_db);                

                                node_db_list[emergency_node].num_emergency_msg++;                                
                                node_db_list[emergency_node].sensor = env_db;
                                node_db_list[emergency_node].async_seq = emergency_reply.seq;
                                node_db_list[emergency_node].async_prr = 100.0*node_db_list[emergency_node].num_emergency_msg/node_db_list[emergency_node].async_seq;

                                memcpy(node_db_list[emergency_node].last_emergency_msg, emergency_reply.arg, MAX_CMD_DATA_LEN);

                                // data test UART here
                                //process_arduino_data(emergency_node, emergency_reply);

                                // send report to server
                                send_data_to_server(emergency_node);
                            }

                        }

                        //send authentication here
                        //if ((emergency_reply.cmd == ASYNC_MSG_JOINED) || (node_db_list[emergency_node].authenticated==FALSE)) {
                        if (emergency_reply.cmd == ASYNC_MSG_JOINED) {
                            // reset node infor
                            node_db_list[emergency_node].encryption_phase = FALSE;
                            node_db_list[emergency_node].async_seq = 0;
                            strcpy(node_db_list[emergency_node].connected,"Y");


                            printf(" - Node %d wants to authenticate network [cmd = 0x%02X]... \n",emergency_node, emergency_reply.cmd);
                            node_db_list[emergency_node].challenge_code = gen_random_num();
                            node_db_list[emergency_node].challenge_code_res = hash(node_db_list[emergency_node].challenge_code);
                            printf("\n1. Send challenge code = 0x%04X to joined node %d\n",node_db_list[emergency_node].challenge_code,emergency_node );
                            printf(" - Expected challenge response = 0x%04X \n",node_db_list[emergency_node].challenge_code_res);

                            gettimeofday(&t0, 0);
                            node_authenticated = authenticate_node(emergency_node, node_db_list[emergency_node].challenge_code, node_db_list[emergency_node].challenge_code_res, &res);
                            gettimeofday(&t1, 0);
                            node_db_list[emergency_node].delay = timedifference_msec(t0, t1);
                            printf(" - Roundtrip delay:\033[1;32m %.2f\033[0m (ms)\n", node_db_list[emergency_node].delay);

                            if (res == -1) {
                                printf(" - ERROR: authetication process \n");
                            } else if (res == 0)   {
                                printf(" - Node %d [\033[1;32m%s\033[0m] unavailable\n", i, node_db_list[emergency_node].ipv6_addr);
                            } else{
                                if (node_authenticated==TRUE) {
                                    node_db_list[emergency_node].authenticated = TRUE;
                                    //node_db_list[emergency_node].encryption_phase = node_db_list[emergency_node].authenticated;
                                    printf(" - Node %d authenticated, encryption_phase = %d \n", emergency_node, node_db_list[emergency_node].encryption_phase);

                                    // rx_reply
                                    node_db_list[emergency_node].channel_id = rx_reply.arg[2];

                                    rssi_rx = rx_reply.arg[3];                                    
                                    node_db_list[emergency_node].rssi = rssi_rx-150;

                                    node_db_list[emergency_node].lqi = rx_reply.arg[4];
                                    node_db_list[emergency_node].tx_power = rx_reply.arg[4];
                                    node_db_list[emergency_node].pan_id = (rx_reply.arg[6] << 8) | (rx_reply.arg[7]);


                                    node_db_list[i].llsec = rx_reply.arg[8] & 0x07;
                                    node_db_list[i].encryption_mode = rx_reply.arg[9];

                                    printf(" - Security_en = %d, LLSEC = %d, encryption_mode = %d \n", (rx_reply.arg[8]>>4), node_db_list[i].llsec, node_db_list[i].encryption_mode);

                                    // get last 8 bytes of next_hop    
                                    for (j=0; j<8; j++) {
                                        node_db_list[emergency_node].next_hop_addr[8+j] = rx_reply.arg[10+j];
                                    } 


                                    add_ipaddr(buf,emergency_node);
                                    strcpy(node_db_list[emergency_node].next_hop_link_addr, buf);
                                    printf(" - Node %d [\033[1;32m%s\033[0m] available, next-hop link-addr =[\033[1;32m%s\033[0m] \n", emergency_node, node_db_list[emergency_node].ipv6_addr, node_db_list[emergency_node].next_hop_link_addr);
                                    set_node_app_key(emergency_node);

                                    //reset counter here
                                    reset_node_cnt(emergency_node);
                                }
                            }
                        }
                        

                        update_sql_row(emergency_node);
                        show_local_db();
                    }
                    else {
                        printf(" - Invalid packet type: 0x%02X (normal/encryption mode mismatched) \n",emergency_reply.type);
                        // send authentication cmd here                               
                    }
                }    
            }
        }
        //close(emergency_sock);


        // waiting for command from software, for UDP    
        //pi_recvlen = recvfrom(pi_fd, pi_buf, BUFSIZE, 0, (struct sockaddr *)&pi_remaddr, &pi_addrlen);

        // Wait for ASYNC msg here
        fd.fd = pi_fd;
        fd.events = POLLIN;
        res = poll(&fd, 1, timeout*1000);   
        if (res>0) {
            //connfd = accept(pi_fd, (struct sockaddr*)NULL, NULL);   //for TCP
            connfd = accept(pi_fd, (struct sockaddr*)&pi_remaddr, &pi_addrlen);   //for TCP
            if (connfd >= 0) {
                printf("\n Accept TCP connection from: [\033[1;32m%s\033[0m]:%d \n", inet_ntoa(pi_remaddr.sin_addr), ntohs(pi_remaddr.sin_port));            
            }
        }
            
        // read data from client
        fd.fd = connfd;
        fd.events = POLLIN;
        res = poll(&fd, 1, timeout*1000); 
        if (res >0) {
            pi_recvlen = recv(connfd, &pi_buf, MAXBUF, 0);
            if (pi_recvlen > 0) {
                printf("1. Received a COMMAND (%d bytes)\n", pi_recvlen);
               		
  		        pi_cmdPtr = (cmd_struct_t *)(char *)(&pi_buf);
                rx_reply = *pi_cmdPtr;
		
                node_id = pi_cmdPtr->len;
                old_seq = pi_cmdPtr->seq;

                // get beginning time
                gettimeofday(&t0, 0);

                if (is_cmd_of_gw(*pi_cmdPtr)==TRUE) {
                    printf(" - Command Analysis: received GW command, cmdID=0x%02X, seq=0x%04X \n", pi_cmdPtr->cmd, pi_cmdPtr->seq);
                    process_gw_cmd(*pi_cmdPtr, node_id);
                    gettimeofday(&t1, 0);
                    printf(" - GW-Cmd execution delay %.2f (ms)\n", timedifference_msec(t0,t1));
                    update_sql_db();
                } else {  // not command for GW: send to node and wait for a reply
                    printf(" - Command Analysis: received LED command, cmdID=0x%02X, seq=0x%04X \n", pi_cmdPtr->cmd, pi_cmdPtr->seq);
                    tx_cmd = *pi_cmdPtr;
                    //res = ip6_send_cmd(node_id, SLS_NORMAL_PORT, NUM_RETRANS, false);
                    res = ip6_send_cmd(node_id, SLS_NORMAL_PORT, NUM_RETRANS, node_db_list[node_id].encryption_phase);
                    printf(" - GW-Cmd execution delay %.2f (ms)\n", node_db_list[node_id].delay);
            
                    //update local DB
                    node_db_list[node_id].num_req++;
                    if (res == -1) {
                        printf(" - ERROR: sending process \n");
                    } else if (res == 0)   {
                        //printf(" - Node %d [%s] unavailable\n", node_id, node_db_list[node_id].ipv6_addr);
                        node_db_list[node_id].num_timeout++;
                        rx_reply.err_code = ERR_TIME_OUT;
                        rx_reply.type = MSG_TYPE_REP;
                    } else {
                        //printf(" - Node %d [%s] available\n", node_id, node_db_list[node_id].ipv6_addr);   
                        node_db_list[node_id].num_rep++;
                        node_db_list[node_id].last_cmd = tx_cmd.cmd;
                        node_db_list[node_id].rdr = 100*node_db_list[node_id].num_rep/node_db_list[node_id].num_req;
                    }         
                    update_sql_row(node_id);
                }

                // send REPLY to sender NODE
                // update corresponding sequence
                rx_reply.seq = old_seq;
                printf("\n5. Sending RESPONE to user ACK-%d, seq = 0x%04X, ", pi_msgcnt++, old_seq);
                res = write(connfd, &rx_reply, sizeof(rx_reply));     // for TCP
                if (res == -1)          {printf(" - ERROR: reply to GW \n");
                } else if (res == 0)    {printf(" ... failed \n");
                } else                  {printf("%d (bytes)... successful \n", res);                    
                }

                // send report to server
                send_data_to_server(node_id);

                show_local_db();
            
                printf("\033[1;32m");
                printf("\nIV. GATEWAY WAITING on PORT %d for COMMANDS, and PORT %d for ASYNC MSG\n", SERVICE_PORT, SLS_EMERGENCY_PORT);
                printf("\033[0m");
            }
        }
        
        close(connfd);

        sleep(1);       // for other threads process
	}

    return 0;
}



//-------------------------------------------------------------------------------------------
/* calculate CRC
    encrypt 64 byte of cmd
*/
void make_packet_for_node(cmd_struct_t *cmd, uint16_t nodeid, boolean encryption_en){
    uint8_t i;
    unsigned char key_arr[16];
    
    convert_str2array(node_db_list[nodeid].app_key, key_arr, 16);

    tx_cmd.crc = 0;
    gen_crc_for_cmd(cmd);
    if (encryption_en==TRUE) {
        encrypt_payload(cmd, key_arr);
    }    

    if (encryption_en==TRUE) {
       printf(" - Make Tx packet for node %d...done; Encryption:\033[0;31m ENABLED \033[0m \n", nodeid);        
    }
    else {
       printf(" - Make Tx packet for node %d...done; Encryption:\033[0;31m DISABLED \033[0m \n", nodeid);                
    }
}

//-------------------------------------------------------------------------------------------
boolean check_packet_for_node(cmd_struct_t *cmd, uint16_t nodeid, boolean encryption_en){
    unsigned char key_arr[16];

    convert_str2array(node_db_list[nodeid].app_key, key_arr, 16);

    if (cmd->sfd!=SFD) {
        printf(" - Maybe packet is encrypted: SFD =\033[0;31m 0x%02X \033[0m\n",cmd->sfd);
        encryption_en = TRUE;
        if (encryption_en==TRUE) {
            decrypt_payload(cmd, key_arr);
            printf(" - Check RX packet for node %d... done;  Decryption:\033[0;31m ENABLED \033[0m \n", nodeid);
        }
        else
            printf(" - Check RX packet for node %d... done;  Decryption:\033[0;31m DISABLED \033[0m \n", nodeid);
    }
    
    return check_crc_for_cmd(cmd);
}

//-------------------------------------------------------------------------------------------
static void send_data_to_server(int node_id) {
    node_db = node_db_list[node_id];  
    
    printf("   +\033[1;35m Send data to server \033[0m[\033[1;32m%s\033[0m:\033[1;32m%d\033[0m]...\033[0;31m DISABLED \033[0m \n", REPORT_SERVER_IP, REPORT_SERVER_PORT);  
    printf("     ++ Temperature = \033[1;33m%.1f (ºC)\033[0m, ", node_db.sensor_db.temperature );
    printf("Light = \033[1;33m%.0f (lux)\033[0m, ", node_db.sensor_db.light);
    printf("Pressure = \033[1;33m%.1f (hPa)\033[0m, ", node_db.sensor_db.pressure);
    printf("Humidity = \033[1;33m%.2f (RH)\033[0m\n", node_db.sensor_db.humidity);

    // connect to server and update data hear, should use MQTT        
}

//-------------------------------------------------------------------------------------------
static int ip6_send_cmd(int nodeid, int port, int retrans, boolean encryption_en) {
    int sock;
    int status, i;
    struct addrinfo sainfo, *psinfo;
    struct sockaddr_in6 sin6;
    int sin6len;
    char buffer[MAXBUF]="";
    char str_app_key[32];
    unsigned char byte_array[16];
    char str_time[80];
    int num_of_retrans, result;


    sin6len = sizeof(struct sockaddr_in6);
    strcpy(dst_ipv6addr,node_db_list[nodeid].ipv6_addr);
    sprintf(str_port,"%d",port);

    //print_cmd(tx_cmd);
    //printf("ipv6_send: node = %d, ipv6= %s\n",nodeid, dst_ipv6addr);  
    prepare_cmd(nodeid);
    strtok(buffer, "\n");

    sock = socket(PF_INET6, SOCK_DGRAM,0);
    memset(&sin6, 0, sizeof(struct sockaddr_in6));
    sin6.sin6_port = htons(port);
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr = in6addr_any;

    status = bind(sock, (struct sockaddr *)&sin6, sin6len);
    if(-1 == status)
        perror("bind"), exit(1);

    memset(&sainfo, 0, sizeof(struct addrinfo));
    memset(&sin6, 0, sin6len);

    sainfo.ai_flags = 0;
    sainfo.ai_family = PF_INET6;
    sainfo.ai_socktype = SOCK_DGRAM;
    sainfo.ai_protocol = IPPROTO_UDP;
    status = getaddrinfo(dst_ipv6addr, str_port, &sainfo, &psinfo);

    rev_sin6len = sizeof(rev_sin6);

    num_of_retrans = 0;
    while (num_of_retrans < retrans) {
        gettimeofday(&t0, 0);

        /* encrypt payload here */
        make_packet_for_node(&tx_cmd, nodeid, encryption_en);
        //make_packet_for_node(&tx_cmd, nodeid, node_db_list[nodeid].authenticated);

        //for testing CRC:
        //tx_cmd.seq = random();

        printf("\n3. Forward REQUEST [retry=\033[1;35m%d\033[0m] (%d bytes) to node \033[1;32m%d\033[0m [\033[1;32m%s\033[0m]:\033[1;32m%s\033[0m ", num_of_retrans, status, nodeid, dst_ipv6addr,str_port);        
        status = sendto(sock, &tx_cmd, sizeof(tx_cmd), 0,(struct sockaddr *)psinfo->ai_addr, sin6len);
        if (status > 0)     {   printf("... done  \n");   } 
        else {                  printf("... ERROR \n");  }    
        printf("\n");

        /*wait for a reply */
        fd.fd = sock;
        fd.events = POLLIN;
        res = poll(&fd, 1, timeout_val*1000); // timeout

        if (res == -1) {
            printf(" - ERROR: GW forwarding command \n");
            num_of_retrans++;
        
        } else if (res == 0) {
            printf("4. Time-out: GW forwarding command [\033[1;35m%d\033[0m]\n", num_of_retrans);
            rx_reply = tx_cmd;

            num_of_retrans++;

            /*update local DB*/
            strcpy(node_db_list[nodeid].connected,"N");
            node_db_list[nodeid].num_of_retrans = num_of_retrans;
            time(&rawtime );
            timeinfo = localtime ( &rawtime );
            strftime(str_time,80,"%x-%H:%M:%S", timeinfo);
            strcpy (node_db_list[nodeid].last_seen, str_time);
            
            /* get the execution delay */
            gettimeofday(&t1, 0);
            node_db_list[nodeid].delay = timedifference_msec(t0, t1);

            update_sql_row(nodeid);

        } else {    // implies (fd.revents & POLLIN) != 0            
            rev_bytes = recvfrom((int)sock, rev_buffer, MAXBUF, 0, (struct sockaddr *)(&rev_sin6), (socklen_t *) &rev_sin6len);
            if (rev_bytes>=0) {

                inet_ntop(AF_INET6, &rev_sin6.sin6_addr, buffer, sizeof(buffer));
                result = find_node(buffer);                    

                printf("4. Got REPLY (%d bytes) from node \033[1;32m%d\033[0m [\033[1;32m%s\033[0m] \n", rev_bytes, result, buffer);   
                //p = (char *) (&rev_buffer); 
                cmdPtr = (cmd_struct_t *)((char *) (&rev_buffer));
                rx_reply = *cmdPtr;

                check_packet_for_node(&rx_reply, nodeid, encryption_en);    

                strcpy(node_db_list[nodeid].connected,"Y");
                node_db_list[nodeid].last_err_code = rx_reply.err_code;
                node_db_list[nodeid].num_of_retrans = num_of_retrans;

                time(&rawtime );  
                timeinfo = localtime ( &rawtime );
                strftime(str_time,80,"%x-%H:%M:%S", timeinfo);
                strcpy (node_db_list[nodeid].last_seen, str_time);
                strcpy (node_db_list[0].last_seen, str_time);   // update Router last seen time = last seen time of last packet received


                /*get the execution delay */    
                gettimeofday(&t1, 0);
                node_db_list[nodeid].delay  = timedifference_msec(t0, t1);

                num_of_retrans = retrans;   // exit while loop
                update_sql_row(nodeid);
                break;
            }
        } /* while */    
    }

    shutdown(sock, 2);
    close(sock); 
    
    // free memory
    freeaddrinfo(psinfo);
    psinfo = NULL;
    return res;
}