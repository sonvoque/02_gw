/*
|-------------------------------------------------------------------|
| HCMC University of Technology                                     |
| Telecommunications Departments                                    |
| Utility for Smart Lighting System (SLS)                           |
| Version: 2.0                                                      |
| Author: sonvq@hcmut.edu.vn                                        |
| Date: 01/2019                                                     |
|-------------------------------------------------------------------|
*/
#include "util.h"

#define CBC 1
#define ECB 1

#include "aes_lib.h"

/*---------------------------------------------------------------------------*/
void print_cmd(cmd_struct_t command) {
    int i;
    printf("\nSFD=0x%02X; ",command.sfd);
    printf("node_id=%02d; ",command.len);
    printf("seq=%02d; ",command.seq);
    printf("type=0x%02X; ",command.type);
    printf("cmd=0x%02X; ",command.cmd);
    printf("err_code=0x%04X; \n",command.err_code); 
    printf("data=[");
    for (i=0;i<MAX_CMD_DATA_LEN;i++) 
        printf("%02X,",command.arg[i]);
    printf("]\n");
}  

/*---------------------------------------------------------------------------*/
void phex_16(uint8_t* data_16) { // in chuoi hex 16 bytes
    unsigned char i;

    for(i = 0; i < 16; ++i)
        printf("%.2x ", data_16[i]);
    printf("\n");
}

/*---------------------------------------------------------------------------*/
void phex_64(uint8_t* data_64) { // in chuoi hex 64 bytes
    unsigned char i;
    for(i = 0; i < 4; ++i) 
        phex_16(data_64 + (i*16));
    printf("\n");
}

/*---------------------------------------------------------------------------*/
// ma hoa 64 bytes
void encrypt_cbc(uint8_t* data_encrypted, uint8_t* data, uint8_t* key, uint8_t* iv) { 
    AES128_CBC_encrypt_buffer(data_encrypted+0,  data+0, 16, key, iv);
    AES128_CBC_encrypt_buffer(data_encrypted+16, data+16, 16, key, iv);
    //AES128_CBC_encrypt_buffer(data_encrypted+32, data+32, 16, key, iv);
    //AES128_CBC_encrypt_buffer(data_encrypted+48, data+48, 16, key, iv);
}

/*---------------------------------------------------------------------------*/
void scramble_data(uint8_t* data_encrypted, uint8_t* data, uint8_t* key) {
    int i;
    for (i=0; i<MAX_CMD_LEN; i++) {
        data_encrypted[i] = data[i] ^ key[i%4];
    }
}

/*---------------------------------------------------------------------------*/
void descramble_data(uint8_t* data_decrypted, uint8_t* data_encrypted, uint8_t* key) {
    int i;
    for (i=0; i<MAX_CMD_LEN; i++) {
        data_decrypted[i] = data_encrypted[i] ^ key[i%4];
    }
} 

/*---------------------------------------------------------------------------*/
void encrypt_payload(cmd_struct_t *cmd, uint8_t* key) {    
    uint8_t payload[MAX_CMD_LEN];    

    if (ENCRYPTION_MODE==1) {
        scramble_data((uint8_t *)cmd, (uint8_t *)cmd, key);
        printf(" - Scramble data ... done \n");
    }
    else if (ENCRYPTION_MODE==2) {
        memcpy(&payload, cmd, MAX_CMD_LEN);
        encrypt_cbc((uint8_t *)cmd, payload, key, iv);
        printf(" - Encrypt AES128-CBC ... done \n");
    }
}

/*---------------------------------------------------------------------------*/
void  decrypt_cbc(uint8_t* data_decrypted, uint8_t* data_encrypted, uint8_t* key, uint8_t* iv)  {
    AES128_CBC_decrypt_buffer(data_decrypted+0,  data_encrypted+0,  16, key, iv);
    AES128_CBC_decrypt_buffer(data_decrypted+16, data_encrypted+16, 16, key, iv);
    //AES128_CBC_decrypt_buffer(data_decrypted+32, data_encrypted+32, 16, key, iv);
    //AES128_CBC_decrypt_buffer(data_decrypted+48, data_encrypted+48, 16, key, iv);
}


/*---------------------------------------------------------------------------*/
void decrypt_payload(cmd_struct_t *cmd, uint8_t* key) {
	
    int i;
    printf(" - Key = [");
    for (i=0; i<=15; i++) {printf("%02X", *(key+i));}
    printf("]\n");
    

    if (ENCRYPTION_MODE==1) {
        descramble_data((uint8_t *)cmd, (uint8_t *)cmd, key);
        printf(" - Descramble data ... done \n");
    }
    else if (ENCRYPTION_MODE==2) {
        decrypt_cbc((uint8_t *)cmd, (uint8_t *)cmd, key, iv);
        printf(" - Decrypt AES128-CBC... done \n");
    }
}


