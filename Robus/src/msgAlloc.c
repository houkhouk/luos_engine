/******************************************************************************
 * @file msgAlloc.c
 * @brief Message allocation manager
 * @author Luos
 * @version 0.0.0
 ******************************************************************************/

#include "config.h"
#include "msgAlloc.h"
#include <string.h>
#include "luosHAL.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

//msg allocation management
volatile uint8_t msg_buffer[MSG_BUFFER_SIZE];         /*!< Memory space used to save and alloc messages. */
volatile msg_allocator_t alloc_table[MAX_MSG_NB];     /*!< Message allocation table. */
volatile uint16_t alloc_id;                           /*!< last writen alloc_table id. */
volatile msg_t *current_msg;                          /*!< current work in progress msg pointer. */
volatile uint8_t *data_ptr;                           /*!< Pointer to the next data able to be writen into msgbuffer. */
vm_t *module_concerned_by_current_msg[MAX_VM_NUMBER]; /*!< Save the module concerned by the current message. */
uint16_t module_concerned_stack_pointer;              /*!< The next empty case of module_concerned_by_current_msg */
uint8_t msg_completely_alloc;                         /*!< A simple flag to avoid multiple allocation of the same memory space. */

/*******************************************************************************
 * Functions
 ******************************************************************************/

/*******************************************************************************
 * Functions --> Allocator memory manager
 ******************************************************************************/

static void MsgAlloc_ClearSlot(uint16_t _alloc_id);
static error_return_t MsgAlloc_ClearMemSpace(void *from, void *to);

/******************************************************************************
 * @brief Clear a slot. This action is due to an error
 * @param None
 * @return None
 ******************************************************************************/
static void MsgAlloc_ClearSlot(uint16_t _alloc_id)
{
    if (_alloc_id > alloc_id)
    {
        return;
    }
    // TODO we should set an error into the concerned module
    for (uint16_t rm = _alloc_id; rm < alloc_id; rm++)
    {
        alloc_table[rm] = alloc_table[rm + 1];
    }
    alloc_id--;
}

/******************************************************************************
 * @brief prepare a buffer space to be usable by cleaning remaining messages and prepare pointers
 * @param from : start of the memory space to clean
 * @param to : start of the memory space to clean
 * @return error_return_t
 ******************************************************************************/
static error_return_t MsgAlloc_ClearMemSpace(void *from, void *to)
{
    //******** Check if there is sufficient space on the buffer **********
    if ((uint32_t)to > ((uint32_t)&msg_buffer[MSG_BUFFER_SIZE - 1]))
    {
        // We reach msg_buffer end return an error
        return FAIL;
    }
    //******** Prepare a memory space to be writable **********
    // check if there is no msg between from and to
    while (((uint32_t)alloc_table[0].msg_pt >= (uint32_t)from) & ((uint32_t)alloc_table[0].msg_pt <= (uint32_t)to) & (i <= alloc_id))
    {
        // This message is in the space we want to use, clear the slot
        MsgAlloc_ClearSlot(0);
    }
    // if we go here there is no reason to continue because newest messages can't overlap the memory zone.
    return SUCESS;
}

/******************************************************************************
 * @brief Init the allocator.
 * @param None
 * @return None
 ******************************************************************************/
void MsgAlloc_Init(void)
{
    //******** Init global vars pointers **********
    current_msg = (msg_t *)&msg_buffer[0];
    data_ptr = (uint8_t *)&msg_buffer[0];
    module_concerned_stack_pointer = 0;
    alloc_id = 0;
}

/******************************************************************************
 * @brief Invalid the current message header by removing it (data will be ignored).
 * @param None
 * @return None
 ******************************************************************************/
void MsgAlloc_InvalidMsg(void)
{
    //******** Remove the header by reseting data_ptr *********
    data_ptr = (uint8_t *)current_msg;
    module_concerned_stack_pointer = 0;
}

/******************************************************************************
 * @brief Valid the current message header by preparing the allocator to get the message data
 * @param concerned_module : The module concerned by this message
 * @return None
 ******************************************************************************/
void MsgAlloc_ValidHeader(vm_t *concerned_module)
{
    //******** Prepare the allocator to get data  *********
    // Save the concerned module pointer into the concerned module pointer stack
    if (module_concerned_stack_pointer >= MAX_VM_NUMBER)
    {
        while (1)
            ;
    }
    module_concerned_by_current_msg[module_concerned_stack_pointer++] = concerned_module;
    // Be sure that the end of msg_buffer is after data_ptr + header_t.size
    if (msg_completely_alloc != TRUE)
    {
        if (MsgAlloc_ClearMemSpace((void *)&current_msg->data[0], (void *)(&current_msg->data[current_msg->header.size + 2])) == FAIL)
        {
            // We are at the end of msg_buffer, we need to move the current space to the begin of msg_buffer
            // Clean the begin of msg_buffer with the space of the complete message
            MsgAlloc_ClearMemSpace((void *)&msg_buffer[0], (void *)(&msg_buffer[0] + sizeof(header_t) + current_msg->header.size + 2));
            // Copy the header at the begining of msg_buffer
            memcpy((void *)&msg_buffer[0], (void *)current_msg, sizeof(header_t));
            // Move current_msg to msg_buffer
            current_msg = (volatile msg_t *)&msg_buffer[0];
            // move data_ptr after the new location of the header
            data_ptr = &msg_buffer[0] + sizeof(header_t) + 1;
            msg_completely_alloc = TRUE;
        }
    }
}

