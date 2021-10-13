#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "user_utils.h"

#include "driver_uart.h"
#include "driver_syscntl.h"
#include "driver_iomux.h"
#include "driver_gpio.h"
#include "driver_ssp.h"
#include "driver_flash_ssp.h"

static uint8_t *boot_send_buffer = (uint8_t *)0x40014000;
static uint8_t *boot_recv_buffer = (uint8_t *)0x40014400;

enum storage_type_t
{
    STORAGE_TYPE_NONE,
    STORAGE_TYPE_FLASH,
    STORAGE_TYPE_RAM,
};

enum update_param_opcode_t
{
    UP_OPCODE_GET_TYPE,         // 0
    UP_OPCODE_SEND_TYPE,        // 1
    UP_OPCODE_WRITE,            // 2
    UP_OPCODE_WRITE_ACK,        // 3
    UP_OPCODE_WRITE_RAM,        // 4
    UP_OPCODE_WRITE_RAM_ACK,    // 5
    UP_OPCODE_READ_ENABLE,      // 6
    UP_OPCODE_READ_ENABLE_ACK,  // 7
    UP_OPCODE_READ,             // 8
    UP_OPCODE_READ_ACK,         // 9
    UP_OPCODE_READ_RAM,         // a
    UP_OPCODE_READ_RAM_ACK,     // b
    UP_OPCODE_BLOCK_ERASE,      // c
    UP_OPCODE_BLOCK_ERASE_ACK,  // d
    UP_OPCODE_CHIP_ERASE,       // e
    UP_OPCODE_CHIP_ERASE_ACK,   // f
    UP_OPCODE_DISCONNECT,       // 10
    UP_OPCODE_DISCONNECT_ACK,   // 11
    UP_OPCODE_CHANGE_BANDRATE,  // 12
    UP_OPCODE_CHANGE_BANDRATE_ACK,  // 13
    UP_OPCODE_ERROR,            // 14
    UP_OPCODE_EXECUTE_CODE,     //15
    UP_OPCODE_BOOT_RAM,         //16
    UP_OPCODE_EXECUTE_CODE_END, //17
    UP_OPCODE_BOOT_RAM_ACK,     //18
    UP_OPCODE_CALC_CRC32,       //19
    UP_OPCODE_CALC_CRC32_ACK,   //1a
    UP_OPCODE_MAX,
};

enum update_cmd_proc_result_t
{
    UP_RESULT_CONTINUE,
    UP_RESULT_NORMAL_END,
    UP_RESULT_BOOT_FROM_RAM,
    UP_RESULT_RESET,
};

struct update_param_header_t
{
    uint8_t code;
    uint32_t address;
    uint16_t length;
} __attribute__((packed));

const uint8_t dsp_program_boot_conn_req[] = {'F','R','E','Q','C','H','I','P'};//from embedded to pc, request
const uint8_t dsp_program_boot_conn_ack[] = {'F','R','8','0','1','H','O','K'};//from pc to embedded,ack
const uint8_t dsp_program_boot_conn_success[] = {'o','k'};

const uint16_t app_boot_uart_baud_map[12] = {
    12,24,48,96,144,192,384,576,1152,2304,4608,9216
};

extern void dsp_program_load_data(uint8_t *dest, uint32_t src, uint32_t len);
extern void dsp_program_save_data(uint32_t offset, uint32_t length, uint8_t *buffer);
extern void dsp_program_flash_sector_erase(uint32_t addr);

static int dsp_program_serial_gets(uint8_t ms, uint8_t *data_buf, uint32_t buf_size)
{
    int i, n=0;
    uint32_t recv_size;

    for(i=0; i<ms; i++)
    {
        co_delay_100us(10);
        recv_size = uart_get_data_nodelay_noint(data_buf+n, buf_size);
        n += recv_size;
        buf_size -= recv_size;
        if(0 == buf_size)
        {
            return n;
        }
    }

    return -1;
}

typedef void (*process_callback_func)(uint8_t *, uint16_t);
typedef uint8_t *(*process_get_buffer)(uint32_t);
static enum update_cmd_proc_result_t dsp_program_process_cmd(uint8_t *data, process_get_buffer get_buffer, process_callback_func callback)
{
    uint32_t req_address, req_length, rsp_length;   //req_length does not include header
    struct update_param_header_t *req_header = (struct update_param_header_t *)data;
    struct update_param_header_t *rsp_header;
    enum update_cmd_proc_result_t result = UP_RESULT_CONTINUE;

    req_address = req_header->address;
    req_length = req_header->length;

