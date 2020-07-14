#ifndef SCULL_H
#define SCULL_H

#define SCULL_MAJOR 0
#define SCULL_QUANTUM 400
#define SCULL_QSET 100
#define SCULL_NR_DEVS 1

extern int scull_major;
extern int scull_nr_devs;

struct scull_qset {
    void** data;
    struct scull_qset* next;
};

struct scull_dev {
    int quantum;
    int qset;
    struct scull_qset* data;
    unsigned long size;
    unsigned int access_key;
    struct semaphore sem;
    struct cdev cdev;
};

#endif