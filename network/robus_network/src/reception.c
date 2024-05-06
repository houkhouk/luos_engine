/******************************************************************************
 * @file reception
 * @brief reception state machine
 * @author Luos
 * @version 0.0.0
 ******************************************************************************/

/******************************* Description of the RX/TX process ***********************************
 *                                          Byte Received
 *                                                |
 *                                                |
 *                                        no      v      yes
 *                         +------------------Tx Enable----------------+
 *                         |                                           |
 *  +------+               |                                           v
 *  | Drop |               |                                    +-------------+
 *  | byte |               |                                    | Get Collision|
 *  +------+               |                                    |      |       |
 *     ^                   |                                    |      v       |
 *     |                   |                                    |  source ID   |yes
 *     |                   |                                    |  received----+----+->Disable Rx
 *     |                   +------------------------------+     |      |       |    |     |
 *     |                   v                              |     |      |no     |    |     |
 *     |            +-------------+   +-------------+     |     |      v       |no  |     v
 *     |            |  Get Header | +>|  Get Data   |<-+  |     |  Collision --+----+  Wait End
 *     |            |      |      | | |     |       |  |  |     +------+-------+       Transmit
 *     |            |      |      | | |     v    no |  |  |            |yes               |
 *     |         yes|      v      | | |   Message---+--+  |            v                  |
 *     +------------+-Should Drop | | |   Complete  |     +------ Disable Tx              v
 *     |            |      |no    | | |      |yes   |                           +-----Ack needed
 *     |            |      |      | | |      |      |                           | no      |
 *     |            |      |      | | |      |      |                           |         |yes
 *     |          no|      v      | | |      v      |no   Drop message          |         v
 *     +------------+-valid header| | |  Valid CRC -+---->Send NACK +-----+     |     +--------+
 *                  |      |yes   | | +------+------+     if needed       |     |     | Get ack|
 *                  |      |      | |        |yes                         |     |     +---+----+
 *                  |      +------+-+        |                            v     v         |
 *                  |             |          v                  +-----------------+       |
 *                  +-------------+    Send ACK if needed       |     Timeout     |       |
 *                                     Valid message----------->|  End reception  |<------+
 *                                                              +-----------------+
 ***********************************************************************************************/

#include <string.h>
#include "reception.h"
#include "robus_hal.h"
#include "robus_network.h"
#include "context.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#ifdef DEBUG
    #include <stdio.h>
#endif

#ifdef SELFTEST
    #include "selftest.h"
#endif

#define COLLISION_DETECTION_NUMBER 4

/*******************************************************************************
 * Variables
 ******************************************************************************/
uint8_t data_rx[sizeof(msg_t)] = {0}; // Buffer to store the received data
uint16_t crc_val               = 0;   // CRC value

/*******************************************************************************
 * Function
 ******************************************************************************/
/******************************************************************************
 * @brief Reception init.
 * @param None
 * @return None
 ******************************************************************************/
void Recep_Init()
{
    // Initialize the reception state machine
    ctx.rx.status.unmap      = 0;
    ctx.rx.callback          = Recep_GetHeader;
    ctx.rx.status.identifier = 0xF;
}
/******************************************************************************
 * @brief Reception init.
 * @param None
 * @return None
 ******************************************************************************/
void Recep_PhyInit(luos_phy_t *phy_robus)
{
    LUOS_ASSERT(phy_robus != NULL);
    phy_robus->rx_timestamp   = 0;
    phy_robus->rx_buffer_base = data_rx;
    phy_robus->rx_data        = data_rx;
    phy_robus->rx_keep        = true;
}
/******************************************************************************
 * @brief Callback to get a complete header
 * @param data come from RX
 * @return None
 * _CRITICAL function call in IRQ
 ******************************************************************************/
