/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations

// AESD-specific includes
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

// module information
MODULE_AUTHOR("Jake Uyechi");
MODULE_LICENSE("Dual BSD/GPL");

// global structs
struct aesd_dev aesd_device;

struct file_operations aesd_fops = {
    .owner =            THIS_MODULE,
    .read =             aesd_read,
    .write =            aesd_write,
    .open =             aesd_open,
    .release =          aesd_release,
    .llseek =           aesd_llseek,
    .unlocked_ioctl =   aesd_unlocked_ioctl,
};

// module open
int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("[AESD] open");

    // add aesd_dev struct to filp private data
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    filp->f_pos = 0;

    // return
    return 0;
}

// module release
int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("[AESD] release");
    
    // set the private_data to NULL
    filp->private_data = NULL;

    return 0;
}

// read data from circular buffer to user
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("[AESD] read %zu bytes with offset %lld",count,*f_pos);

    // check if the read requests 0 bytes, return early if needed
    if (count == 0) return 0;
    
    // retrieve driver data from filp
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;

    // lock mutex
    if (mutex_lock_interruptible(&dev->lock)) return -EINTR;

    // start retrieving data; call find_entry_offset function to retrieve the data needed from the circular buffer
    size_t offset;
    struct aesd_buffer_entry *read_entry = 
        aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &offset);
    if (read_entry == NULL || read_entry->buffptr == NULL) {
        // entry data is missing; return 0
        retval = 0;
        goto cleanup;
    }
    
    // determine the read size based on the entry size at the offset and the count limit
    size_t read_size = min(count, read_entry->size - offset);

    // copy to user, limited by the read_size
    // this copy should start at the entry returned from the find_offset function, plus the offset within that entry
    if (copy_to_user(buf, read_entry->buffptr + offset, read_size)) {
        retval = -EFAULT;
        goto cleanup;
    }

    // update the f_pos argument with the new read pointer
    *f_pos += read_size;

    // set return value equal to the read size
    retval = read_size;

cleanup:
    mutex_unlock(&dev->lock);
    return retval;
}

// write data from user to circular buffer
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("[AESD] write %zu bytes with offset %lld",count,*f_pos);
    
    // get the device struct from file pointer
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;

    // lock device mutex
    if (mutex_lock_interruptible(&dev->lock)) return -EINTR;

    // kmalloc a new buffer for userspace data
    size_t total_size = dev->incomplete_command_size + count;
    char *to_write = kmalloc(total_size, GFP_KERNEL);

    // copy the existing buffer
    if (dev->incomplete_command_buffer != NULL) {
        memcpy(to_write, dev->incomplete_command_buffer, dev->incomplete_command_size);
        kfree(dev->incomplete_command_buffer);
    }

    // copy the incoming buffer from userspace
    if (copy_from_user(to_write + dev->incomplete_command_size, buf, count)) {
        retval = -EFAULT;
        kfree(to_write);
        goto cleanup;
    }

    // free the old buffer in the device struct and point it to the new buffer
    dev->incomplete_command_buffer = to_write;
    dev->incomplete_command_size = total_size;
    
    // check if there's a newline to process
    char *cmd_break;
    char *cmd_start = dev->incomplete_command_buffer;
    size_t read_size = total_size;
    while ((cmd_break = memchr(cmd_start, '\n', read_size))) {
        // there is a newline, and cmd_break points to it

        // create a new sub command for everything before the first break
        size_t subcmd_size = (cmd_break - dev->incomplete_command_buffer) + 1;
        char *subcmd = kmalloc(subcmd_size, GFP_KERNEL);
        memcpy(subcmd, cmd_start, subcmd_size);

        // create a new entry struct and copy the sub command to it, freeing it after copying
        struct aesd_buffer_entry to_add;
        to_add.size = subcmd_size;
        to_add.buffptr = kmalloc(subcmd_size, GFP_KERNEL);
        if (to_add.buffptr == NULL) {
            retval = -ENOMEM;
            kfree(subcmd);
            goto cleanup;
        }
        memcpy((void *)to_add.buffptr, subcmd, subcmd_size);
        kfree(subcmd);

        // add the new entry, capturing the old one 
        const char *to_free = aesd_circular_buffer_add_entry(&dev->circular_buffer, &to_add);
        if (to_free) {
            kfree(to_free);
        }

        // update the cmd_start and read_size pointers
        cmd_start = cmd_break + 1;
        read_size -= (subcmd_size > read_size) ? read_size : subcmd_size;
    }

    // check if there's an excess command
    if (cmd_start < dev->incomplete_command_buffer + total_size) {
        // create a buffer for excess command
        size_t remaining_size = dev->incomplete_command_buffer + total_size - cmd_start;
        char *remaining = kmalloc(remaining_size, GFP_KERNEL);
        if (remaining == NULL) {
            retval = -ENOMEM;
            goto cleanup;
        }
        memcpy(remaining, cmd_start, remaining_size);

        // free the old buffer
        kfree(dev->incomplete_command_buffer);
        dev->incomplete_command_buffer = remaining;
        dev->incomplete_command_size = remaining_size;
    } else {
        // no excess command, set the incomplete buffer to NULL and size to 0
        kfree(dev->incomplete_command_buffer);
        dev->incomplete_command_buffer = NULL;
        dev->incomplete_command_size = 0;
    }

    // set the return value to count
    retval = count;

    // debug print
    aesd_print_cb(&dev->circular_buffer);