    rsp_length = sizeof(struct update_param_header_t);
    
    switch(req_header->code)
    {
        case UP_OPCODE_GET_TYPE:
            rsp_header = (struct update_param_header_t *)get_buffer(sizeof(struct update_param_header_t));
            if(rsp_header != NULL)
            {
                rsp_header->code = UP_OPCODE_SEND_TYPE;
                rsp_header->address = 0x01;
            }
            break;
        case UP_OPCODE_WRITE:
            rsp_header = (struct update_param_header_t *)get_buffer(sizeof(struct update_param_header_t));
            if(rsp_header != NULL)
            {
                //app_boot_save_data(req_address, req_length, data + sizeof(struct update_param_header_t));
                ssp_flash_write(req_address, req_length, data + sizeof(struct update_param_header_t));
                rsp_header->code = UP_OPCODE_WRITE_ACK;
                rsp_header->address = req_address;
                rsp_header->length = req_length;
            }
            break;
        case UP_OPCODE_READ:
            rsp_length += req_length;
            rsp_header = (struct update_param_header_t *)get_buffer(rsp_length);
            if(rsp_header != NULL)
            {
                //app_boot_load_data((uint8_t *)rsp_header + sizeof(struct update_param_header_t), req_address, req_length);
                ssp_flash_read(req_address, req_length, (uint8_t *)rsp_header + sizeof(struct update_param_header_t));
                rsp_header->code = UP_OPCODE_READ_ACK;
                rsp_header->address = req_address;
                rsp_header->length = req_length;
            }
            break;
        case UP_OPCODE_WRITE_RAM:
            rsp_header = (struct update_param_header_t *)get_buffer(rsp_length);
            {
                memcpy((uint8_t *)req_address,data+sizeof(struct update_param_header_t),req_length);
                rsp_header->code = UP_OPCODE_WRITE_RAM_ACK;
                rsp_header->address = req_address;
                rsp_header->length = req_length;
            }
            break;
        case UP_OPCODE_READ_ENABLE:
            rsp_header = (struct update_param_header_t *)get_buffer(rsp_length);
            {
                rsp_header->code = UP_OPCODE_READ_ENABLE_ACK;
                rsp_header->address = req_address;
                rsp_header->length = req_length;
            }
            break;
        case UP_OPCODE_READ_RAM:
            rsp_header = (struct update_param_header_t *)get_buffer(sizeof(struct update_param_header_t)+req_length);
            if(rsp_header != NULL)
            {
                memcpy((uint8_t *)rsp_header + sizeof(struct update_param_header_t), (uint8_t *)req_address, req_length);
                rsp_header->code = UP_OPCODE_READ_RAM_ACK;
                rsp_header->address = req_address;
                rsp_header->length = req_length;
                rsp_length += req_length;
            }
            break;
            
        case UP_OPCODE_BLOCK_ERASE:
            rsp_header = (struct update_param_header_t *)get_buffer(sizeof(struct update_param_header_t));
            if(rsp_header != NULL)
            {
                //app_boot_flash_sector_erase(req_address);
                ssp_flash_erase(req_address, 0x1000);
                rsp_header->code = UP_OPCODE_BLOCK_ERASE_ACK;
                rsp_header->address = req_address;
                rsp_header->length = req_length;
            }
            break;
        case UP_OPCODE_CHIP_ERASE:
            rsp_header = (struct update_param_header_t *)get_buffer(sizeof(struct update_param_header_t));
            if(rsp_header != NULL)
            {
                ssp_flash_chip_erase();
                rsp_header->code = UP_OPCODE_CHIP_ERASE_ACK;
                rsp_header->address = req_address;
                rsp_header->length = req_length;
            }
        break;
        case UP_OPCODE_DISCONNECT:
            rsp_header = (struct update_param_header_t *)get_buffer(sizeof(struct update_param_header_t));
            if(rsp_header != NULL)
            {
                rsp_header->code = UP_OPCODE_DISCONNECT_ACK;
            }
            result = (enum update_cmd_proc_result_t) (req_address & 0xFF);
            break;
        case UP_OPCODE_CHANGE_BANDRATE:
            rsp_header = (struct update_param_header_t *)get_buffer(sizeof(struct update_param_header_t));
            if(rsp_header != NULL)
            {
                rsp_header->code = UP_OPCODE_CHANGE_BANDRATE_ACK;
            }
            callback((uint8_t *)rsp_header, rsp_length);
            uart_init(req_address & 0xFF);
            break;
        case UP_OPCODE_EXECUTE_CODE:
            (*(void(*)(void))req_address)();
            rsp_header = (struct update_param_header_t *)get_buffer(sizeof(struct update_param_header_t));
            if(rsp_header != NULL)
            {
                rsp_header->code = UP_OPCODE_EXECUTE_CODE_END;
            }
            break;
        default:
            break;
    }

