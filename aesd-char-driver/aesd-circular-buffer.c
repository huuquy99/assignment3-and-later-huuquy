/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include <stdio.h>
#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    /**
    * TODO: implement per description
    */
    // Check for NULL pointers
    if (buffer == NULL || entry_offset_byte_rtn == NULL) {
        printf("Invalid buffer or entry_offset_byte_rtn pointer\n");
        return NULL;
    }
    
    // Check if the buffer is empty
    if (buffer->in_offs == buffer->out_offs && !buffer->full) {
        // Buffer is empty
        printf("Buffer is empty\n");
        return NULL;
    }

    size_t total_bytes = 0;
    uint8_t index = buffer->out_offs;
    do{
        // Get the current entry
        struct aesd_buffer_entry *entry = &buffer->entry[index];
        // Check if the entry's buffptr is NULL, indicating an empty entry
        if(entry->buffptr == NULL)
        {
            //Skip empty entry
            index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            continue;
        }
        // Add the size of the current entry to total_bytes
        total_bytes += entry->size;
        // Check if the char_offset is within the total_bytes accumulated so far
        if(char_offset < total_bytes)
        {
            // Calculate the byte offset within the current entry
            *entry_offset_byte_rtn = char_offset - (total_bytes - entry->size);
            return entry;
        }
        // Move to the next index
        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }while(index != buffer->in_offs);

    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */
    if (buffer == NULL || add_entry == NULL) {
        printf("Invalid buffer or add_entry pointer\n");
        return;
    }
    if (buffer->full) {
        // Buffer is full, overwrite the oldest entry
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    // Add the new entry at the current in_offs position
    buffer->entry[buffer->in_offs] = *add_entry;
    // Advance in_offs to the next position
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    // Check if the buffer is now full
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
