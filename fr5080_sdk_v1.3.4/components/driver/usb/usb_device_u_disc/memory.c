/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

#include "driver_flash.h"
#include "driver_sdc.h"

#include "os_mem.h"

#include "usbmass.h"
#include "usb_scsi.h"
#include "usb_bot.h"
#include "memory.h"
#include "mass_mal.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
uint32_t Block_Read_count = 0;
uint32_t Block_offset;
uint32_t Counter = 0;

uint32_t  Idx;
//��ʹ��malloc��ʱ��
//uint32_t Data_Buffer[BULK_MAX_PACKET_SIZE *2]; //��ʹ��malloc

//ʹ��malloc��ʱ��
uint32_t *Data_Buffer = 0;   //�ⲿ������malloc���� BULK_MAX_PACKET_SIZE*2*4 ��ô���ֽڵ��ڴ档

uint8_t TransferState = TXFR_IDLE;

////////////////////////////�Լ������һ�����USB״̬�ļĴ���///////////////////
//bit0:��ʾ����������SD��д������
//bit1:��ʾ��������SD����������
//bit2:SD��д���ݴ����־λ
//bit3:SD�������ݴ����־λ
//bit4:1,��ʾ��������ѯ����(�������ӻ�������)
uint8_t USB_STATUS_REG=0;
////////////////////////////////////////////////////////////////////////////////

/* Extern variables ----------------------------------------------------------*/
extern uint8_t *Bulk_Data_Buff;//��usb_bot.c����������
//extern uint8_t Bulk_Data_Buff[BULK_MAX_PACKET_SIZE];  /* data buffer*/
extern uint16_t Data_Len;
extern uint8_t Bot_State;
extern Bulk_Only_CBW CBW;
extern Bulk_Only_CSW CSW;

/* Private function prototypes -----------------------------------------------*/
/* Extern function prototypes ------------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

void Memory_init(void)
{
    //Bulk_Data_Buff =
    if(Data_Buffer == 0)
        Data_Buffer = os_malloc(BULK_MAX_PACKET_SIZE*8);
}

/*******************************************************************************
* Function Name  : Read_Memory
* Description    : Handle the Read operation from the microSD card.
* Input          : None.
* Output         : None.
* Return         : None.
*******************************************************************************/
void Read_Memory(uint8_t lun, uint32_t Memory_Offset, uint32_t Transfer_Length)
{
    static uint32_t Offset, Length;
//    uint8_t STA;
    
    if (TransferState == TXFR_IDLE )
    {
        Offset = Memory_Offset * Mass_Block_Size[lun];
        Length = Transfer_Length * Mass_Block_Size[lun];
        TransferState = TXFR_ONGOING;
    }
    
    if (TransferState == TXFR_ONGOING )
    {
        if (!Block_Read_count)
        {
            //ipc_nand_flash_read(Mass_Block_Size[lun], Offset);
            #if 0
            STA=MAL_Read(   lun,
                            Offset,
                            Data_Buffer,
                            Mass_Block_Size[lun]);
            if(STA)USB_STATUS_REG|=0X08;//SD��������!
            #else
            //printf("read offset is %d.\r\n", Offset/Mass_Block_Size[lun]);
            sdmem_read_single_block(Data_Buffer, Offset/Mass_Block_Size[lun]);
            #endif

            USB_SIL_Write(USBMASS_OUT_EP, (uint8_t *)Data_Buffer, BULK_MAX_PACKET_SIZE);

            Block_Read_count = Mass_Block_Size[lun] - BULK_MAX_PACKET_SIZE;
            Block_offset = BULK_MAX_PACKET_SIZE;
        }
        else
        {
            USB_SIL_Write(USBMASS_OUT_EP, (uint8_t *)Data_Buffer + Block_offset, BULK_MAX_PACKET_SIZE);

            Block_Read_count -= BULK_MAX_PACKET_SIZE;
            Block_offset += BULK_MAX_PACKET_SIZE;
        }
        SetEPTxCount(USBMASS_OUT_EP, BULK_MAX_PACKET_SIZE);
        SetEPTxStatus(USBMASS_OUT_EP, EP_TX_VALID);
        Offset += BULK_MAX_PACKET_SIZE;
        Length -= BULK_MAX_PACKET_SIZE;

        CSW.dDataResidue -= BULK_MAX_PACKET_SIZE;
    }
    
    if (Length == 0)
    {
        Block_Read_count = 0;
        Block_offset = 0;
        Offset = 0;
        Bot_State = BOT_DATA_IN_LAST;
        TransferState = TXFR_IDLE;
    }
}

/*******************************************************************************
* Function Name  : Write_Memory
* Description    : Handle the Write operation to the microSD card.
* Input          : None.
* Output         : None.
* Return         : None.
*******************************************************************************/
void Write_Memory (uint8_t lun, uint32_t Memory_Offset, uint32_t Transfer_Length)
{
    static uint32_t W_Offset, W_Length;
//    uint8_t STA;

    uint32_t temp =  Counter + 64;

    if (TransferState == TXFR_IDLE )
    {
        W_Offset = Memory_Offset * Mass_Block_Size[lun];
        W_Length = Transfer_Length * Mass_Block_Size[lun];
        TransferState = TXFR_ONGOING;
    }

    if (TransferState == TXFR_ONGOING )
    {

        for (Idx = 0 ; Counter < temp; Counter++)
        {
            *((uint8_t *)Data_Buffer + Counter) = Bulk_Data_Buff[Idx++];
        }
        W_Offset += Data_Len;
        W_Length -= Data_Len;
        if (!(W_Length % Mass_Block_Size[lun]))
        {
            Counter = 0;
            #if 0
            STA=MAL_Write(  lun,
                            W_Offset - Mass_Block_Size[lun],
                            Data_Buffer,
                            Mass_Block_Size[lun]);
            if(STA)USB_STATUS_REG|=0X04;//SD��д����!
            #else
            sdmem_write_single_block(Data_Buffer, (W_Offset - Mass_Block_Size[lun])/Mass_Block_Size[lun]);
            #endif
        }

        CSW.dDataResidue -= Data_Len;
        SetEPRxStatus(USBMASS_IN_EP, EP_RX_VALID); /* enable the next transaction*/
    }

    if ((W_Length == 0) || (Bot_State == BOT_CSW_Send))
    {
        Counter = 0;
        Set_CSW (CSW_CMD_PASSED, SEND_CSW_ENABLE);
        TransferState = TXFR_IDLE;
    }
}

