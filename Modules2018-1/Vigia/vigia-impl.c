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
#include <linux/uaccess.h> /* copy_from/to_user */

#include "kmutex.h"

MODULE_LICENSE("Dual BSD/GPL");

/* Declaration of pipe.c functions */
static int pipe_open(struct inode *inode, struct file *filp);
static int pipe_release(struct inode *inode, struct file *filp);
static ssize_t pipe_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t pipe_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static ssize_t in_proxy(struct file *filp, const char *buf,size_t ucount, loff_t *f_pos, int n_buf);
static ssize_t out_proxy(struct file *filp, const char *buf, size_t ucount, loff_t *f_pos, int n_buf);
static ssize_t middle_proxy(struct file *filp, const char *buf, size_t ucount, loff_t *f_pos, int n_buf);

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

/* Buffer to store data */
#define MAX_SIZE 100
static char *pipe_buffer;
static int in, out, size;

/* El mutex y la condicion para pipe */
static KMutex mutex;
static KCondition cond;

/* VARIABLES globales para vigia */
#define MAX_VIGIA 3
static char *buffers[MAX_VIGIA];
static int ins[MAX_VIGIA];
static int sizes[MAX_VIGIA];
static int last_buffer;
static KCondition conds[MAX_VIGIA];

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

	/* Vigias */
	printk("<1>\t Alocando vigias\n");
	last_buffer = 0;
	for (int v=0;v<MAX_VIGIA;v++){
		buffers[v] =	kmalloc(MAX_SIZE, GFP_KERNEL);
		ins[v] = 0;
		sizes[v] = 0;
		c_init(&conds[v]);
	}

  /* Allocating pipe_buffer */
  pipe_buffer = kmalloc(MAX_SIZE, GFP_KERNEL);
  if (pipe_buffer==NULL) {
    pipe_exit();
    return -ENOMEM;
  }
  memset(pipe_buffer, 0, MAX_SIZE);

  printk("<1>Insertando modulo Vigia\n");
  return 0;
}

void pipe_exit(void) {
  /* Freeing the major number */
  unregister_chrdev(pipe_major, "pipe");
	
	/* Liberar espacio tomado por vigias */
	printk("<1>liberando vigias\n");
	for (int v=0;v<MAX_VIGIA;v++){
		kfree(buffers[v]);
	}

  /* Freeing buffer pipe */
  if (pipe_buffer) {
    kfree(pipe_buffer);
  }

  printk("<1>Borrando modulo Vigia\n");
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

  printk("<1>read %p %ld\n", filp, count);
  m_lock(&mutex);

  while (size==0) {
    /* si no hay nada en el buffer, el lector espera */
    if (c_wait(&cond, &mutex)) {
      printk("<1>read interrupted\n");
      count= -EINTR;
      goto epilog;
    }
  }

  if (count > size) {
    count= size;
  }

  /* Transfiriendo datos hacia el espacio del usuario */
  for (int k= 0; k<count; k++) {
    if (copy_to_user(buf+k, pipe_buffer+out, 1)!=0) {
      /* el valor de buf es una direccion invalida */
      count= -EFAULT;
      goto epilog;
    }
    printk("<1>read byte %c (%d) from %d\n",
            pipe_buffer[out], pipe_buffer[out], out);
    out= (out+1)%MAX_SIZE;
    size--;
  }

epilog:
  c_broadcast(&cond);
  m_unlock(&mutex);
  return count;
}
/* Funcion a modificar para modulo vigia */
static ssize_t pipe_write( struct file *filp, const char *buf,
                      size_t ucount, loff_t *f_pos) {
/* write decidirá a que buffer direccionar la entrada y la salida */
	ssize_t in_count, m_count, out_count, count;
    int actual_buff= (last_buffer+1)%MAX_VIGIA;
    int next_buff= (actual_buff+1)%MAX_VIGIA;
    count = ucount;

	printk("<1>write %p %ld\n", filp, ucount);
    m_lock(&mutex);
	printk("<1>\t Leer el buffer\n");
	
	/* Leemos el nombre y lo guardamos en el buffer del vigia */
	in_count = in_proxy(filp,buf,count,f_pos,actual_buff);
	if(in_count<0){
	    count = in_count;
		goto epiloge;
	}
	last_buffer=actual_buff; //actualizamos el ultimo vigia   
	/* Avisamos al pipe que entro un vigia */
	m_count = middle_proxy(filp, pipe_buffer, ucount, f_pos, actual_buff);
    if(trans_count < 0){
        count = m_count;
        goto epiloge;
    }
	 /* Si el siguiente está esperando, lo despierto y sale */
	c_broadcast(&conds[next_buff]);

	/* Espero hasta que deba salir */
	c_wait(&conds[actual_buff], &mutex);

	/* Avisamos al pipe que un vigia sale */
	out_count = out_proxy(filp, pipe_buffer, ucount, f_pos, actual_buff); 
    if(ocount < 0){
        count = out_count;
        goto epiloge;
    }

	epiloge:
		m_unlock(&mutex);
		return count;
}