_CRITICAL void Recep_GetHeader(luos_phy_t *phy_robus, volatile uint8_t *data)
{
    // Catch the byte.
    phy_robus->rx_data[phy_robus->received_data++] = *data;

    // Check if we have all we need.
    switch (phy_robus->received_data)
    {
        case 1:
            // When we catch the first byte we timestamp the msg
            // We remove the time of the first byte to get the exact reception date.
            // 1 byte is 10 bits and we convert it to nanoseconds
            phy_robus->rx_timestamp = Phy_GetTimestamp() - ((uint32_t)10 * (uint32_t)1000000000 / (uint32_t)ROBUS_NETWORK_BAUDRATE);

            // Declare Robus as busy
            ctx.tx.lock = true;
            // Switch the transmit status to disable to be sure to not interpreat the end timeout as an end of transmission.
            ctx.tx.status = TX_DISABLE;
            // Reset CRC computation
            crc_val = 0xFFFF;
            break;

        case (sizeof(header_t)):
            // Header finished
#ifdef DEBUG
            printf("*******header data*******\n");
            printf("protocol : 0x%04x\n", phy_robus->rx_msg->header.config);         /*!< Protocol version. */
            printf("target : 0x%04x\n", phy_robus->rx_msg->header.target);           /*!< Target address, it can be (ID, Multicast/Broadcast, Type). */
            printf("target_mode : 0x%04x\n", phy_robus->rx_msg->header.target_mode); /*!< Select targeting mode (ID, ID+ACK, Multicast/Broadcast, Type). */
            printf("source : 0x%04x\n", phy_robus->rx_msg->header.source);           /*!< Source address, it can be (ID, Multicast/Broadcast, Type). */
            printf("cmd : 0x%04x\n", phy_robus->rx_msg->header.cmd);                 /*!< msg definition. */
            printf("size : 0x%04x\n", phy_robus->rx_msg->header.size);               /*!< Size of the data field. */
#endif

            // Switch state machine to data reception
            ctx.rx.callback = Recep_GetData;
            if (ctx.rx.status.rx_framing_error == false)
            {
                // We complete the header reception, we need to compute all the needed values.
                // Compute message header size, keep, ... the result will be available in phy_robus->rx_size, ...
                Phy_ComputeHeader(phy_robus);
            }
            break;

        default:
            break;
    }
    RobusHAL_ComputeCRC((uint8_t *)&phy_robus->rx_data[phy_robus->received_data - 1], (uint8_t *)&crc_val);
}
/******************************************************************************
 * @brief Callback to get a complete data
 * @param data come from RX
 * @return None
 * _CRITICAL function call in IRQ
 ******************************************************************************/
_CRITICAL void Recep_GetData(luos_phy_t *phy_robus, volatile uint8_t *data)
{
    static uint16_t crc;
    if (phy_robus->rx_keep == false)
    {
        ctx.rx.callback = Recep_Drop;
        return;
    }
    if (phy_robus->received_data < phy_robus->rx_size)
    {
        // Catch the byte.
        phy_robus->rx_data[phy_robus->received_data] = *data;
        // Continue the CRC computation until the end of data
        RobusHAL_ComputeCRC((uint8_t *)&phy_robus->rx_data[phy_robus->received_data], (uint8_t *)&crc_val);
    }
    else if (phy_robus->received_data > phy_robus->rx_size)
    {
        crc = crc | ((uint16_t)(*data) << 8);
        if (crc == crc_val)
        {
            // Message is OK
            // Check if we need to send an ack
            if ((phy_robus->rx_ack == true) && (phy_robus->rx_keep == true))
            {
                // Send an Ack
                Transmit_SendAck();
            }
            // Remove the CRC additional byte
            phy_robus->received_data--;
            // Valid the message
            Phy_ValidMsg(phy_robus);
        }
        else
        {
            ctx.rx.status.rx_error = true;
            if ((phy_robus->rx_ack == true) && (phy_robus->rx_keep == true))
            {
                Transmit_SendAck();
            }
            // We dont valid the message, this will fragment a little bit the memory but it will be faster than a full memory check
        }
        // We will drop any data received between now and the timeout
        ctx.rx.callback = Recep_Drop;
        return;
    }
    else
    {
        // This is the first byte of the CRC, store it
        crc = (uint16_t)(*data);
    }
    phy_robus->received_data++;
}
/******************************************************************************
 * @brief Callback to get a collision beetween RX and Tx
 * @param data come from RX
 * @return None
 * _CRITICAL function call in IRQ
 ******************************************************************************/