cleanup:
    mutex_unlock(&dev->lock);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence) {
    PDEBUG("[AESD] llseek using offset %lld, starting from %d", off, whence);
    
    int retval;

    // get the device struct from file pointer
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    loff_t cb_size = (loff_t)aesd_size(&dev->circular_buffer);

    // use the fixed_size_llseek, immediately returning the result of the function
    mutex_lock(&filp->f_pos_lock);
    retval = fixed_size_llseek(filp, off, whence, cb_size);
    mutex_unlock(&filp->f_pos_lock);

    // return
    return retval;
}

/**
 * Adjust f_pos of the file based on the location of the write command and offset
 * 
 * @param filp                  file pointer
 * @param write_cmd             the index of the write command to seek to
 * @param write_cmd_offset      the offset within write_cmd to seek to
 * 
 * @return 0 on success, -ERR on failure
 */
static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset) {
    // get private data from device
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    
    // check if command exceeds the circular buffer size
    if (!(write_cmd < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)) {
        PDEBUG("[AESD] File offset to index %d exceeds command buffer size.", write_cmd);
        return -EINVAL;
    }

    // check if command offset exceeds the command
    struct aesd_buffer_entry *selected_entry = &dev->circular_buffer.entry[write_cmd];
    if (!(write_cmd_offset < selected_entry->size)) {
        PDEBUG("[AESD] Command offset to %d in '%s' exceeds command size.",
            write_cmd_offset, selected_entry->buffptr);
        return -EINVAL;
    }

    // calculate offset
    struct aesd_buffer_entry *temp;
    uint8_t index;
    loff_t calculated_offset = 0;
    AESD_CIRCULAR_BUFFER_FOREACH(temp, &aesd_device.circular_buffer, index) {
        if (index < write_cmd) {
            calculated_offset += temp->size;
        } else if (index == write_cmd) {
            calculated_offset += write_cmd_offset;
            break;
        }
    }

    PDEBUG("[AESD] aesd_adjust_file_offset New Offset: %lld", calculated_offset);

    // valid command offset, update f_pos with calculated offset
    mutex_lock(&filp->f_pos_lock);
    filp->f_pos = calculated_offset;
    mutex_unlock(&filp->f_pos_lock);

    return 0;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    // define a return value for all ioctls 
    long retval = -ENOTTY;

    // switch statement to handle different ioctl commands
    switch (cmd) {
        case AESDCHAR_IOCSEEKTO:
            PDEBUG("[AESD] Received ioctl, cmd: AESDCHAR_IOCSEEKTO.");
            struct aesd_seekto seekto;
            if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) == 0) {
                retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
            } else {
                retval = EFAULT;
            }
            break;
    }

    PDEBUG("[AESD] filp offset after ioctl: %lld", filp->f_pos);

    // return
    return retval;
}