/* Proxy entrada: leer el buffer global y guardo en buffer del vigia
donde n_buf es el numero del buffer vigia entrante */
static ssize_t in_proxy( struct file *filp, const char *buf, size_t ucount, loff_t *f_pos, int n_buf) {
	ssize_t count;
	count = ucount;
	sizes[n_buf] = 0;
	ins[n_buf] = 0;
	printk("<1>\t Copiando vigia entrante\n");

	for (int k= 0; k<count; k++) {
		while (size==MAX_SIZE) { //buffer lleno
		  if (c_wait(&cond, &mutex)) {
			printk("<1>write interrupted\n");
			count= -EINTR;
			goto epilog;
		  }
		}
		if (copy_from_user(buffers[n_buf]+ins[n_buf], buf+k, 1)!=0) {//direccion invalida
		  count= -EFAULT;
		  goto epilog;
		}
		printk("<1>\t\t write byte %c at %d on vigia's buffer\n", buffers[n_buf][ins[n_buf]], ins[n_buf]);
		ins[n_buf] = (ins[n_buf]+1)%MAX_SIZE;
		sizes[n_buf]++;
  	}
	epilog:
		return count;
}

/* Escribe desde el buffer del vigia actual al buff del pipe */
static ssize_t middle_proxy( struct file *filp, const char *buf, size_t ucount, loff_t *f_pos, int n_buf) {
    ssize_t count, in_len;
	char text_in[]= "entra: ";
	in_len= (ssize_t)strlen(text_in);
	count = (ssize_t) sizes[n_buf]; 

    printk("<1>\t Escribiendo al buffer del pipe \n");

    for (int k=0; k < in_len; k++) {
        while (size==MAX_SIZE) {
            if (c_wait(&cond, &mutex)) {
                printk("<1>write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        pipe_buffer[in] = text_in[k];
        printk("<1>\t\t write byte %c at %d\n",text_in[k], in);
        in= (in+1)%MAX_SIZE;
        size++;
    }
	printk("<1>\t Copiando del vigia al buffer \n");
    for(int k=0; k<count; k++) {
        while (size==MAX_SIZE) {
            if (c_wait(&cond, &mutex)) {
                printk("<1>write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        pipe_buffer[in] = buffers[n_buf][k];
        printk("<1>\t\t write byte %c at %d\n", buffers[n_buf][k], in);
		in= (in+1)%MAX_SIZE;
        size++;
		c_broadcast(&cond);
    }
	
    epilog:
    	return count;
}

/* Proxy salida: escribe el buffer global y desde el buffer del vigia
donde n_buf es el numero del buffer vigia saliente */
static ssize_t out_proxy( struct file *filp, const char *buf, size_t ucount, loff_t *f_pos, int n_buf) {
    ssize_t count, out_size;
	char text_out[] = "sale: ";
	out_size = (ssize_t) strlen(text_out);
	count = (ssize_t) sizes[n_buf];
    printk("<1>\t Sacando al vigia mas antiguo\n");

    
    for (int k=0; k < out_size; k++) {
        while (size==MAX_SIZE) {
            if (c_wait(&cond, &mutex)) {
                printk("<1>write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        pipe_buffer[in] = text_out[k];
        printk("<1>\t\t write byte %c at %d\n",text_out[k], in);
        in= (in+1)%MAX_SIZE;
        size++;
    }
	printk("<1>\t Copiando del vigia al buffer \n");
    for (int k= 0; k<count; k++) {
        while (size==MAX_SIZE) {
            if (c_wait(&cond, &mutex)) {
                printk("<1>write interrupted\n");
		        count= -EINTR;
		        goto epilog;
        	}
        }
        pipe_buffer[in]=buffers[n_buf][k];
        printk("<1>\t\t write byte %c at %d\n",buffers[n_buf][k], in);
        in= (in+1)%MAX_SIZE;
        size++;
		c_broadcast(&cond);
    }
	epilog:
		return count;
}