/*---------------------------------------------------------------------------*/
unsigned short gen_crc16(uint8_t *data_p, unsigned short length) {
    unsigned char i;
    unsigned int data;
    unsigned int crc = 0xffff;
    uint8_t len;
    len = length;

    if (len== 0)
        return (~crc);
    do    {
        for (i=0, data=(unsigned int)0xff & *data_p++;
            i < 8; i++, data >>= 1) {
            if ((crc & 0x0001) ^ (data & 0x0001))
                crc = (crc >> 1) ^ POLY;
            else  crc >>= 1;
        }
    } while (--len);

    crc = ~crc;
    data = crc;
    crc = (crc << 8) | (data >> 8 & 0xff);

    return (crc);
}

/*---------------------------------------------------------------------------*/
void gen_crc_for_cmd(cmd_struct_t *cmd) {
    uint16_t crc16_check, i;
    uint8_t byte_arr[MAX_CMD_LEN-2];

    memcpy(&byte_arr, cmd, MAX_CMD_LEN-2);
    crc16_check = gen_crc16(byte_arr, MAX_CMD_LEN-2);
    cmd->crc = (uint16_t)crc16_check;
    printf(" - Generate CRC-16 [0x%04X]...done \n", crc16_check);
}


/*---------------------------------------------------------------------------*/
boolean check_crc_for_cmd(cmd_struct_t *cmd) {
    uint16_t crc16_check;
    uint8_t byte_arr[MAX_CMD_LEN-2];

    memcpy(&byte_arr, cmd, MAX_CMD_LEN-2);
    crc16_check = gen_crc16(byte_arr, MAX_CMD_LEN-2);

    if (crc16_check == cmd->crc) {
        printf(" - Check CRC: CRC16-cal = 0x%04X ...matched \n", crc16_check);
        return TRUE;
    }
    else{
        printf(" - Check CRC: CRC16-cal = 0x%04X; CRC16-val =  0x%04X ...failed \n",crc16_check, cmd->crc);
        return FALSE;        
    }        
}

/*---------------------------------------------------------------------------*/
uint16_t hash(uint16_t a) {
    uint32_t tem;
    tem =a;
    tem = (a+0x7ed55d16) + (tem<<12);
    tem = (a^0xc761c23c) ^ (tem>>19);
    tem = (a+0x165667b1) + (tem<<5);
    tem = (a+0xd3a2646c) ^ (tem<<9);
    tem = (a+0xfd7046c5) + (tem<<3);
    tem = (a^0xb55a4f09) ^ (tem>>16);
   return tem & 0xFFFF;
}

/*---------------------------------------------------------------------------*/
void convert_array2str(unsigned char *bin, unsigned int binsz, char **result) {
    char hex_str[]= "0123456789ABCDEF";
    unsigned int  i;

    *result = (char *)malloc(binsz * 2 + 1);
    (*result)[binsz * 2] = 0;

    if (!binsz)
        return;

    for (i = 0; i < binsz; i++) {
        (*result)[i * 2 + 0] = hex_str[(bin[i] >> 4) & 0x0F];
        (*result)[i * 2 + 1] = hex_str[(bin[i]     ) & 0x0F];
    }  
}

/*---------------------------------------------------------------------------*/
int convert_str2array(const char *hex_str, unsigned char *byte_array, int byte_array_max) {
    int hex_str_len = strlen(hex_str);
    int i = 0, j = 0;
    // The output array size is half the hex_str length (rounded up)
    int byte_array_size = (hex_str_len+1)/2;
    if (byte_array_size > byte_array_max) {
        // Too big for the output array
        return -1;
    }
    if (hex_str_len % 2 == 1){
        // hex_str is an odd length, so assume an implicit "0" prefix
        if (sscanf(&(hex_str[0]), "%1hhx", &(byte_array[0])) != 1){
            return -1;
        }
        i = j = 1;
    }
    for (; i < hex_str_len; i+=2, j++){
        if (sscanf(&(hex_str[i]), "%2hhx", &(byte_array[j])) != 1){
            return -1;
        }
    }
    return byte_array_size;
}



/*---------------------------------------------------------------------------*/
float timedifference_msec(struct timeval t0, struct timeval t1){
    return (t1.tv_sec - t0.tv_sec) * 1000.0f + (t1.tv_usec - t0.tv_usec) / 1000.0f;
}

/*---------------------------------------------------------------------------
//float float_example = 1.11;
//uint8_t bytes[4];
//float2Bytes(float_example, &bytes[0]);
/*---------------------------------------------------------------------------*/
void float2Bytes(float val, uint8_t* bytes_array){
  union {
    float float_variable;
    uint8_t temp_array[4];
  } u;
  u.float_variable = val;
  memcpy(bytes_array, u.temp_array, 4);
}


/*---------------------------------------------------------------------------*/
uint16_t gen_random_num() {
    uint16_t random1, random2;
    random1 = rand() % 255;
    random2 = rand() % 255;    
    return (random1 << 8) | (random2);   
}

/*---------------------------------------------------------------------------*/
void gen_random_key_128(unsigned char* key){
    int i;
    unsigned char byte_array[16];
    uint16_t temp;

    for (i=0; i<16; i++) {
        temp = gen_random_num();
        byte_array[i] = (unsigned char)(temp & 0xFF);
    }
    memcpy(key, byte_array, 16); 
}