/*
|-------------------------------------------------------------------|
| HCMC University of Technology                                     |
| Telecommunications Departments                                    |
| Command Line Interface for Smart Lighting System (SLS)            |
| Version: 1.0                                                      |
| Author: sonvq@hcmut.edu.vn                                        |
| Date: 01/2017                                                     |
|-------------------------------------------------------------------|
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

#include "sls.h"
#include "sls_cli.h"
#include "aes_lib.h"  

#define POLY 0x8408
/*
//                                      16   12   5
// this is the CCITT CRC 16 polynomial X  + X  + X  + 1.
// This works out to be 0x1021, but the way the algorithm works
// lets us use 0x8408 (the reverse of the bit pattern).  The high
// bit is always assumed to be set, thus we only use 16 bits to
// represent the 17 bit value.
*/

void 	print_cmd();
void	encrypt_payload(cmd_struct_t *cmd, uint8_t* key);
void	decrypt_payload(cmd_struct_t *cmd, uint8_t* key);
void	gen_crc_for_cmd(cmd_struct_t *cmd);
boolean check_crc_for_cmd(cmd_struct_t *cmd);
unsigned short gen_crc16(uint8_t *data_p, unsigned short length);
boolean check_crc16(uint8_t *data_p, unsigned short length);
uint16_t hash( uint16_t a);
int  	convert_str2array(const char *hex_str, unsigned char *byte_array, int byte_array_max);
void 	convert_array2str(unsigned char *bin, unsigned int binsz, char **result);
void 	float2Bytes(float val,uint8_t* bytes_array);
float 	timedifference_msec(struct timeval t0, struct timeval t1);
uint16_t gen_random_num();
void 	gen_random_key_128(uint8_t* key);

void 	encrypt_cbc(uint8_t* data_encrypted, uint8_t* data,  uint8_t* key,  uint8_t* iv);
void  	decrypt_cbc(uint8_t* data_decrypted, uint8_t* data_encrypted,  uint8_t* key,  uint8_t* iv);
void 	scramble_data(uint8_t* data_encrypted, uint8_t* data, uint8_t* key);
void 	descramble_data(uint8_t* data_decrypted, uint8_t* data_encrypted, uint8_t* key);
