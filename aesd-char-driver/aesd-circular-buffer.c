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
    // guard clause to check for valid arguments
    if (buffer == NULL) {
        return NULL;
    }

    // set up arguments for parsing
    int index = 0;
    int temp = char_offset;
    bool valid = false;

    // iterative loop
    while (true) {
        if (temp < buffer->entry[index].size && temp > 0) {
            valid = true;
            break;
        } else if (temp == 0) {
            valid = true;
            break;
        } else if (temp < 0) {
            break;
        } else {
            temp = temp - buffer->entry[index].size;
            index++;
        }

        // check if overflow occurs
        if (index == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
            return NULL;
        }
    }
    
    // check if parsing is valid
    if (valid) {
        *entry_offset_byte_rtn = (size_t)&buffer->entry[index].buffptr[temp];
        return &buffer->entry[index];
    }

    // invalid; return NULL
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
    // guard clause to check for valid arguments
    if (buffer == NULL || add_entry == NULL) {
        return;
    }

    // check if the buffer is full and free the buffer in the oldest entry
    if (buffer->full && (buffer->entry[buffer->in_offs].buffptr != NULL)) {
        // this needs to free a void pointer, as the original field has a const qualifier
        free((void *)buffer->entry[buffer->in_offs].buffptr); 
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // re-allocate memory for the place in memory the new entry should go
    char *new_buff = (char *)malloc(sizeof(char) * add_entry->size);
    if (new_buff == NULL) {
        return;
    }
    memcpy(new_buff, add_entry->buffptr, add_entry->size);

    // fill in new entry data
    buffer->entry[buffer->in_offs].buffptr = new_buff;
    buffer->entry[buffer->in_offs].size = add_entry->size;

    // increment buffer index, wrapping around if full
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // check if the buffer is full
    buffer->full = (buffer->in_offs == buffer->out_offs);
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
