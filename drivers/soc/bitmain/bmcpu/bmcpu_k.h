#ifndef BMCPU_K_H
#define BMCPU_K_H

#define BMCPU_DEV_MAJOR           0
#define BMCPU_DEV_NAME            "bmcpu"
#define BMCPU_CLASS_NAME          "bmcpu"

#define BMCPU_MEM_MAP_NORMAL                                 0
#define BMCPU_MEM_MAP_NORMAL_NOCACHE                         1
#define BMCPU_MEM_MAP_DEVICE                                 2

static int bmcpu_init(void);
static void bmcpu_exit(void);
int bmcpu_open(struct inode *inode, struct file *filp);
int bmcpu_release(struct inode *inode, struct file *filp);
static ssize_t bmcpu_read(struct file *filp, __user char *buf, size_t count, loff_t *f_pos);
long bmcpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int bmcpu_mmap(struct file *filp, struct vm_area_struct *vma);
ssize_t bmcpu_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);

#endif /* BMCPU_K_H */
