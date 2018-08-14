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
/* VARIABLES globales para vigia */
#define MAX_VIGIA 3
static char *buffers[MAX_VIGIA];
static int ins[MAX_VIGIA];
static int sizes[MAX_VIGIA];
static int last_buffer;
static KCondition conds[MAX_VIGIA];


/* Buffer to store data */
#define MAX_SIZE 1024
static char *pipe_buffer;
static int in, out, size;

/* El mutex y la condicion para pipe */
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

	/* Vigias */
	printk("<1>Alocando vigias\n");
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
	
	/* Liberar vigias */
	printk("<1>\t liberando vigias\n");
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
    if (copy_to_user(buf+k, pipe_buffer+out, 1)!=0) {
      /* el valor de buf es una direccion invalida */
      count= -EFAULT;
      goto epilog;
    }
    printk("<1>\t read byte %c (%d) from %d\n",
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
	ssize_t icount;
	ssize_t trans_count;
	ssize_t ocount;
    int actual_buff= (last_buffer+1)%MAX_VIGIA;
    int next_buff= (actual_buff+1)%MAX_VIGIA;
    int scount = 0;

	printk("<1> \t write %p %ld\n", filp, ucount);
    	m_lock(&mutex);
	printk("<1>Leer el buffer\n");
	
	icount = in_write(filp,buf,ucount,f_pos,actual_buff);
	if(icount<0){
	    scount = icount;
		goto epiloge;
	}
	last_buffer=actual_buff;

	/* Hasta acá debería haber entrado y haberle pasado el mensaje al actual_buffer*/
    /* debo pasar la información del actual buffer al pipe_buffer */
    
	trans_count = trans_write(filp, pipe_buffer, ucount, f_pos, actual_buff);//fix ucount-icount
    if(trans_count < 0){
        scount = trans_count;
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
	ocount = out_write(filp, pipe_buffer, ucount, f_pos, actual_buff); /* revisar */
    if(ocount < 0){
        scount = ocount;
        goto epiloge;
    }

    /* Si me despiestan, debo liberar el mutex principal antes de salir */
	// c_broadcast(&cond);
	epiloge:
		m_unlock(&mutex);
		return scount;
}

/* Proxy entrada: leer el buffer global y guardo en buffer del vigia
donde n_buf es el numero del buffer vigia entrante */
static ssize_t in_write( struct file *filp, const char *buf,
                      size_t ucount, loff_t *f_pos, int n_buf) {
	ssize_t count;
	count = ucount;
	printk("<1>Copiando vigia entrante\n");
	sizes[n_buf] = 0;
	ins[n_buf] = 0;
  for (int k= 0; k<count; k++) {
    while (size==MAX_SIZE) {
      /* si el buffer esta lleno, el escritor espera */
      if (c_wait(&cond, &mutex)) {
        printk("<1>write interrupted\n");
        count= -EINTR;
        goto epilog;
      }
    }
    if (copy_from_user(buffers[n_buf]+ins[n_buf], buf+k, 1)!=0) {
      /* el valor de buf es una direccion invalida */
      count= -EFAULT;
      goto epilog;
    }

    printk("<1>\t write byte %c at %d\n",
           buffers[n_buf][ins[n_buf]], ins[n_buf]);
    ins[n_buf] = (ins[n_buf]+1)%MAX_SIZE;
    sizes[n_buf]++;
  }
	epilog:
		return count;
}

/* Escribe desde n_buf a buf, escribiendo antes el texto "entra: " */
static ssize_t trans_write( struct file *filp, const char *buf,
                         size_t ucount, loff_t *f_pos, int n_buf) {
    	ssize_t count;
	ssize_t in_len;
	char text_in[]= "entra: ";
	in_len= (ssize_t)strlen(text_in);

	count = (ssize_t) sizes[n_buf]; /* size string del buffer de vigia sizes[n_buf] */
    printk("<1> Transfiriendo entrante a buffer principal \n");

    
    for (int k=0; k < in_len; k++) {
        while (size==MAX_SIZE) {
            /* si el buffer esta lleno, el escritor espera */
            if (c_wait(&cond, &mutex)) {
                printk("<1>write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        pipe_buffer[in] = text_in[k];

        printk("<1>write byte %c at %d\n",text_in[k], in);
        in= (in+1)%MAX_SIZE;
        size++;
    }
	/* Copiar buffer vigia al buffer del pipe */
    for(int k=0; k<count; k++) {
        while (size==MAX_SIZE) {
            /* si el buffer esta lleno, el escritor espera */
            if (c_wait(&cond, &mutex)) {
                printk("<1>write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        pipe_buffer[in] = buffers[n_buf][k];

        printk("<1>\t write byte %c at %d\n",
               buffers[n_buf][k], in);
		in= (in+1)%MAX_SIZE;
        size++;
		c_broadcast(&cond);
    }
	
    epilog:
    return count;
}

/* Proxy salida: escribe el buffer global y desde el buffer del vigia
donde n_buf es el numero del buffer vigia saliente */
static ssize_t out_write( struct file *filp, const char *buf,
                      size_t ucount, loff_t *f_pos, int n_buf) {
    ssize_t count, out_size;
	char text_in[] = "sale: ";
	out_size = (ssize_t) len(text_in);

	count = (ssize_t) sizes[n_buf];
    /* sizes[n_buf] es la cantidad que vamos a copiar*/
    printk("<1>Pegando vigia saliente\n");

    
    for (int k=0; k < out_size; k++) {
        while (size==MAX_SIZE) {
            /* si el buffer esta lleno, el escritor espera */
            if (c_wait(&cond, &mutex)) {
                printk("<1>write interrupted\n");
                count = -EINTR;
                goto epilog;
            }
        }
        pipe_buffer[in] = text_in[k];

        printk("<1>write byte %c at %d\n",text_in[k], in);
        in= (in+1)%MAX_SIZE;
        size++;
    }

    for (int k= 0; k<count; k++) {
        while (size==MAX_SIZE) {
            /* si el buffer esta lleno, el escritor espera */
            if (c_wait(&cond, &mutex)) {
                printk("<1>write interrupted\n");
            count= -EINTR;
            goto epilog;
        }
        }
        /* copiamos del buffer vigia al buffer global(pipe_buffer) caracter por caracter*/
        pipe_buffer[in]=buffers[n_buf][k];

        printk("<1>write byte %c at %d\n",buffers[n_buf][k], in);
        in= (in+1)%MAX_SIZE;
        size++;
		c_broadcast(&cond);
    }
	

	epilog:
		return count;
}

