/*
 * Pair of character devices acting as a kind of pipe or loopback.
 *
 * (c) 2019 Christophe BLAESS <christophe.blaess@logilin.fr>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */



#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/wait.h>



static uint charloop_buffer_size = 16384;
module_param_named(buffer_size, charloop_buffer_size, uint, 0644);
MODULE_PARM_DESC(buffer_size, " the internal buffer size in bytes");

struct charloop_struct {

	char *read_buffer;
	int  *read_buffer_length;
	struct mutex *read_mutex;
	struct wait_queue_head *read_wq;

	char *write_buffer;
	int  *write_buffer_length;
	struct mutex *write_mutex;
	struct wait_queue_head *write_wq;
};


static struct charloop_struct  charloop_data[2];
static char                   *charloop_buffer[2];
static int                     charloop_buffer_length[2];
static struct mutex            charloop_mtx[2];
static struct wait_queue_head  charloop_wq[2];
static int                     charloop_minor[2];


static int charloop_open(struct inode *ind, struct file *filp)
{
	if (iminor(ind) == charloop_minor[0]) {
		filp->private_data = &(charloop_data[0]);
	} else {
		filp->private_data = &(charloop_data[1]);
	}
	return 0;
}



static ssize_t charloop_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
	struct charloop_struct *charloop = (struct charloop_struct *)(filp->private_data);

	if (mutex_lock_interruptible(charloop->read_mutex) != 0)
		return -ERESTARTSYS;

	while (*charloop->read_buffer_length == 0) {
		mutex_unlock(charloop->read_mutex);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(*charloop->read_wq, (*charloop->read_buffer_length != 0)) != 0)
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(charloop->read_mutex) != 0)
			return -ERESTARTSYS;
	}
	if (length > *charloop->read_buffer_length)
		length = *charloop->read_buffer_length;

	if (copy_to_user(buffer, charloop->read_buffer, length) != 0) {
		mutex_unlock(charloop->read_mutex);
		return -EFAULT;
	}

	*charloop->read_buffer_length -= length;
	if (*charloop->read_buffer_length > 0) {
		memmove(charloop->read_buffer, & (charloop->read_buffer[length]), *charloop->read_buffer_length);
	}

	mutex_unlock(charloop->read_mutex);

	wake_up_interruptible(charloop->read_wq);

	return length;
}



static ssize_t charloop_write(struct file *filp, const char *buffer, size_t length, loff_t *offset)
{
	struct charloop_struct *charloop = (struct charloop_struct *)(filp->private_data);

	if (mutex_lock_interruptible(charloop->write_mutex) != 0)
		return -ERESTARTSYS;

	while (*(charloop->write_buffer_length) == charloop_buffer_size) {
		mutex_unlock(charloop->write_mutex);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(*charloop->write_wq, (*charloop->write_buffer_length != charloop_buffer_size)) != 0)
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(charloop->write_mutex) != 0)
			return -ERESTARTSYS;
	}
	if (length > (charloop_buffer_size - *charloop->write_buffer_length))
		length = charloop_buffer_size - *charloop->write_buffer_length;

	if (copy_from_user(charloop->write_buffer + *(charloop->write_buffer_length), buffer, length) != 0) {
		mutex_unlock(charloop->write_mutex);
		return -EFAULT;
	}

	*charloop->write_buffer_length += length;

	mutex_unlock(charloop->write_mutex);

	wake_up_interruptible(charloop->write_wq);

	return length;
}



static unsigned int charloop_poll(struct file *filp, poll_table *table)
{
	struct charloop_struct *charloop = (struct charloop_struct *)(filp->private_data);
	int ret = 0;

	poll_wait(filp, charloop->read_wq, table);
	poll_wait(filp, charloop->write_wq, table);

	if (mutex_lock_interruptible(charloop->read_mutex) != 0)
		return -ERESTARTSYS;

	if (*charloop->read_buffer_length != 0)
		ret = ret | POLLIN | POLLRDNORM;

	mutex_unlock(charloop->read_mutex);

	if (mutex_lock_interruptible(charloop->write_mutex) != 0)
		return -ERESTARTSYS;

	if (*charloop->write_buffer_length != charloop_buffer_size)
		ret = ret | POLLOUT | POLLWRNORM;

	mutex_unlock(charloop->write_mutex);

	return ret;
}