// module setup cdev
static int aesd_setup_cdev(struct aesd_dev *dev)
{
    PDEBUG("[AESD] Setting up AESD cdev");

    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

// module initialize
int aesd_init_module(void)
{
    PDEBUG("[AESD] Initializing AESD module");

    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    // initialize device mutex
    mutex_init(&aesd_device.lock);

    // initialize circular buffer inside device
    aesd_circular_buffer_init(&aesd_device.circular_buffer);

    // initialize the incomplete command buffer
    aesd_device.incomplete_command_buffer = NULL;
    aesd_device.incomplete_command_size = 0;

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

// module cleanup
void aesd_cleanup_module(void)
{
    PDEBUG("[AESD] Cleaning up AESD module");
    
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    // clear the incomplete command buffer
    if (aesd_device.incomplete_command_buffer) {
        kfree(aesd_device.incomplete_command_buffer);
        aesd_device.incomplete_command_buffer = NULL;
        aesd_device.incomplete_command_size = 0;
    }

    // free each of the individual entries in the circular buffer
    struct aesd_buffer_entry *temp;
    uint8_t index;
    AESD_CIRCULAR_BUFFER_FOREACH(temp, &aesd_device.circular_buffer, index) {
        if (temp->buffptr) {
            kfree(temp->buffptr);
            temp->buffptr = NULL;
        }
    }

    // destroy mutex
    mutex_destroy(&aesd_device.lock);

    cdev_del(&aesd_device.cdev);

    unregister_chrdev_region(devno, 1);
}

ssize_t aesd_size(struct aesd_circular_buffer *cb)
{
    struct aesd_buffer_entry *temp;
    uint8_t index;
    ssize_t bytes = 0;
    AESD_CIRCULAR_BUFFER_FOREACH(temp, &aesd_device.circular_buffer, index) {
        bytes += temp->size;
    }
    return bytes;
}

void aesd_print_cb(struct aesd_circular_buffer *cb)
{
    PDEBUG("===== [CIRCULAR BUFFER] =====");
    struct aesd_buffer_entry *temp;
    uint8_t index;
    AESD_CIRCULAR_BUFFER_FOREACH(temp, &aesd_device.circular_buffer, index) {
        if (temp->buffptr) {
            char *printbuf = kmalloc(temp->size + 1, GFP_KERNEL);
            memcpy(printbuf, temp->buffptr, temp->size);
            printbuf[temp->size] = '\0';

            if (cb->in_offs == index && cb->out_offs == index) {
                // in/out pointers are at the same index
                PDEBUG("[I/O %d] %s", index, printbuf);
            } else if (cb->in_offs == index) {
                // current index is the in pointer
                PDEBUG("[ I  %d] %s", index, printbuf);
            } else if (cb->out_offs == index) {
                // current index is the out pointer
                PDEBUG("[ O  %d] %s", index, printbuf);
            } else {
                PDEBUG("[    %d] %s", index, printbuf);
            }
            kfree(printbuf);
        } else {
            if (cb->in_offs == index && cb->out_offs == index) {
                // in/out pointers are at the same index
                PDEBUG("[I/O %d] (null)", index);
            } else if (cb->in_offs == index) {
                // current index is the in pointer
                PDEBUG("[ I  %d] (null)", index);
            } else if (cb->out_offs == index) {
                // current index is the out pointer
                PDEBUG("[ O  %d] (null)", index);
            } else {
                PDEBUG("[    %d] (null)", index);
            }
        }
    }
    PDEBUG("===== [TOTAL SIZE: %ld] =====", aesd_size(cb));
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
