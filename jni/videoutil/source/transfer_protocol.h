#ifndef __TRANSFER_PROTOCOL_H__
#define __TRANSFER_PROTOCOL_H__
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <utils.h>

typedef struct _data_package {
	uint16_t package_length;
	uint8_t seq;
	uint8_t package_type;
	uint8_t slice_ident;
	uint8_t *nal_data;
	uint8_t verify;
} data_package;

typedef struct _control_package {
	uint16_t package_length;
	uint8_t seq;
	uint8_t package_type;
	uint8_t command:6;
	uint8_t ack:2;
	uint8_t *oid;
	uint8_t verify;
} control_package;

typedef struct _manage_package {
	uint16_t package_length;
	uint8_t seq;
	uint8_t package_type;
	uint8_t command:6;
	uint8_t ack:2;
	uint8_t *param;
	uint8_t verify;
} manage_package;

typedef struct _transfer_package {
	uint8_t package_length;
	uint8_t seq;
	uint8_t src_addr;
	uint8_t dest_addr;
	uint8_t *content;
	uint8_t verify;
	uint8_t end_flag;
} transfer_package;

typedef enum _slice_type {
	SLICE_TYPE_INTER = 0,
	SLICE_TYPE_LAST,
	SLICE_TYPE_FIRST,
	SLICE_TYPE_NONE
} slice_type;

#define CONTROL_PORT  	6006
#define DATA_PORT 	6007
#define MANAGE_PORT  	6008
#define TRANSFER_PORT 	6009

#define SWAP16(a) ( (a)<<8 | (a)>>8 )

/*
 * 功能：按协议获取数据的有效长度
 * 参数：
 * 	data 	接收到的数据包
 * 返回值:
 * 	n 	有效数据长度
 */
int get_valid_data_length(const char *data) {
	assert(data);
	uint16_t *p16 = (uint16_t *)data;
	return SWAP16(p16[0]);
}

/*
 * 功能：按位异或方式校验
 * 参数：
 * 	data 	需要校验的数据
 * 返回值:
 * 	0 	校验成功
 * 	-1 	校验失败
 */
int verify(const char *data, int size) {
	assert(data);
	const char *buf = data;
	char result=0;
	for(int i=0; i<size; i++) {
		result^=*buf++;
	}
	if(result != data[size]) {
		return -1;
	}

	return 0;
}

/*
 * 功能：分析数据并填充到对应数据包
 * 参数：
 *  	data 	需要分析的数据
 *  	package 返回分析后的数据包
 * 返回值：
 * 	0 	成功
 * 	-1 	失败
 */
int parse_data_package(const char *data, data_package *package) {
	assert(package);
	uint16_t *p16 = (uint16_t *)data;
	uint8_t *p8 = (uint8_t *)data;
	package->package_length = SWAP16(p16[0]);
	package->seq = p8[2];
	package->package_type = p8[3];
	if(package->package_type != 0x01) {
		perror("Unknown package type !");
		return -1;
	}
	package->slice_ident = p8[4];
	package->nal_data = (uint8_t *)malloc(package->package_length-5);
	assert(package->nal_data);
	if(!package->nal_data) {
		perror("Cannot malloc package->nal_data");
	}
	memcpy(package->nal_data, &p8[5], package->package_length-5);
	package->verify=p8[package->package_length];
	return 0;
}

/*
 * 功能：判断当前包的分片类型
 * 参数：
 * 	package 待判断的数据包
 * 返回值：
 * 	0 图像帧的中间分片
 * 	1 图像帧的最后一个分
 * 	2 图像帧的第一个分片
 * 	3 图像帧没有分片
 */
int get_data_package_slice_ident_type(data_package *package) {
	return package->slice_ident & 0x03;
}

int has_sps_pps(data_package *package, uint8_t *sps, uint8_t *pps, uint8_t *sps_pps) {
	assert(package);
	uint8_t *data = package->nal_data;

        bool hasSps = false;
        bool hasPps = false;
	int sps_offset = 0;
	int pps_offset = 0;
	int sps_size = 0;
	int pps_size = 0;

	enum _status {
		STATUS_PARSE_SPS = 0,
		STATUS_PARSE_PPS,
		STATUS_PARSE_PPS_END,
		STATUS_PARSE_ERROR,
		STATUS_PARSE_SUCCESS
	} status;

	for(int i=0; i<package->package_length; i++) {
		switch (status) {
			case STATUS_PARSE_SPS:
				if( ((data[i] & 0x0f) == 0x07)
						&& (data[i-1] == 0x01)
						&& (data[i-2] == 0x00) 
						&& (data[i-3] == 0x00)
						&& (data[i-4] == 0x00)) {
					sps_offset = i+1;
					status = STATUS_PARSE_PPS;
					continue;
				}
			case STATUS_PARSE_PPS:
				if( ((data[i] & 0x0f) == 0x08)
						&& (data[i-1] == 0x01)
						&& (data[i-2] == 0x00) 
						&& (data[i-3] == 0x00)
						&& (data[i-4] == 0x00)) {
					pps_offset = i+1;
					sps_size = pps_offset-sps_offset-5;
					status = STATUS_PARSE_PPS_END;
					continue;
				}
			case STATUS_PARSE_PPS_END:
				if( ((data[i] & 0x0f) == 0x08)
						&& (data[i-1] == 0x01)
						&& (data[i-2] == 0x00) 
						&& (data[i-3] == 0x00)
						&& (data[i-4] == 0x00)) {
					sps_size = i-pps_offset-4;
					status = STATUS_PARSE_SUCCESS;
					break;
				}
		}	

		status = STATUS_PARSE_ERROR;
	}


	if(status == STATUS_PARSE_SUCCESS) {
		sps = (uint8_t *)malloc(sps_size*sizeof(uint8_t));
		memcpy(sps, data+sps_offset, sps_size);

#ifdef DEBUG
		LOGI("Has sps");
		for(int k=0; k<sps_size; k++)
			LOGI("0x%02x ", sps[k]);
#endif
		pps = (unsigned char *)malloc(pps_size*sizeof(unsigned char));
		memcpy(pps, data+pps_offset, pps_size);

		sps_pps = chrs_join(sps, pps);
		return 0;
	}

	return -1;
}

int parse_control_package(char *data) {

}

int parse_manage_package(char *data) {

}

int parse_transfer_package(char *data) {

}
#endif