_CRITICAL void Recep_GetCollision(luos_phy_t *phy_robus, volatile uint8_t *data)
{
    static uint8_t data_count = 0;
    // Check data integrity
    if ((ctx.tx.data[data_count++] != *data) || (!ctx.tx.lock) || (ctx.rx.status.rx_framing_error == true))
    {
        // Data dont match, or we don't start to send the message, there is a collision
        ctx.tx.collision = true;
        // Stop TX trying to save input datas
        RobusHAL_SetTxState(false);
        // Save the received data into the allocator to be able to continue the reception
        for (uint8_t i = 0; i < data_count - 1; i++)
        {
            Recep_GetHeader(phy_robus, (volatile uint8_t *)&ctx.tx.data[i]);
        }
        Recep_GetHeader(phy_robus, data);
        // Switch to get header.
        ctx.rx.callback = Recep_GetHeader;
        ctx.tx.status   = TX_NOK;
        data_count      = 0;
    }
    else
    {
        if (data_count == COLLISION_DETECTION_NUMBER)
        {
#ifdef SELFTEST
            selftest_SetRxFlag();
#endif
            // Collision detection end
            data_count = 0;
            RobusHAL_SetRxState(false);
            RobusHAL_ResetTimeout(false, 0);
            if (ctx.tx.status == TX_NOK)
            {
                // Switch to catch Ack.
                ctx.rx.callback = Recep_CatchAck;
            }
            else
            {
                // Switch to get header.
                ctx.rx.callback = Recep_GetHeader;
            }
        }
    }
}
/******************************************************************************
 * @brief Callback to drop received data wrong header, data, or collision
 * @param data come from RX
 * @return None
 * _CRITICAL function call in IRQ
 ******************************************************************************/
_CRITICAL void Recep_Drop(luos_phy_t *phy_robus, volatile uint8_t *data)
{
    return;
}
/******************************************************************************
 * @brief end of a reception and prepare next receive or transmit
 * @param None
 * @return None
 * _CRITICAL function call in IRQ
 ******************************************************************************/
_CRITICAL void Recep_Timeout(void)
{
    if ((ctx.rx.callback != Recep_GetHeader) && (ctx.rx.callback != Recep_Drop))
    {
        ctx.rx.status.rx_timeout = true;
    }
    Recep_Reset();
    Transmit_End(); // This is possibly the end of a transmission, check it.
}
/******************************************************************************
 * @brief reset the reception state machine
 * @param None
 * @return None
 * _CRITICAL function call in IRQ
 ******************************************************************************/
_CRITICAL void Recep_Reset(void)
{
    luos_phy_t *phy_robus = Robus_GetPhy();
    Phy_ResetMsg(phy_robus);
    crc_val                        = 0xFFFF;
    ctx.rx.status.rx_framing_error = false;
    ctx.rx.status.rx_error         = false;
    ctx.rx.callback                = Recep_GetHeader;
    phy_robus->rx_buffer_base      = data_rx;
    phy_robus->rx_data             = data_rx;
    RobusHAL_SetRxDetecPin(true);
}
/******************************************************************************
 * @brief Catch ack when needed for the sent msg
 * @param data come from RX
 * @return None
 * _CRITICAL function call in IRQ
 ******************************************************************************/
_CRITICAL void Recep_CatchAck(luos_phy_t *phy_robus, volatile uint8_t *data)
{
    volatile status_t status;
    status.unmap = *data;
    if ((!status.rx_error) && (status.identifier == 0x0F))
    {
        ctx.tx.status = TX_OK;
    }
    else
    {
        ctx.tx.status = TX_NOK;
    }
}
