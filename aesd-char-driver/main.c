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
#include <linux/string.h>
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Jake Uyechi");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("[AESD] open");

    // add aesd_dev struct to filp private data
    filp->private_data = &aesd_device;

    // return
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("[AESD] release");
    
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("[AESD] read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

    

    // return
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("[AESD] write %zu bytes with offset %lld",count,*f_pos);

    // get struct from filp
    struct aesd_dev *dev = filp->private_data;

    // create a new buffer for data from userspace
    char *kernel_buffer = kmalloc(count, GFP_KERNEL);
    if (kernel_buffer == NULL) {
        retval = -ENOMEM;
        goto cleanup_kernelbuffer;
    }
    if (copy_from_user(kernel_buffer, buf, count)) {
        retval = -EFAULT;
        goto cleanup_kernelbuffer;
    }

    // create a new buffer to hold total command
    mutex_lock(&dev->lock);
    char *new_buffer = kmalloc(dev->buffer_size + count, GFP_KERNEL);
    if (new_buffer == NULL) {
        retval = -ENOMEM;
        goto cleanup_appendcmd;
    }

    // if there's an existing command, add it to new command buffer first
    if (dev->buffer != NULL) {
        memcpy(new_buffer, dev->buffer, dev->buffer_size);
        kfree(dev->buffer);
    }

    // append incoming command to new command buffer, updating the pending buffer
    memcpy(new_buffer + dev->buffer_size, kernel_buffer, count);
    dev->buffer = new_buffer;
    dev->buffer_size += count;

    // main processing loop; assuming there's a possibility of multiple \n chars being written
    // to the char driver, we want to make sure we only process completed commands
    char *currline_pos = dev->buffer;
    size_t remaining_size = dev->buffer_size;
    while (1) {
        // check the new command for newline char
        char *newline_pos = memchr(currline_pos, '\n', remaining_size);

        // if NULL, there's no new newline chars; exit loop
        if (newline_pos == NULL) {
            break;
        }

        // there's a command break; process command data to write to circular buffer
        size_t substr_len = newline_pos - currline_pos + 1; // +1 to include newline

        // allocate memory for the subcommand to add to buffer
        char *substr_buffer = kmalloc(substr_len, GFP_KERNEL);
        if (substr_buffer == NULL) {
            retval = -ENOMEM;
            goto cleanup_substringprocessloop;
        }

        // copy command to the new buffer
        memcpy(substr_buffer, currline_pos, substr_len);

        // create a temporary buffer entry to add
        struct aesd_buffer_entry *to_add = kmalloc(sizeof(aesd_buffer_entry), GFP_KERNEL);
        if (to_add == NULL) {
            kfree(substr_buffer);
            retval = -ENOMEM;
            goto cleanup_substringprocessloop;
        }
        to_add->buffptr = substr_buffer;
        to_add->size = substr_len;

        // write data to cb
        const char *to_free = aesd_circular_buffer_add_entry(dev->cb_commands, to_add, count);

        // free entry data, if necessary
        if (to_free != NULL) {
            kfree(to_free);
        }

        // free the kmallocs used in this process iteration
        kfree(to_add);
        kfree(substr_buffer);

        // update the pointers and size for the processing loop
        currline_pos = newline_pos + 1; // set to the next char after the newline
        remaining_size -= substr_len; // reduce the remainder by the length of the substring processed
    }

    // case where there's an incomplete substring command following the processing step
    if (remaining_size > 0) {
        // create an excess buffer
        char *excess = kmalloc(remaining_size, GFP_KERNEL);
        if (excess == NULL) {
            retval = -ENOMEM;
            goto cleanup_excesshandling;
        }

        // copy and replace the command buffer
        memcpy(excess, currline_pos, remaining_size);
        kfree(dev->buffer);
        dev->buffer = excess;
        dev->buffer_size = remaining_size;
    } else {
        // empty command; update buffer accordingly
        kfree(dev->buffer);
        dev->buffer = NULL;
        dev->buffer_size = 0;
    }
    
    // return value
    retval = count;

cleanup_excesshandling:
cleanup_substringprocessloop:
cleanup_appendcmd:
    mutex_unlock(&dev->lock);
    kfree(new_buffer);

cleanup_kernelbuffer:
    kfree(kernel_buffer);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

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

    // initialize circular buffer using the init function
    aesd_circular_buffer_init(aesd_device->cb_commands);

    // create the mutex
    mutex_init(&aesd_device->lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    PDEBUG("[AESD] Cleaning up AESD module");

    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    // deinitialize the mutex
    mutex_destroy(&aesd_device->lock);

    // free each of the individual entries in the circular buffer
    struct aesd_buffer_entry *temp;
    uint8_t index;
    AESD_CIRCULAR_BUFFER_FOREACH(temp, aesd_device->cb_commands, index) {
        if (temp->buffptr) {
            kfree(temp->buffptr);
        }
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
