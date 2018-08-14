/* Le hubiese cambiado todo a titanic, pero no pude hacer refactor y pajita */

/* Necessary includes for device drivers */
#include <linux/init.h>
/* #include <linux/config.h> */
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/uaccess.h> /* copy_from/to_user that works*/

#include "kmutex.h"

MODULE_LICENSE("Dual BSD/GPL");

/* Declaration of pipe.c functions */
static int pipe_open(struct inode *inode, struct file *filp);
static int pipe_release(struct inode *inode, struct file *filp);
static ssize_t pipe_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t pipe_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static ssize_t in_write(struct file *filp, const char *buf,size_t ucount, loff_t *f_pos, int n_buf);
static ssize_t out_write(struct file *filp, const char *buf, size_t ucount, loff_t *f_pos, int n_buf);
static ssize_t trans_write(struct file *filp, const char *buf, size_t ucount, loff_t *f_pos, int n_buf);

void pipe_exit(void);
int pipe_init(void);

/* Structure that declares the usual file */
/* access functions */
struct file_operations pipe_fops = {
  read: pipe_read,
  write: pipe_write,
  open: pipe_open,
  release: pipe_release
};

/* Declaration of the init and exit functions */
module_init(pipe_init);
module_exit(pipe_exit);

/*** El driver para lecturas sincronas *************************************/

#define TRUE 1
#define FALSE 0

/* Global variables of the driver */

int pipe_major = 61;     /* Major number */

/* custom global variables, added to pipe ones */
#define MAX_VIGIAS 3
static char *buffers[MAX_VIGIAS];
static int ins[MAX_VIGIAS];
static int sizes[MAX_VIGIAS];
static int last_buffer;
static KCondition conds[MAX_VIGIAS];


/* Buffer to store data, renamed titanic bc of context */
#define MAX_SIZE 100
static char *titanic_buffer;
static int in, out, size;

/* Mutex and condition */
static KMutex mutex;
static KCondition cond;

int pipe_init(void) {
  int rc;

  /* Registering device */
  rc = register_chrdev(pipe_major, "pipe", &pipe_fops);
  if (rc < 0) {
    printk(
      "<1>pipe: cannot obtain major number %d\n", pipe_major);
    return rc;
  }

  in= out= size= 0;
  m_init(&mutex);
  c_init(&cond);

	/* Allocating custom buffers, and extra conditions */
	printk("<1> Allocating vigias\n");
	last_buffer = 0;
	for (int v=0;v<MAX_VIGIAS;v++){
		buffers[v] = kmalloc(MAX_SIZE, GFP_KERNEL);
		ins[v] = 0;
		sizes[v] = 0;
		/* extra conditions to hold-on-write the command prompt */
		c_init(&conds[v]);
	}

  /* Allocating titanic_buffer */
  titanic_buffer = kmalloc(MAX_SIZE, GFP_KERNEL);
  if (titanic_buffer==NULL) {
    pipe_exit();
    return -ENOMEM;
  }
  memset(titanic_buffer, 0, MAX_SIZE);

  printk("<1> Inserting Vigia module into the sea...\n");
  return 0;
}

void pipe_exit(void) {
  /* Freeing the major number */
  unregister_chrdev(pipe_major, "pipe");
	
	/* Ship landed safe, going to drink something
	 * (Freeing vigias)*/
	printk("<1>\t Freeing Vigias\n");
	for (int v=0;  v<MAX_VIGIAS; v++){
		kfree(buffers[v]);
	}

  /* Freeing buffer ship */
  if (titanic_buffer) {
    kfree(titanic_buffer);
  }

  printk("<1> Erasing Vigia module\n");
}

static int pipe_open(struct inode *inode, struct file *filp) {
  char *mode=   filp->f_mode & FMODE_WRITE ? "write" :
                filp->f_mode & FMODE_READ ? "read" :
                "unknown";
  printk("<1>open %p for %s\n", filp, mode);
  return 0;
}

static int pipe_release(struct inode *inode, struct file *filp) {
  printk("<1>release %p\n", filp);
  return 0;
}

static ssize_t pipe_read(struct file *filp, char *buf,
                    size_t ucount, loff_t *f_pos) {
  ssize_t count= ucount;

  printk("<1>\t read %p %ld\n", filp, count);
  m_lock(&mutex);

  while (size==0) {
    /* si no hay nada en el buffer, el lector espera */
    if (c_wait(&cond, &mutex)) {
      printk("<1>\t read interrupted\n");
      count= -EINTR;
      goto epilog;
    }
  }

  if (count > size) {
    count= size;
  }

  /* Transfiriendo datos hacia el espacio del usuario */
  for (int k= 0; k<count; k++) {
    if (copy_to_user(buf+k, titanic_buffer+out, 1)!=0) {
      /* el valor de buf es una direccion invalida */
      count= -EFAULT;
      goto epilog;
    }
    printk("<1>\t read byte %c (%d) from %d\n",
            titanic_buffer[out], titanic_buffer[out], out);
    out= (out+1)%MAX_SIZE;
    size--;
  }

epilog:
  c_broadcast(&cond);
  m_unlock(&mutex);
  return count;
}