    if(req_header->code != UP_OPCODE_CHANGE_BANDRATE)
    {
        callback((uint8_t *)rsp_header, rsp_length);
    }

    return result;

}

static uint8_t *dsp_program_get_buffer(uint32_t length)
{
    return (uint8_t *)&boot_send_buffer[0];
}

static void dsp_program_send_rsp(uint8_t *buffer, uint16_t length)
{
    uart_write(buffer, length);
}

static void dsp_program_host_comm_loop(void)
{
    enum update_cmd_proc_result_t result = UP_RESULT_CONTINUE;
    struct update_param_header_t *req_header = (struct update_param_header_t *)&boot_recv_buffer[0]; //this address is useless after cpu running into this function

    while(result == UP_RESULT_CONTINUE)
    {
        uart_get_data_noint((uint8_t *)req_header, sizeof(struct update_param_header_t));
        if((req_header->length != 0)
           &&(req_header->code != UP_OPCODE_READ)
           &&(req_header->code != UP_OPCODE_READ_RAM))
        {
            uart_get_data_noint(((uint8_t *)req_header)+sizeof(struct update_param_header_t), req_header->length);
        }

        result = dsp_program_process_cmd((uint8_t *)req_header, dsp_program_get_buffer, dsp_program_send_rsp);
    }
    while(1);
}

void dsp_program(void)
{
    uint8_t buffer[sizeof(dsp_program_boot_conn_ack)];
    uint8_t do_handshake = 1;
    uint8_t retry_count = 1;

    while(retry_count) {
        uart_write((uint8_t *)dsp_program_boot_conn_req, sizeof(dsp_program_boot_conn_req));
        if(dsp_program_serial_gets(15, buffer, sizeof(dsp_program_boot_conn_ack))==sizeof(dsp_program_boot_conn_ack)) {
            if(memcmp(buffer, dsp_program_boot_conn_ack, sizeof(dsp_program_boot_conn_ack)) != 0) {
                do_handshake = 0;
            }
            else {
                break;
            }
        }
        else {
            do_handshake = 0;
        }

        retry_count--;
    }

    if(do_handshake) {
        system_set_port_mux(GPIO_PORT_A, GPIO_BIT_0, PORTA0_FUNC_SSP_SCLK);
        system_set_port_mux(GPIO_PORT_A, GPIO_BIT_1, PORTA1_FUNC_SSP_CSN);
        system_set_port_mux(GPIO_PORT_A, GPIO_BIT_2, PORTA2_FUNC_SSP_MOSI);
        system_set_port_mux(GPIO_PORT_A, GPIO_BIT_3, PORTA3_FUNC_SSP_MISO);
        system_set_port_mux(GPIO_PORT_D, GPIO_BIT_6, PORTD6_FUNC_D6);
        system_set_port_mux(GPIO_PORT_D, GPIO_BIT_7, PORTD7_FUNC_D7);
        gpio_set_dir(GPIO_PORT_D, GPIO_BIT_6, GPIO_DIR_OUT);
        gpio_set_dir(GPIO_PORT_D, GPIO_BIT_7, GPIO_DIR_OUT);
        gpio_set_pin_value(GPIO_PORT_D, GPIO_BIT_6, 1);
        gpio_set_pin_value(GPIO_PORT_D, GPIO_BIT_7, 1);
        ssp_init(8, 0, 0, 500000, 2, 0);
        
        uart_write((uint8_t *)dsp_program_boot_conn_success, sizeof(dsp_program_boot_conn_success));
        
        dsp_program_host_comm_loop();
        
        system_set_port_mux(GPIO_PORT_A, GPIO_BIT_0, PORTA0_FUNC_A0);
        system_set_port_mux(GPIO_PORT_A, GPIO_BIT_1, PORTA1_FUNC_A1);
        system_set_port_mux(GPIO_PORT_A, GPIO_BIT_2, PORTA2_FUNC_A2);
        system_set_port_mux(GPIO_PORT_A, GPIO_BIT_3, PORTA3_FUNC_A3);
        gpio_set_dir(GPIO_PORT_D, GPIO_BIT_6, GPIO_DIR_IN);
        gpio_set_dir(GPIO_PORT_D, GPIO_BIT_7, GPIO_DIR_IN);

    }
}

