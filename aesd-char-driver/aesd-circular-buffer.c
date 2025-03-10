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
    if (buffer == NULL || entry_offset_byte_rtn == NULL) {
        return NULL;
    }

    // set up arguments for parsing
    int index = buffer->out_offs;   // current index for reading the circular buffer
    size_t temp = char_offset;      // decremented by each entry's length, until it can be used to index an entry
    int iterations = 0;             // keeps track of iterations to make sure wraparound doesn't occur

    // iterate through circular buffer, updating the temp variable until it can be used as an index
    while (1) {
        if (temp < buffer->entry[index].size) {
            // case: entry found with a valid character offset
            *entry_offset_byte_rtn = temp; // The offset within this entry
            return &buffer->entry[index];
        } else {
            // case: temp is still out of bounds; iterate again
            temp -= buffer->entry[index].size;
            index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        }
        
        // stop if attempting to wrap around
        if (++iterations == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
            return NULL;
        }
    }
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
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // point reference to entry pointer passed in
    // this assumes the caller will manage mallocs and frees for the data this points to
    buffer->entry[buffer->in_offs] = *add_entry;

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
