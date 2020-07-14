#include <linux/module.h> // module_init
#include <linux/init.h>
#include <linux/fs.h> // file_operation
#include <linux/moduleparam.h> // module_param
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/slab.h> // kmalloc
#include <linux/errno.h>
#include <linux/fcntl.h> // access mode
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>

#include "scull.h"

MODULE_LICENSE("Dual BSD/GPL");

int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;

struct scull_dev *scull_devices;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

int scull_trim(struct scull_dev *dev) {
    struct scull_qset *next, *dptr;
    int qset = dev->qset;
    int i;

    for(dptr = dev->data; dptr; dptr = next) {
        if(dptr->data) {
            for(i = 0; i < qset; i++)
                kfree(dptr->data[i]);
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        kfree(dptr);
    }

    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;
    return 0;
}

struct scull_qset *scull_follow(struct scull_dev *dev, int n) {
    struct scull_qset *qs = dev->data;
    if(!qs) {
        qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
        if(qs == NULL) {
            printk(KERN_ALERT "scull_follow : kmalloc qs Failed!\n");
            return NULL;
        }
        memset(qs, 0, sizeof(struct scull_qset));
    }

    while(n--) {
        if(!qs->next) {
            qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
            if(qs->next == NULL) {
                printk(KERN_ALERT "scull_follow : kmalloc qs->next Failed!\n");
                return NULL;
            }
            memset(qs->next, 0, sizeof(struct scull_qset));
        }
        qs = qs->next;
    }
    return qs;
}

int scull_open(struct inode *inode, struct file *filp) {
    struct scull_dev *dev;
    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;

    if((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        scull_trim(dev);
    }
    return 0;
}

int scull_release(struct inode *inode, struct file *filp) {
    return 0;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    if(*f_pos >= dev->size)
        goto out;
    if(*f_pos + count >= dev->size)
        count = dev->size - *f_pos;

    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    dptr = scull_follow(dev, item);
    if(dptr == NULL || !dptr->data || !dptr->data[s_pos])
        goto out;

    if(count > quantum - q_pos)
        count = quantum - q_pos;

    if(copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

out:
    up(&dev->sem);
    return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, q_pos, s_pos, rest;
    ssize_t retval = -ENOMEM;

    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    dptr = scull_follow(dev, item);
    if(dptr == NULL)
        goto out;

    if(!dptr->data) {
        dptr->data = kmalloc(sizeof(char*) * qset, GFP_KERNEL);
        if(!dptr->data) {
            printk(KERN_ALERT "scull_write : dptr->data kmalloc Failed!\n");
            goto out;
        }
        memset(dptr->data, 0, sizeof(char*) * qset);
    }

    if(!dptr->data[s_pos]) {
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if(!dptr->data[s_pos]) {
            printk(KERN_ALERT "scull_write : dptr->data[s_pos] kmalloc Failed!\n");
            goto out;
        }
        memset(dptr->data[s_pos], 0, quantum);
    }

    if(count > quantum - q_pos)
        count = quantum - q_pos;

    if(copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
        retval = -EFAULT;
        goto out;
    }

    *f_pos += count;
    retval = count;
    if(dev->size < *f_pos)
        dev->size = *f_pos;
out:
    up(&dev->sem);
    return retval;
}

struct file_operations scull_fops = {
    .owner = THIS_MODULE,
    .read = scull_read,
    .write = scull_write,
    .open = scull_open,
    .release = scull_release
    //.llseek = scull_llseek,
    //.ioctl = scull_ioctl
};

static void scull_setup_cdev(struct scull_dev* dev, int index) {
    int err;
    dev_t devno = MKDEV(scull_major, scull_minor + index);

    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_fops;
    err = cdev_add(&dev->cdev, devno, 1);

    if(err < 0)
        printk(KERN_ALERT "Error %d adding scull%d\n", err, index);
}

static void scull_cleanup_module(void) {
    int i;
    dev_t devno = MKDEV(scull_major, scull_minor);

    printk(KERN_NOTICE "Scull_exit Called!\n");
    if(scull_devices) {
        for(i = 0; i < scull_nr_devs; i++) {
            scull_trim(scull_devices + i);
            cdev_del(&scull_devices[i].cdev);
        }
        kfree(scull_devices);
    }
    unregister_chrdev_region(devno, scull_nr_devs);
}

static int scull_init_module(void) {
    int i, result;
    dev_t devno;

    printk(KERN_NOTICE "Scull_init Called!\n");

    if(scull_major) {
        devno = MKDEV(scull_major, scull_minor);
        result = register_chrdev_region(devno, scull_nr_devs, "scull");
    }
    else {
        result = alloc_chrdev_region(&devno, 0, scull_nr_devs, "scull");
        scull_major = MAJOR(devno);
        scull_minor = MINOR(devno);
    }

    if(result < 0) {
        printk(KERN_ALERT "Device number %d, %d is NOT Allocated!!\n", scull_major, scull_minor);
        return result;
    }
    else {
        printk(KERN_NOTICE "Device number %d, %d Allocated!\n", scull_major, scull_minor);
    }

    scull_devices = kmalloc(sizeof(struct scull_dev) * scull_nr_devs, GFP_KERNEL);
    if(!scull_devices) {
        result = -ENOMEM;
        goto fail;
    }
    memset(scull_devices, 0, sizeof(struct scull_dev) * scull_nr_devs);

    for(i = 0; i < scull_nr_devs; i++) {
        scull_devices[i].qset = scull_qset;
        scull_devices[i].quantum = scull_quantum;
        sema_init(&scull_devices[i].sem, 1);
        scull_setup_cdev(&scull_devices[i], i);
    }

    return 0;

fail:
    scull_cleanup_module();
    return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);