/******************************************************************************
 * @brief Finish and valid the current message
 * @param None
 * @return None
 ******************************************************************************/
void MsgAlloc_EndMsg(void)
{
    //******** End the message **********
    // create a slot for each concerned module
    while (module_concerned_stack_pointer > 0)
    {
        // find a free slot
        if (alloc_id == MAX_MSG_NB)
        {
            // There is no more space on the alloc_table, remove the oldest msg.
            MsgAlloc_ClearSlot(0);
        }
        // fill the informations of the message in this slot
        alloc_table[alloc_id].msg_pt = (msg_t *)current_msg;
        alloc_table[alloc_id].vm_pt = module_concerned_by_current_msg[module_concerned_stack_pointer - 1];
        module_concerned_stack_pointer--;
        alloc_id++;
    }
    //******** Prepare the next msg *********
    msg_completely_alloc = FALSE;
    //data_ptr is actually 2 bytes after the message data because of the CRC. Remove the CRC.
    data_ptr -= 2;
    // clean space between data_ptr (data_ptr + sizeof(header_t)+2)
    if (MsgAlloc_ClearMemSpace((void *)data_ptr, (void *)(data_ptr + sizeof(header_t) + 2)) == FAIL)
    {
        MsgAlloc_ClearMemSpace((void *)&msg_buffer[0], (void *)(&msg_buffer[0] + sizeof(header_t) + 2));
        current_msg = (volatile msg_t *)&msg_buffer[0];
        data_ptr = &msg_buffer[0];
        return;
    }
    // update the current_msg
    current_msg = (volatile msg_t *)data_ptr;
}

/******************************************************************************
 * @brief write a byte into the current message.
 * @param uint8_t data to write in the allocator
 * @return None
 ******************************************************************************/
void MsgAlloc_SetData(uint8_t data)
{
    //******** Write data  *********
    *data_ptr = data;
    data_ptr++;
}

/******************************************************************************
 * @brief return the current message
 * @param None
 * @return msg_t* pointer to the current message
 ******************************************************************************/
msg_t *MsgAlloc_GetCurrentMsg(void)
{
    return (msg_t *)current_msg;
}

/*******************************************************************************
 * Functions --> Allocator find and consume
 ******************************************************************************/

/******************************************************************************
 * @brief Pull a message allocated to a specific module
 * @param target_module : The module concerned by this message
 * @param returned_msg : The message pointer.
 * @return error_return_t
 ******************************************************************************/
error_return_t MsgAlloc_PullMsg(vm_t *target_module, msg_t **returned_msg)
{
    //find the oldest message allocated to this module
    for (uint16_t i = 0; i < alloc_id; i++)
    {
        if (alloc_table[i].vm_pt == target_module)
        {
            *returned_msg = alloc_table[i].msg_pt;
            LuosHAL_SetIrqState(FALSE);
            // Clear the slot by sliding others to the left on it
            for (uint16_t rm = i; rm < alloc_id; rm++)
            {
                alloc_table[rm] = alloc_table[rm + 1];
            }
            alloc_id--;
            LuosHAL_SetIrqState(TRUE);
            return SUCESS;
        }
    }
    // At this point we don't find any message for this module
    return FAIL;
}
/******************************************************************************
 * @brief get back the module who received the oldest message 
 * @param allocated_module : Return the module concerned by the oldest message
 * @param alocator_id : Id of the allocator slot
 * @param returned_header : The header of the message.
 * @return error_return_t : Fail is there is no more message available.
 ******************************************************************************/
error_return_t MsgAlloc_LookAtAllocatorSlot(uint16_t alocator_id, vm_t **allocated_module, msg_t **unconsumed_message)
{
    if (alocator_id < alloc_id)
    {
        *allocated_module = alloc_table[alocator_id].vm_pt;
        // return the message header pointer
        *unconsumed_message = alloc_table[alocator_id].msg_pt;
        return SUCESS;
    }
    return FAIL;
}
/******************************************************************************
 * @brief return the number of allocated messages
 * @param None
 * @return the number of messages
 ******************************************************************************/
uint16_t MsgAlloc_AllocNbr(void)
{
    return (uint16_t)alloc_id;
}