static struct file_operations charloop_fops = {
	.owner   =  THIS_MODULE,
	.open    =  charloop_open,
	.read    =  charloop_read,
	.write   =  charloop_write,
	.poll    =  charloop_poll,
};


static struct miscdevice charloop_miscdevice[2] = {
	{
		.minor = MISC_DYNAMIC_MINOR,
		.name  = "charloop0",
		.fops  = &charloop_fops,
		.mode  = 0666,
	}, {
		.minor = MISC_DYNAMIC_MINOR,
		.name  = "charloop1",
		.fops  = &charloop_fops,
		.mode  = 0666,
	}
};


static int __init charloop_init (void)
{
	int err;

	charloop_buffer[0] = kmalloc(charloop_buffer_size, GFP_KERNEL);
	if (charloop_buffer[0] == NULL) {
		return -ENOMEM;
	}
	charloop_buffer_length[0] = 0;
	mutex_init(&(charloop_mtx[0]));
	init_waitqueue_head(&(charloop_wq[0]));

	charloop_buffer[1] = kmalloc(charloop_buffer_size, GFP_KERNEL);
	if (charloop_buffer[1] == NULL) {
		mutex_destroy(&(charloop_mtx[0]));
		kfree(charloop_buffer[0]);
		return -ENOMEM;
	}
	charloop_buffer_length[1] = 0;
	mutex_init(&(charloop_mtx[1]));
	init_waitqueue_head(&(charloop_wq[1]));

	if ((err = misc_register(&(charloop_miscdevice[0]))) != 0) {
		mutex_destroy(&(charloop_mtx[1]));
		kfree(charloop_buffer[1]);
		mutex_destroy(&(charloop_mtx[0]));
		kfree(charloop_buffer[0]);
		return err;
	}

	if ((err = misc_register(&(charloop_miscdevice[1]))) != 0) {
		misc_deregister(&(charloop_miscdevice[0]));
		mutex_destroy(&(charloop_mtx[1]));
		kfree(charloop_buffer[1]);
		mutex_destroy(&(charloop_mtx[0]));
		kfree(charloop_buffer[0]);
		return err;
	}

	charloop_minor[0] = charloop_miscdevice[0].minor;
	charloop_data[0].read_buffer = charloop_buffer[0];
	charloop_data[0].read_buffer_length = &charloop_buffer_length[0];
	charloop_data[0].read_mutex = &charloop_mtx[0];
	charloop_data[0].read_wq = &(charloop_wq[0]);
	charloop_data[0].write_buffer = charloop_buffer[1];
	charloop_data[0].write_buffer_length = &(charloop_buffer_length[1]);
	charloop_data[0].write_mutex = &charloop_mtx[1];
	charloop_data[0].write_wq = &(charloop_wq[1]);

	charloop_minor[1] = charloop_miscdevice[1].minor;
	charloop_data[1].read_buffer = charloop_buffer[1];
	charloop_data[1].read_buffer_length = &charloop_buffer_length[1];
	charloop_data[1].read_mutex = &charloop_mtx[1];
	charloop_data[1].read_wq = &(charloop_wq[1]);
	charloop_data[1].write_buffer = charloop_buffer[0];
	charloop_data[1].write_buffer_length = &(charloop_buffer_length[0]);
	charloop_data[1].write_mutex = &charloop_mtx[0];
	charloop_data[1].write_wq = &(charloop_wq[0]);

	return 0;
}


static void __exit charloop_exit (void)
{
	misc_deregister(&(charloop_miscdevice[1]));
	misc_deregister(&(charloop_miscdevice[0]));
	mutex_destroy(&(charloop_mtx[1]));
	kfree(charloop_buffer[1]);
	mutex_destroy(&(charloop_mtx[0]));
	kfree(charloop_buffer[0]);
}


module_init(charloop_init);
module_exit(charloop_exit);


MODULE_DESCRIPTION("Pair of character devices acting as a bidirectional pipe.");
MODULE_AUTHOR("Christophe Blaess <Christophe.Blaess@Logilin.fr>");
MODULE_LICENSE("GPL");