static ssize_t pipe_write( struct file *filp, const char *buf,
                      size_t ucount, loff_t *f_pos) {
    /* write decides wich auxiliar buffer uses the new vigia */
	ssize_t insert_count, transfer_count, out_count, scount;
    int actual_buff= (last_buffer + 1)%MAX_VIGIAS;
    int next_buff= (actual_buff + 1)%MAX_VIGIAS;
    scount = ucount;

	printk("<1> \t write %p %ld\n", filp, ucount);
    	m_lock(&mutex);
	printk("<1>Read from user\n");
	
	insert_count = in_write(filp, buf, ucount, f_pos, actual_buff);
	if(insert_count < 0){
	    scount = insert_count;
		goto epiloge;
	}
	last_buffer = actual_buff;

	/* Hasta acá debería haber entrado y haberle pasado el mensaje al actual_buffer*/
    /* debo pasar la información del actual buffer al titanic_buffer */
    
	transfer_count = trans_write(filp, titanic_buffer, ucount, f_pos, actual_buff);
    if(transfer_count < 0){
        scount = transfer_count;
        goto epiloge;
    }
	 /* Entonces acá debo chequear si hay que despertar a alguien antes de dormirme
	 *  (hacer broadcast de la siguiente condición debería ser suficiente creo) */
    /* usar las condiciones del mutex para que el otro ql se eche solo Y así el dice que el sale */
	c_broadcast(&conds[next_buff]);

	/* Dormir hasta que deba salir */
	c_wait(&conds[actual_buff], &mutex);

    /* Luego de sacar al vigia, me duermo esperando a que me saquen  */
    /* Llamar a out write */
	out_count = out_write(filp, titanic_buffer, ucount, f_pos, actual_buff); /* revisar */
    if(out_count < 0){
        scount = out_count;
        goto epiloge;
    }

    /* Si me despiestan, debo liberar el mutex principal antes de salir */
	// c_broadcast(&cond);
	epiloge:
		m_unlock(&mutex);
		return scount;
}

/* In manager: read from user and save the vigia in a buffer n_buf */
static ssize_t in_write( struct file *filp, const char *buf,
                      size_t ucount, loff_t *f_pos, int n_buf) {
	ssize_t count;
	count = ucount;
	printk("<1>\t Copying name of vigia\n");
	sizes[n_buf] = 0;
	ins[n_buf] = 0;
  for (int k= 0; k<count; k++) {
    while (size==MAX_SIZE) {
      /* si el buffer esta lleno, el escritor espera */
      if (c_wait(&cond, &mutex)) {
        printk("<1>\t write interrupted\n");
        count= -EINTR;
        goto epilog;
      }
    }
    if (copy_from_user(buffers[n_buf] + ins[n_buf], buf + k, 1) != 0) {
      /* el valor de buf es una direccion invalida */
      count= -EFAULT;
      goto epilog;
    }

    printk("<1>\t write byte %c at %d\n",
           buffers[n_buf][ins[n_buf]], ins[n_buf]);
    ins[n_buf] = (ins[n_buf] + 1)%MAX_SIZE;
    sizes[n_buf]++;
  }
	epilog:
		return count;
}

/* Escribe desde n_buf a buf, escribiendo antes el texto "entra: " */
static ssize_t trans_write( struct file *filp, const char *buf,
                         size_t ucount, loff_t *f_pos, int n_buf) {
    ssize_t count, in_len;
	char text_in[] = "entra: ";
	in_len = (ssize_t) strlen(text_in);
	count = (ssize_t) sizes[n_buf]; /* size string del buffer de vigia sizes[n_buf] */
    printk("<1>\t Transfering vigia to titanic \n");

    for (int k=0; k < in_len; k++) {
        while (size == MAX_SIZE) {
            /* si el buffer esta lleno, el escritor espera */
            if (c_wait(&cond, &mutex)) {
                printk("<1> write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        titanic_buffer[in] = text_in[k];

        printk("<1> write byte %c at %d\n",text_in[k], in);
        in = (in + 1)%MAX_SIZE;
        size++;
    }
	/* Copy vigia from aux buffer to titanic */
    for(int k = 0; k < count; k++) {
        while (size == MAX_SIZE) {
            /* si el buffer esta lleno, el escritor espera */
            if (c_wait(&cond, &mutex)) {
                printk("<1> write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        titanic_buffer[in] = buffers[n_buf][k];
        printk("<1>\t write byte %c at %d\n", buffers[n_buf][k], in);
		in= (in + 1)%MAX_SIZE;
        size++;
		c_broadcast(&cond);
    }
	
    epilog:
    return count;
}

/* Out manager: writes the exit on the titanic buffer, before return statement */
static ssize_t out_write( struct file *filp, const char *buf,
                      size_t ucount, loff_t *f_pos, int n_buf) {
    ssize_t count, out_size;
	char text_in[] = "sale: ";
	out_size = (ssize_t) strlen(text_in);
	count = (ssize_t) sizes[n_buf];
    printk("<1> Log of a vigia's exit\n");

    
    for (int k=0; k < out_size; k++) {
        while (size == MAX_SIZE) {
            /* si el buffer esta lleno, el escritor espera */
            if (c_wait(&cond, &mutex)) {
                printk("<1>write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        titanic_buffer[in] = text_in[k];
        printk("<1> write byte %c at %d\n",text_in[k], in);
        in= (in + 1)%MAX_SIZE;
        size++;
    }

    for (int k = 0; k < count; k++) {
        while (size == MAX_SIZE) {
            /* si el buffer esta lleno, el escritor espera */
            if (c_wait(&cond, &mutex)) {
                printk("<1> write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        /* copiamos del buffer vigia al buffer global(titanic_buffer) caracter por caracter*/
        titanic_buffer[in] = buffers[n_buf][k];
        printk("<1>write byte %c at %d\n", buffers[n_buf][k], in);
        in = (in + 1) % MAX_SIZE;
        size++;
        c_broadcast(&cond);
    }

	epilog:
		return count;
}

